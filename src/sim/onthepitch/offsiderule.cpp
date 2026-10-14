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

bool OffsideBlocksChase(int category) {
  // PES 2021's ball-list reader FUN_14045e0d0 (docs/re/pes-re-offside.md 4):
  // the score of a player offside-flagged in category 0 or 1 is the impossible
  // one, so he never appears in his team's ball list.
  return category >= 0 && category < 2;
}

bool ReceiverIsOffside(int attackingSide,
                       const std::vector<blunted::Vector3>& opponentCentres,
                       const std::vector<blunted::Vector3>& opponentVelocities,
                       const blunted::Vector3& ballCentre,
                       const blunted::Vector3& ballVelocity,
                       const blunted::Vector3& receiverCentre,
                       const blunted::Vector3& receiverVelocity, float prediction_s) {
  const float s = static_cast<float>(attackingSide);
  if (s == 0.0f) return false;

  // The five last defenders, each predicted forward, in the attacking team's
  // own attack frame. The rearmost first, so the first five are PES's "5 last
  // defenders" and the deepest of them is the 5th rearmost - the line the
  // enumeration puts its receivers behind.
  std::vector<float> rear;
  rear.reserve(opponentCentres.size());
  for (size_t i = 0; i < opponentCentres.size(); ++i) {
    rear.push_back(s * (opponentCentres[i].coords[0] +
                        opponentVelocities[i].coords[0] * prediction_s));
  }
  std::sort(rear.begin(), rear.end());
  // PES's running maximum starts at 0 (the halfway line) and then takes the
  // ball, predicted forward: whichever of the 5th-rearmost defender and the
  // ball is further up the pitch is the line.
  float line = 0.0f;
  for (size_t i = 0; i < rear.size() && i < kReceiverDefenders; ++i) {
    line = std::max(line, rear[i]);
  }
  line = std::max(line, s * (ballCentre.coords[0] + ballVelocity.coords[0] * prediction_s));

  // Offside when his position now, or his position prediction_s ahead, is
  // beyond it - the enumeration reads both (0x142117c30, LAB_14211818f).
  const float now = s * receiverCentre.coords[0];
  const float later = s * (receiverCentre.coords[0] + receiverVelocity.coords[0] * prediction_s);
  return now > line || later > line;
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
