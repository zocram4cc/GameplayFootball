#include "peskeeper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pes {
namespace {

// .rdata floats (OBS, docs/re/pes-re-keeper-port.md §5).
constexpr uint64_t ONE_VA = 0x14256ae1c;       // 1.0
constexpr uint64_t TWO_VA = 0x142565604;       // 2.0
constexpr uint64_t FOUR_VA = 0x1425656c0;      // 4.0
constexpr uint64_t SIXTY_VA = 0x14259a764;     // 60.0
constexpr uint64_t NINETY_VA = 0x14259be5c;    // 90.0
constexpr uint64_t ONE_TWENTY_VA = 0x14256ae30;  // 120.0
constexpr uint64_t TWO_POINT_FOUR_VA = 0x1425c7660;  // 2.4
constexpr uint64_t THREE_SIXTY_VA = 0x14259858c;     // 360.0
constexpr uint64_t RAD_TO_DEG_VA = 0x14259be40;      // 57.2957764

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

}  // namespace

std::optional<KeeperConstants> LoadKeeperConstants(const PeImage& exe, std::string* error) {
  KeeperConstants k{};
  bool ok = true;
  for (auto [va, out] : {
           std::pair{ONE_VA, &k.deflectSidewaysRadius_m},
           std::pair{ONE_VA, &k.distributionOwnLineMargin_m},
           std::pair{TWO_VA, &k.deflectRequestedRadius_m},
           std::pair{FOUR_VA, &k.distributionCarryUpfield_m},
           std::pair{FOUR_VA, &k.distributionTargetStep_m},
           std::pair{SIXTY_VA, &k.deflectFarSideAngle_deg},
           std::pair{NINETY_VA, &k.quarterTurn_deg},
           std::pair{ONE_TWENTY_VA, &k.deflectSidewaysGate_deg},
           std::pair{TWO_POINT_FOUR_VA, &k.deflectRaisedHeight_m},
           std::pair{THREE_SIXTY_VA, &k.angleWrap_deg},
           std::pair{RAD_TO_DEG_VA, &k.degreesPerRadian},
       }) {
    ok = ReadFloat(exe, va, out) && ok;
  }
  if (!ok) {
    *error = "keeper constant: an .rdata float is unreadable in this exe";
    return std::nullopt;
  }
  return k;
}

DeflectPoint PesDeflectPoint(float requestAngle_deg, float heading_deg,
                             const KeeperConstants& k) {
  // FUN_1405bb170: the wrapped difference of the requested angle from his
  // heading, in (-180, 180].
  float delta = requestAngle_deg - heading_deg;
  while (delta > k.angleWrap_deg * 0.5f) delta -= k.angleWrap_deg;
  while (delta <= -k.angleWrap_deg * 0.5f) delta += k.angleWrap_deg;

  DeflectPoint p;
  p.raised = true;  // this engine never has a height input: PES lifts the point
  if (delta >= k.deflectSidewaysGate_deg || delta <= -k.deflectSidewaysGate_deg) {
    // The ball is across him: meeting it where it was asked for would push it
    // along his own flank, so he takes the far side, heading ∓ 60 degrees, 1 m
    // out (FUN_140470d00/FUN_140470d60, the wrapped heading ∓ 60).
    float angle = heading_deg + (delta < 0.0f ? -k.deflectFarSideAngle_deg
                                              : k.deflectFarSideAngle_deg);
    angle = std::fmod(angle, k.angleWrap_deg);
    if (angle < 0.0f) angle += k.angleWrap_deg;
    p.radius_m = k.deflectSidewaysRadius_m;
    p.angle_deg = angle;
    return p;
  }
  // Within 120 degrees he meets the ball where it was asked for, 2 m out.
  p.radius_m = k.deflectRequestedRadius_m;
  float angle = std::fmod(requestAngle_deg, k.angleWrap_deg);
  if (angle < 0.0f) angle += k.angleWrap_deg;
  p.angle_deg = angle;
  return p;
}

DistributionPoint PesAfterCatchCarry(float keeperX, float keeperZ, int attackDir,
                                      float ownPenaltyLineX, float goalAreaHalfZ,
                                      const KeeperConstants& k) {
  // AfterCatchMove::exec's kick-off carry (OBS arithmetic, the report's §2.1):
  //   A = |ownPenaltyLineX| + 1;              // FieldInfo+0x1c = 36.14, +1
  //   if (-A <= dir*keeper.x + 4) x = dir*4 + keeper.x;
  //   else                        x = -A*dir;
  //   if (|z| > goalAreaHalfZ)    z = z - sign(z)*2*goalAreaHalfZ;
  const float dir = attackDir >= 0 ? 1.0f : -1.0f;
  // PES's A = FieldInfo+0x1c + 1 is a MAGNITUDE (36.14 + 1), applied with his
  // attack sign only at the end: the mark itself is 1 m into his own half.
  const float a_m = std::fabs(ownPenaltyLineX) + k.distributionOwnLineMargin_m;
  float x;
  if (-a_m <= dir * keeperX + k.distributionCarryUpfield_m)
    x = keeperX + dir * k.distributionCarryUpfield_m;
  else
    x = -a_m * dir;
  float z = keeperZ;
  if (goalAreaHalfZ < std::fabs(z)) z -= (z < 0.0f ? -1.0f : 1.0f) * 2.0f * goalAreaHalfZ;
  return {x, z};
}

DistributionPoint PesAfterCatchStep(float carryX, float carryZ, float targetX, float targetZ,
                                    int attackDir, float ownPenaltyLineX, float goalAreaHalfZ,
                                    const KeeperConstants& k) {
  // Then FUN_140a8f7c0 steps 4 m farther on the bearing to the chosen teammate
  // (FUN_140a8f7c0 in degrees: p += r·(sin θ, sin(θ+90)) with θ the bearing in
  // degrees), clamped back inside the same two marks.
  const float dir = attackDir >= 0 ? 1.0f : -1.0f;
  const float a_m = std::fabs(ownPenaltyLineX) + k.distributionOwnLineMargin_m;
  const float dx = targetX - carryX;
  const float dz = targetZ - carryZ;
  float x = carryX;
  float z = carryZ;
  if (dx * dx + dz * dz > 0.000001f) {
    const float bearing_deg = std::atan2(dz, dx) * k.degreesPerRadian;
    const float quarter = k.quarterTurn_deg / k.degreesPerRadian;  // 90 degrees
    x += std::sin(bearing_deg / k.degreesPerRadian) * k.distributionTargetStep_m;
    z += std::sin(bearing_deg / k.degreesPerRadian + quarter) * k.distributionTargetStep_m;
  }
  // PES's post-step clamp: `if (-A < dir*x) x = -A*dir`, i.e. the target may not
  // sit nearer the far side of the pitch than 1 m short of the line on his own
  // half.
  if (-a_m < dir * x) x = -a_m * dir;
  if (goalAreaHalfZ < std::fabs(z)) z -= (z < 0.0f ? -1.0f : 1.0f) * 2.0f * goalAreaHalfZ;
  return {x, z};
}

}  // namespace pes
