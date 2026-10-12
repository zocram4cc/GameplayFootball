// Port of GameplayFootball tools/pes21_import/cpk.py: @UTF tables, the CRI
// XOR obfuscation, CRILAYLA decompression and the TOC base-offset rule.
#include "pes/cpk.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace pes {
namespace {

constexpr long HEADER_UTF_OFFSET = 16;    // @UTF header block follows the 16-byte "CPK " chunk header
constexpr size_t HEADER_UTF_BYTES = 2048; // header @UTF is always inside the first 2 KiB
constexpr size_t UTF_PREAMBLE = 8;        // "@UTF" + big-endian table size
constexpr size_t UTF_TABLE_HEADER = 24;   // rows/strings/data offsets, name, columns, row width, rows
constexpr uint8_t XOR_SEED = 0x5F;        // CRI @UTF obfuscation: x0 = 0x5F, x *= 0x15 per byte
constexpr uint8_t XOR_MUL = 0x15;
constexpr size_t CRILAYLA_HEADER = 16;    // "CRILAYLA" + u32 uncompressed size + u32 header offset
constexpr size_t CRILAYLA_PREFIX = 0x100; // raw bytes stored uncompressed at the end of the blob
constexpr int CRILAYLA_OFFSET_BITS = 13;
constexpr size_t CRILAYLA_MIN_MATCH = 3;

// @UTF column storage classes and value types.
constexpr uint8_t STORAGE_MASK = 0xF0, TYPE_MASK = 0x0F;
constexpr uint8_t STORAGE_ZERO = 0x10, STORAGE_CONST = 0x30, STORAGE_PER_ROW = 0x50;
constexpr uint8_t TYPE_FLOAT = 0x8, TYPE_STRING = 0xA, TYPE_DATA = 0xB;

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint64_t Be64(const uint8_t* p) { return uint64_t(Be32(p)) << 32 | Be32(p + 4); }
uint32_t Le32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

bool StripCrypt(std::vector<uint8_t>& blob) {
  if (blob.size() >= 4 && std::memcmp(blob.data(), "@UTF", 4) == 0) return true;
  uint8_t x = XOR_SEED;
  for (auto& b : blob) {
    b ^= x;
    x = uint8_t(x * XOR_MUL);
  }
  return blob.size() >= 4 && std::memcmp(blob.data(), "@UTF", 4) == 0;
}

struct UtfValue {
  uint64_t u = 0;
  std::string s;
};
using UtfRow = std::unordered_map<std::string, UtfValue>;

class UtfReader {
 public:
  UtfReader(const uint8_t* body, size_t size) : body_(body), size_(size) {}
  bool ok = true;

  bool Need(size_t pos, size_t n) {
    if (pos + n > size_) ok = false;
    return ok;
  }
  std::string String(uint32_t off) {
    size_t at = stringsOffset + off;
    if (at >= size_) { ok = false; return {}; }
    const void* end = std::memchr(body_ + at, 0, size_ - at);
    size_t len = end ? static_cast<const uint8_t*>(end) - (body_ + at) : size_ - at;
    return std::string(reinterpret_cast<const char*>(body_ + at), len);
  }
  UtfValue Value(size_t& pos, uint8_t type) {
    UtfValue v;
    switch (type) {
      case 0: case 1: if (Need(pos, 1)) v.u = body_[pos]; pos += 1; break;
      case 2: case 3: if (Need(pos, 2)) v.u = Be16(body_ + pos); pos += 2; break;
      case 4: case 5: case TYPE_FLOAT: if (Need(pos, 4)) v.u = Be32(body_ + pos); pos += 4; break;
      case 6: case 7: if (Need(pos, 8)) v.u = Be64(body_ + pos); pos += 8; break;
      case TYPE_STRING: if (Need(pos, 4)) v.s = String(Be32(body_ + pos)); pos += 4; break;
      case TYPE_DATA: if (Need(pos, 8)) v.u = dataOffset + Be32(body_ + pos); pos += 8; break;
      default: ok = false;
    }
    return v;
  }

  const uint8_t* body_;
  size_t size_;
  size_t stringsOffset = 0, dataOffset = 0;
};

bool ReadUtf(const std::vector<uint8_t>& data, std::vector<UtfRow>* rows, std::string* error) {
  if (data.size() < UTF_PREAMBLE + UTF_TABLE_HEADER || std::memcmp(data.data(), "@UTF", 4) != 0) {
    *error = "not a @UTF table";
    return false;
  }
  const size_t tableSize = Be32(data.data() + 4);
  if (UTF_PREAMBLE + tableSize > data.size()) {
    *error = "@UTF table truncated";
    return false;
  }
  const uint8_t* body = data.data() + UTF_PREAMBLE;
  UtfReader r(body, tableSize);
  const uint32_t rowsOffset = Be32(body);
  r.stringsOffset = Be32(body + 4);
  r.dataOffset = Be32(body + 8);
  const uint16_t columns = Be16(body + 16), rowWidth = Be16(body + 18);
  const uint32_t rowCount = Be32(body + 20);

  struct Column { std::string name; uint8_t storage, type; UtfValue constant; };
  std::vector<Column> cols;
  size_t pos = UTF_TABLE_HEADER;
  for (uint16_t c = 0; c < columns && r.ok; ++c) {
    if (!r.Need(pos, 1)) break;
    const uint8_t flags = body[pos++];
    if (flags == 0) { pos += 4; continue; }  // padded schema slot
    if (!r.Need(pos, 4)) break;
    Column col{r.String(Be32(body + pos)), uint8_t(flags & STORAGE_MASK), uint8_t(flags & TYPE_MASK), {}};
    pos += 4;
    if (col.storage == STORAGE_CONST) col.constant = r.Value(pos, col.type);
    cols.push_back(std::move(col));
  }
  rows->clear();
  for (uint32_t i = 0; i < rowCount && r.ok; ++i) {
    size_t rp = rowsOffset + size_t(i) * rowWidth;
    UtfRow row;
    for (const auto& col : cols) {
      if (col.storage == STORAGE_CONST) row[col.name] = col.constant;
      else if (col.storage == STORAGE_ZERO) row[col.name] = UtfValue{};
      else if (col.storage == STORAGE_PER_ROW) row[col.name] = r.Value(rp, col.type);
      else { *error = "unknown @UTF storage class"; return false; }
    }
    rows->push_back(std::move(row));
  }
  if (!r.ok) *error = "@UTF table out of bounds";
  return r.ok;
}

bool ReadAt(std::FILE* f, uint64_t offset, size_t size, std::vector<uint8_t>* out) {
  out->resize(size);
  return std::fseek(f, long(offset), SEEK_SET) == 0 && std::fread(out->data(), 1, size, f) == size;
}

uint64_t Get(const UtfRow& row, const char* name) {
  auto it = row.find(name);
  return it == row.end() ? 0 : it->second.u;
}

}  // namespace

std::optional<std::vector<uint8_t>> CrilaylaDecompress(const std::vector<uint8_t>& data, std::string* error) {
  if (data.size() < 8 || std::memcmp(data.data(), "CRILAYLA", 8) != 0) return data;
  if (data.size() < CRILAYLA_HEADER + CRILAYLA_PREFIX) {
    *error = "CRILAYLA blob too small";
    return std::nullopt;
  }
  const size_t outSize = Le32(data.data() + 8);
  // The raw prefix and the bitstream are anchored to the END of the blob.
  const size_t prefixAt = data.size() - CRILAYLA_PREFIX;
  const size_t bitTop = prefixAt - 1;
  std::vector<uint8_t> out(CRILAYLA_PREFIX + outSize);
  std::memcpy(out.data(), data.data() + prefixAt, CRILAYLA_PREFIX);
  uint8_t* body = out.data() + CRILAYLA_PREFIX;

  size_t bitpos = 0;
  bool overrun = false;
  auto bits = [&](int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
      const size_t back = bitpos >> 3;
      if (back > bitTop) { overrun = true; return v; }
      const uint8_t byte = data[bitTop - back];
      v = v << 1 | ((byte >> (7 - (bitpos & 7))) & 1);  // MSB first within each byte
      ++bitpos;
    }
    return v;
  };

  constexpr int LENGTH_CHUNK_BITS[] = {2, 3, 5};
  constexpr int LENGTH_TAIL_BITS = 8;
  size_t outPos = outSize;  // filled backwards
  while (outPos > 0 && !overrun) {
    if (bits(1)) {
      const size_t offset = bits(CRILAYLA_OFFSET_BITS) + CRILAYLA_MIN_MATCH;
      size_t length = CRILAYLA_MIN_MATCH;
      bool more = true;
      for (int b : LENGTH_CHUNK_BITS) {
        const uint32_t chunk = bits(b);
        length += chunk;
        if (chunk != (1u << b) - 1) { more = false; break; }
      }
      while (more) {
        const uint32_t chunk = bits(LENGTH_TAIL_BITS);
        length += chunk;
        more = chunk == (1u << LENGTH_TAIL_BITS) - 1;
      }
      for (size_t i = 0; i < length; ++i) {
        if (outPos == 0 || outPos - 1 + offset >= outSize) { overrun = true; break; }
        body[outPos - 1] = body[outPos - 1 + offset];
        --outPos;
      }
    } else {
      body[outPos - 1] = uint8_t(bits(8));
      --outPos;
    }
  }
  if (overrun) {
    *error = "CRILAYLA stream corrupt";
    return std::nullopt;
  }
  return out;
}

bool CpkArchive::Open(const std::string& path, std::string* error, uint64_t at, uint64_t length) {
  path_ = path;
  entries_.clear();
  file_.reset(std::fopen(path.c_str(), "rb"));
  if (!file_) { *error = "cannot open " + path; return false; }
  std::FILE* f = file_.get();
  // A nested cpk is read in place inside its parent's file: every offset below
  // is relative to `at`, and entry offsets are made absolute in the file.
  std::vector<uint8_t> buf;
  auto read = [&](uint64_t offset, size_t n) { return ReadAt(f, at + offset, n, &buf); };

  if (!read(0, 4) || std::memcmp(buf.data(), "CPK ", 4) != 0) { *error = path + ": not a CPK"; return false; }
  std::fseek(f, 0, SEEK_END);
  const uint64_t fileSize = length ? length : uint64_t(std::ftell(f)) - at;
  const size_t headerBytes = std::min<size_t>(HEADER_UTF_BYTES, size_t(fileSize - HEADER_UTF_OFFSET));
  std::vector<UtfRow> header;
  if (!read(HEADER_UTF_OFFSET, headerBytes) || !StripCrypt(buf) || !ReadUtf(buf, &header, error) ||
      header.empty()) {
    *error = path + ": bad CPK header " + *error;
    return false;
  }
  const uint64_t tocOffset = Get(header[0], "TocOffset");
  const uint64_t contentOffset = Get(header[0], "ContentOffset");

  if (!read(tocOffset, 4) || std::memcmp(buf.data(), "TOC ", 4) != 0) { *error = path + ": no TOC"; return false; }
  // The @UTF block states its own size (big-endian at +4, after de-obfuscation).
  if (!read(tocOffset + HEADER_UTF_OFFSET, UTF_PREAMBLE) || !StripCrypt(buf)) {
    *error = path + ": bad TOC";
    return false;
  }
  const uint32_t utfSize = Be32(buf.data() + 4);
  std::vector<UtfRow> toc;
  if (!read(tocOffset + HEADER_UTF_OFFSET, UTF_PREAMBLE + utfSize) || !StripCrypt(buf) ||
      !ReadUtf(buf, &toc, error)) {
    *error = path + ": bad TOC " + *error;
    return false;
  }
  const uint64_t base = at + (contentOffset ? std::min(tocOffset, contentOffset) : tocOffset);
  entries_.reserve(toc.size());
  for (const auto& row : toc) {
    auto dir = row.find("DirName");
    auto name = row.find("FileName");
    CpkEntry e;
    const std::string dirName = dir == row.end() ? "" : dir->second.s;
    e.path = dirName.empty() ? (name == row.end() ? "" : name->second.s)
                             : dirName + "/" + (name == row.end() ? "" : name->second.s);
    e.offset = Get(row, "FileOffset") + base;
    e.size = Get(row, "FileSize");
    e.extractSize = row.count("ExtractSize") ? Get(row, "ExtractSize") : e.size;
    entries_.push_back(std::move(e));
  }
  return true;
}

std::optional<std::vector<uint8_t>> CpkArchive::Read(const CpkEntry& entry, std::string* error) const {
  std::vector<uint8_t> blob;
  if (!ReadAt(file_.get(), entry.offset, entry.size, &blob)) {
    *error = path_ + ": short read of " + entry.path;
    return std::nullopt;
  }
  if (entry.extractSize != entry.size || (blob.size() >= 8 && std::memcmp(blob.data(), "CRILAYLA", 8) == 0)) {
    return CrilaylaDecompress(blob, error);
  }
  return blob;
}

}  // namespace pes
