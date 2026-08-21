// A cutscene's own clock: it stops when the match stops, and it can be skipped.
//
// Cutscenes were timed off EnvironmentManager::GetTime_ms() against an end stamp
// taken when the cutscene started. Two reported faults follow from that:
//
//   Pause did not pause them. Match::Process runs its simulation under `if (!pause)`,
//   but a wall clock does not care, so a paused cutscene played on to its end and the
//   match came back somewhere else entirely.
//
//   A replay fired while one was still running showed the cutscene rather than the
//   action, because the cutscene went on driving the camera off its own clock while
//   the replay was driving the scene.
//
// And there was no way to skip one, which PES has.
//
// So the elapsed time is accumulated from the match's own frame deltas instead, and
// skipping is just ending it now.

#ifndef _HPP_ONTHEPITCH_CUTSCENEPLAYBACK
#define _HPP_ONTHEPITCH_CUTSCENEPLAYBACK

namespace CutscenePlayback {

struct State {
  unsigned long length_ms = 0;
  unsigned long elapsed_ms = 0;
  bool playing = false;
};

// Begins a cutscene of `length_ms`, discarding whatever was playing. A zero length
// is a cutscene with nothing to show, and is over at once.
void Start(State& state, unsigned long length_ms);

// Adds a frame's worth of time, or none at all while the match is paused.
void Advance(State& state, unsigned long delta_ms, bool paused);

// Ends it now, wherever it had got to.
void Skip(State& state);

bool IsPlaying(const State& state);
bool IsDone(const State& state);

// Never past the length, so a caller can use it as a cursor into the cutscene.
unsigned long Elapsed_ms(const State& state);

// 0 at the start, 1 when finished - and 1 for anything not playing, so a caller
// with no cutscene reads as "nothing left to show".
float Progress(const State& state);

}  // namespace CutscenePlayback

#endif
