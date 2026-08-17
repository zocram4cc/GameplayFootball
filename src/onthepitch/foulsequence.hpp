// Timing for what happens between the referee's whistle and the restart.
//
// The sibling of goalsequence.hpp, and it exists for the same reason: the
// referee's restart calls Match::ResetSituation, so whatever has to happen in
// between - the telling-off or card cutscene, then the close-up replay - must be
// scheduled ahead of it. A foul used to give the restart two seconds while the
// warning cutscene alone runs three and a half, so the cutscene was already
// being cut short before a replay was ever added to it.
//
// Referee reads its restart delays from here and Match fires the replay off the
// same timings, so the two cannot drift apart.

#ifndef _HPP_ONTHEPITCH_FOULSEQUENCE
#define _HPP_ONTHEPITCH_FOULSEQUENCE

namespace FoulSequence {

// The cutscenes referee.cpp starts: a word with the offender, or a booking.
constexpr unsigned long kWarningCutscene_ms = 3500;
constexpr unsigned long kCardCutscene_ms = 5000;

// How far before the whistle the replay opens, so it shows the challenge and
// not the aftermath.
constexpr unsigned long kReplayLeadIn_ms = 6000;

// Gap between the replay firing and the restart being prepared. The match clock
// is frozen while a replay plays, so this is what remains once the viewer is
// done with it.
constexpr unsigned long kRestartPrepareAfterReplay_ms = 1500;

// Prepared set piece to the kick actually being taken.
constexpr unsigned long kTakeAfterPrepare_ms = 2000;

// referee.cpp's own numbering: 2 is a booking, 3 a sending off, anything else a
// foul the referee only has a word about.
unsigned long CutsceneLength_ms(int foulType);

// When the close-up replay should fire for a foul whistled at `foulTime_ms`.
unsigned long ReplayFiresAt_ms(unsigned long foulTime_ms, int foulType);

// When the referee should prepare the restart, and when it should be taken.
unsigned long RestartPrepareAt_ms(unsigned long foulTime_ms, int foulType);
unsigned long RestartTakeAt_ms(unsigned long foulTime_ms, int foulType);

// How far back the replay should reach, given how long the cutscene ran.
unsigned long ReplayStartOffset_ms(unsigned long cutsceneElapsed_ms);

// Do the constants still describe whistle, cutscene, replay, restart?
bool ScheduleIsConsistent();

}  // namespace FoulSequence

#endif
