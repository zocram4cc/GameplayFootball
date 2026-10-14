#include "pes/mbinfo.hpp"

#include <algorithm>
#include <cstring>

namespace pes {
namespace {

constexpr const char* ANIMATION_PATH = "common/anime/Mbinfo/bin/Animation.bin";
constexpr const char* HIT_DATA_PATH = "common/anime/Mbinfo/bin/HitData.bin";
constexpr const char* CANCEL_DATA_PATH = "common/anime/Mbinfo/bin/CancelData.bin";

// Record sizes (animselect 2.2; HitData and CancelData measured on the files:
// 315,900 = 2,925 x 108 and 57,828 = 4,819 x 12, each record opening with a
// motion-number word).
constexpr size_t ANIMATION_RECORD = 36;
constexpr size_t HIT_RECORD = 108;
constexpr size_t CANCEL_RECORD = 12;

// Shared word layout: bits 0-12 motion number.
constexpr uint32_t MOTION_MASK = 0x1fff;
constexpr int FRAME_SHIFT = 13;
constexpr uint32_t HIT_FRAME_MASK = 0xff;      // bits 13..20
constexpr int HIT_PART_SHIFT = 27;             // bits 27..31, stored - 1
constexpr uint32_t CANCEL_FRAME_MASK = 0x3ff;  // bits 13..22
constexpr int BLEND_START_SHIFT = 10;          // u32 b bits 10..19
constexpr uint32_t BLEND_START_MASK = 0x3ff;

// HitData field offsets (anim-select 6.1).
constexpr size_t HIT_BALL_Z = 0x24, HIT_BALL_Y = 0x28, HIT_BALL_X = 0x2c;
constexpr size_t HIT_ROOT_X = 0x30, HIT_ROOT_Z = 0x38, HIT_VEL_X = 0x3c, HIT_VEL_Z = 0x40;
constexpr size_t HIT_YAW = 0x44;

template <typename T>
T At(const std::vector<uint8_t>& b, size_t off) {
  T v;
  std::memcpy(&v, &b[off], sizeof v);
  return v;
}

template <typename V>
V& Slot(std::vector<V>& v, uint32_t motion) {
  if (v.size() <= motion) v.resize(motion + 1);
  return v[motion];
}

const std::vector<HitRecord> kNoHits;

}  // namespace

const MotionTiming* Mbinfo::Timing(int m) const {
  return m >= 0 && size_t(m) < timing_.size() && timing_[m] ? &*timing_[m] : nullptr;
}
const std::vector<HitRecord>& Mbinfo::Hits(int m) const {
  return m >= 0 && size_t(m) < hits_.size() ? hits_[m] : kNoHits;
}
const CancelRecord* Mbinfo::Cancel(int m) const {
  return m >= 0 && size_t(m) < cancel_.size() && cancel_[m] ? &*cancel_[m] : nullptr;
}

std::optional<Mbinfo> ParseMbinfo(const std::vector<uint8_t>& animation,
                                  const std::vector<uint8_t>& hitData,
                                  const std::vector<uint8_t>& cancelData, std::string* error) {
  if (animation.size() % ANIMATION_RECORD || hitData.size() % HIT_RECORD ||
      cancelData.size() % CANCEL_RECORD) {
    *error = "mbinfo: a file is not a whole number of records";
    return std::nullopt;
  }
  Mbinfo m;
  for (size_t o = 0; o < animation.size(); o += ANIMATION_RECORD) {
    const uint32_t a = At<uint32_t>(animation, o), b = At<uint32_t>(animation, o + 4);
    MotionTiming t;
    t.endFrame = int(a >> FRAME_SHIFT) + 1;
    t.blendStart = int(b >> BLEND_START_SHIFT & BLEND_START_MASK);
    float f[7];
    std::memcpy(f, &animation[o + 8], sizeof f);
    t.rootBlendZ = f[0], t.rootBlendX = f[1], t.rootBlendY = f[2], t.rootFirstX = f[3];
    t.rootFirstZ = f[4], t.cancelSpeed = f[5], t.endMoveAngle = f[6];
    Slot(m.timing_, a & MOTION_MASK) = t;
  }
  for (size_t o = 0; o < hitData.size(); o += HIT_RECORD) {
    const uint32_t w = At<uint32_t>(hitData, o);
    HitRecord h;
    h.frame = int(w >> FRAME_SHIFT & HIT_FRAME_MASK);
    h.contactPart = int(w >> HIT_PART_SHIFT) + 1;
    h.ballX = At<float>(hitData, o + HIT_BALL_X);
    h.ballY = At<float>(hitData, o + HIT_BALL_Y);
    h.ballZ = At<float>(hitData, o + HIT_BALL_Z);
    h.rootX = At<float>(hitData, o + HIT_ROOT_X);
    h.rootZ = At<float>(hitData, o + HIT_ROOT_Z);
    h.rootVelX = At<float>(hitData, o + HIT_VEL_X);
    h.rootVelZ = At<float>(hitData, o + HIT_VEL_Z);
    h.yaw_deg = At<float>(hitData, o + HIT_YAW);
    Slot(m.hits_, w & MOTION_MASK).push_back(h);
  }
  for (size_t o = 0; o < cancelData.size(); o += CANCEL_RECORD) {
    const uint32_t w = At<uint32_t>(cancelData, o);
    Slot(m.cancel_, w & MOTION_MASK) = CancelRecord{int(w >> FRAME_SHIFT & CANCEL_FRAME_MASK)};
  }
  const size_t n = std::max({m.timing_.size(), m.hits_.size(), m.cancel_.size()});
  m.timing_.resize(n), m.hits_.resize(n), m.cancel_.resize(n);
  return m;
}

std::optional<Mbinfo> LoadMbinfo(const PesVfs& vfs, std::string* error) {
  auto a = vfs.Read(ANIMATION_PATH, error);
  if (!a) return std::nullopt;
  auto h = vfs.Read(HIT_DATA_PATH, error);
  if (!h) return std::nullopt;
  auto c = vfs.Read(CANCEL_DATA_PATH, error);
  if (!c) return std::nullopt;
  return ParseMbinfo(*a, *h, *c, error);
}

}  // namespace pes
