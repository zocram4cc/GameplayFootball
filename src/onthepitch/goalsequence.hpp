// Timing for what happens between a goal and the kickoff that follows it.
//
// The order is goal, celebration, replay, kickoff. Two different places have to
// agree for that to hold: Match fires the replay once the celebration has run,
// and Referee schedules the kickoff restart - which calls ResetSituation and
// clears the goal state, and with it the very timer the replay trigger is
// waiting on. When the celebration was lengthened without moving the restart,
// the goal state was cleared first and the replay stopped firing entirely.
//
// Both read their timings from here so they cannot drift apart again, and
// tests/onthepitch/goal_sequence_test.cpp asserts the order they imply.

#ifndef _HPP_ONTHEPITCH_GOALSEQUENCE
#define _HPP_ONTHEPITCH_GOALSEQUENCE

namespace GoalSequence {

// How long the celebration is allowed to run before the replay takes over.
// Nine seconds is where the scoring team's chant has finished fading.
constexpr unsigned long kCelebration_ms = 9000;

// How far before the goal the replay opens, so it shows the build-up rather
// than the celebration it just interrupted.
constexpr unsigned long kReplayLeadIn_ms = 7000;

// The recorded window the replay is cut from. It has to cover the celebration
// plus the lead-in, or a replay fired after the celebration can no longer
// reach the goal.
constexpr unsigned long kReplayBuffer_ms = 22000;

// Gap between the replay firing and the referee preparing the restart. The
// match clock is frozen while the replay plays (Match::Process runs its
// simulation under `if (!pause)`), so this is the delay that remains once the
// viewer is done with it.
constexpr unsigned long kRestartPrepareAfterReplay_ms = 1500;

// Prepared set piece to actual kickoff.
constexpr unsigned long kKickOffAfterPrepare_ms = 2000;

// When the replay should fire for a goal scored at `goalTime_ms`.
// `cutsceneEnd_ms` is the end of a goal cutscene if one is playing, 0 if not;
// a cutscene running past the plain celebration wins, a shorter one does not
// cut the celebration short.
unsigned long ReplayFiresAt_ms(unsigned long goalTime_ms, unsigned long cutsceneEnd_ms = 0);

// When the referee should prepare the kickoff, and when it should start.
unsigned long RestartPrepareAt_ms(unsigned long goalTime_ms);
unsigned long RestartKickOffAt_ms(unsigned long goalTime_ms);

// How far back the replay should start, given how long the celebration
// actually ran (the elapsed goal timer at the moment it fires).
unsigned long ReplayStartOffset_ms(unsigned long celebrationElapsed_ms);

// Do the constants above still describe goal, celebration, replay, kickoff -
// with the replay inside the recorded buffer?
bool ScheduleIsConsistent();

}  // namespace GoalSequence

#endif
