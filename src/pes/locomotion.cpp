#include "pes/locomotion.hpp"

#include <algorithm>
#include <cstring>

namespace pes {
namespace {
constexpr size_t RECORD_SIZE = 8;
constexpr int MOTION_NO_BIAS = 0x520;  // parser FUN_1406b2950
constexpr uint32_t MOTION_BITS = 0xfff, GROUP_SHIFT = 12, GROUP_BITS = 0x7f, MIRROR_SHIFT = 19;
constexpr const char* BLEND_TABLE_PATH = "common/anime/AnimeTable/bin/Parametricblendtable.bin";
constexpr uint64_t SPEED_CLASS_TABLE_VA = 0x14344e250;  // .data, read by FUN_1406deb30
}  // namespace

std::vector<BlendMember> ParseBlendTable(const std::vector<uint8_t>& bytes) {
  std::vector<BlendMember> out;
  for (size_t o = 0; o + RECORD_SIZE <= bytes.size(); o += RECORD_SIZE) {
    uint32_t bits;
    float param;
    std::memcpy(&bits, &bytes[o], 4);
    std::memcpy(&param, &bytes[o + 4], 4);
    out.push_back({int(bits & MOTION_BITS) + MOTION_NO_BIAS, int(bits >> GROUP_SHIFT & GROUP_BITS),
                   bool(bits >> MIRROR_SHIFT & 1), param});
  }
  return out;
}

std::optional<std::vector<BlendMember>> LoadBlendTable(const PesVfs& vfs, std::string* error) {
  auto bytes = vfs.Read(BLEND_TABLE_PATH, error);
  if (!bytes) return std::nullopt;
  return ParseBlendTable(*bytes);
}

std::vector<BlendMember> BlendGroup(const std::vector<BlendMember>& table, int group) {
  std::vector<BlendMember> out;
  for (const auto& m : table)
    if (m.group == group) out.push_back(m);
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.param < b.param; });
  return out;
}

Bracketed Bracket(const std::vector<float>& axis, float x) {
  const int n = int(axis.size());
  if (n == 0) return {0, 0, 0.0f};
  if (x <= axis.front()) return {0, 0, 0.0f};
  if (x >= axis.back()) return {n - 1, n - 1, 1.0f};
  int hi = int(std::upper_bound(axis.begin(), axis.end(), x) - axis.begin());
  const int lo = hi - 1;
  const float span = axis[hi] - axis[lo];
  return {lo, hi, span > 0.0f ? (x - axis[lo]) / span : 0.0f};
}

float RateForSpeed(const std::vector<float>& params, const std::vector<float>& speeds_mps, float speed_mps) {
  if (params.empty() || params.size() != speeds_mps.size()) return 0.0f;
  // Member speeds are not monotonic (walk_1_1 outpaces run_1_1): take the
  // first segment that reaches the speed.
  if (speed_mps <= speeds_mps.front()) return params.front();
  for (size_t i = 0; i + 1 < params.size(); ++i) {
    const float a = speeds_mps[i], b = speeds_mps[i + 1];
    if ((speed_mps - a) * (speed_mps - b) <= 0.0f && a != b)
      return params[i] + (params[i + 1] - params[i]) * (speed_mps - a) / (b - a);
  }
  return speed_mps > *std::max_element(speeds_mps.begin(), speeds_mps.end()) ? params.back() : params.front();
}

std::optional<std::array<float, 4>> SpeedClassLimits(const PeImage& exe) {
  std::array<float, 4> out;
  if (!exe.ReadVa(SPEED_CLASS_TABLE_VA, sizeof(out), reinterpret_cast<uint8_t*>(out.data()))) return std::nullopt;
  return out;
}

}  // namespace pes
