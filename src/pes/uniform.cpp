// Kit config: UniformParameter.bin and the 120-byte *_realUni.bin records it
// holds. Layout from the game's own parser FUN_141ea3190 (size == 0x78 branch),
// checked field by field against the kit struct of the running game.
#include "pes/uniform.hpp"

#include <cstring>

#include "pes/inflate.hpp"

namespace pes {
namespace {

constexpr uint8_t WESYS_MAGIC[] = {0x05, 0x10, 0xd1, 0x57, 'W', 'E', 'S', 'Y', 'S'};
constexpr size_t WESYS_ZLIB_START = 0x10;
constexpr size_t TABLE_HEADER_BYTES = 8, TABLE_RECORD_BYTES = 12;

constexpr size_t REAL_UNI_BYTES = 0x78;
constexpr size_t SHORTS_MODEL = 3, COLLAR = 0x14, WINTER_COLLAR = 0x15;
constexpr size_t SLEEVE_ROUTING_WORD = 24;  // d6
constexpr int SLEEVE_LIMITS_SHIFT = 29, TIGHT_KIT_SHIFT = 31;
constexpr size_t PATTERN_WORD = 36;  // d9
constexpr int SHIRT_PATTERN_SHIFT = 5;
constexpr uint32_t SHIRT_PATTERN_MASK = 7;
constexpr size_t KIT_FILE = 0x28, BACK_NUMBERS_FILE = 0x38, CHEST_NUMBERS_FILE = 0x48,
                 LEG_NUMBERS_FILE = 0x58, NAME_FONT_FILE = 0x68, FILE_NAME_BYTES = 16;
constexpr int SLEEVE_CODE_TYPE_DIVISOR = 1000;  // code = type * 1000 + model
enum SleeveLimits { ROUTE_SHORT_A_LONG_B = 0, ROUTE_BOTH_B = 1, ROUTE_BOTH_A = 2 };

template <typename T>
T Load(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

SleeveCode DecodeSleeve(int code) {
  SleeveCode c{code / SLEEVE_CODE_TYPE_DIVISOR, code % SLEEVE_CODE_TYPE_DIVISOR};
  if (c.model == 0) c.model = 1;
  return c;
}

std::string FileName(std::span<const uint8_t> r, size_t at) {
  const char* s = reinterpret_cast<const char*>(r.data() + at);
  return std::string(s, strnlen(s, FILE_NAME_BYTES));
}

}  // namespace

std::optional<std::span<const uint8_t>> UniformTable::Find(std::string_view name) const {
  for (const Entry& e : entries)
    if (e.name == name) return std::span<const uint8_t>(data).subspan(e.offset, e.size);
  return std::nullopt;
}

std::optional<UniformTable> ParseUniformTable(std::span<const uint8_t> blob, std::string* error) {
  UniformTable t;
  if (blob.size() >= sizeof WESYS_MAGIC && std::memcmp(blob.data(), WESYS_MAGIC, sizeof WESYS_MAGIC) == 0) {
    if (blob.size() < WESYS_ZLIB_START ||
        !Inflate(blob.data() + WESYS_ZLIB_START, blob.size() - WESYS_ZLIB_START, &t.data)) {
      *error = "UniformParameter: corrupt WESYS zlib stream";
      return std::nullopt;
    }
  } else {
    t.data.assign(blob.begin(), blob.end());
  }
  const std::vector<uint8_t>& d = t.data;
  if (d.size() < TABLE_HEADER_BYTES) {
    *error = "UniformParameter: truncated header";
    return std::nullopt;
  }
  const uint32_t count = Load<uint32_t>(&d[0]), table = Load<uint32_t>(&d[4]);
  if (table > d.size() || count > (d.size() - table) / TABLE_RECORD_BYTES) {
    *error = "UniformParameter: record table past end";
    return std::nullopt;
  }
  t.entries.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* rec = &d[table + TABLE_RECORD_BYTES * i];
    const uint32_t off = Load<uint32_t>(rec), size = Load<uint32_t>(rec + 4), name = Load<uint32_t>(rec + 8);
    if (off > d.size() || size > d.size() - off || name >= d.size()) {
      *error = "UniformParameter: entry " + std::to_string(i) + " past end";
      return std::nullopt;
    }
    const char* s = reinterpret_cast<const char*>(&d[name]);
    t.entries.push_back({std::string(s, strnlen(s, d.size() - name)), off, size});
  }
  return t;
}

std::optional<KitConfig> ParseRealUni(std::span<const uint8_t> r, std::string* error) {
  if (r.size() != REAL_UNI_BYTES) {
    *error = "realUni: expected 120 bytes, got " + std::to_string(r.size());
    return std::nullopt;
  }
  // Two 12-bit codes packed over bytes 0-2.
  const SleeveCode a = DecodeSleeve(r[0] | (r[1] & 0xF) << 8);
  const SleeveCode b = DecodeSleeve(r[1] >> 4 | r[2] << 4);
  const uint32_t routing = Load<uint32_t>(&r[SLEEVE_ROUTING_WORD]);
  KitConfig k;
  switch ((routing >> SLEEVE_LIMITS_SHIFT) & 3) {
    case ROUTE_BOTH_B: k.shortSleeves = k.longSleeves = b; break;
    case ROUTE_BOTH_A: k.shortSleeves = k.longSleeves = a; break;
    default: k.shortSleeves = a, k.longSleeves = b;
  }
  k.tight = (routing >> TIGHT_KIT_SHIFT) & 1;
  k.shortsModel = r[SHORTS_MODEL] ? r[SHORTS_MODEL] : 1;
  k.collar = r[COLLAR];
  k.winterCollar = r[WINTER_COLLAR];
  k.shirtPattern = (Load<uint32_t>(&r[PATTERN_WORD]) >> SHIRT_PATTERN_SHIFT) & SHIRT_PATTERN_MASK;
  k.kitFile = FileName(r, KIT_FILE);
  k.backNumbersFile = FileName(r, BACK_NUMBERS_FILE);
  k.chestNumbersFile = FileName(r, CHEST_NUMBERS_FILE);
  k.legNumbersFile = FileName(r, LEG_NUMBERS_FILE);
  k.nameFontFile = FileName(r, NAME_FONT_FILE);
  return k;
}

std::string RealUniName(int teamId, int kitIndex, bool goalkeeper) {
  static constexpr const char* ORDINALS[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th"};
  return std::to_string(teamId) + "_DEF_" + (goalkeeper ? "GK" : "") + ORDINALS[kitIndex] + "_realUni.bin";
}

}  // namespace pes
