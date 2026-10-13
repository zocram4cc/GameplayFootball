// Referee personalities. PES 2021 has none: its foul decision reads no referee
// data (docs/re/pes-re-duels-fouls.md 2.7), and fouls are judged by
// pes::JudgeLevel. What survives is GF's advantage window, until PES's own
// advantage (report 3) is ported.

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

e_Profile Parse(const std::string& name);
std::string GetName(e_Profile profile);

// How long the referee lets an advantage run before bringing play back.
unsigned long GetAdvantageWindow_ms(e_Profile profile);

}  // namespace RefereeProfile

#endif
