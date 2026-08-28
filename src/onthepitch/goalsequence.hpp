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

// How long a celebration runs when nothing else decides it. It used to be the whole
// answer - a flat nine seconds - which both cut long celebrations off partway and
// held short ones on a player running in place. The clips say how long they are:
// measured over the 387 imported celebration animations at 10 ms a frame, they run
// 0.4 s to 10.0 s, median 2.7 s and p90 6.8 s. So the length comes from the clip and
// this is only the floor for a very short one.
//
// The floor is set from the reference broadcast (docs/PRESENTATION_SPEC.md §3.1),
// where the celebration shot runs about five seconds and is followed by a two to
// three second spotlight on the scorer. Four seconds put the restart in motion
// while the celebration was still the thing on screen; with a median clip of 2.7 s
// the floor is what most goals actually get, so it has to cover the beat.
constexpr unsigned long kMinCelebration_ms = 6000;

// Kept as the plain default for callers with no clip to hand.
constexpr unsigned long kCelebration_ms = 9000;

// The longest celebration the schedule will run: the longest single clip is 10.0 s,
// and a celebration may chain an intro into a loop, so two of them. Everything below
// depends on this, because the recorded buffer has to reach back past it.
constexpr unsigned long kLongestCelebration_ms = 20000;

// How far before the goal the replay opens, so it shows the build-up rather
// than the celebration it just interrupted.
constexpr unsigned long kReplayLeadIn_ms = 10000;

// The recorded window the replay is cut from. It has to cover the longest
// celebration plus the lead-in, or a replay fired after the celebration can no
// longer reach the goal and plays back the celebration instead of the action.
constexpr unsigned long kReplayBuffer_ms = kLongestCelebration_ms + kReplayLeadIn_ms;

// Gap between the replay firing and the referee preparing the restart. The
// match clock is frozen while the replay plays (Match::Process runs its
// simulation under `if (!pause)`), so this is the delay that remains once the
// viewer is done with it.
constexpr unsigned long kRestartPrepareAfterReplay_ms = 1500;

// Prepared set piece to actual kickoff.
constexpr unsigned long kKickOffAfterPrepare_ms = 2000;

// How long a celebration of `animLength_ms` should be held on screen: the clip's own
// length, floored so a very short one is not cut to nothing and capped so a long one
// cannot outrun the recorded buffer. Pass 0 when the clip is unknown and it falls
// back to the plain default.
unsigned long CelebrationLength_ms(unsigned long animLength_ms);

// When the replay should fire for a goal scored at `goalTime_ms`.
// `cutsceneEnd_ms` is the end of a goal cutscene if one is playing, 0 if not;
// a cutscene running past the celebration wins, a shorter one does not cut it short.
// `celebrationLength_ms` is what CelebrationLength_ms returned for the clip on screen.
unsigned long ReplayFiresAt_ms(unsigned long goalTime_ms, unsigned long cutsceneEnd_ms = 0,
                               unsigned long celebrationLength_ms = kCelebration_ms);

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
