#pragma once

// PES 2021's standing-tackle sub-kind (docs/re/pes-re-contact-port.md pass 4
// stage C): the readiness mask FUN_14078e5a0, the sub-kind pick
// FUN_14078b550 over the per-kind group tables FUN_14078fe90, and the
// row -> clip resolution those tables' ids go through.
//
// The pick, in full:
//   for kind 1..8:
//     if (mask >> kind) & 1:                      // FUN_14078e5a0's bit k = kind k
//       bits = FUN_14078f290(...)                 // the per-kind clip mask
//       for each (row, bit) of kind's group table (FUN_14078fe90):
//         if (bits >> bit) & 1 and FUN_14078a640(row) != 0:
//           sub-kind = kind; done
//   nothing ready (or nothing solved): sub-kind = 7.
// The group table's entries carry Animetable row ids, so a solved entry names
// a situation row, whose angle column gives the motion number and mirror bit
// (the same table the dodge selectors read).
//
// What is NOT ported, and why: FUN_14078a640's own geometry. It runs the
// tackle action's clip solver FUN_1406ecbb0, which steps a physics prediction
// of the tackle capsule over the clip's own contact record (HitData /
// Animation bins) and tests the reach against the carrier's predicted
// position. GF carries no tackle capsule, no clip contact window and no
// physics environment to predict through, so a solved entry is taken as a
// solved entry: the readiness mask, the first-ready-kind-wins order and the
// row -> clip chain are ported and the per-clip reach test is named as the
// gap. See the report for the measurement that bounds it.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pes/animetable.hpp"
#include "pes/exe.hpp"

namespace pes {

// The sub-kinds, Tackle+0x28's +0x40 (FUN_14078b550's store). The default when
// nothing is ready is 7.
enum class TackleSubKind : int {
  kNone = 0,
  k1 = 1,
  k2 = 2,
  k3 = 3,
  k4 = 4,
  k5 = 5,
  k6 = 6,
  k7 = 7,  // the default: nothing ready, or nothing solved
  k8 = 8,
  kCount = 9,
};

// One entry of a kind's group table: the Animetable row id and the bit index
// into the kind's clip mask (FUN_14078f290's return).
struct TackleGroupEntry {
  int row = -1;
  int bit = -1;
};

// The per-kind group tables of FUN_14078fe90, read from the exe.
//   kinds 1, 2, 3, 8 and the two halves of 7: a table of {row, bit}[count],
//     indexed directly by the slot (FUN_14078f4f0's speed class 0..3);
//   kinds 4, 5, 6: two such tables per slot (stride 0x20), the second at +0x10.
struct TackleConstants {
  // [kind - 1][slot] and [kind - 1][slot][0/1] for the double-width kinds.
  // The slot is FUN_14078f4f0's speed class: 0, 1 or 2.
  std::array<std::array<std::vector<TackleGroupEntry>, 3>, 8> groups{};
  // Kind 7's table is indexed (param_2 + slot * 2), where param_2 is
  // FUN_14078f4f0's third output: 0 standing, 1 in contact.
  std::array<std::array<std::vector<TackleGroupEntry>, 3>, 2> group7{};
  // FUN_14078b550's default when the readiness mask is 0: 7.
  int defaultSubKind = 7;
  // FUN_14078e5a0's bit-4 window: the opposed-body distance (5 m), the pair's
  // near and keeper-hands squared bounds (1.44 / 0.09 m^2), the gate that picks
  // between them, and the three angular bounds (135 / 36 / 36 deg).
  float opposedBody_m = 5.0f;
  float pairNearSq_m = 1.44f;
  float pairKeeperSq_m = 0.09f;
  float keeperGate_m = 0.434744f;
  float headingToBall_deg = 135.0f;
  float carrierToPoint_deg = 36.0f;
  float carrierToStep_deg = 36.0f;
  // FUN_14078e5a0's delay roll, per player per tick: 6 s of window over 60
  // frames per second, +0.15 s at the stream-2 draw, +0.125 s at the stream-1
  // draw and +0.09 s when the keeper gate holds (0x14259bdec / 0x14259a764 /
  // 0x1425c48a0 / 0x142565600 / 0x14259bdcc).
  float delayWindow_s = 6.0f;
  float delayFramesPerSecond = 60.0f;
  float delayStream2_s = 0.15f;
  float delayStream1_s = 0.125f;
  float delayKeeper_s = 0.09f;
  float delayFloor_s = 0.5f;
};

std::optional<TackleConstants> LoadTackleConstants(const PeImage& exe, std::string* error);

// FUN_14078e5a0: which sub-kinds are ready, as a bitmask (bit k => kind k).
// The exe's own gate chain, bit by bit:
//   bit 1 (0x2):   the player may play (param_7) and the post-tackle cooldown
//                  at think+0x2c has cleared;
//   bit 2 (0x4):   he is touching (param_4) and FUN_140477130 (the opposed-body
//                  distance) clears 5 m;
//   bit 3 (0x8):   the second-man flag (param_5) and think+0x60 clear, i.e. no
//                  other tackle is already committed;
//   bit 4 (0x10):  the pair lives inside the exe's "close" window and three
//                  angular tests pass:
//                    - their squared separation clears 1.44 m^2, or 0.09 m^2
//                      when the ball is above 0.434744 m (a lifted ball, read
//                      out of the physics env at +0x4) and the bound
//                      tightens;
//                    - the angle from the carrier's heading to the ball's
//                      resolved position is under 135 deg;
//                    - the angle from the carrier to that same resolved point
//                      is under 36 deg, twice: once against his own heading and
//                      once against a step the exe draws per frame;
//                    - and the tackle's frame-delay roll clears.
//   bits 5-6 (0x60): the same touching gate, set as one pair;
//   bit 7 (0x80):  the fallback when neither contact flag is live;
//   bit 8 (0x100): the "not in play" early-out.
// Bit 4's three angles read the tackle action's own resolved contact point
// (FUN_140439e50 over the clip's hit position), which only the clip solver
// produces, and its delay roll is FUN_14078e5a0's per-player RNG draw. So the
// caller supplies them and the port does not invent them.
struct TackleReadiness {
  bool mayPlay = false;       // param_7: the action may be played at all
  bool touching = false;      // param_4: he is touching the opponent
  bool secondMan = false;     // param_5: the second man in
  float opposedBody_m = -1.0f;  // FUN_140477130's distance (<0 = not opposed)
  bool cooldownClear = false;   // think+0x2c: the post-tackle cooldown is over
  bool otherTackleCommitted = false;  // think+0x60: another tackle already running
  float pairDistSq_m = 0.0f;    // the pair's squared separation
  bool ballLifted = false;      // the ball's height clears the 0.434744 gate
  float headingToBall_deg = 1e9f;  // the carrier's heading -> the resolved point
  float carrierToPoint_deg = 1e9f;  // the carrier -> the resolved point, his heading
  float carrierToStep_deg = 1e9f;   // the carrier -> the resolved point, drawn step
  bool delayRollOk = false;         // the frame-delay roll of bit 4
};

uint32_t ReadinessMask(const TackleReadiness& r, const TackleConstants& k);

// FUN_14078b550's pick: kinds 1..8 in order, the first ready kind whose group
// table has an entry whose clip-mask bit is set wins; nothing ready (or nothing
// live in any ready kind's table) gives kind 7.
// `clipMask` is FUN_14078f290's per-slot return, indexed the same way as the
// group tables' slots. `solverOk` answers "FUN_14078a640 solved this kind's
// row" per kind: the one thing the port cannot itself decide (see the header),
// so a caller that can decide it supplies the real answer and a caller that
// cannot passes all-true to take the first live entry.
TackleSubKind PickSubKind(const TackleConstants& k, uint32_t readiness,
                          const std::array<uint32_t, 8>& clipMask,
                          const Animetable& table, const std::array<bool, 8>& solverOk);

// The same, with the readiness mask assembled from `ready` first.
TackleSubKind PickSubKind(const TackleConstants& k, const TackleReadiness& ready,
                          const std::array<uint32_t, 8>& clipMask, const Animetable& table,
                          const std::array<bool, 8>& solverOk);

// The clip a solved entry names: the row's angle column of the Animetable.
// Returns -1 when the row is out of range or its column is empty.
struct TackleClip {
  int row = -1;
  int col = -1;
  int motionNo = -1;
  bool mirror = false;
};
std::optional<TackleClip> ResolveClip(const Animetable& t, const TackleConstants& k, int row,
                                      float relativeAngle_deg);

// FUN_14078f290: the clip mask, a bit per candidate clip of the kind's group.
// The bits come from the caller's standing flag, whether the player is
// committed (his anime slot's action class is 3) and the ball is ahead, and
// whether the second flag holds with the ball in front.
//
// The exe returns 0xffffffff - every bit set - when the record is not a live
// tackle record, and the same when the kind is 5 or 6 (both are gated off
// entirely), which is what makes those kinds' tables unreachable in the
// install. The ported form takes the same shape.
struct TackleClipMaskInput {
  bool recordLive = false;   // the Toughle record's +0x14 flag
  int kind = 0;              // the kind 1..8 whose mask this is
  bool standing = false;     // the caller's flag (1 at FUN_14078b550's call)
  bool committed = false;    // his anime slot's action class is 3 (a tackle)
  bool keeperBallHands = false;  // his action class is 0x34/0x35 (keeper hands)
  bool ballInFront = false;  // the resolved point is in front of him
  bool ballAheadOfSelf = false;  // the record's own +0x15 flag
};
uint32_t ClipMask(const TackleClipMaskInput& in);

// The pick as the simulation runs it, from the state GF carries.
//
// ponytail: PES's fit (FUN_14078a640) is a physics prediction of the tackle
// capsule against the clip's own contact record; GF has neither, so a ready
// kind's first entry is taken as solvable and the engine plays that clip. The
// consequence is that GF plays every sub-kind's first clip rather than the one
// whose reach actually fits, which is a coarser answer than PES's but uses the
// same readiness gates, the same order and the same clips. The nearest-opponent
// distance stands in for FUN_140477130's opposed-body distance, and the
// neighbour's Interfere clip for think+0x60's "another tackle committed".
//
// `bearingToCarrier_deg` is the defender's heading relative to the carrier, and
// `speedClass` FUN_14078f4f0's 0..2 (the caller's own, as the dodge selectors
// take it).
struct TackleSituation {
  float opposedBody_m = -1.0f;      // the nearest opponent's distance
  float pairDistSq_m = 0.0f;        // the carrier's squared separation
  float bearingToCarrier_deg = 0.0f;  // his heading towards the carrier
  float ballHeight_m = 0.0f;        // the ball's height, for the lifted-ball bound
  int speedClass = 0;               // FUN_14078f4f0's 0..2
  bool touching = false;            // he is touching the opponent
  bool secondMan = false;           // FUN_14078f4f0's third flag
  bool otherTackleCommitted = false;  // a team mate already tackling
  bool mayPlay = true;              // the action may be played at all
};

// The sub-kind and the clip it names, or nothing when no kind is ready (in
// which case PES declines the tackle's clip too and the caller keeps GF's own
// interfere clip).
struct TackleChoice {
  TackleSubKind kind = TackleSubKind::k7;
  int row = -1;
  int motionNo = -1;
  bool mirror = false;
};
TackleChoice ChooseTackle(const TackleConstants& k, const Animetable& t,
                          const TackleSituation& s);

}  // namespace pes
