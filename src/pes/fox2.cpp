// Fox engine binary FoxDataFile ("fox2") reader.
// Layout: docs/re/pes-re-fox2-format.md (all sizes verified against the
// stadium/pitch/goal/cornerflag/light .fox2 files, not the FoxKit summary).
#include "pes/fox2.hpp"

#include <cstring>
#include <unordered_map>

#include "pes/strcode.hpp"

namespace pes {
namespace {

// File header (32 bytes): magic, u32 entity count, u32 string-table offset,
// u32 entity-data offset (= 32), 12 reserved bytes (zero).
constexpr size_t FOX2_HEADER_BYTES = 32;
constexpr size_t FOX2_ENTITY_HEADER_BYTES = 64;  // hs field, always 64
constexpr size_t FOX2_PROP_HEADER_BYTES = 32;    // 16-byte header + 16 pad
constexpr size_t FOX2_DATA_OFFSET = 32;          // value data starts here
constexpr size_t FOX2_ALIGN = 16;  // property blocks end 16-byte aligned
// String-table entry: u64 hash, u32 length, then bytes (no terminator);
// a zero hash ends the table.
constexpr size_t FOX2_STRING_HEADER_BYTES = 12;
// Hashes in the file are the low 48 bits of StrCode64 (CityHash64 1.0 with
// seeds); StrCode64 is already implemented in pes/strcode.hpp.
constexpr uint64_t FOX2_HASH_MASK = 0xFFFFFFFFFFFFULL;

// Property value type ids (FoxKit TNames order; only ids seen in real files
// get a value size — anything else stays raw).
constexpr uint8_t FOX2_BOOL = 10, FOX2_STRING = 11, FOX2_PATH = 12;
constexpr uint8_t FOX2_ENTITY_PTR = 13, FOX2_WIDE_VEC3 = 14, FOX2_VEC4 = 15;
constexpr uint8_t FOX2_QUAT = 16, FOX2_COLOR = 19, FOX2_FILE_PTR = 20;
constexpr uint8_t FOX2_ENTITY_HANDLE = 21;  // 22 = EntityLink, kept raw (§5 of the format doc)
// Containers: 0 scalar, 1 dynamic array, 2 StringMap (u64 key + value per
// item), 3 static list (packed u64s, e.g. children addresses).
constexpr uint8_t FOX2_STRING_MAP = 2;

// Decoded element width per type id (bytes, before alignment padding).
size_t ElementBytes(uint8_t type) {
  switch (type) {
    case 0:
    case 1:
    case FOX2_BOOL:
      return 1;
    case 2:
    case 3:
      return 2;
    case 4:
    case 5:
    case 8:
      return 4;
    case 6:
    case 7:
    case 9:
    case FOX2_STRING:
    case FOX2_PATH:
    case FOX2_ENTITY_PTR:
    case FOX2_FILE_PTR:
    case FOX2_ENTITY_HANDLE:
      return 8;
    case FOX2_WIDE_VEC3:
    case FOX2_VEC4:
    case FOX2_QUAT:
    case FOX2_COLOR:
      return 16;
    default:
      return 0;  // unknown (e.g. FOX2_ENTITY_LINK): kept as raw bytes
  }
}

template <typename T>
T Load(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof v);
  return v;
}

// Renders an unresolvable hash the way the RE tools do ("#b8a0bf169f98"):
// 12 lowercase hex digits of the masked value.
std::string HashName(uint64_t hash) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string s("#");
  for (int i = 44; i >= 0; i -= 4) s += kHex[(hash >> i) & 0xF];
  return s;
}

}  // namespace

const Fox2Property* Fox2Entity::Find(std::string_view name) const {
  for (const auto& p : props)
    if (p.name == name) return &p;
  return nullptr;
}

const Fox2Entity* Fox2File::ByAddress(uint64_t address) const {
  for (const auto& e : entities)
    if (e.address == address) return &e;
  return nullptr;
}

std::optional<Fox2File> ParseFox2(std::span<const uint8_t> blob, std::string* error) {
  auto fail = [&](const char* reason) -> std::optional<Fox2File> {
    *error = reason;
    return std::nullopt;
  };
  // NOTE: never write the magic as "\xf2box5" — \x consumes the 'b' as a hex
  // digit (\xf2b truncates to one char). Real files start with F2 62 6F 78 35.
  static constexpr uint8_t kMagic[] = {0xF2, 'b', 'o', 'x', '5'};
  if (blob.size() < FOX2_HEADER_BYTES || std::memcmp(blob.data(), kMagic, sizeof kMagic) != 0)
    return fail("not a fox2 file");
  const uint32_t entityCount = Load<uint32_t>(blob.data() + 8);
  const uint32_t stringTable = Load<uint32_t>(blob.data() + 12);
  const uint32_t dataOffset = Load<uint32_t>(blob.data() + 16);
  if (dataOffset != FOX2_HEADER_BYTES || size_t(stringTable) > blob.size())
    return fail("fox2 header offsets out of range");
  // String table: u64 hash + u32 length + bytes, terminated by a zero hash.
  std::unordered_map<uint64_t, std::string> names;
  size_t at = stringTable;
  while (true) {
    if (at + FOX2_STRING_HEADER_BYTES > blob.size()) return fail("fox2 string table truncated");
    const uint64_t hash = Load<uint64_t>(blob.data() + at);
    const uint32_t length = Load<uint32_t>(blob.data() + at + 8);
    if (hash == 0) break;
    if (uint64_t(at) + FOX2_STRING_HEADER_BYTES + length > blob.size())
      return fail("fox2 string entry out of range");
    names.emplace(hash, std::string(reinterpret_cast<const char*>(blob.data() + at + 12), length));
    at += FOX2_STRING_HEADER_BYTES + length;
  }
  auto nameOf = [&](uint64_t hash) -> std::string {
    if (const auto it = names.find(hash & FOX2_HASH_MASK); it != names.end()) return it->second;
    return HashName(hash & FOX2_HASH_MASK);
  };
  // Entities in file order: 64-byte header (u32 address at +10, u64 class
  // hash at +0x1c, static/dynamic property counts at +0x24, block size at
  // +0x30), then (nstat + ndyn) property blocks. The next entity starts at
  // block start + esize, and the last one ends at the string table.
  Fox2File file;
  size_t pos = dataOffset;
  for (uint32_t e = 0; e < entityCount; ++e) {
    if (pos + FOX2_ENTITY_HEADER_BYTES > blob.size()) return fail("fox2 entity header truncated");
    const uint8_t* hdr = blob.data() + pos;
    const uint64_t address = Load<uint32_t>(hdr + 10);
    const uint64_t classHash = Load<uint64_t>(hdr + 0x1c);
    const uint16_t nstat = Load<uint16_t>(hdr + 0x24), ndyn = Load<uint16_t>(hdr + 0x26);
    const uint32_t esize = Load<uint32_t>(hdr + 0x30);
    if (esize < FOX2_ENTITY_HEADER_BYTES || uint64_t(pos) + esize > blob.size())
      return fail("fox2 entity block out of range");
    Fox2Entity entity;
    entity.address = address;
    entity.className = nameOf(classHash);
    size_t q = pos + FOX2_ENTITY_HEADER_BYTES;
    for (uint32_t k = 0, props = uint32_t(nstat) + ndyn; k < props; ++k) {
      if (q + FOX2_PROP_HEADER_BYTES > blob.size()) return fail("fox2 property header truncated");
      const uint64_t propHash = Load<uint64_t>(blob.data() + q);
      const uint8_t type = blob[q + 8], container = blob[q + 9];
      const uint16_t count = Load<uint16_t>(blob.data() + q + 10);
      const uint16_t dataAt = Load<uint16_t>(blob.data() + q + 12);
      const uint16_t blockSize = Load<uint16_t>(blob.data() + q + 14);
      if (blockSize < FOX2_PROP_HEADER_BYTES || dataAt < FOX2_PROP_HEADER_BYTES || dataAt > blockSize ||
          uint64_t(q) + blockSize > uint64_t(pos) + esize)
        return fail("fox2 property block out of range");
      Fox2Property prop;
      prop.name = nameOf(propHash);
      prop.type = type;
      prop.container = container;
      const size_t elem = ElementBytes(type);
      const uint8_t* data = blob.data() + q + dataAt;
      if (elem == 0) {
        // Unknown type id (e.g. EntityLink 22, 32 bytes/element): keep each
        // element's raw bytes so callers still see counts, keys, addresses.
        // StringMap keys are still decoded; the value width is unknown, so
        // the whole data run is kept on the first value.
        if (container == FOX2_STRING_MAP) {
          Fox2Value v;
          v.raw.assign(data, blob.data() + q + blockSize);
          for (uint16_t i = 0; i < count; ++i) {
            if (uint64_t(data - blob.data()) + 8 > blob.size())
              return fail("fox2 property data out of range");
            if (uint64_t(data - blob.data()) + 8 > uint64_t(q) + blockSize)
              return fail("fox2 property data out of range");
            prop.keys.push_back(nameOf(Load<uint64_t>(data)));
            prop.values.push_back(i == 0 ? std::move(v) : Fox2Value{});
            data += 8;  // keys verified file-ordered; values stay packed raw
          }
        } else {
          const size_t span = blockSize - dataAt;
          if (count > 0 && span % count != 0) return fail("fox2 property size mismatch");
          const size_t stride = count == 0 ? 0 : span / count;
          for (uint16_t i = 0; i < count; ++i) {
            Fox2Value v;
            v.raw.assign(data + size_t(i) * stride, data + size_t(i + 1) * stride);
            prop.values.push_back(std::move(v));
          }
        }
        q += blockSize;
        entity.props.push_back(std::move(prop));
        continue;
      }
      // Block size check: align16(dataAt + payload) must equal blockSize, so
      // truncated or overrunning counts fail instead of decoding garbage.
      // StringMap items are u64 key + value element; the rest pack tightly.
      const size_t stride = container == FOX2_STRING_MAP ? 8 + elem : elem;
      const uint64_t payload = uint64_t(count) * stride;
      if (dataAt + ((payload + FOX2_ALIGN - 1) & ~(FOX2_ALIGN - 1)) != blockSize)
        return fail("fox2 property size mismatch");
      if (uint64_t(q) + dataAt + payload > blob.size())
        return fail("fox2 property data out of range");
      for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* item = data + size_t(i) * stride;
        if (container == FOX2_STRING_MAP) {
          prop.keys.push_back(nameOf(Load<uint64_t>(item)));
          item += 8;
        }
        Fox2Value v;
        switch (type) {
          case FOX2_BOOL:
            v.b = *item != 0;
            v.u = v.b ? 1u : 0u;
            break;
          case 0:
            v.i = static_cast<int8_t>(*item);
            break;
          case 2:
            v.i = Load<int16_t>(item);
            break;
          case 4:
            v.i = Load<int32_t>(item);
            break;
          case 1:
            v.u = *item;
            break;
          case 3:
            v.u = Load<uint16_t>(item);
            break;
          case 5: {
            // u32 bitfields read both ways: enums/counts as u, flags as-is.
            const uint32_t w = Load<uint32_t>(item);
            v.u = w;
            v.i = int64_t(int32_t(w));
            break;
          }
          case 8:
            v.d = Load<float>(item);
            break;
          case 6:
            v.i = Load<int64_t>(item);
            v.d = double(v.i);
            break;
          case 7:
            v.u = Load<uint64_t>(item);
            v.d = double(v.u);
            break;
          case 9:
            v.d = Load<double>(item);
            v.u = uint64_t(v.d);
            break;
          case FOX2_STRING:
          case FOX2_PATH:
          case FOX2_FILE_PTR: {
            const uint64_t hash = Load<uint64_t>(item);
            if (const auto it = names.find(hash & FOX2_HASH_MASK); it != names.end())
              v.s = it->second;
            else {
              v.u = hash;  // unresolved hash kept for the caller
              v.s = HashName(hash & FOX2_HASH_MASK);
            }
            break;
          }
          case FOX2_ENTITY_PTR:
          case FOX2_ENTITY_HANDLE:
            v.u = Load<uint64_t>(item);  // runtime address; ByAddress dereferences it
            break;
          case FOX2_WIDE_VEC3:
            // WideVec3 is 4 floats; the 4th is padding/garbage — take xyz.
            v.v3 = {Load<float>(item), Load<float>(item + 4), Load<float>(item + 8)};
            break;
          case FOX2_VEC4:
          case FOX2_COLOR:
            v.v4 = {Load<float>(item), Load<float>(item + 4), Load<float>(item + 8),
                    Load<float>(item + 12)};
            break;
          case FOX2_QUAT:
            v.q = {Load<float>(item), Load<float>(item + 4), Load<float>(item + 8),
                   Load<float>(item + 12)};
            break;
          default:
            return fail("fox2 property size mismatch");  // ElementBytes said known
        }
        prop.values.push_back(std::move(v));
      }
      entity.props.push_back(std::move(prop));
      q += blockSize;
    }
    if (q != pos + esize) return fail("fox2 entity size mismatch");
    file.entities.push_back(std::move(entity));
    pos += esize;
  }
  if (pos != stringTable) return fail("fox2 entities do not reach the string table");
  return file;
}

}  // namespace pes
