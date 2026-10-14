#include "offsiderule.hpp"

#include <algorithm>
#include <cmath>

namespace OffsideRule {

float OffsideLineX(int defendingSide, const std::vector<blunted::Vector3>& defenderCentres,
                   float ballX, float halfLength_m, const pes::JudgeConstants& k) {
  const float s = static_cast<float>(defendingSide);
  if (s == 0.0f) return 0.0f;

  // PES 2021's line (report 3), in the defending team's own attack frame with
  // its own goal at -halfLength: the second-rearmost defender's rear surface,
  // capped by the ball and by halfway, clamped to the goal line. Both running
  // minima start at 0, so a defender beyond halfway never deepens the line, and
  // fewer than two eligible defenders makes the second one the goal line.
  float rearmost = 0.0f;
  float second = 0.0f;
  int count = 0;
  for (const blunted::Vector3& centre : defenderCentres) {
    // The rear surface: the centre shifted towards his own goal by his torso
    // radius (PES: x + s*(-r), its constant already the negated radius), which
    // in this frame is one shift because s*s = 1.
    // ponytail: no per-limb points; arms and toes not modelled - the torso
    // shift stands in for every body point's, so a defender's rear surface is
    // his centre plus the torso shift and no deeper.
    const float v = s * centre.coords[0] + k.offsideRearShiftTorso_m;
    ++count;
    if (v < rearmost) {
      second = rearmost;
      rearmost = v;
    } else if (v < second) {
      second = v;
    }
  }
  // Fewer than two eligible defenders makes the second one the goal line, and
  // the ball caps the line from there. The halfway cap is not a separate step:
  // both running minima start at 0 and only ever go down, so `second` is never
  // positive - PES's `second > 0 -> 0` is the same invariant, stated again.
  if (count < 2) second = -halfLength_m;
  second = std::min(second, s * ballX);
  if (std::fabs(second) > halfLength_m) second = -halfLength_m;
  return s * second;
}

int OffsideCategory(float attackerCentreX, float lineX, int attackingSide,
                    const pes::JudgeConstants& k) {
  const float s = static_cast<float>(attackingSide);

  // PES 2021's attacker test (report 4): s*P.x + m > s*L + tol, with P the
  // leading body point and m its surface margin, and the category from
  // d = |P.x - L| at that point's centre.
  // ponytail: GF has centre positions only - no per-limb points; arms and toes
  // are not modelled. The torso point stands in for every body point, so the
  // leading point is the centre shifted by the torso margin (its margin is
  // already inside that surface) and no arm ever leads it: the 0.30 m tolerance
  // cannot apply, and the centre itself must pass the line, exactly as PES's
  // own head/torso point does. What this misses is PES's per-limb strictness:
  // a toe leading gives it 0.05 m, an elbow 0.24 m, a hand tip 0.30 m.
  const float leadingX = attackerCentreX + s * k.offsideMarginTorso_m;
  // The non-arm tolerance is PES's own shared float with the torso margin.
  const float tolerance = k.offsideMarginTorso_m;
  const float d = std::fabs(attackerCentreX - lineX);

  if (s * leadingX > s * lineX + tolerance)
    return d < k.offsideNearBand_m ? 2 : (d <= k.offsideClearBand_m ? 1 : 0);
  return d < k.offsideNearBand_m ? 3 : -1;
}

bool CanCreateOffside(e_SetPiece restart) {
  // PES 2021's gate (docs/re/pes-re-offside.md 2.2): the delivery of its staged
  // set 1..4 - a kick-off, a throw-in, a goal kick or a corner - never arms a
  // flag. A free kick or a penalty can put its receivers offside.
  return restart != e_SetPiece_KickOff && restart != e_SetPiece_ThrowIn &&
         restart != e_SetPiece_GoalKick && restart != e_SetPiece_Corner;
}

bool ShouldSnapshot(bool inPlay, bool restartQueued, e_SetPiece queuedRestart) {
  if (!inPlay)
    return false;
  if (restartQueued && !CanCreateOffside(queuedRestart))
    return false;
  return true;
}

bool IsDeliberatePlay(e_TouchType touchType) {
  return touchType == e_TouchType_Intentional_Kicked;
}

bool TouchResetsPhase(bool opponentOfFlagged, e_TouchType touchType) {
  if (!opponentOfFlagged)
    return true;
  return IsDeliberatePlay(touchType);
}

blunted::Vector3 RestartPosition(const blunted::Vector3& positionAtPass,
                                 const blunted::Vector3& positionAtInvolvement) {
  (void)positionAtPass;  // recorded to judge the offence, not to restart from
  return positionAtInvolvement;
}

}  // namespace OffsideRule
