#include "onthepitch/cutsceneplayback.hpp"

namespace CutscenePlayback {

void Start(State& state, unsigned long length_ms) {
  state.length_ms = length_ms;
  state.elapsed_ms = 0;
  state.playing = length_ms > 0;
}

void Advance(State& state, unsigned long delta_ms, bool paused) {
  if (!state.playing || paused)
    return;
  state.elapsed_ms += delta_ms;
  if (state.elapsed_ms >= state.length_ms) {
    state.elapsed_ms = state.length_ms;
    state.playing = false;
  }
}

void Skip(State& state) {
  state.elapsed_ms = state.length_ms;
  state.playing = false;
}

bool IsPlaying(const State& state) { return state.playing; }

bool IsDone(const State& state) { return !state.playing; }

unsigned long Elapsed_ms(const State& state) {
  return state.elapsed_ms < state.length_ms ? state.elapsed_ms : state.length_ms;
}

float Progress(const State& state) {
  if (!state.playing || state.length_ms == 0)
    return 1.0f;
  return static_cast<float>(state.elapsed_ms) / static_cast<float>(state.length_ms);
}

}  // namespace CutscenePlayback
