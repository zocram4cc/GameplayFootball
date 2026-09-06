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
// The montage below is the floor and the default now: three shots run back to
// back whatever the clip does, because PES's celebration is as long as its
// SHOTS, not as long as the scorer's animation (a 2.7 s median clip used to
// mean a 6 s celebration and a thirty-second sequence).
constexpr unsigned long kMinCelebration_ms = 42000;  // = the three shots

// Kept as the plain default for callers with no clip to hand.
constexpr unsigned long kCelebration_ms = kMinCelebration_ms;

// The longest celebration the schedule will run: the montage plus room for a
// clip that outlasts the shot it plays under (the longest single celebration
// clip is 10.0 s, and one may chain an intro into a loop). Everything below
// depends on this, because the recorded buffer has to reach back past it.
constexpr unsigned long kLongestCelebration_ms = 52000;

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

// PES's goal sequence is a MONTAGE, not one held shot. Frame by frame off the
// reference (youtu.be/ns5C3zpD6Ig at 0:05 and 0:44): the goal goes in on the
// live camera, then a tracking shot follows the scorer with the score bug up,
// then a tight close-up carries his card, then a wide holds the teammates
// mobbing him - and only then does it dip to black for a multi-angle replay
// and hand back to the restart. Goal to kickoff runs 60-80 s.
//
// Ours ran one shot and one replay angle in about thirty seconds. These are
// the three shots; each is filmed by its own goal camtrack (Match picks a
// different one per beat), and the length is the SHOTS, never a timer held
// over a finished performance - the cast is released to the animation
// machinery when its clips run out (Match::UpdateCutsceneChoreo) and jogs
// back like PES's does, so a long window is motion rather than statues.
enum class Shot { Tracking, Tight, Group };
constexpr int kShotCount = 3;
constexpr unsigned long kTrackingShot_ms = 15000;
constexpr unsigned long kTightShot_ms = 12000;
constexpr unsigned long kGroupShot_ms = 15000;

// The replay PES cuts to shows the BUILD-UP and the finish, from two angles -
// and then hands back. Ours played the tape all the way to the present, so a
// goal replay replayed the celebration that had just been on screen. Each
// angle plays a window ending at the goal; the close one runs at half speed,
// so its wall time is twice the tape it covers.
constexpr unsigned long kReplayWideAngle_ms = 9000;
constexpr unsigned long kReplayCloseAngle_ms = 7000;
constexpr unsigned long kReplayPlayback_ms = kReplayWideAngle_ms + kReplayCloseAngle_ms;

// The whole celebration: the three shots back to back. A clip longer than the
// montage extends the shot it is playing under rather than being cut off.
unsigned long CelebrationLength_ms(unsigned long animLength_ms);

// Which shot is on air `celebration_ms` into a celebration of that length, and
// where the current shot began (so the camera can cut rather than drift).
Shot ShotAt(unsigned long celebration_ms, unsigned long celebrationLength_ms);
unsigned long ShotStartedAt_ms(unsigned long celebration_ms,
                               unsigned long celebrationLength_ms);

// Goal to kickoff, for the test that pins the reference's 60-80 s window.
unsigned long WholeSequence_ms(unsigned long animLength_ms);

// When the replay should fire for a goal scored at `goalTime_ms`.
// `cutsceneEnd_ms` is the end of a goal cutscene if one is playing, 0 if not;
// a cutscene running past the celebration wins, a shorter one does not cut it short.
// `celebrationLength_ms` is what CelebrationLength_ms returned for the clip on screen.
unsigned long ReplayFiresAt_ms(unsigned long goalTime_ms, unsigned long cutsceneEnd_ms = 0,
                               unsigned long celebrationLength_ms = kCelebration_ms);

// When the referee should prepare the kickoff, and when it should start - both
// behind the replay for the celebration that is actually on screen. The referee
// schedules these the tick after the goal, before the clip has been chosen, so
// Match pushes them back once it knows (match.cpp, UpdateIngameCamera).
unsigned long RestartPrepareAt_ms(unsigned long goalTime_ms,
                                  unsigned long celebrationLength_ms = kCelebration_ms);
unsigned long RestartKickOffAt_ms(unsigned long goalTime_ms,
                                  unsigned long celebrationLength_ms = kCelebration_ms);

// How far back the replay should start, given how long the celebration
// actually ran (the elapsed goal timer at the moment it fires).
unsigned long ReplayStartOffset_ms(unsigned long celebrationElapsed_ms);

// Do the constants above still describe goal, celebration, replay, kickoff -
// with the replay inside the recorded buffer?
bool ScheduleIsConsistent();

}  // namespace GoalSequence

#endif
