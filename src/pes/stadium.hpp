#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "pes/types.hpp"
#include "pes/vfs.hpp"

namespace pes {

// A model's placement in Fox world space (metres; X long axis, Z width, Y up),
// exactly as the fox2 TransformEntity says — scale, rotation (x,y,z,w) and
// translation. Identity by default (PES leaves unrotated entities this way).
struct Transform {
  Vec3 scale = {1, 1, 1};
  Quat rotation = {0, 0, 0, 1};
  Vec3 translation = {0, 0, 0};
};

// One model a stadium's fox2 files place: a fmdl inside a sibling .fpk with a
// transform. fpk is the VFS path of the .fpk; member is the FpkEntry::path of
// the .fmdl inside it (the modelFile FilePtr, leading '/' stripped).
struct PlacedModel {
  std::string entityName;  // the fox2 "name" property, e.g. "StadiumModel_C1"
  std::string className;   // e.g. "StadiumModel", "GoalnetData", "MobCharacterModel"
  std::string fpk;         // VFS path of the .fpk holding the fmdl
  std::string member;      // FpkEntry::path of the .fmdl inside it
  Transform transform;
  uint32_t kind = 0;       // fox2 "kind" (0 scene/center, 1 weather overlay, ...)
  uint32_t direction = 0;  // fox2 "direction" (0..4 compass)
  uint32_t flags = 0;      // fox2 "flags"
};

// One row of common/etc/pesdb/Stadium.bin: the stadium id. Its content folder
// is derived from the id ("st" + id zero-padded to 3 digits, e.g. "st002");
// the string at +370 is not the folder (vanilla holds names like "CAMP_NOU").
struct StadiumInfo {
  int id = 0;
};

// Pure parse of Stadium.bin (after WESYS unwrap): 392-byte records, u16 id at +4.
std::vector<StadiumInfo> ParseStadiumRecords(std::span<const uint8_t> blob,
                                             std::string* error);

// ReadStadiumList reads common/etc/pesdb/Stadium.bin from the VFS and returns
// its records. Returns an empty vector (with *error set) on failure.
std::vector<StadiumInfo> ReadStadiumList(const PesVfs& vfs, std::string* error);

// Assemble one stadium's model placements from its fox2 files, in this order:
// the scene (<folder>.fpkd), the day/fine overlay (<folder>_df.fpkd, when the
// pack ships one), the pitch (pitch/<folder>.fpkd), the goals (common goal_post
// .fpkd: post + shadow meshes), the two nets (common goalnet_{l,r}.fpkd), and
// the corner flags (a per-stadium override when present, else common). Models
// whose fmdl member is not packaged in the sibling .fpk are dropped — PES
// cannot place a model its pack does not ship. The scene, pitch and goal_post
// sources are required; the rest are best-effort.
std::optional<std::vector<PlacedModel>> LoadStadiumScene(const PesVfs& vfs,
                                                         int stadiumId,
                                                         std::string* error);

}  // namespace pes
