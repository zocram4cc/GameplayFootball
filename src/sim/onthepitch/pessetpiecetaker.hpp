#pragma once

// PES 2021's set-piece taker timing: how long the AI holds a restart before the
// kicker takes it, docs/re/pes-re-setpiece-choreography.md 9 item 2.
//
// The set-piece AI (match::ai::bp::ThinkUnitSetPlay, think 0x142135c50) counts
// elapsed logic frames from the restart being armed (`MI+0x1198` minus the AI
// context's stored base frame) and releases the kicker once the count reaches a
// frame-rate multiple:
//
//   corner, long cross   3.5 x fps   189 frames at 54 Hz -> CORNERKICK (50)
//   free kick, long pass 3.0 x fps   162 frames at 54 Hz -> FREEKICK_LONGPASS (32)
//   penalty              1.0 x fps    54 frames at 54 Hz
//
// Measured live, counted from the set-play counter's first frame: corner 379 and
// 191 (the 189-frame window seen twice), free kick 164 and 164, penalty not
// observed, kick-off 28 exactly (three times).
//
// The kick-off is not governed by these numbers - its pass is released by the
// receiver's own ball-ownership check (KickoffReceiver, 0x140781fc0) - and
// neither are throw-ins or goal kicks, whose release is the taker's arrival and
// readiness. `waitTimerMin` in constant::BallplayerSetplay (+0x28) is NOT read on
// any of these paths: the gates are the fps multiples above, and the JSON value
// is recorded so that nobody ports it.
//
// Every value is a plain .rdata float in the install's PES2021.exe, read through
// pes::PeImage::ReadVa.

#include <optional>
#include <string>

#include "pes/exe.hpp"

namespace pes {

// The wait gates in seconds of logic time, and the short-corner window.
struct SetPieceTakerConstants {
  float cornerGate_s = 3.5f;    // 3.5, .rdata 0x14259bde8
  float freeKickGate_s = 3.0f;  // 3.0, .rdata 0x14258c264
  float penaltyGate_s = 1.0f;   // 1.0, .rdata 0x14256ae1c
  // The short corner may be played inside this window instead (ThinkUnitSetPlay
  // 0x142135feb): 2 x fps, or 7 x fps while a team-mate is already running the
  // corner (byte 0x14259bdf0), and once the partner check passes.
  float shortCornerWindow_s = 2.0f;
  float shortCornerRunningWindow_s = 7.0f;
};

std::optional<SetPieceTakerConstants> LoadSetPieceTakerConstants(const PesVfs& vfs,
                                                                const PeImage& exe,
                                                                std::string* error);

}  // namespace pes
