// Port of GameplayFootball tools/pes21_import/gani.py: full curve decode of
// GZ-generation Fox animations (bitstream quats, AnimHalf vectors, hermite
// tangents, static units).
#include "pes/gani.hpp"

#include <cmath>
#include <cstring>

namespace pes {
namespace {

constexpr uint32_t GANI_MAGIC = 0x0BFCA2D2;
constexpr char MOTION_TAG[] = "MOTION";
constexpr size_t MOTION_TRACK_HEADER = 0x40;  // TrackHeader follows the MOTION node
constexpr size_t HDR_FRAME_COUNT = 12, HDR_FRAME_SCALE = 16, HDR_UNIT_OFFSETS = 20;
constexpr uint32_t MAX_UNITS = 256, MAX_SEGMENTS = 1024;  // GF's plausibility bounds
constexpr size_t UNIT_HEADER_BYTES = 8, SEGMENT_HEADER_BYTES = 8;
constexpr uint8_t SEGMENT_TYPE_MASK = 0x0F;
constexpr int SIGN_BITS = 3, DELTA_BITS = 8;
constexpr int HALF_BITS = 16;
constexpr uint32_t ANIM_HALF_EXP_REBASE = 0x1DC00;

// Validation thresholds (GF gani.py validate()).
constexpr double QUAT_NORM_TOLERANCE = 1e-3;
// (GF's quaternion-jump and |v| > 1e5 checks are not used: PES's own clips
// turn > 120 deg between sparse keys and travel up to 13 m.)

template <typename T>
bool Load(std::span<const uint8_t> b, size_t at, T* out) {
  if (at + sizeof(T) > b.size()) return false;
  std::memcpy(out, b.data() + at, sizeof(T));
  return true;
}

class BitReader {
 public:
  BitReader(std::span<const uint8_t> b, size_t bitPos) : b_(b), pos_(bitPos) {}
  bool ok = true;
  uint32_t Read(int n) {
    if (n == 0) return 0;
    uint64_t raw = 0;
    const size_t byte = pos_ >> 3, shift = pos_ & 7, bytes = (shift + n + 7) >> 3;
    if (byte + bytes > b_.size()) { ok = false; return 0; }
    for (size_t i = 0; i < bytes; ++i) raw |= uint64_t(b_[byte + i]) << (8 * i);
    pos_ += n;
    return uint32_t((raw >> shift) & ((uint64_t(1) << n) - 1));
  }
  Quat ReadQuat(int bits) {
    const uint32_t a = Read(bits), b = Read(bits), c = Read(bits), s = Read(SIGN_BITS);
    return DequantQuat(a, b, c, s, bits);
  }

 private:
  std::span<const uint8_t> b_;
  size_t pos_;
};

int ComponentCount(uint8_t type) {
  switch (type) {
    case GANI_FLOAT: return 1;
    case GANI_VECTOR2: return 2;
    case GANI_VECTOR4: return 4;
    default: return 3;  // VECTOR3, VECTOR_DIFF
  }
}

bool ReadComps(std::span<const uint8_t> b, size_t* at, int count, int bits, Vec4* out) {
  float c[4] = {0, 0, 0, 0};
  for (int i = 0; i < count; ++i) {
    if (bits == HALF_BITS) {
      uint16_t h;
      if (!Load(b, *at, &h)) return false;
      c[i] = AnimHalf(h);
      *at += 2;
    } else if (bits != 0) {
      if (!Load(b, *at, &c[i])) return false;
      *at += 4;
    }
  }
  *out = {c[0], c[1], c[2], c[3]};
  return true;
}

bool DecodeSegment(std::span<const uint8_t> b, size_t dataAt, bool isStatic, bool hermite, uint32_t frameCount,
                   GaniSegment* seg, std::string* error) {
  seg->frames = {0};
  uint32_t acc = 0;
  if (seg->type == GANI_QUAT || seg->type == GANI_QUAT_DIFF) {
    BitReader r(b, dataAt * 8);
    seg->quats = {r.ReadQuat(seg->componentBits)};
    while (!isStatic && acc < frameCount && r.ok) {
      const uint32_t delta = r.Read(DELTA_BITS);
      acc += delta;
      seg->quats.push_back(r.ReadQuat(seg->componentBits));
      seg->frames.push_back(acc);
      if (delta == 0 && acc < frameCount) { *error = "stuck quat stream"; return false; }
    }
    if (!r.ok) *error = "quat stream past end of clip";
    return r.ok;
  }
  const int comps = ComponentCount(seg->type);
  size_t at = dataAt;
  Vec4 v;
  if (!ReadComps(b, &at, comps, seg->componentBits, &v)) { *error = "vector stream past end"; return false; }
  seg->vecs = {v};
  if (hermite) seg->tans = {Vec4{}};
  while (!isStatic && acc < frameCount) {
    uint8_t delta;
    if (!Load(b, at++, &delta)) { *error = "vector stream past end"; return false; }
    acc += delta;
    if (!ReadComps(b, &at, comps, seg->componentBits, &v)) { *error = "vector stream past end"; return false; }
    seg->vecs.push_back(v);
    if (hermite) {
      if (!ReadComps(b, &at, comps, seg->componentBits, &v)) { *error = "tangent stream past end"; return false; }
      seg->tans.push_back(v);
    }
    seg->frames.push_back(acc);
    if (delta == 0 && acc < frameCount) { *error = "stuck vector stream"; return false; }
  }
  return true;
}

}  // namespace

float AnimHalf(uint16_t bits) {
  uint32_t num = bits & 0x7C00u;
  if (num > 0) num = (num + ANIM_HALF_EXP_REBASE) << 13;
  num |= (uint32_t(bits & 0x8000u) << 16) | (uint32_t(bits & 0x3FFu) << 13);
  float f;
  std::memcpy(&f, &num, 4);
  return f;
}

Quat DequantQuat(uint32_t a, uint32_t b, uint32_t c, uint32_t signs, int bitSize) {
  const double mask = double((uint64_t(1) << bitSize) - 1);
  const double x = b / mask, y = c / mask, halfTheta = (a / mask) * M_PI * 0.5;
  const double z = (1.0 - x) - y;
  const double lensq = (z * z + y * y) + x * x;
  const double invLen = lensq > 0.0 ? 1.0 / std::sqrt(lensq) : 0.0;
  const double f = std::sin(halfTheta) * invLen;
  return {float(x * (signs & 1 ? -1 : 1) * f), float(y * (signs & 2 ? -1 : 1) * f),
          float(z * (signs & 4 ? -1 : 1) * f), float(std::cos(halfTheta))};
}

std::optional<Gani> ParseGani(std::span<const uint8_t> b, std::string* error) {
  uint32_t magic = 0;
  if (!Load(b, 0, &magic) || magic != GANI_MAGIC) { *error = "not a gani"; return std::nullopt; }
  const std::string_view view(reinterpret_cast<const char*>(b.data()), b.size());
  const size_t motionAt = view.find(std::string_view(MOTION_TAG, sizeof MOTION_TAG - 1));
  if (motionAt == std::string_view::npos) { *error = "no MOTION section"; return std::nullopt; }

  Gani g;
  const size_t hdr = motionAt + MOTION_TRACK_HEADER;
  uint32_t unitCount = 0, segmentCount = 0;
  int8_t scale = 1;
  if (!Load(b, hdr, &unitCount) ||
      !Load(b, hdr + 4, &segmentCount) || !Load(b, hdr + HDR_FRAME_COUNT, &g.frameCount) ||
      !Load(b, hdr + HDR_FRAME_SCALE, &scale)) {
    *error = "truncated track header";
    return std::nullopt;
  }
  g.frameScale = scale;
  if (unitCount < 1 || unitCount > MAX_UNITS || segmentCount < 1 || segmentCount > MAX_SEGMENTS) {
    *error = "implausible track header";
    return std::nullopt;
  }

  uint32_t totalSegments = 0;
  for (uint32_t u = 0; u < unitCount; ++u) {
    uint32_t unitOffset = 0;
    if (!Load(b, hdr + HDR_UNIT_OFFSETS + 4 * u, &unitOffset)) { *error = "truncated unit table"; return std::nullopt; }
    if (unitOffset == 0) continue;
    size_t at = hdr + unitOffset;
    GaniUnit unit;
    uint8_t segCount = 0;
    if (!Load(b, at, &unit.nameHash) || !Load(b, at + 4, &segCount) || !Load(b, at + 5, &unit.flags)) {
      *error = "truncated unit";
      return std::nullopt;
    }
    at += UNIT_HEADER_BYTES;
    std::vector<size_t> dataAt;
    for (uint8_t s = 0; s < segCount; ++s, at += SEGMENT_HEADER_BYTES) {
      int32_t rel = 0;
      uint8_t packed = 0;
      GaniSegment seg;
      if (!Load(b, at, &rel) || !Load(b, at + 6, &packed) || !Load(b, at + 7, &seg.componentBits)) {
        *error = "truncated segment";
        return std::nullopt;
      }
      seg.type = packed & SEGMENT_TYPE_MASK;
      dataAt.push_back(size_t(int64_t(at) + rel));
      unit.segments.push_back(std::move(seg));
    }
    totalSegments += segCount;
    for (size_t s = 0; s < unit.segments.size(); ++s)
      if (!DecodeSegment(b, dataAt[s], unit.flags & GANI_STATIC, unit.flags & GANI_HERMITE, g.frameCount,
                         &unit.segments[s], error))
        return std::nullopt;
    g.units.push_back(std::move(unit));
  }
  if (totalSegments != segmentCount) {
    *error = "segment count mismatch";
    return std::nullopt;
  }
  return g;
}

std::vector<std::string> ValidateGani(const Gani& g) {
  std::vector<std::string> problems;
  for (size_t u = 0; u < g.units.size(); ++u) {
    const auto& unit = g.units[u];
    for (size_t s = 0; s < unit.segments.size(); ++s) {
      const auto& seg = unit.segments[s];
      const std::string tag = "unit" + std::to_string(u) + "/seg" + std::to_string(s);
      if (!(unit.flags & GANI_STATIC) && seg.frames.back() < g.frameCount)
        problems.push_back(tag + ": keys end before the last frame");
      for (const auto& q : seg.quats)
        if (std::fabs(std::sqrt(double(q.x) * q.x + q.y * q.y + q.z * q.z + q.w * q.w) - 1.0) > QUAT_NORM_TOLERANCE) {
          problems.push_back(tag + ": non-unit quaternion");
          break;
        }
      for (const auto& v : seg.vecs)
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.w)) {
          problems.push_back(tag + ": non-finite vector");
          break;
        }
    }
  }
  return problems;
}

}  // namespace pes
