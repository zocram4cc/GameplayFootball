// PES 2021's presser picker arithmetic (docs/re/pes-re-press-layer-port.md).
// See pespress.hpp for what each function is and where its constants live.

#include "onthepitch/pespress.hpp"

#include <cmath>
#include <cstring>

namespace pes {
namespace {

// A .rdata float: PES keeps every one of these as a plain float.
bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof(float), reinterpret_cast<uint8_t*>(out));
}

struct PressVa {
  uint64_t va;
  float PressConstants::*field;
};

constexpr PressVa kPressVas[] = {
    {0x14253ee30, &PressConstants::rampStart_deg},
    {0x14259be6c, &PressConstants::rampEnd_deg},
    {0x14259a764, &PressConstants::rampSpan_deg},
    {0x142588210, &PressConstants::radiusBase_m},
    {0x142598580, &PressConstants::halfTurn_deg},
    {0x14259858c, &PressConstants::fullTurn_deg},
    {0x14259be40, &PressConstants::radToDeg},
    {0x14259d634, &PressConstants::notSlotPenalty},
    {0x1425cee24, &PressConstants::notRolePenalty},
    {0x14259be18, &PressConstants::carrierRampStart_m},
    {0x14259be20, &PressConstants::carrierRampEnd_m},
    {0x14259bdf0, &PressConstants::carrierRampSpan_m},
    {0x14258c264, &PressConstants::pass2Lo},
    {0x14259bdec, &PressConstants::pass2Hi},
    {0x14259d654, &PressConstants::pass2Div},
    {0x14259be0c, &PressConstants::offsetBase_deg},
    {0x1425a1ff8, &PressConstants::offsetSpan_deg},
    {0x14259be38, &PressConstants::slot1Lo_deg},
    {0x1425cb488, &PressConstants::slot1Hi_deg},
    {0x14259be30, &PressConstants::lowBand_deg},
    {0x14259be60, &PressConstants::highBand_deg},
    {0x1425c48d4, &PressConstants::slot2Band_deg},
    {0x1425c6608, &PressConstants::slot3Lo_deg},
    {0x1425cee28, &PressConstants::slot3Hi_deg},
    {0x14259a764, &PressConstants::secPerMinute},
    {0x14256ae34, &PressConstants::metresPerKm},
    {0x14351ae54, &PressConstants::framesPerSec},
    {0x1425a9464, &PressConstants::pass2DelayDistSq},
    {0x1425c6780, &PressConstants::pass2ZeroRoleDistSq},
    {0x14259bdf4, &PressConstants::pass2SameCodeAdd},
    {0x1425656c0, &PressConstants::pass2ActionAdd},
};

}  // namespace

std::optional<PressConstants> LoadPressConstants(const PeImage& exe, std::string* error) {
  PressConstants c{};
  for (const PressVa& v : kPressVas) {
    if (!ReadFloat(exe, v.va, &(c.*v.field))) {
      if (error) *error = "press constant unreadable";
      return std::nullopt;
    }
  }
  return c;
}

void SortPressBucket(std::vector<PressCandidate>& bucket) {
  // Insertion sort: stable, and the buckets hold at most 11 entries.
  for (size_t i = 1; i < bucket.size(); ++i) {
    PressCandidate cur = bucket[i];
    size_t j = i;
    while (j > 0 && bucket[j - 1].score > cur.score) {
      bucket[j] = bucket[j - 1];
      --j;
    }
    bucket[j] = cur;
  }
}

float PressBearingDeg(const PressConstants& c, float fromX, float fromZ, float toX, float toZ) {
  float dx = toX - fromX;
  float dz = toZ - fromZ;
  if (dx == 0.0f && dz == 0.0f) return 0.0f;
  // ponytail: std::atan2 in place of PES's own atan2 polynomial; the float
  // difference is below what the picker's thresholds can see.
  float deg = std::atan2(dx, dz) * c.radToDeg;
  if (deg < 0.0f) deg += c.fullTurn_deg;
  return deg;
}

float PressWrapDeg(const PressConstants& c, float deg) {
  float r = std::fmod(deg, c.fullTurn_deg);
  if (r < 0.0f) r += c.fullTurn_deg;
  return r;
}

float PressSideAngleDeg(const PressConstants& c, float bearingDeg, float referenceDeg) {
  float d = bearingDeg - referenceDeg;
  if (std::fabs(d) > c.halfTurn_deg) d -= std::copysign(c.fullTurn_deg, d);
  return d;
}

bool PressAngleOutsideBand(const PressConstants& c, float deg, float eps) {
  float w = deg;
  if (std::fabs(w) > c.halfTurn_deg) w -= std::copysign(c.fullTurn_deg, w);
  float a = std::fabs(w);
  return !(eps <= a && a <= c.halfTurn_deg - eps);
}

float PressSpeedKmh(const PressConstants& c, float perFrame) {
  return ((c.framesPerSec * c.secPerMinute * c.secPerMinute) / c.metresPerKm) * perFrame;
}

constexpr uint64_t kRoleTableVa = 0x1426484f0;

std::optional<PressRoleTable> LoadPressRoleTable(const PeImage& exe, std::string* error) {
  PressRoleTable t{};
  std::array<uint8_t, kRoleCount * sizeof(uint32_t)> raw{};
  if (!exe.ReadVa(kRoleTableVa, raw.size(), raw.data())) {
    if (error) *error = "role table unreadable";
    return std::nullopt;
  }
  for (int i = 0; i < kRoleCount; ++i) {
    std::memcpy(&t.bits[i], raw.data() + i * sizeof(uint32_t), sizeof(uint32_t));
  }
  return t;
}

bool PressRoleHas(const PressRoleTable& t, int role, int bit) {
  if (role < 0 || role >= kRoleCount) return false;
  return (t.bits[role] >> bit) & 1u;
}

float PressCarrierRamp(const PressConstants& c, float carrierDist_m) {
  if (carrierDist_m <= c.carrierRampStart_m) return 0.0f;
  if (carrierDist_m < c.carrierRampEnd_m) {
    return (carrierDist_m - c.carrierRampStart_m) / c.carrierRampSpan_m;
  }
  return 1.0f;
}

float PressOffsetDeg(const PressConstants& c, float root) {
  float ramp;
  if (root <= c.pass2Lo) {
    ramp = 1.0f;
  } else if (root < c.pass2Hi) {
    ramp = (root - c.pass2Hi) / c.pass2Div;
  } else {
    ramp = 0.0f;
  }
  return ramp * c.offsetSpan_deg + c.offsetBase_deg;
}

float PressManScore(const PressConstants& c, float distSq, float angleDeg, int role, int slot) {
  float a = std::fabs(angleDeg);
  float r;
  if (a <= c.rampStart_deg) {
    r = 0.0f;
  } else if (a < c.rampEnd_deg) {
    r = (a - c.rampStart_deg) / c.rampSpan_deg;
  } else {
    r = 1.0f;
  }
  float radius = r * c.radiusBase_m + c.radiusBase_m;
  float coneTerm = c.rampStart_deg;
  if (distSq <= radius * radius) {
    float inner = (1.0f - r) * c.radiusBase_m;
    coneTerm = inner * inner;
  }
  float score = distSq + coneTerm;
  if (role != kPresserRole) score += c.notRolePenalty;
  if (slot < 0) score += c.notSlotPenalty;
  return score;
}

int PressAngleCode(const PressConstants& c, float a, int slot) {
  if (slot == 0) return 0;
  if (slot == 1 && c.slot1Lo_deg < a && a < c.slot1Hi_deg) return 1;
  if (slot == 2 && std::fabs(a) > c.slot2Band_deg) return 2;
  if (slot == 3 && c.slot3Hi_deg < a && a < c.slot3Lo_deg) return 3;
  // Fall-through: the band code on the magnitude and the sign.
  float m = std::fabs(a);
  if (m < c.lowBand_deg) return 0;
  if (m <= c.highBand_deg) return a <= 0.0f ? 3 : 1;
  return 2;
}

namespace {

// Press slot of `man` (team+0xa548 index), or -1.
int PressSlotOf(const std::array<int, 4>& pressSlotMan, int man) {
  for (int i = 0; i < 4; ++i) {
    if (pressSlotMan[i] == man) return i;
  }
  return -1;
}

// Angle code of `man` for the carrier bearing shifted by `offsetDeg`
// (FUN_140639600): bearing ball->man, plus offset, minus the side reference.
int PressManAngleCode(const PressConstants& c, const PressPickInput& in, int man, float offsetDeg,
                      int slot) {
  float bearing = PressBearingDeg(c, in.ballX, in.ballZ, in.posX[man], in.posZ[man]);
  float a = PressSideAngleDeg(c, PressWrapDeg(c, bearing + offsetDeg), in.referenceDeg);
  return PressAngleCode(c, a, slot);
}

}  // namespace
std::vector<PressCandidate> RunPresserPass1(const PressConstants& c, const PressPickInput& in,
                                            int side) {
  std::vector<PressCandidate> out;
  for (int slot = 0; slot < 11; ++slot) {
    const int man = side * 11 + slot;
    if (man >= 22 || !in.onField[man]) continue;
    float dx = in.posX[man] - in.ballX;
    float dz = in.posZ[man] - in.ballZ;
    float distSq = dx * dx + dz * dz;
    float angle = PressSideAngleDeg(
        c, PressWrapDeg(c, PressBearingDeg(c, in.ballX, in.ballZ, in.posX[man], in.posZ[man])),
        in.referenceDeg);
    int pressSlot = PressSlotOf(in.pressSlotMan, man);
    float score = PressManScore(c, distSq, angle, in.role[slot], pressSlot);
    out.push_back({man, score});
  }
  SortPressBucket(out);
  return out;
}


int PressExcludedMan(int side, const std::array<bool, 22>& onField, const std::array<int, 11>& flag) {
  for (int i = 0; i < 11; ++i) {
    const int man = side * 11 + i;
    if (man < 22 && onField[man] && flag[i] == 0) return man;
  }
  return kPressNoMan;
}

std::vector<PressCandidate> RunPresserPicker(const PressConstants& c, const PressPickInput& in,
                                             const std::vector<PressCandidate>& pass1Bucket,
                                             PressPickState& state) {
  // Radius of the pass-2 window: the carrier ramp scales it between the start and span.
  float carrierDist = std::hypot(in.ballX - in.refX, in.ballZ - in.refZ);
  float radius = PressCarrierRamp(c, carrierDist) * c.carrierRampSpan_m + c.carrierRampStart_m;
  float radiusSq = radius * radius;

  std::vector<PressCandidate> out;
  for (const PressCandidate& e : pass1Bucket) {
    const int man = e.man;
    if (man >= 22 || man == in.excludedMan || !in.onField[man]) continue;
    float dx = in.posX[man] - in.ballX;
    float dz = in.posZ[man] - in.ballZ;
    float distSq = dx * dx + dz * dz;
    if (distSq > radiusSq) continue;

    const int role = in.role[man % 11];
    const int slot = PressSlotOf(in.pressSlotMan, man);
    const float root = std::sqrt(e.score);
    const float offset = PressOffsetDeg(c, root);
    int code0 = PressManAngleCode(c, in, man, 0.0f, slot);
    int codePlus = PressManAngleCode(c, in, man, offset, slot);
    int codeMinus = PressManAngleCode(c, in, man, -offset, slot);

    // Skip a man on the zero code who is far from the ball, unless the team
    // has a first presser or the man is the presser role.
    if (!in.presserFlag && code0 == 0 && distSq > c.rampStart_deg && role != kPresserRole) continue;

    // DELAY (action 0x33/0x34): a man in a press slot close to the ball takes
    // that slot as his code.
    const bool delay = static_cast<unsigned>(in.action[man] - 0x33) < 2u;
    if (delay && slot >= 0 && distSq <= c.pass2DelayDistSq && code0 == 0) {
      codePlus = slot;
      codeMinus = slot;
    }

    float score = root;
    if (state.codeTaken[code0] == 0) {
      state.codeTaken[code0] = 1;
      if (state.pressCode[man % 11] < 0) state.pressCode[man % 11] = code0;
    } else if (codePlus != code0 || codeMinus != code0) {
      int chosen = codePlus != code0 ? codePlus : codeMinus;
      if (chosen == 0 && distSq >= c.pass2ZeroRoleDistSq && role != kPresserRole) {
        score += c.pass2SameCodeAdd;
      } else {
        if (!delay) score += c.pass2ActionAdd;
        state.pressCode[man % 11] = chosen;
      }
    } else {
      score += c.pass2SameCodeAdd;
    }
    if (state.pressCode[man % 11] == 0) state.zeroCodeFlag = true;
    out.push_back({man, score});
  }
  SortPressBucket(out);
  return out;
}

}  // namespace pes
