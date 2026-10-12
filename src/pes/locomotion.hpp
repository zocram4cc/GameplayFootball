#pragma once

// Locomotion as PES plays it (docs/re/pes-re-animselect.md): steady movement
// is a 1-D parametric blend over speed (Parametricblendtable.bin, group 80
// walk_1_1 .. run_5_5), all members at one normalised phase; the speed classes
// that decide "still" come from the exe.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pes {

inline constexpr int LOCOMOTION_BLEND_GROUP = 80;  // walk_1_1 0 .. run_5_5 1 (report 2.6)

struct BlendMember {
  int motionNo;
  int group;
  bool mirror;
  float param;
};

// 8-byte records: u32 bits 0-11 motionNo - 0x520, 12-18 group, 19 mirror; f32 parameter (FUN_1406b2950).
std::vector<BlendMember> ParseBlendTable(const std::vector<uint8_t>& bytes);
std::optional<std::vector<BlendMember>> LoadBlendTable(const PesVfs& vfs, std::string* error);
std::vector<BlendMember> BlendGroup(const std::vector<BlendMember>& table, int group);  // sorted by param

struct Bracketed {
  int lo, hi;
  float t;  // weight of hi
};
// The two members around `x` on a sorted axis, clamped to its ends.
Bracketed Bracket(const std::vector<float>& axis, float x);
// Blend rate for a ground speed: the lowest rate whose linearly blended member
// root speed equals it. INF (report 4.3.3): matches live rates (0 at 1.4 m/s,
// 0.5 at 3.9) against member root speeds.
float RateForSpeed(const std::vector<float>& params, const std::vector<float>& speeds_mps, float speed_mps);

// Speed-class upper bounds in km/h: still / walk / jog / run (FUN_1406deb30).
std::optional<std::array<float, 4>> SpeedClassLimits(const PeImage& exe);

}  // namespace pes
