#pragma once

// PES 2021's player-vs-player contact reaction: docs/re/pes-re-contact.md §1-2
// and pes-re-contact-2.md (ContactRE, ContactLeftoversRE).
//
// Every tick each player in body contact runs AI reaction entry 0
// (FUN_140844cb0): the 3x3 level table of FUN_140842d20 over how far the
// contact has pushed his body off its animation and the pair's closing speed,
// cut by the contact.o thresholds; the forced falls of FUN_140844540 (which
// skip the vetoes); then the nine veto predicates of 0x14348cab0. Level 0
// nothing, 1-2 STAGGER, 3 FALL_DOWN. Separately, the side-by-side balance gauge
// ap+0x2820 (FUN_1406f9b70) drains while two runners are shoulder to shoulder
// and staggers the loser when it goes negative. No RNG anywhere (report §5).
//
// Pure functions over plain inputs, so the layer is testable against synthetic
// constants; Match builds the inputs from GF's state. Angles are unsigned
// degrees, speeds km/h, distances metres.
//
// Where the numbers live: contact.o of common/match/constant/constant_player.bin
// (VFS, payload offsets = runtime offsets - 8, contact-2 §2.1) and the exe's
// .rdata / instruction immediates (PeImage).

#include <optional>
#include <string>

#include "pes/exe.hpp"
#include "pes/vfs.hpp"

namespace pes {

// FUN_140843c30's hit-part classes.
enum class ContactPart { kRightLeg = 0, kLeftLeg = 1, kBody = 2 };

// What the opponent is doing (report §1.3 `m`).
enum class ContactOpponentMode { kPlain = 0, kTackle = 1, kSlide = 2 };

enum class ContactReaction { kNone, kStagger, kFall };

struct ContactConstants {
  // --- contact.o (VFS), indexed t = (selfPart == body) + 2*mode ---
  float dS_m[6] = {};  // stagger displacement
  float dF_m[6] = {};  // fall displacement
  float vS_kmh = 0.0f;  // +0x34
  float vF_kmh = 0.0f;  // +0x30
  float forcedFallSpeedBody_kmh = 0.0f;  // +0x04
  float forcedFallDisplacement_m = 0.0f;          // +0x24 (runtime +0x2c)
  float forcedFallDisplacementTackling_m = 0.0f;  // +0x2c (runtime +0x34)
  bool fromBehindFallEnabled = false;             // +0x00 byte

  // --- .rdata ---
  int levelTable[9] = {};   // 0x1425efbf0, M[dl][vl]
  int levelAction[4] = {};  // 0x1425efc18, level -> AI action (0x58 STAGGER / 0x59 FALL)
  int fallAction = 0;       // the FALL_DOWN id the table maps level 3 to
  // Opponent keeper handling (mask 0x3500000000000000, raw 0x14084306f).
  float keeperDS_m = 0.0f, keeperDF_m = 0.0f, keeperVS_kmh = 0.0f, keeperVF_kmh = 0.0f;
  float committedScale = 0.0f;  // 0x1425ab128: thresholds x(s), forced speed x(1+s)
  // Receiving-body clash (raw 0x1408434e9..0x14084366e).
  float abilityFloor = 0.0f, abilitySpan = 0.0f;  // 40 / 59
  float clashAbilityGain_kmh = 0.0f;              // 3
  float clashTrapFall_kmh = 0.0f, clashTrapStagger_kmh = 0.0f;  // 25 / 22
  float clashPartner_m = 0.0f;   // 0.65
  float clashBallClear_m = 0.0f;  // 0.25
  float approachAngle_deg = 0.0f;  // 112.5: slide approach, running away, carrier cap
  float behindAngle_deg = 0.0f;    // 135
  float runnerSpeed_kmh = 0.0f;    // 13.5
  // Veto 8 (FUN_1408424c0).
  float vetoRelativeSpeed_kmh = 0.0f;  // 10
  float vetoStillExtra_kmh = 0.0f;     // +12 when nearly still
  float vetoStill_kmh = 0.0f;          // 5
  float vetoHoldExtra_kmh = 0.0f;      // -5 inside a hold window
  float vetoFacing_deg = 0.0f;         // 60
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
};

std::optional<ContactConstants> LoadContactConstants(const PeImage& exe, const PesVfs& vfs,
                                                     std::string* error);

struct ContactInput {
  ContactPart selfPart = ContactPart::kBody;
  ContactPart oppPart = ContactPart::kBody;
  ContactOpponentMode mode = ContactOpponentMode::kPlain;
  float displacement_m = 0.0f;   // body pushed off its animation
  float closingSpeed_kmh = 0.0f;
  float relativeSpeed_kmh = 0.0f;
  float selfSpeed_kmh = 0.0f;
  float oppSpeed_kmh = 0.0f;
  float headingDelta_deg = 0.0f;   // self heading vs opponent heading
  float movementDelta_deg = 0.0f;  // self movement vs opponent movement
  float oppAway_deg = 0.0f;        // opponent movement vs opponent->self
  float slideApproach_deg = 0.0f;  // opponent movement vs opponent->contact
  float oppBehind_deg = 0.0f;      // self heading vs self->opponent
  float partnerDistance_m = 0.0f;
  float selfBallDistance_m = 0.0f;
  int selfContactAbility = 0;  // Physical Contact, PES 40..99
  bool oppKeeperHandling = false;
  bool selfCommitted = false;  // mid-kick before its contact frame
  bool selfTackling = false;
  bool selfTrapping = false;
  bool selfStaggering = false;
  bool selfOwnsBall = false;
  bool selfLocomotion = false;
  bool oppLocomotion = false;
  bool oppTackling = false;            // standing tackle or slide
  bool oppBeforeContactFrame = false;  // its tackle not yet at its contact frame
};

// FUN_140842d20 + FUN_140844540 + the veto walk: the final level 0..3.
int ContactLevel(const ContactConstants& k, const ContactInput& in);
ContactReaction ReactionForLevel(const ContactConstants& k, int level);

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

}  // namespace pes
