#pragma once

// PES 2021's player-vs-player contact reaction: docs/re/pes-re-contact.md §1-4
// and pes-re-contact-2.md (ContactRE, ContactLeftoversRE).
//
// Every tick each player in body contact runs AI reaction entry 0
// (FUN_140844cb0): the jostle result, then the 3x3 level table of
// FUN_140842d20 over how far the contact has pushed his body off its
// animation and the pair's closing speed, cut by the contact.o thresholds;
// the forced falls of FUN_140844540 (which skip the vetoes); the nine veto
// predicates of 0x14348cab0; then the direct geometric triggers of §1.6.
// Level 0 nothing, 1-2 STAGGER, 3 FALL_DOWN. Separately, the side-by-side
// balance gauge ap+0x2820 (FUN_1406f9b70) drains while two runners are
// shoulder to shoulder and staggers the loser. No RNG anywhere (report §5).
//
// Pure functions over plain inputs, so the layer is testable against synthetic
// constants; Match builds the inputs from GF's state. Angles are unsigned
// degrees, speeds km/h, distances metres, times seconds.
//
// Where the numbers live: contact.o of common/match/constant/constant_player.bin
// (VFS, payload offsets = runtime offsets - 8, contact-2 §2.1) and the exe's
// .rdata / instruction immediates (PeImage).

#include <optional>
#include <string>

#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pes {

// The 19-bone collision enum (pes-re-collision.md §2.1).
enum ContactBone : int {
  kBoneHip = 0,
  kBoneThighL, kBoneLegL, kBoneFootL,
  kBoneThighR, kBoneLegR, kBoneFootR,
  kBoneBelly, kBoneChest, kBoneNeck, kBoneHead,
  kBoneShoulderL, kBoneUpperArmL, kBoneForearmL, kBoneHandL,
  kBoneShoulderR, kBoneUpperArmR, kBoneForearmR, kBoneHandR,
};

// FUN_140843c30's hit-part classes.
enum class ContactPart { kRightLeg = 0, kLeftLeg = 1, kBody = 2 };
ContactPart PartClass(int bone);
// The arm parts veto 0 tests ((part - 12) & ~4 == 0).
bool IsArmBone(int bone);

// The PES anime action classes the contact code tests (action-property
// predicates of the report's notation).
enum class ContactAction {
  kLoco,              // NOTHING / MOVE / STEP_MOVE
  kDribble,           // DRIBBLE family (owns the ball, not locomotion)
  kTrap,              // TRAP
  kKick,              // LONG_PASS / SHORT_PASS / SHOOT
  kSlide,             // SLIDING(_KEEP)
  kTackle,            // TACKLE
  kBlock,             // BLOCK
  kStagger,           // STAGGER
  kFall,              // FALL_DOWN
  kJostle,            // JOSTLE
  kDodge,             // DODGE
  kKeeperHandle,      // KP_CATCH / KP_DEFLECT / KP_SCOOP_OUT / KP_BLOCK
  kKeeperDistribute,  // KP_THROWPASS / KP_PUNTKICK
  kOther,
};

enum class ContactOpponentMode { kPlain = 0, kTackle = 1, kSlide = 2 };
enum class ContactReaction { kNone, kStagger, kFall };

struct ContactConstants {
  // --- contact.o (VFS), indexed t = (selfPart == body) + 2*mode ---
  float dS_m[6] = {};  // stagger displacement
  float dF_m[6] = {};  // fall displacement
  float vS_kmh = 0.0f;  // +0x34
  float vF_kmh = 0.0f;  // +0x30
  float forcedFallSpeedBody_kmh = 0.0f;           // +0x04
  float forcedFallDisplacement_m = 0.0f;          // +0x24 (runtime +0x2c), part 0
  float forcedFallDisplacementUpper_m = 0.0f;     // +0x28 (runtime +0x30), parts 1-2
  float forcedFallDisplacementTackling_m = 0.0f;  // +0x2c (runtime +0x34)
  bool fromBehindFallEnabled = false;             // +0x00 byte

  // --- .rdata ---
  int levelTable[9] = {};   // 0x1425efbf0, M[dl][vl]
  int levelAction[4] = {};  // 0x1425efc18, level -> AI action
  int fallAction = 0;       // the FALL_DOWN id level 3 maps to
  float forcedFallPart3_m = 0.0f;  // 0.5 @0x142565600, part 3's own bar
  // Opponent keeper handling (mask 0x3500000000000000, raw 0x14084306f).
  float keeperDS_m = 0.0f, keeperDF_m = 0.0f, keeperVS_kmh = 0.0f, keeperVF_kmh = 0.0f;
  // Frontal standing tackle (raw 0x140843098..0x1408430d2).
  float frontalHeading_deg = 0.0f;  // 78.75
  float frontalDF_m = 0.0f;         // 0.05 (dS becomes 0)
  float committedScale = 0.0f;  // 0x1425ab128: thresholds x(s), forced speed x(1+s)
  // Push-away jostle clips (FUN_140a6e170) between 8/60 and 16/60 s: x1.5.
  float pushAwayFrom_frames = 0.0f, pushAwayTo_frames = 0.0f;  // 8 / 16 @60
  float pushAwayScale = 0.0f;                                  // 1.5
  float framesPerSecondRef = 0.0f;                             // 60, the @60 base
  // Jostle hold-up (raw 0x140843308..0x1408433fb).
  int holdUpContactOffset = 0;       // lea ecx,[rbx+0x3c]
  float holdUpFloor = 0.0f, holdUpCeil = 0.0f, holdUpSpan = 0.0f;  // 20 / 100 / 80
  float holdUpFrames = 0.0f;         // 120 (base and gain)
  float holdUpDSScale = 0.0f;        // 0.005
  float holdUpVS_kmh = 0.0f;         // -3
  float holdUpHalf = 0.0f;           // 0.5: dF, vF x0.5, forced x1.5
  int shoulderThroughMargin = 0;     // add ecx,5 @0x140780c55
  // Receiving-body clash (raw 0x1408434e9..0x14084366e).
  float abilityFloor = 0.0f, abilityTop = 0.0f, abilitySpan = 0.0f;  // 40 / 99 / 59
  float clashAbilityGain_kmh = 0.0f;                    // 3
  float clashFall_kmh = 0.0f, clashStagger_kmh = 0.0f;  // +7 / -6
  float clashTrapFall_kmh = 0.0f, clashTrapStagger_kmh = 0.0f;  // 25 / 22
  float clashPartner_m = 0.0f;    // 0.65
  float clashFeetHeight_m = 0.0f;  // 0.25: both feet above it
  float approachAngle_deg = 0.0f;  // 112.5
  float behindAngle_deg = 0.0f;    // 135
  float sideAngle_deg = 0.0f;      // 45
  float runnerSpeed_kmh = 0.0f;    // 13.5
  // Veto 0 (FUN_140842950): the touched foot trailing the body.
  float footTrail_m = 0.0f;  // -0.25 x scale
  // Veto 8 (FUN_1408424c0).
  float vetoRelativeSpeed_kmh = 0.0f;  // 10
  float vetoStillExtra_kmh = 0.0f;     // +12 when nearly still
  float vetoStill_kmh = 0.0f;          // 5
  float vetoHoldExtra_kmh = 0.0f;      // -5 inside a hit window
  float vetoFacing_deg = 0.0f;         // 60
  float vetoPastContact_frames = 0.0f;  // 10 @60
  // Displacement fallbacks (FUN_140843c50).
  float slideLegDisplacement_m = 0.0f;  // 0.2 @0x142598578
  float slideFlag_frames = 0.0f;  // 6 @60: FUN_140789d00 sets bit 15 in slide phase 4
  // Direct triggers (§1.6, FUN_140844cb0 tail).
  float directRange_m = 0.0f;           // 1.5
  float directFootHead_m = 0.0f;        // 0.1
  float directFootHeadGamesmanship_m = 0.0f;  // 0.2 with skill bit 21 (MALICIA)
  float directNear_m = 0.0f;            // 1.2
  float studsRunSpeed_kmh = 0.0f;       // 16
  float studsContest_frames = 0.0f;     // 10 @60
  float studsBallLateral_m = 0.0f;      // 0.7
  float studsBase_m = 0.0f, studsGain_m = 0.0f;  // 0.3 + 0.2 x (1 - PC term)
  float tripRange_m = 0.0f;             // 1.0
  float tripLateral_m = 0.0f;           // 0.5
  int tackleContactWindow_frames = 0;   // +-4 frames
  float tackleStudBase_m = 0.0f, tackleStudGain_m = 0.0f;  // 0.2 + 0.1 x (1 - PC term)
  float pushTripFoot_m = 0.0f;          // 0.3
  float pushTripLateral_m = 0.0f;       // 1.0
  // Balance gauge (raw 0x1406f9de8..0x1406fa13f).
  int gaugeFull = 0;            // 1e6 immediate
  int gaugeDrainNumerator = 0;  // -1e6 immediate
  int gaugeRefillSeconds = 0;   // the lea [rax+rax*2] scale
  float gaugeSideMin_deg = 0.0f, gaugeSideMax_deg = 0.0f;  // 45 / 135
  float gaugeSideSpan_deg = 0.0f;                          // 90
  float gaugeBase_s = 0.0f;                                // 1.5
  float gaugeAbilityDivisor = 0.0f;                        // 120
  float gaugeHalf = 0.0f;                                  // 0.5
  float gaugeOne = 0.0f;                                   // 1.0
  int gaugeContactMin = 0, gaugeContactMax = 0;            // FUN_1408caf60 stat 0x28 bounds
  float kmhPerMps = 0.0f;                                  // 3.6
  // Playback (§4.3).
  float staggerRateBase = 0.0f, staggerRateGain = 0.0f;  // 0.95 / 0.15
  float riseRateLow = 0.0f, riseRateHigh = 0.0f;         // 0.95 / 1.1
  float riseAbilitySpan = 0.0f;                          // 60
  float riseBlend_s = 0.0f;                              // 0.3, rise-up blend-out
  // Push-away start (FUN_1407a5cc0).
  float pushFoot_m = 0.0f;         // 0.5
  float pushBallAhead_m = 0.0f;    // 0.3
  float pushBallAcross_m = 0.0f;   // 0.5
  float pushAngle_deg = 0.0f;      // 90
  float pushOppSpeed_kmh = 0.0f;   // 8
  float pushBallDir_deg = 0.0f;    // 67.5
  float pushOppAlong_m = 0.0f;     // -0.1
  // FUN_1408440f0 / FUN_140843e30: the deep-overlap allowance.
  float contactAllowance_m = 0.0f;  // 0.23
};

std::optional<ContactConstants> LoadContactConstants(const PeImage& exe, const PesVfs& vfs,
                                                     std::string* error);

struct ContactInput {
  int selfBone = kBoneChest;  // own part at the contact
  int oppBone = kBoneChest;   // partner's part
  int oppSlideLegBone = kBoneChest;  // human(opp)+5: the slide's leg
  ContactAction selfAction = ContactAction::kLoco;
  ContactAction oppAction = ContactAction::kLoco;
  bool selfRecovering = false;  // locomotion still blending out of a stagger/fall
  bool oppPastLastHit = false;  // opponent's clip past its last hit frame (veto 7)
  // Displacement and speed (FUN_140843c50 / FUN_140843a20).
  float displacement_m = 0.0f;       // part 0 (hip, this tick's separation removed)
  float displacementLegs_m = 0.0f;   // parts 1-3 (2D)
  float closingSpeed_kmh = 0.0f;
  float relativeSpeed_kmh = 0.0f;
  float selfSpeed_kmh = 0.0f;
  float oppSpeed_kmh = 0.0f;
  float headingDelta_deg = 0.0f;    // self heading vs opponent heading
  float movementDelta_deg = 0.0f;   // self movement vs opponent movement
  float oppAway_deg = 0.0f;         // opponent heading vs bearing to self (veto 6)
  float slideApproach_deg = 0.0f;   // opponent movement vs opponent->contact point
  float oppBehind_deg = 0.0f;       // self heading vs bearing to opponent
  float selfToOppBearing_deg = 0.0f;  // self heading vs bearing to opponent (veto 1)
  float partnerDistance_m = 0.0f;
  float selfFeetHeight_m = 0.0f;    // the lower foot's height
  float contactHeight_m = 0.0f;     // contact point height
  float selfKneeHeight_m = 0.0f;
  float touchedFootForward_m = 0.0f;  // touched foot along own heading, rel. to body
  int selfContactAbility = 0;  // Physical Contact, PES 40..99
  int oppContactAbility = 0;
  bool oppTackleFrontal = false;      // tacklefoot clip set (FUN_140a6e360)
  float oppClipProgress = 0.0f;       // frame / contact frame of its clip
  bool oppBeforeContactFrame = false;
  bool selfBeforeContactFrame = false;
  bool selfInHitWindow = false;
  float selfPastContact_s = -1.0f;    // < 0: no contact frame passed
  float selfPushAway_s = -1.0f;       // time into a push-away jostle clip, < 0 none
  float oppJostleContact_s = 0.0f;    // human(opp)+0x2f in seconds
  bool selfOwnsBall = false;
  bool selfIsKeeper = false;  // FUN_140a638c0: the team's role-0 player
  bool oppSlidingFlag = false;  // human+8 bit 15: the slide's 6/60 s impact window
};

// FUN_140842d20 + FUN_140844540 + the veto walk: the final level 0..3.
int ContactLevel(const ContactConstants& k, const ContactInput& in);
ContactReaction ReactionForLevel(const ContactConstants& k, int level);

// FUN_140843c50 over GF's rigid body: every tracked part moves with the root
// offset, so part 0 is the offset less this tick's separation and parts 1-3
// are its 2D length. A slide whose leg matches the hit leg reads 0.2 m.
struct Displacement {
  float part0_m, legs_m;
};
Displacement TrackedDisplacement(const ContactConstants& k, float offX, float offY, float sepX,
                                 float sepY, bool slideLegMatches);

// FUN_140843a20: both velocities less their contact separation, projected on
// the line between them (m/s in, km/h out).
float ClosingSpeed(const ContactConstants& k, float selfVx, float selfVy, float oppVx, float oppVy,
                   float dirX, float dirY);

// Jostle hold-up: the challenger's contact time after which the carrier's
// thresholds collapse, seconds.
float JostleHoldUp_s(const ContactConstants& k, int carrierContact, int jostlerContact);
// Shoulder-through: the guard_back clip becomes tackleshoulder_3_3.
bool ShoulderThrough(const ContactConstants& k, int jostlerContact, int carrierContact);

// The dribbler's push-away (FUN_1407a5cc0 @0x1407a65e0..0x1407a6640): the
// js_run_*_push_away clips of FUN_140a6e170's set start when an opponent's
// foot is at his belly beside him, the ball ahead and the run straight.
struct PushAwayInput {
  bool dribbling = false;
  bool alreadyPushing = false;
  float oppFootToSelfBelly_m = 0.0f;
  float ballAhead_m = 0.0f, ballAcross_m = 0.0f;  // ball in own heading frame
  float headingVsOppHeading_deg = 0.0f;
  float headingVsOppMove_deg = 0.0f;
  float oppSpeed_kmh = 0.0f;
  float ballDirVsHeading_deg = 0.0f;
  float oppAlong_m = 0.0f;  // opponent along own heading
};
bool PushAwayStarts(const ContactConstants& k, const PushAwayInput& in);

// §1.6, run after the level path when it produced nothing.
struct DirectTriggerInput {
  ContactAction selfAction = ContactAction::kLoco;
  ContactAction oppAction = ContactAction::kLoco;
  float distance_m = 0.0f;
  float oppFootToSelfHead_m = 0.0f;
  float oppFootToSelfChest_m = 0.0f;
  float oppFootToSelfBelly_m = 0.0f;
  bool selfOwnsBall = false;
  float selfSpeed_kmh = 0.0f;
  float selfContested_s = 0.0f;
  bool oppPastContactFrame = false;
  float oppFromContact_frames = 0.0f;  // |frame - contact frame| @60
  bool oppStandingTackleKind1 = false;
  float headingDelta_deg = 0.0f;
  float ballLateral_m = 0.0f;  // ball across own heading
  bool selfFeintBeforeContact = false;
  bool oppFeintBeforeContact = false;
  float selfMoveVsHeading_deg = 0.0f, oppMoveVsHeading_deg = 0.0f;
  float oppAlongSelf_m = 0.0f, oppAcrossSelf_m = 0.0f;  // opp in own heading frame
  float selfAlongOpp_m = 0.0f, selfAcrossOpp_m = 0.0f;  // self in opp heading frame
  float oppPushAway_s = -1.0f;
  int selfContactAbility = 0;
  bool selfGamesmanship = false;  // engine skill bit 21, MALICIA
};
struct DirectTrigger {
  ContactReaction reaction = ContactReaction::kNone;
  int cause = 0;
};
DirectTrigger DirectTriggers(const ContactConstants& k, const DirectTriggerInput& in);

struct BalanceGaugeInput {
  bool touching = false;
  bool bothLocomotion = false;
  float selfSpeed_kmh = 0.0f, partnerSpeed_kmh = 0.0f;
  float selfBearing_deg = 0.0f;     // own heading vs direction to partner
  float partnerBearing_deg = 0.0f;  // partner heading vs direction to self
  int selfContactAbility = 0, partnerContactAbility = 0;
};

// One update of ap+0x2820 at `framesPerSecond`; returns the new gauge.
int UpdateBalanceGauge(const ContactConstants& k, const BalanceGaugeInput& in, int gauge,
                       int framesPerSecond);

// §4.3: clip playback rates by Balance.
float StaggerRate(const ContactConstants& k, int balance);
float RiseUpRate(const ContactConstants& k, int balance);

}  // namespace pes
