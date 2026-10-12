#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pes {

// Where PES takes every kit config from (the kit loader FUN_141ea3190 looks the
// file name up in this table; the loose *_realUni.bin only gates existence).
inline constexpr const char* kUniformParameterPath =
    "common/character0/model/character/uniform/team/UniformParameter.bin";

// u32 count, u32 tableOff, then {u32 dataOff, u32 size, u32 nameOff} records.
// Vanilla ships it zlib-wrapped in a WESYS container; mods ship it raw.
struct UniformTable {
  std::vector<uint8_t> data;
  struct Entry { std::string name; uint32_t offset, size; };
  std::vector<Entry> entries;
  std::optional<std::span<const uint8_t>> Find(std::string_view name) const;
};
std::optional<UniformTable> ParseUniformTable(std::span<const uint8_t> blob, std::string* error);

// Sleeve code: type indexes {_short, _long, _half, _inner}; model 0 means 1.
struct SleeveCode {
  int type = 0, model = 1;
  bool operator==(const SleeveCode&) const = default;
};

// The 120-byte *_realUni.bin fields that pick parts and textures.
struct KitConfig {
  SleeveCode shortSleeves, longSleeves;  // after SleeveLimits routing
  int shortsModel = 1;                   // pants_%03d
  int collar = 0, winterCollar = 0;      // collar_%03d, short / long sleeves
  bool tight = false;
  int shirtPattern = 0;
  std::string kitFile, backNumbersFile, chestNumbersFile, legNumbersFile, nameFontFile;
};
std::optional<KitConfig> ParseRealUni(std::span<const uint8_t> record, std::string* error);

// FUN_141ea2400 with real=1. kitIndex 0 = 1st .. 8 = 9th. The competition tag is
// always _DEF_: ponytail: FUN_1414bda30's other tags (_LB_, _JL_ ...) belong to
// licensed leagues absent from this install; add them when one is needed.
std::string RealUniName(int teamId, int kitIndex, bool goalkeeper);

// Per-player appearance the part choice depends on. Defaults are the record
// every 4cc player carries in PlayerAppearance.bin (short sleeves, socks 0,
// shirt out); the real values live in the user's EDIT save.
struct PlayerLook {
  bool longSleeves = false;
  int socks = 0;  // 0 middle, 1 long, 2 short, 3 no guard
  bool shirtTailOut = true;
  int gloveId = 0;  // glove/g%04d, 0 = none
  int bootsId = 0;  // boots/k%04d, 0 = look the player up in BootsList.bin
  int skin = 1;     // 1..6, arm_N_bsm / thigh_N_bsm
};

// One body part model: a loose fmdl in the VFS, or a member of
// common_package.fpk (kCommonPackagePath).
struct UniformPart {
  bool inCommonPackage = false;
  std::string path;
};
inline constexpr const char* kCommonPackagePath = "Asset/model/character/#Win/common_package.fpk";

// FUN_1419637c0, without the ".fmdl".
std::string SleeveModelName(SleeveCode code, bool glove, bool cap, bool tight);

// The uniform and bare-body parts an outfield player wears (FUN_141a89120 and
// its arm/thigh table), in no particular order. Face pack, gloves and boots are
// chosen elsewhere.
std::vector<UniformPart> SelectUniformParts(const KitConfig& kit, const PlayerLook& look);

// Skin textures the engine binds to arm_mat / thigh_mat (FUN_1419112b0).
std::string ArmSkinPath(int skin);
std::string ThighSkinPath(int skin);

// BootsList.bin: sorted {u32 playerId, u32 bootsId} pairs, no header.
inline constexpr const char* kBootsListPath = "common/character0/model/character/boots/BootsList.bin";
// The list's default for an unlisted player, read live (list object +0x18) and
// also the hard-coded result when the list is not loaded (FUN_1414e03c0).
inline constexpr int kBootsListDefaultId = 62;
using BootsList = std::vector<std::pair<uint32_t, uint32_t>>;
std::optional<BootsList> ParseBootsList(std::span<const uint8_t> blob, std::string* error);
// The appearance's boots id, or the player's BootsList entry when it is 0.
int ResolveBootsId(int appearanceBootsId, int playerId, const BootsList& list);
std::string BootsPackPath(int bootsId);
std::string GlovePackPath(int gloveId);

}  // namespace pes
