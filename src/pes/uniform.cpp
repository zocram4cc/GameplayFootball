// Kit config: UniformParameter.bin and the 120-byte *_realUni.bin records it
// holds. Layout from the game's own parser FUN_141ea3190 (size == 0x78 branch),
// checked field by field against the kit struct of the running game.
#include "pes/uniform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "pes/inflate.hpp"

namespace pes {
namespace {

// WESYS: u8 type, 0x10, u8 flags, "WESYS" at +3, u32 zlib/raw sizes, zlib
// from +0x10 (vanilla UniformParameter "\x05\x10\xd1WESYS", constants "\xff\x10\x51WESYS").
constexpr char WESYS_TAG[] = {'W', 'E', 'S', 'Y', 'S'};
constexpr size_t WESYS_TAG_OFFSET = 3;
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

std::string Numbered(const char* format, int n) {
  char s[96];
  std::snprintf(s, sizeof s, format, n);
  return s;
}

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
  if (blob.size() >= WESYS_TAG_OFFSET + sizeof WESYS_TAG &&
      std::memcmp(blob.data() + WESYS_TAG_OFFSET, WESYS_TAG, sizeof WESYS_TAG) == 0) {
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
  auto bits = [&](size_t word, int lo, int n) { return int((Load<uint32_t>(&r[4 * word]) >> lo) & ((1u << n) - 1)); };
  k.legNumberH = bits(5, 16, 5), k.legNumberV = bits(5, 21, 5), k.legNumberSize = bits(5, 26, 5), k.legNumberSide = bits(5, 31, 1);
  k.backNumberY = bits(6, 0, 5), k.backNumberSize = bits(6, 5, 5), k.backNumberSpacing = bits(6, 10, 4);
  k.chestNumberY = bits(6, 14, 5), k.chestNumberX = bits(6, 19, 5), k.chestNumberSize = bits(6, 24, 5);
  k.backNumberType = bits(7, 2, 1), k.nameY = bits(7, 3, 6), k.nameSize = bits(7, 9, 5), k.nameShape = bits(7, 14, 3);
  k.nameStretch = bits(9, 8, 2);
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

std::string SleeveModelName(SleeveCode code, bool glove, bool cap, bool tight) {
  static constexpr const char* TYPE_SUFFIX[] = {"_short", "_long", "_half", "_inner"};
  enum { SHORT = 0, LONG = 1, HALF = 2, INNER = 3 };
  glove = glove && code.type != SHORT && code.type != HALF;
  const bool longGlove = code.type == LONG && glove;  // one model, no suffix or number
  std::string name = code.type == INNER ? "s" : "sleeve";
  if (!longGlove) name += TYPE_SUFFIX[code.type & 3];
  if (glove) name += "_glove";
  if (cap) name += "_cap";
  if (tight) name += "_tight";
  if (!longGlove) {
    char num[8];
    std::snprintf(num, sizeof num, "_%03d", code.model);
    name += num;
  }
  return name;
}

std::vector<UniformPart> SelectUniformParts(const KitConfig& kit, const PlayerLook& look) {
  static const std::string NOCLOTH = "Asset/model/character/uniform/nocloth/#Win/";
  static constexpr const char* SOCKS[] = {"socks_middle", "socks_long", "socks_short", "socks_noguard"};
  static constexpr const char* THIGHS[] = {"thigh_middle", "thigh_long", "thigh_short", "thigh_middle"};
  enum { LONG = 1, HALF = 2, INNER = 3 };
  auto loose = [](const std::string& name) { return UniformPart{false, NOCLOTH + name + ".fmdl"}; };
  auto common = [](const std::string& name) { return UniformPart{true, "common/" + name + ".fmdl"}; };
  auto numbered = [](const char* stem, int n) {
    char s[32];
    std::snprintf(s, sizeof s, "%s_%03d", stem, n);
    return std::string(s);
  };

  // Long sleeves wear the long slot and the winter collar (FUN_141a89120).
  const SleeveCode sleeve = look.longSleeves ? kit.longSleeves : kit.shortSleeves;
  const std::string tight = kit.tight ? "_tight" : "";
  std::vector<UniformPart> parts = {
      loose(numbered("pants", kit.shortsModel)),
      loose(numbered("collar", look.longSleeves ? kit.winterCollar : kit.collar)),
      loose((look.shirtTailOut ? "shirt_out" : "shirt_in") + tight),
      loose(SleeveModelName(sleeve, look.gloveId != 0, false, kit.tight)),
      common(SOCKS[look.socks & 3]),
      common(THIGHS[look.socks & 3]),
      common(kit.tight ? "pants_out_sub_tight" : sleeve.type == INNER ? "pants_out_inner_sub" : "pants_out_sub"),
      common("hand_l"),
      common("hand_r"),
      common("eye_new"),
      common("mouth"),
  };
  if (!look.shirtTailOut) parts.push_back(common("pants_in_sub"));
  // FUN_141a8d530: no bare arm at all under a worn long or inner sleeve (by type,
  // even when the sleeve model is missing); otherwise half sleeves bare a half
  // arm and gloves a gloved one.
  if (sleeve.type != LONG && sleeve.type != INNER) {
    parts.push_back(common(sleeve.type == HALF ? "arm_half" : look.gloveId ? "arm_glove" : "arm"));
    parts.push_back(common(look.gloveId ? "arm_glove_cap" : "arm_cap"));
  }
  return parts;
}

std::string ArmSkinPath(int skin) { return Numbered("Asset/model/character/common/sourceimages/#windx11/arm_%d_bsm.ftex", skin); }
std::string ThighSkinPath(int skin) { return Numbered("Asset/model/character/common/sourceimages/#windx11/thigh_%d_bsm.ftex", skin); }

std::optional<BootsList> ParseBootsList(std::span<const uint8_t> blob, std::string* error) {
  constexpr size_t PAIR_BYTES = 8;
  if (blob.size() % PAIR_BYTES != 0) {
    *error = "BootsList: size is not whole {playerId, bootsId} pairs";
    return std::nullopt;
  }
  BootsList list(blob.size() / PAIR_BYTES);
  for (size_t i = 0; i < list.size(); ++i)
    list[i] = {Load<uint32_t>(&blob[PAIR_BYTES * i]), Load<uint32_t>(&blob[PAIR_BYTES * i + 4])};
  return list;
}

int ResolveBootsId(int appearanceBootsId, int playerId, const BootsList& list) {
  if (appearanceBootsId != 0) return appearanceBootsId;
  // Sorted by player id, binary-searched like FUN_1414e03c0.
  auto it = std::lower_bound(list.begin(), list.end(), uint32_t(playerId),
                             [](const auto& pair, uint32_t id) { return pair.first < id; });
  return it != list.end() && it->first == uint32_t(playerId) ? int(it->second) : kBootsListDefaultId;
}

std::string BootsPackPath(int bootsId) { return Numbered("Asset/model/character/boots/k%04d/#Win/boots.fpk", bootsId); }
std::string GlovePackPath(int gloveId) { return Numbered("Asset/model/character/glove/g%04d/#Win/glove.fpk", gloveId); }

// Converters: every config int maps linearly, lo + v*(hi-lo)/N (realUni
// parser FUN_141ea3190 -> FUN_141e58c80 / 8d90 / 594a0 / 59150 / 59240), then
// the shader vectors of FUN_1419a44f0 (back), 4bf0 (chest), 5000 (leg) and
// 4d20 (name). Formulas and live checks: pes-kit-assembly.md 8.7.
namespace {
float Back(int v) { return 1.2f - v * 1.2f / 29.0f; }  // BackNumberY
float ChestX(int v) { return 0.4f + v * 0.207f / 29.0f; }
float ChestY(int v) { return 1.0f - (0.22f + v * 0.24f / 29.0f); }
float NumberSize(int v) { return 0.6f + v * 1.2f / 28.0f; }  // chest and leg
float LegH(int v, int side) { return side == 0 ? 0.96f - v * 0.19f / 29.0f : 0.04f + v * 0.19f / 29.0f; }
float LegV(int v) { return 1.0f - (0.1f + v * 0.14f / 29.0f); }
float NameY(int v) { return 1.0f - (0.95f - v * 0.37f / 39.0f); }
float NameSizeF(int v) { return 0.9f + v * 1.5f / 20.0f; }
float NameStretchF(int v) { return 0.5f + (v ? v : 3) * 0.5f / 3.0f; }  // 0 means 3
constexpr float NAME_CURVATURE[] = {0.0f, -0.002f, -0.016f, -0.019f};  // FUN_1419a4e30, by NameShape
constexpr float MARKING_UNIT = 16.0f;     // shader scale numerator
constexpr float ZERO_SIZE_SCALE = 1e-4f;  // FUN_1419a4bf0 guard for size 0
constexpr float NAME_SPACE_UNITS = 512.0f;
}  // namespace

Vec4 BackNumberScale(const KitConfig& k) {
  const float size = k.backNumberSize / 28.0f, y = Back(k.backNumberY);
  const float s = 12.0f * size - 20.0f, offset = -(0.27557f - 0.1f * y) * s;
  return {0.5f * s, s, offset, offset};
}

Vec4 ChestNumberScale(const KitConfig& k) {
  const float size = NumberSize(k.chestNumberSize);
  const float s = size != 0 ? MARKING_UNIT / size : ZERO_SIZE_SCALE;
  return {s, s, 0.5f - s * ChestX(k.chestNumberX), 0.5f - s * ChestY(k.chestNumberY)};
}

Vec4 LegNumberScale(const KitConfig& k) {
  const float x = LegH(k.legNumberH, k.legNumberSide), y = LegV(k.legNumberV);
  const float size = x > 0.5f ? -NumberSize(k.legNumberSize) : NumberSize(k.legNumberSize);  // right leg mirrors
  const float s = MARKING_UNIT / size;
  return {s, s, 0.5f - s * x, 0.5f - s * y};
}

Vec4 NameScale(const KitConfig& k, float glyphWidthSum) {
  const float sy = MARKING_UNIT / NameSizeF(k.nameSize);
  const float off = 0.5f - sy * (NameY(k.nameY) + NAME_CURVATURE[k.nameShape & 3]);
  const float w = std::min(1.0f, glyphWidthSum / 2.0f / NAME_SPACE_UNITS);
  return {w > 0 ? 4.0f / (w * NameStretchF(k.nameStretch)) : 0.0f, sy, off, off};
}

}  // namespace pes
