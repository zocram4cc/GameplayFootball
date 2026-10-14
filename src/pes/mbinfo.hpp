#pragma once

// PES's per-motion metadata, the Mbinfo bins (docs/re/pes-re-animselect.md 2.1-2.2,
// pes-re-anim-select.md 5.3 and 6.1), read from the user's install:
//   Animation.bin  36-byte records: end frame, blend start, root offsets
//   HitData.bin   108-byte records: the frame and place a clip meets the ball
//   CancelData.bin 12-byte records: the frame a clip may be cancelled from
// Frames are gani frames (30 Hz); positions cm, as stored.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pes/vfs.hpp"

namespace pes {

struct MotionTiming {
  int endFrame = -1;    // last frame (u32 a bits 13+ = end - 1)
  int blendStart = -1;  // u32 b bits 10-19
  // root_pos_blend_z/x/y, root_pos_first_x/z, cancel_speed, end_move_angle
  float rootBlendZ = 0, rootBlendX = 0, rootBlendY = 0, rootFirstX = 0, rootFirstZ = 0;
  float cancelSpeed = 0, endMoveAngle = 0;
};

struct HitRecord {
  int frame = 0;        // u32 +0 bits 13..20
  int contactPart = 0;  // bits 27..31 + 1
  float ballX = 0, ballY = 0, ballZ = 0;  // +0x2c / +0x28 / +0x24, cm
  float rootX = 0, rootZ = 0;             // +0x30 / +0x38, cm
  float rootVelX = 0, rootVelZ = 0;       // +0x3c / +0x40, cm per frame
  float yaw_deg = 0;                      // +0x44
};

struct CancelRecord {
  int frame = 0;  // u32 bits 13..22
};

class Mbinfo {
 public:
  // Null when the motion has no record of that kind.
  const MotionTiming* Timing(int motionNo) const;
  const std::vector<HitRecord>& Hits(int motionNo) const;
  const CancelRecord* Cancel(int motionNo) const;
  size_t MotionCount() const { return hits_.size(); }

 private:
  friend std::optional<Mbinfo> ParseMbinfo(const std::vector<uint8_t>&, const std::vector<uint8_t>&,
                                           const std::vector<uint8_t>&, std::string*);
  std::vector<std::optional<MotionTiming>> timing_;
  std::vector<std::vector<HitRecord>> hits_;
  std::vector<std::optional<CancelRecord>> cancel_;
};

std::optional<Mbinfo> ParseMbinfo(const std::vector<uint8_t>& animation,
                                  const std::vector<uint8_t>& hitData,
                                  const std::vector<uint8_t>& cancelData, std::string* error);
std::optional<Mbinfo> LoadMbinfo(const PesVfs& vfs, std::string* error);

}  // namespace pes
