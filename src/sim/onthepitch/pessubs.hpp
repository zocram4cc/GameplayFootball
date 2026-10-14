#pragma once

// PES 2021's CPU stamina substitutions FUN_140523b60 and its replacement
// picker FUN_140521930 / FUN_140522970 / FUN_140520390
// (docs/re/pes-re-stamina-subs.md). Tunable literals are read out of the
// instructions; the picker's position tiers are its control flow and live in
// the code below.

#include <optional>
#include <string>
#include <vector>

#include "../gametypes.hpp"
#include "pes/exe.hpp"

namespace pes {

inline constexpr int kSubModes = 3;      // modes 1..3; 0 switches stamina subs off
inline constexpr int kSubRoleClasses = 10;

struct SubstitutionConstants {
  // Second-half cap schedule (0x140523c82..0x140523cef), minutes per mode.
  int mode1TwoThirdsFrom;  // 25
  int mode2TwoThirdsFrom;  // 25
  int mode2ThirdFrom;      // 15
  int mode3FullFrom;       // 20
  int mode3TwoThirdsFrom;  // 10
  // Per role class 0..9 (the 0x140524280 jump table routes classes 2..9):
  // threshold by mode 1..3 and the sort base.
  int threshold[kSubRoleClasses][kSubModes];
  int base[kSubRoleClasses];
  bool neverSubbed[kSubRoleClasses];  // classes the switch skips: GK, CB
  // FUN_140520390's gaps.
  float tierGap;   // 1.0
  float fitWide;   // 15.0
  float fitNear;   // 8.0
};

std::optional<SubstitutionConstants> LoadSubstitutionConstants(const PeImage& exe,
                                                               std::string* error);

// FUN_1408c51f0: GF's role to PES's role class 0..9.
int SubRoleClass(e_PlayerRole role);

// How many stamina subs the CPU may have made by now. `secondHalf` is PES's
// period 3; other periods (the breaks, extra time) give the whole cap; the
// first half gives none.
int StaminaSubCap(const SubstitutionConstants& k, int mode, bool firstHalf, bool secondHalf,
                  int minuteInHalf, int cap);

struct StaminaSubCandidate {
  int index;          // the caller's own
  e_PlayerRole role;  // his slot's role
  int staminaPct;     // cur * 100 / max
};

// The ordered list of who comes off: the tired below their class threshold,
// sorted ascending by base + stamina % (attackers first, tiredest first).
std::vector<int> PickStaminaSubs(const SubstitutionConstants& k, int mode,
                                 const std::vector<StaminaSubCandidate>& onPitch);

struct BenchCandidate {
  int index;
  std::vector<e_PlayerRole> registered;  // first = his listed position
  float fit;                             // his rating for the role, PES's 0..99
  bool superSub;                         // skill and period gate already applied
};

enum class SubKind { Injury = 2, Stamina = 3 };

// FUN_140521930: the bench player for `role`, or -1.
int PickReplacement(const SubstitutionConstants& k, e_PlayerRole role, SubKind kind,
                    const std::vector<BenchCandidate>& bench);

}  // namespace pes
