#pragma once

// PES 2021's set-piece team AI: who takes a restart, where everyone stands, and
// what the wall does. docs/re/pes-re-setpiece-kicker.md 1 and 3,
// docs/re/pes-re-setpiece-choreography.md 5.2, 5.3, 11.3, 11.4, 11.5, 11.6 and 11.8.
//
// Every number comes from the user's own PES2021.exe through
// pes::PeImage::ReadVa: floats from .rdata, immediates only after their opcode
// bytes have been checked. The functions below are pure so the rules can be
// pinned without a match.

#include <vector>

#include "../gametypes.hpp"

#include "pes/exe.hpp"

namespace pes {

// One player, as the set-piece selection rules see him. GF's Player* glue in
// teamAIcontroller.cpp fills this in; nothing here needs a match.
struct SetPieceCandidate {
  int index = -1;             // index into the team's active list
  e_PlayerRole role = e_PlayerRole_GK;
  float suitability = 0.0f;   // the per-player set-piece rating for this kind
  float aerial = 0.0f;        // height + jump + heading, PES's b[0x31]+b[0x2c]+b[0x1d]
  float distanceToBall_m = 0.0f;
  bool onPitch = true;
  bool isKeeper = false;
};

struct SetPieceTeamConstants {
  // FUN_140929610: every placement move is a jog at 16 km/h
  // (`16000` @0x14259d638 over `fps*3600`, `3600` @0x142598590).
  float jogKmh = 0.0f;
  float secondsPerHour = 0.0f;
  // FUN_140927ab0's budget table, in seconds of logic time; a negative budget is
  // "no limit" (PES passes FLT_MAX for the kicker, the second kicker, a wall
  // man, the own keeper and the corner post man).
  float budgetCorner_s = 0.0f;         // 20 @0x14259be1c
  float budgetPostMan_s = 0.0f;        // 300 @0x1425a266c
  float budgetOwnBack_s = 0.0f;        // 7 @0x14259bdf0, own side, back class
  float budgetOwnMid_s = 0.0f;         // 10 @0x142588210, own side, mid class
  float budgetOwnFront_s = 0.0f;       // 13 @0x1425c5948, own side, front class
  float budgetOtherMarking_s = 0.0f;   // 8 @0x14259bdf4, other side, marked
  float budgetOther_s = 0.0f;          // 12 @0x14259be04, other side, unmarked
  float budgetThrowInSupport_s = 0.0f; // 3 @0x14258c264
  float budgetThrowIn_s = 0.0f;        // 2 @0x142565604
  float takerSlotBudgetFactor = 0.0f;  // 1.5 @0x14259bddc
  // FUN_1404a8ad0's free-kick waterfall distances.
  float fkDesignatedNear_m = 0.0f;  // 32 @0x14259be28
  float fkDesignatedFar_m = 0.0f;   // 60 @0x14259a764
  float fkKeeperOwnGoal_m = 0.0f;   // 28 @0x1425a2004
  float fkNearest_m = 0.0f;         // 65 @0x14259be48
  // FUN_1404a9ce0 / FUN_140a3c9b0: the wall.
  float wallDistance_m = 0.0f;  // 9.15 @0x1425a945c
  float wallSpacing_m = 0.0f;   // 0.5 @0x142565600
  float wallNearPost_m = 0.0f;  // 1.3 @0x14259bdd8
  // FUN_1404a9920's size table: distances from the ball to the goal, angles in
  // degrees off the goal's centre line.
  float wallGoalLineBand_m = 0.0f;   // 7.5 @0x14259ce3c
  float wallNarrowDist_m = 0.0f;     // 28 @0x1425a2004
  float wallNarrowBand_m = 0.0f;     // 5 @0x14259222c
  float wallCentralGap_m = 0.0f;     // 3 @0x14258c264
  float wallCentralHalfZ_m = 0.0f;   // 10 @0x142588210
  float wallD25_m = 0.0f;            // 25 @0x14259be20
  float wallD29_m = 0.0f;            // 29 @0x1425a9460
  float wallD32_m = 0.0f;            // 32 @0x14259be28
  float wallD36_m = 0.0f;            // 36 @0x1425a9464
  float wallD40_m = 0.0f;            // 40 @0x14259be2c
  float wallAngle20_deg = 0.0f;      // 20 @0x14259be1c
  float wallAngle30_deg = 0.0f;      // 30 @0x142591140
  float wallAngle40_deg = 0.0f;      // 40 @0x14259be2c
  float wallAngle45_deg = 0.0f;      // 45 @0x14259be30
  float wallAngle50_deg = 0.0f;      // 50 @0x14259be34
  float wallSpan20_deg = 0.0f;       // 20 @0x14259be2c
  float wallSpan15_deg = 0.0f;       // 15 @0x14259d668, negated in the image
  float wallSpan10_deg = 0.0f;       // 10 @0x142588210
};

// The immediate counts of FUN_1404a9920, FUN_140525ec0 and FUN_1404f6680. They
// are `mov ecx, imm` operands, so there is no .rdata word to read and they are
// carried here with their instruction address instead.
// ponytail: immediates, not readable at runtime; the wall's cap and the branch
// bases, so a different build only shifts these counts.
struct SetPieceTeamCounts {
  int wallCap = 7;          // mov eax, 7 @0x1404a996e
  int wallCentralCount = 6;  // mov ecx, 6 @0x1404a9aeb
  int wallNarrowBase = 2;    // 2 + (ax <= F[6] + 5)
  int wallD25Base = 4;      // mov ecx, 4 @0x1404a9b38
  int wallD29Base = 3;      // mov ecx, 3 @0x1404a9b8b
  int wallD32Base = 2;      // 2 + int(2*t)
  int wallD36Base = 1;      // 1 + int(t)
  int wallDouble = 2;       // int(2*t), `mulss xmm2, -2` @0x14259a768
  // FUN_140525ec0's jump slots, as immediates: jump when r > 24 (`cmp cl, 0x19`),
  // press when r <= 2 (`cmp cl, 3; jge`).
  int jumpRollMax = 24;
  int pressRollMax = 2;
  // FUN_1404f6680's box-attacker count: 4 + min(attack level, 2).
  int boxAttackerBase = 4;
  int boxAttackerCap = 2;
};

std::optional<SetPieceTeamConstants> LoadSetPieceTeamConstants(const PeImage& exe,
                                                              std::string* error);

// PES's attack level 0..4 (choreography 11.5/11.6), read straight off the team's
// mentality rung: GF's five rungs are one-to-one with PES's five levels, so the
// map is the identity.
// ponytail: PES also shifts its level with plan commands (T+0x210) on top of
// the strategy's base; GF's mentality has no such offset, so a level change is
// only the manager's own.
int AttackLevel(int mentalityRung);

// FUN_1404f6680: how many of the attacking team go into the box.
int BoxAttackerCount(int attackLevel, const SetPieceTeamCounts& counts);

// The jog every placement move is clamped to, in m/s.
float JogSpeed_mps(const SetPieceTeamConstants& k);

// FUN_140927ab0: the seconds a player gets to reach his mark. Negative means
// "no limit".
float PlacementBudget_s(const SetPieceTeamConstants& k, int setPiece, bool ownSide,
                        e_PlayerRole role, bool kicker, bool secondKicker, bool wallMan,
                        bool ownKeeper, bool postMan, bool isMarking);

// FUN_140929610's whole rule as one factor: `n <= B ? 1 : B / n`.
float JogFraction(float budget_s, float dist_m, const SetPieceTeamConstants& k);

// FUN_140a621f0: the rating fallback. Highest suitability wins, first on ties;
// a keeper is never in the running.
int PickBySuitability(const std::vector<SetPieceCandidate>& candidates);

// FUN_1404a8ad0's waterfall as far as GF can reach it: GF carries no team sheet
// of designated takers (PES T+0x8e08), so that branch is not representable. What
// is left is the keeper near the own goal, the own-half defender, the
// long-range midfielder and the rating fallback. All distances in metres.
int FreeKickTaker(const std::vector<SetPieceCandidate>& candidates, float ballX, float ballZ,
                  float dir, float attackedGoalX, float ownGoalX, float penaltyX,
                  float penaltyHalfZ, const SetPieceTeamConstants& k);

// FUN_1404a9920: how many men stand in the wall. `ballAngle_deg` is the ball's
// angle off the goal's centre line.
int WallSize(float ballToGoal_m, float ballAngle_deg, float ballX, float ballZ, float dir,
             float goalHalfLength, float penaltyX, float penaltyHalfZ,
             const SetPieceTeamConstants& k, const SetPieceTeamCounts& counts);

// The state FUN_140525ec0 threads through the wall's members in slot order.
struct WallJumpState {
  bool active = false;
  int notJumpLeft = 0;
  bool pressed = false;
};

// 0 jump, 1 not jump, 2 press (PES's WALL_JUMP / WALL_NOT_JUMP / WALL_PRESS).
int WallAction(int wallSize, int slot, int playerIndex, int frameCounter,
               WallJumpState& state);

// The state a wall of `wallSize` starts with: notJumpLeft = (n-1)/2.
WallJumpState WallJumpStateStart(int wallSize);

}  // namespace pes
