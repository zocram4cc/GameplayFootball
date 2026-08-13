// Referee personalities and VAR review triggers.
// The standard profile reproduces the thresholds that used to be hard-coded in
// Referee::TripNotice, so an unconfigured match is judged exactly as before.
// See SIMULATION_IMPROVEMENT_PROPOSAL.md section 4B.

#ifndef _HPP_REFEREE_PROFILE
#define _HPP_REFEREE_PROFILE

#include <string>

namespace RefereeProfile {

enum e_Profile {
  e_Profile_Standard = 0,
  e_Profile_Lenient,
  e_Profile_Strict,
  e_Profile_Count,
};

struct Thresholds {
  float foul = 1.0f;
  float yellow = 1.4f;
  float red = 2.0f;
};

// Offside calls tighter than this (in metres) are worth a second look.
const float varOffsideMargin = 0.5f;
// Contact this close to the foul threshold is a judgement call.
const float varPenaltyMargin = 0.2f;
const unsigned long varReviewDuration_ms = 25000;

e_Profile Parse(const std::string& name);
std::string GetName(e_Profile profile);

Thresholds GetThresholds(e_Profile profile);

// 0: nothing, 1: foul, 2: yellow, 3: red - same ladder as struct Foul.
int GetFoulType(e_Profile profile, float severity);

// How long the referee lets an advantage run before bringing play back.
unsigned long GetAdvantageWindow_ms(e_Profile profile);

// `offsideMargin` is how far beyond the line the attacker was, in metres;
// negative means he was onside by that much.
bool ShouldReviewOffside(float offsideMargin, bool goalScored);

bool ShouldReviewPenalty(e_Profile profile, float severity, bool insidePenaltyBox);

}  // namespace RefereeProfile

#endif
