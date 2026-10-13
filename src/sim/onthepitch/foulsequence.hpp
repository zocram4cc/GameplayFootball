// PES 2021's restart schedule: how long a stoppage holds, and when the restart
// is prepared and taken. docs/re/pes-re-foul-choreography.md 9, whose 9.8 maps
// each PES timing onto the field that holds it.
//
// THE OLD DESIGN: a telling-off or card cutscene, then a close-up replay,
// then the restart - the replay fired at the cutscene's end and the restart
// was prepared 1.5 s after the replay. Those GF values (6 s/7.5 s cutscenes,
// 6 s replay lead-in, 1.5 s gap, 2 s prepare-to-take) were never PES's.
//
// THE PES DESIGN: a stop is a step program (report 1): the whistle holds,
// the hold the players act out in, a wipe that hides the cut to the restart
// setup, a teleport, a fade-in, and only then is the restart armed. The whole
// ordinary foul stop-to-armed bracket is 407 frames = 7.54 s, measured end to
// end live (9.6).
//
//   phase                   frames   seconds
//   whistle hold (duels 3)   43       0.80   -> pes::JudgeConstants::whistleHold_s
//   step 3 stop stamp        2        0.04   -> kStopStamp_ms
//   step 5 foul hold         272      5.04   -> sf.holdKind1_s
//   0x11 wipe + UI           5+11     0.30   -> sf.wipe_s + kUiFrames_ms
//   0x14/0x15 teleport       40       0.74   -> kTeleport_ms
//   0xe fade-in              59       1.09   -> sf.fadeIn_s
//   0x34 + 0x19              18       0.33   -> sf.restartTail_s
//   stop -> armed            407      7.54
//   armed -> the kick        164      3.04   -> the taker's own decision (9.4)
//
// This engine has no scenes, no camera, no UI windows and no teleport, so its
// bracket is the same table minus the presentation beats: the hold, the stamp,
// the wipe, the fade-in and the tail. The UI window collapses into a single
// figure, since the referee's kickoff/throw-in asks are answered in one go.
//
// Where the report says PES has no such timing at all (a replay between the
// whistle and the restart, and anything that holds a restart while the referee
// gestures) this engine does not carry one either. The goal-sequence replay
// machinery lives in goalsequence.* and is untouched; there never was any
// foul-replay machinery in the sim.

#ifndef _HPP_ONTHEPITCH_FOULSEQUENCE
#define _HPP_ONTHEPITCH_FOULSEQUENCE

#include "gametypes.hpp"
#include "pesstoppage.hpp"

namespace FoulSequence {

// Step 3's stamp: 2 frames of PES's 54 Hz (9.6).
constexpr unsigned long kStopStamp_ms = 40;
// The UI window that follows the wipe (0x1d/0x1f/0x10, 9.4): 11 frames. PES
// makes it UI-dependent and notes a substitution UI can extend it; the 11
// frames are what the live captures measure without one.
constexpr unsigned long kUiFrames_ms = 200;
// The teleport pair (0x14 + 0x15, 9.4): 20 + 20 frames, static.
constexpr unsigned long kTeleport_ms = 740;

// When the players are released from the hold, before the wipe hides the cut.
// The hold is table[1] for a foul, table[5] for an out-of-play stop, plus
// PES's +2.5 s while a player is down.
unsigned long HoldEndsAt_ms(unsigned long stopTime_ms, const pes::StoppageConstants& sf,
                           bool downed, int holdKind);

// When the restart may be prepared (the taker walks to his mark), and when it
// may be taken, from the moment play stopped.
unsigned long RestartPrepareAt_ms(unsigned long stopTime_ms, const pes::StoppageConstants& sf,
                                 bool downed, int holdKind);
unsigned long RestartTakeAt_ms(unsigned long stopTime_ms, const pes::StoppageConstants& sf,
                              bool downed, int holdKind);

// Do the constants still describe hold, wipe, teleport, fade-in, restart?
bool ScheduleIsConsistent();

}  // namespace FoulSequence

#endif
