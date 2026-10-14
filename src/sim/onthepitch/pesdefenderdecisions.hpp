#pragma once

// PES 2021's individual defender decisions: the ball-winning act an engaging
// defender picks, docs/re/pes-re-defender-decisions.md (DefenderDecisionsRE).
//
// PES gives the engaging defender no think-unit list. Team AI puts him in
// PRESS / DELAY / SAND / MARK (movement); the act itself is chosen by one
// ordered reaction table (FUN_140a06dd0, the ScriptBind table @0x14263ec60)
// walked every logic frame, first predicate that returns true winning. Tackle
// is entry 1, slide entry 2, block entry 8. No score competition, no RNG.
//
// This is the decision core only. Every function is pure (arguments plus the
// constants), so the whole layer is testable against synthetic constants
// without an install and without a Match; the caller (ElizaController) builds
// the inputs from its own state.
//
// Angles are degrees, distances metres, times seconds. The pitch frame is GF's
// (z up); heights are the y coordinate, which is the record's second float.
//
// Where the numbers live:
//   - tackle.json / block.json: the `tackle.o` / `block.o` records of
//     common/match/constant/constant_player.bin, read through PesVfs the way
//     LoadPesBallConstants reads ball.o. The disk record is a flat field
//     array whose offsets are the deserializer's own store offsets; they are
//     the same in the stock and the modded file (verified against both).
//     Nothing is pinned: the VFS resolves whatever install is mounted.
//   - everything else: the exe's .rdata, read through PeImage::ReadVa like
//     the ball's image constants.
//   - frame counts are converted to this engine's 10 ms tick the way
//     pesballphysics.hpp converts the ball's per-tick factors.

#include <optional>
#include <string>
#include <vector>

#include "onthepitch/pesreceive.hpp"
#include "pes/exe.hpp"
#include "pes/types.hpp"
#include "pes/vfs.hpp"

namespace pes {

// The WESYS object container the constant files are read from.
inline constexpr const char* kConstantPlayerPath =
    "common/match/constant/constant_player.bin";

struct DefenderDecisionConstants {
  // --- standing tackle geometry (report 2/12.2, FUN_14096fda0) ---
  // Outside PRESS/DELAY/SAND (and without a WAIT_TIMER/FREE_RUN action) the
  // ball this far ahead must be inside the ball distance and off his heading
  // by less than the angle. `tackleBallDistanceSq_m2` is compared squared.
  float tackleBallDistanceSq_m2 = 9.0f;      // 0x14259d60c
  float tackleBallHeadingAngle_deg = 22.5f;  // 0x14259d618
  // The solver's contact point must be this high at most: its HEIGHT, not a
  // distance (report 12.3).
  float tackleContactHeight_m = 3.0f;        // 0x14258c264
  // The engagement window on the carrier.
  float tackleEngageAngle_deg = 112.5f;  // 0x1425cb30c
  float tackleEngageGap_m = 2.0f;        // 0x142565604
  // From behind: above the carrier-relative angle the tackle is refused unless
  // the approach clears the tolerance (by aggression level A = 0/1/2) or the
  // extra angle. A = 2 has a zeroed register, so it never tackles from behind.
  float tackleBehindAngle_deg = 135.0f;             // 0x14259be60
  float tackleBehindTolerance_deg[3] = {45.0f, 11.25f, 0.0f};  // 0x14259be30, 0x1425cc680
  float tackleBehindExtraAngle_deg = 25.0f;         // 0x14259be20
  // The challenge is timed into the carrier's touch, x frames before it, x by
  // aggression level (report 12.2: 0.14259da90 / 0x142588200 / 0x14259bdcc).
  float tackleTouchLead_s[3] = {0.075f, 0.1f, 0.125f};
  // The lateral-offset branch: the carrier-offender line may not be offset by
  // this much laterally with the ball closer to the defender unless he comes
  // at it head on and it is fast.
  float tackleLateralOffset_m = 0.8f;      // 0x142572348
  float tackleLateralSpeed_kmh = 5.0f;     // 0x14259222c
  // The safety vetoes (report 2 step "safety", 12.2 raw): the cover search's
  // radius and wedge, the last line's distance, and the frames since the last
  // ball event after which they apply (gs/9).
  float safetyCoverRadius_m = 10.0f;  // 0x142588210
  float safetyCoverAngle_deg = 60.0f;  // 0x14259a764
  float safetyLastLine_m = 10.0f;      // |MA + 20*id + 0x40 - defender x| < 10
  int safetyEventFramesDivisor = 9;    // gs / 9

  // tackle.o (VFS): the two safety terms. `sf` is compared against both; with
  // the install's values the veto fires whenever sf is below them (sf is a
  // sine in [0, 1]).
  float tackleSafetyRateLastLine = 0.0f;  // tackle.o +0x0c
  float tackleSafetyRateNoCover = 0.0f;   // tackle.o +0x10

  // --- slide (report 3, FUN_140971ba0) ---
  float slideDistanceBase_m = 4.0f;    // 0x1425656c0
  float slideDistancePerLevel_m = 0.8f;  // 0x142572348
  float slideBallHeight_m = 0.5f;       // 0x142565600, 10 frames ahead
  // The target must be inside the engagement window off his heading: the same
  // 112.5 deg the standing tackle uses (raw 0x140972377..0x140972398,
  // `COMISS XMM0,[0x1425cb30c]; JNC <refuse>`).
  float slideEngageAngle_deg = 112.5f;  // 0x1425cb30c
  // The slide's own direction window: the bearing from him to the point must
  // be within 90 deg of his heading, else he would slide sideways (raw
  // 0x14097230c..0x14097231e, `COMISS XMM0,XMM7; JNC <refuse>`, XMM7 = 0x14259be5c).
  float slideDirectionAngle_deg = 90.0f;  // 0x14259be5c

  // --- block (report 5, FUN_140a08ea0) ---
  float blockRadiusNormal_m = 1.3f;    // 0x14259bdd8
  float blockRadiusPressing_m = 3.0f;  // 0x14258c264
  float blockRadiusLate_m = 7.0f;      // 0x14259bdf0
  float blockShotRadiusScale = 1.3f;   // 0x14259bdd8, the same slot
  float blockFeintRadiusScale = 1.2f;  // 0x14259d5d4
  // A pass/shot clip widens the radius once it has passed this fraction of the
  // frame before its contact; a long pass's fraction is the higher one and a
  // ball above the height threshold halves it.
  float blockClipProgress = 0.7f;      // 0x14257a194
  float blockClipProgressLongPass = 0.8f;  // 0x142572348
  float blockClipProgressHighScale = 0.5f;  // 0x142565600
  float blockHighBall_m = 0.5f;           // 0x142565600
  // Loose ball: blockable for this long after it was kicked; the speedBall
  // window replaces it once the ball is faster.
  float blockLooseSpeedBall_kmh = 70.0f;  // 0x14259be4c
  // block.o (VFS): the two windows in seconds.
  float blockNormal_s = 0.0f;   // block.o +0x40
  float blockSpeedBall_s = 0.0f;  // block.o +0x44

  // --- receive (reaction entry 3, docs/re/pes-re-receiving.md) ---
  ReceiveConstants receive;
};


std::optional<DefenderDecisionConstants> LoadDefenderDecisionConstants(
    const PeImage& exe, const PesVfs& vfs, std::string* error);

// --- The reaction chooser (report 1) ---

// The reactions GF can express, in PES's table order. Each is one entry of
// FUN_140a06dd0's 17-entry walk; the entries GF cannot express are listed in
// the port report (docs/re/pes-re-defender-decisions.md 13).
enum class DefenderReaction {
  kNone,           // nothing fired: the team-level movement command stands
  kTackle,         // entry 1, standing tackle (PES AI 0x31 / anime 0xd)
  kSlide,          // entry 2, slide tackle (PES AI 0x30 / anime 0xb or 0xc)
  kReceive,        // entry 3, ball receive FUN_1409f8220 (pes-re-receiving.md §1;
                   // entry 0 is the body-contact reaction, not the receive)
  kBlock,          // entry 8, pass/shot block (PES anime 0xe)
  kJostleForPass,  // entry 11, jostle for an incoming pass (PES anime 0x48)
};

// Which predicate each predicate said yes to this frame.
struct DefenderReactionInput {
  bool receive = false;
  bool tackle = false;
  bool slide = false;
  bool block = false;
  bool jostleForPass = false;
};

// The first predicate that returns true wins. PES: `cmd+0x880 = 1` on the
// winner and the reaction command replaces the movement command.
DefenderReaction ChooseReaction(const DefenderReactionInput& in);

// --- Standing tackle (report 2) ---

// Step 5's ball gate, the branch a defender outside
// {FREE_RUN, WAIT_TIMER, DELAY, PRESS, SAND} takes: the ball 0.25 s ahead
// inside the radius and off his predicted heading by less than the angle.
// `ballAheadDistance_m` is a length, compared squared.
bool StandingTackleBallGate(const DefenderDecisionConstants& c, float ballAheadDistance_m,
                            float ballAheadHeadingAngle_deg);

// The from-behind tolerance (report 12.2, direction OBS). Above the
// carrier-relative angle the tackle is refused unless the approach clears the
// aggression tolerance, or the carrier's and the defender's headings differ by
// more than the extra angle. `approachOffFacing_deg` is
// |bearing(carrier->defender) - carrier heading|, `headingDelta_deg` the
// carrier's and defender's headings, `carrierBehind` the first angle above the
// threshold. PES reads `comiss xmm6, xmm0; jbe <allow>` for the tolerance and
// `comiss 25, xmm1; jae <refuse>` for the extra angle. The A = 2 tolerance is
// the zeroed register PES leaves at 0x1409707a2, so the first clause is always
// satisfied at A = 2 and this branch never refuses a behind challenge there
// (report 12.2 reads the branch the other way; the raw branch sense goes this
// way, so the port follows the raw).
bool StandingTackleFromBehindAllowed(const DefenderDecisionConstants& c, bool carrierBehind,
                                     float approachOffFacing_deg, float headingDelta_deg,
                                     int aggressionLevel);

// The engagement window: his heading inside the angle of the carrier and the
// predicted gap inside the distance.
bool StandingTackleEngageWindow(const DefenderDecisionConstants& c, float engageAngle_deg,
                                float engagedGap_m);

// The safety term (report 2 "safety", 12.2 raw). `sf` is PES's
// `amp * sin(90 deg * clip progress)`, in [0, 1]. A defender with no cover, or
// on his team's last line, is refused once `gs / safetyEventFramesDivisor`
// frames have passed since the last ball event and the progress's sine is
// below either threshold. PES applies it only to a defender who is neither
// PRESSing nor SANDing. `eventAgeTicks` is this engine's own tick count, as
// everywhere in GF.
bool StandingTackleSafetyVeto(const DefenderDecisionConstants& c, float sf, bool noCover,
                              bool lastLine, float eventAgeTicks);

// The carrier's `sf` from his clip progress (0 = the clip just started, 1 = its
// contact frame): the sine of 90 deg times it.
float SafetyProgressSine(float clipProgress01);

// The lead, in GF ticks, the challenge is timed into the carrier's touch with:
// PES waits until the contact is (tackleReach - x) frames away, x = 0.075/0.1/
// 0.125 seconds by aggression level A = 0/1/2. PES frames become GF ticks the
// way pesballphysics.hpp converts the ball's per-tick factors: one GF tick is
// 0.54 of a PES tick.
int TackleTouchLeadTicks(const DefenderDecisionConstants& c, int aggressionLevel);

// --- Slide (report 3) ---

// PES's slide aggression L. ENFORCER style (+1), the team's aggressive sheet
// toggle (+1), the team's defensive mode (+2) and stamina under 30% (+1),
// capped at 3. The report's remaining team bytes (the marking plan flag, the
// press timer, the attack-level byte) have no GF counterpart and are not
// counted; see the port report.
struct SlideAggressionInput {
  bool enforcerStyle = false;
  bool aggressiveTeamSheet = false;
  bool deepDefensiveMode = false;
  bool staminaUnder30 = false;
};
int SlideAggressionLevel(const SlideAggressionInput& in);

// The slide's reach: `4 + 0.8 * L` metres to the contact point.
float SlideReach_m(const DefenderDecisionConstants& c, int aggressionLevel);

// The engagement window the slide shares with the standing tackle (raw
// 0x140972377..0x140972398, `COMISS XMM0,[0x1425cb30c]; JNC <refuse>`): the
// bearing from him to the point he is sliding at must be inside the angle of
// his heading. `targetBearing_deg` is where the point is, `heading_deg` where
// he faces.
bool SlideEngageWindow(const DefenderDecisionConstants& c, float targetBearing_deg,
                       float heading_deg);

// The slide's own direction window (raw 0x14097230c..0x14097231e,
// `COMISS XMM0,XMM7; JNC <refuse>`, XMM7 = 0x14259be5c = 90): beyond this the
// slide would run sideways, which the anime layer cannot express.
bool SlideDirectionWindow(const DefenderDecisionConstants& c, float slideBearing_deg,
                          float heading_deg);

// Inside his own box the slide only happens at L = 3, and PES then lowers L
// (2 when the team's defensive mode is set, else 0). `inOwnBox` gates on that
// rule; the level is returned.
int SlideAggressionOwnBox(const DefenderDecisionConstants& c, bool inOwnBox, bool deepDefensiveMode,
                          int aggressionLevel);

// --- Block (report 5) ---

// The owner radius: 3.0 m while the defender presses (or is pad-controlled in
// PES, which never happens in AI-vs-AI), else 1.3 m. A shot multiplies it and
// a feint sub-class does too.
float BlockRadius_m(const DefenderDecisionConstants& c, bool pressing, bool shot, bool feint);

// The clip-progress fraction after which a pass/shot widens the radius, and
// whether the kick's clip has reached it.
float BlockClipProgressFraction(const DefenderDecisionConstants& c, bool longPass, bool highBall);
bool BlockClipWindowOpen(float clipProgress01, float fraction);

// The seconds a loose ball stays blockable: speedBall once it is faster than
// the threshold, normal otherwise.
float BlockLooseWindow_s(const DefenderDecisionConstants& c, float ballSpeed_kmh);

}  // namespace pes
