#include "pessetpieceteam.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pes {
namespace {

// Every value below is a plain .rdata float in the user's PES2021.exe, verified
// against the shipped image (OBS). The address is the place the instruction
// reads it from, not a copy of the number: a different build fails the read
// instead of silently producing a wrong constant.
constexpr uint64_t JOG_KMH_VA = 0x14259d638;            // 16000
constexpr uint64_t SECONDS_PER_HOUR_VA = 0x142598590;   // 3600
constexpr uint64_t BUDGET_CORNER_VA = 0x14259be1c;      // 20
constexpr uint64_t BUDGET_POSTMAN_VA = 0x1425a266c;     // 300
constexpr uint64_t BUDGET_OWN_BACK_VA = 0x14259bdf0;    // 7
constexpr uint64_t BUDGET_OWN_MID_VA = 0x142588210;     // 10
constexpr uint64_t BUDGET_OWN_FRONT_VA = 0x1425c5948;   // 13
constexpr uint64_t BUDGET_OTHER_MARKING_VA = 0x14259bdf4;  // 8
constexpr uint64_t BUDGET_OTHER_VA = 0x14259be04;          // 12
constexpr uint64_t BUDGET_THROWIN_SUPPORT_VA = 0x14258c264;  // 3
constexpr uint64_t BUDGET_THROWIN_VA = 0x142565604;         // 2
constexpr uint64_t TAKER_SLOT_FACTOR_VA = 0x14259bddc;      // 1.5
constexpr uint64_t FK_DESIGNATED_NEAR_VA = 0x14259be28;    // 32
constexpr uint64_t FK_DESIGNATED_FAR_VA = 0x14259a764;     // 60
constexpr uint64_t FK_KEEPER_OWN_GOAL_VA = 0x1425a2004;    // 28
constexpr uint64_t FK_NEAREST_VA = 0x14259be48;           // 65
constexpr uint64_t WALL_DISTANCE_VA = 0x1425a945c;        // 9.15
constexpr uint64_t WALL_SPACING_VA = 0x142565600;        // 0.5
constexpr uint64_t WALL_NEAR_POST_VA = 0x14259bdd8;       // 1.3
constexpr uint64_t WALL_GOAL_LINE_BAND_VA = 0x14259ce3c;  // 7.5
constexpr uint64_t WALL_NARROW_DIST_VA = 0x1425a2004;    // 28
constexpr uint64_t WALL_NARROW_BAND_VA = 0x14259222c;     // 5
constexpr uint64_t WALL_CENTRAL_GAP_VA = 0x14258c264;     // 3
constexpr uint64_t WALL_CENTRAL_HALFZ_VA = 0x142588210;   // 10
// The wall size table's distances and its four angle bands.
constexpr uint64_t WALL_D25_VA = 0x14259be20;  // 25
constexpr uint64_t WALL_D29_VA = 0x1425a9460;  // 29
constexpr uint64_t WALL_D32_VA = 0x14259be28;  // 32
constexpr uint64_t WALL_D36_VA = 0x1425a9464;  // 36
constexpr uint64_t WALL_D40_VA = 0x14259be2c;  // 40
constexpr uint64_t WALL_ANGLE20_VA = 0x14259be1c;  // 20, a band's start
constexpr uint64_t WALL_ANGLE30_VA = 0x142591140;  // 30
constexpr uint64_t WALL_ANGLE40_VA = 0x14259be2c;  // 40
constexpr uint64_t WALL_ANGLE45_VA = 0x14259be30;  // 45
constexpr uint64_t WALL_ANGLE50_VA = 0x14259be34;  // 50
constexpr uint64_t WALL_SPAN20_VA = 0x14259be2c;   // 40 - 20
constexpr uint64_t WALL_SPAN15_VA = 0x14259d668;   // 15, negated in the image
constexpr uint64_t WALL_SPAN10_VA = 0x142588210;  // 10

const SetPieceTeamCounts kCounts;

bool ReadFloat(const PeImage& exe, uint64_t va, float* out) {
  return exe.ReadVa(va, sizeof *out, reinterpret_cast<uint8_t*>(out));
}

// PES's role classes (choreography 11.3: `role <= 2`, `3..5`, `>= 6`), read off
// GF's own role list. PES: 0 GK, 1 CB, 2 SB, 3 DMF, 4 CMF, 5 SMF, 6 AMF, 7 SS,
// 8 WF, 9 CF. GF carries the same order with the full-backs split off the centre
// backs, so GF 1..3 are PES 1..2, GF 4..7 are PES 3..5 and GF 8.. are PES 6+.
bool IsBackClass(e_PlayerRole role) {
  return role == e_PlayerRole_CB || role == e_PlayerRole_LB || role == e_PlayerRole_RB;
}
bool IsMidClass(e_PlayerRole role) {
  return role == e_PlayerRole_DM || role == e_PlayerRole_CM || role == e_PlayerRole_LM ||
         role == e_PlayerRole_RM;
}

// FUN_1404a9920's ramp, which its two `mulss -2` folds make a straight line:
// 1 at or below the band's start, 0 at its end, linear between.
float AngleRamp(float angle_deg, float from, float to) {
  if (angle_deg <= from) return 1.0f;
  if (angle_deg >= to) return 0.0f;
  return (to - angle_deg) / (to - from);
}

}  // namespace

std::optional<SetPieceTeamConstants> LoadSetPieceTeamConstants(const PeImage& exe,
                                                              std::string* error) {
  SetPieceTeamConstants k{};
  bool ok = true;
  for (auto [va, out] :
       {std::pair{JOG_KMH_VA, &k.jogKmh}, std::pair{SECONDS_PER_HOUR_VA, &k.secondsPerHour},
        std::pair{BUDGET_CORNER_VA, &k.budgetCorner_s},
        std::pair{BUDGET_POSTMAN_VA, &k.budgetPostMan_s},
        std::pair{BUDGET_OWN_BACK_VA, &k.budgetOwnBack_s},
        std::pair{BUDGET_OWN_MID_VA, &k.budgetOwnMid_s},
        std::pair{BUDGET_OWN_FRONT_VA, &k.budgetOwnFront_s},
        std::pair{BUDGET_OTHER_MARKING_VA, &k.budgetOtherMarking_s},
        std::pair{BUDGET_OTHER_VA, &k.budgetOther_s},
        std::pair{BUDGET_THROWIN_SUPPORT_VA, &k.budgetThrowInSupport_s},
        std::pair{BUDGET_THROWIN_VA, &k.budgetThrowIn_s},
        std::pair{TAKER_SLOT_FACTOR_VA, &k.takerSlotBudgetFactor},
        std::pair{FK_DESIGNATED_NEAR_VA, &k.fkDesignatedNear_m},
        std::pair{FK_DESIGNATED_FAR_VA, &k.fkDesignatedFar_m},
        std::pair{FK_KEEPER_OWN_GOAL_VA, &k.fkKeeperOwnGoal_m},
        std::pair{FK_NEAREST_VA, &k.fkNearest_m}, std::pair{WALL_DISTANCE_VA, &k.wallDistance_m},
        std::pair{WALL_SPACING_VA, &k.wallSpacing_m},
        std::pair{WALL_NEAR_POST_VA, &k.wallNearPost_m},
        std::pair{WALL_GOAL_LINE_BAND_VA, &k.wallGoalLineBand_m},
        std::pair{WALL_NARROW_DIST_VA, &k.wallNarrowDist_m},
        std::pair{WALL_NARROW_BAND_VA, &k.wallNarrowBand_m},
        std::pair{WALL_CENTRAL_GAP_VA, &k.wallCentralGap_m},
        std::pair{WALL_CENTRAL_HALFZ_VA, &k.wallCentralHalfZ_m},
        std::pair{WALL_D25_VA, &k.wallD25_m}, std::pair{WALL_D29_VA, &k.wallD29_m},
        std::pair{WALL_D32_VA, &k.wallD32_m}, std::pair{WALL_D36_VA, &k.wallD36_m},
        std::pair{WALL_D40_VA, &k.wallD40_m}, std::pair{WALL_ANGLE20_VA, &k.wallAngle20_deg},
        std::pair{WALL_ANGLE30_VA, &k.wallAngle30_deg},
        std::pair{WALL_ANGLE40_VA, &k.wallAngle40_deg},
        std::pair{WALL_ANGLE45_VA, &k.wallAngle45_deg},
        std::pair{WALL_ANGLE50_VA, &k.wallAngle50_deg},
        std::pair{WALL_SPAN20_VA, &k.wallSpan20_deg}, std::pair{WALL_SPAN10_VA, &k.wallSpan10_deg},
        std::pair{WALL_SPAN15_VA, &k.wallSpan15_deg}}) {
    if (!ReadFloat(exe, va, out)) {
      char buf[32];
      std::snprintf(buf, sizeof buf, "%#llx", static_cast<unsigned long long>(va));
      *error = std::string("set-piece team constant: unreadable .rdata at ") + buf;
      ok = false;
    }
  }
  if (!ok) return std::nullopt;
  // A wall closer than nothing, or a budget of zero, would silently do nothing;
  // the shipped values are all positive and a failed read must not look like a rule.
  if (k.wallDistance_m <= 0.0f || k.wallSpacing_m <= 0.0f || k.jogKmh <= 0.0f ||
      k.secondsPerHour <= 0.0f || k.fkNearest_m <= 0.0f || k.wallD40_m <= 0.0f ||
      k.wallAngle50_deg <= 0.0f) {
    *error = "set-piece team constant: non-positive value in .rdata";
    return std::nullopt;
  }
  // FUN_1404a9920 negates its spans (`mulss xmm2, -2`, `divss xmm2, -20`): the
  // compiler folds a subtraction into the divisor's sign. Take the magnitude so
  // the rule below reads the way the report spells it.
  k.wallSpan15_deg = std::fabs(k.wallSpan15_deg);
  return k;
}

int AttackLevel(int mentalityRung) { return std::clamp(mentalityRung, 0, 4); }

int BoxAttackerCount(int attackLevel, const SetPieceTeamCounts& counts) {
  return counts.boxAttackerBase + std::min(attackLevel, counts.boxAttackerCap);
}

float JogSpeed_mps(const SetPieceTeamConstants& k) {
  // 16000 metres in an hour is 16 km/h; PES's `16000 / (fps * 3600)` is the same
  // speed per logic frame, and the budget is measured in seconds.
  return k.jogKmh / k.secondsPerHour;
}

float PlacementBudget_s(const SetPieceTeamConstants& k, int setPiece, bool ownSide,
                        e_PlayerRole role, bool kicker, bool secondKicker, bool wallMan,
                        bool ownKeeper, bool postMan, bool isMarking) {
  // FUN_140927ab0. The kicker, the second kicker, a wall man, the own keeper and
  // the corner post man walk to wherever the choreography wants them (PES passes
  // FLT_MAX for all five), as do the two restarts with no placement window of
  // their own: a kick-off and a penalty are re-formed behind their own wipe.
  if (kicker || secondKicker || wallMan || ownKeeper || postMan) return -1.0f;
  if (setPiece == e_SetPiece_KickOff || setPiece == e_SetPiece_Penalty) return -1.0f;

  if (setPiece == e_SetPiece_Corner) return k.budgetCorner_s;

  if (setPiece == e_SetPiece_ThrowIn) {
    // PES gives a throw-in's designated supporter 3 s and everyone else 2 s.
    // GF has no supporter designation, so every player of the restarting side
    // gets the supporter's budget and the other side the marking one.
    return ownSide ? k.budgetThrowInSupport_s : k.budgetThrowIn_s;
  }

  // Goal kick and free kick: the restarting side by role class, the other side
  // by whether its man-marking slot is busy (PES T+0x87d0[slot] == 2).
  if (ownSide) {
    if (role == e_PlayerRole_GK || IsBackClass(role)) return k.budgetOwnBack_s;
    if (IsMidClass(role)) return k.budgetOwnMid_s;
    return k.budgetOwnFront_s;
  }
  return isMarking ? k.budgetOtherMarking_s : k.budgetOther_s;
}

float JogFraction(float budget_s, float dist_m, const SetPieceTeamConstants& k) {
  if (budget_s < 0.0f) return 1.0f;  // no limit
  if (dist_m <= 0.0f) return 1.0f;
  return std::min(1.0f, (JogSpeed_mps(k) * budget_s) / dist_m);
}

int PickBySuitability(const std::vector<SetPieceCandidate>& candidates) {
  // FUN_140a621f0: over the slots on the pitch, the highest rating wins and the
  // first one wins a tie. A keeper is excluded for every kind.
  int best = -1;
  float bestRating = -1.0f;
  for (const SetPieceCandidate& c : candidates) {
    if (!c.onPitch || c.isKeeper || c.index < 0) continue;
    if (c.suitability > bestRating) {
      bestRating = c.suitability;
      best = c.index;
    }
  }
  return best;
}

int FreeKickTaker(const std::vector<SetPieceCandidate>& candidates, float ballX, float ballZ,
                  float dir, float attackedGoalX, float ownGoalX, float penaltyX,
                  float penaltyHalfZ, const SetPieceTeamConstants& k) {
  // FUN_1404a8ad0's waterfall without its first branch: GF carries no team sheet
  // of designated takers (PES T+0x8e08), so "the sheet's free-kick taker" has
  // nothing to read. What is left is the keeper, the own-half defender, the
  // long-range midfielder and the rating fallback.
  const float goalDx = ballX - attackedGoalX;
  const float goalD = std::sqrt(goalDx * goalDx + ballZ * ballZ);
  const bool ownHalf = dir * ballX < 0.0f;

  // The keeper, when the ball is near the goal line inside the own box.
  // Confirmed live: a free kick 21.8 m from its own goal was taken by that
  // team's keeper, who was standing 46 m away.
  if (std::fabs(ballZ) < penaltyHalfZ) {
    const float ownDx = ballX - ownGoalX;
    const float ownD = std::sqrt(ownDx * ownDx + ballZ * ballZ);
    if (ownD < k.fkKeeperOwnGoal_m || dir * ballX < -penaltyX) {
      for (const SetPieceCandidate& c : candidates)
        if (c.onPitch && c.isKeeper) return c.index;
    }
  }

  // In our own half: the nearest centre back or full back within 65 m. PES also
  // excludes one slot it keeps as an outlet (T+0x4b40), which GF has no data for.
  if (ownHalf) {
    int best = -1;
    float bestDist = k.fkNearest_m;
    for (const SetPieceCandidate& c : candidates) {
      if (!c.onPitch || c.isKeeper) continue;
      if (!IsBackClass(c.role)) continue;
      if (c.distanceToBall_m < bestDist) {
        bestDist = c.distanceToBall_m;
        best = c.index;
      }
    }
    if (best >= 0) return best;
  }

  // A free kick from far out: the nearest outfield player within 65 m. PES's
  // role filter is "role < 7", which is every role except the three forwards.
  if (goalD > k.fkDesignatedFar_m) {
    int best = -1;
    float bestDist = k.fkNearest_m;
    for (const SetPieceCandidate& c : candidates) {
      if (!c.onPitch || c.isKeeper) continue;
      if (c.role == e_PlayerRole_CF || c.role == e_PlayerRole_LW || c.role == e_PlayerRole_RW ||
          c.role == e_PlayerRole_SS)
        continue;
      if (c.distanceToBall_m < bestDist) {
        bestDist = c.distanceToBall_m;
        best = c.index;
      }
    }
    if (best >= 0) return best;
  }

  // The rating fallback: the best set-piece taker on the pitch.
  return PickBySuitability(candidates);
}

int WallSize(float ballToGoal_m, float ballAngle_deg, float ballX, float ballZ, float dir,
             float goalHalfLength, float penaltyX, float penaltyHalfZ,
             const SetPieceTeamConstants& k, const SetPieceTeamCounts& counts) {
  const float ax = std::fabs(ballX), zz = std::fabs(ballZ);
  if (ax > goalHalfLength - k.wallGoalLineBand_m) return 1;
  if (ballToGoal_m <= k.wallNarrowDist_m && ax > penaltyX && zz >= penaltyHalfZ)
    return counts.wallNarrowBase + (ax <= penaltyX + k.wallNarrowBand_m ? 1 : 0);
  if (dir * ballX > penaltyX - k.wallCentralGap_m && zz < k.wallCentralHalfZ_m)
    return counts.wallCentralCount;
  if (ballToGoal_m <= k.wallD25_m)
    return counts.wallD25Base + static_cast<int>(counts.wallDouble * AngleRamp(ballAngle_deg,
                                                                              k.wallAngle20_deg,
                                                                              k.wallAngle20_deg + k.wallSpan20_deg));
  if (ballToGoal_m <= k.wallD29_m)
    return counts.wallD29Base + static_cast<int>(counts.wallDouble *
                                                 AngleRamp(ballAngle_deg, k.wallAngle30_deg,
                                                           k.wallAngle45_deg));
  if (ballToGoal_m < k.wallD32_m)
    return counts.wallD32Base + static_cast<int>(counts.wallDouble *
                                                AngleRamp(ballAngle_deg, k.wallAngle30_deg,
                                                          k.wallAngle40_deg));
  if (ballToGoal_m < k.wallD36_m)
    return counts.wallD36Base +
           static_cast<int>(AngleRamp(ballAngle_deg, k.wallAngle40_deg, k.wallAngle50_deg));
  if (ballToGoal_m < k.wallD40_m) return 1;
  return 0;
}

int WallAction(int wallSize, int slot, int playerIndex, int frameCounter, WallJumpState& state) {
  // FUN_140525ec0, called from FUN_140525b50 once FUN_140525900's lead-frame
  // countdown has run out. `slot` is the defending team's slot (0..10) and
  // `playerIndex` its absolute index, exactly as PES's loop has them, because
  // the roll mixes the two.
  if (wallSize <= 0 || !state.active) return 1;
  const int f = frameCounter;
  const int fMod100 = f - (f / 100) * 100;
  const long slotSq = static_cast<long>(slot) * static_cast<long>(slot);
  const long first = static_cast<long>(fMod100) - slotSq;
  const long second =
      static_cast<long>(playerIndex) * static_cast<long>(slot) + static_cast<long>(fMod100);
  const long product = first * second;
  const long magnitude = product < 0 ? -product : product;
  const int r = static_cast<int>(magnitude % 100);
  // A press: at most one, and only on an even (frame XOR slot) frame.
  if (((f ^ slot) & 1) == 0 && r <= kCounts.pressRollMax && !state.pressed) {
    state.pressed = true;
    return 2;
  }
  // A jump, once the not-jump slots have run out or the roll is high.
  if (state.notJumpLeft == 0 || r > kCounts.jumpRollMax) return 0;
  state.notJumpLeft--;
  return 1;
}

WallJumpState WallJumpStateStart(int wallSize) {
  WallJumpState s;
  s.active = wallSize > 0;
  s.notJumpLeft = (wallSize - 1) / 2;
  s.pressed = false;
  return s;
}

}  // namespace pes
