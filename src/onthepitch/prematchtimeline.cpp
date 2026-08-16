#include "prematchtimeline.hpp"

#include <algorithm>
#include <istream>
#include <sstream>

namespace PrematchTimeline {

namespace {

Camera ParseCamera(const std::string& value) {
  if (value == "orbit") return Camera::Orbit;
  if (value == "aerial") return Camera::Aerial;
  if (value == "hold") return Camera::Hold;
  if (value == "entrance") return Camera::Entrance;
  if (value == "walkout") return Camera::Walkout;
  if (value == "lineup") return Camera::Lineup;
  return Camera::Entrance;  // unknown: the imported camerawork is the safe default
}

Overlay ParseOverlay(const std::string& value) {
  if (value == "formation_home") return Overlay::FormationHome;
  if (value == "formation_away") return Overlay::FormationAway;
  return Overlay::None;
}

Beat MakeBeat(const std::string& name, float seconds, Camera camera, Overlay overlay) {
  Beat beat;
  beat.name = name;
  beat.seconds = seconds;
  beat.camera = camera;
  beat.overlay = overlay;
  return beat;
}

}  // namespace

float Timeline::TotalSeconds() const {
  float total = 0.0f;
  for (const Beat& beat : beats) total += beat.seconds;
  return total;
}

bool Parse(std::istream& in, Timeline& timeline) {
  timeline.beats.clear();

  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream fields(line);
    std::string keyword;
    if (!(fields >> keyword)) continue;  // blank
    if (keyword.empty() || keyword[0] == '#') continue;
    if (keyword != "beat") continue;  // not a beat line: ignore, do not fail

    Beat beat;
    if (!(fields >> beat.name)) continue;
    if (!(fields >> beat.seconds)) continue;
    // A beat that holds for no time is not a beat, and a negative one would
    // run the sequence backwards.
    if (!(beat.seconds > 0.0f)) continue;

    std::string attribute;
    while (fields >> attribute) {
      const size_t equals = attribute.find('=');
      if (equals == std::string::npos) continue;
      const std::string key = attribute.substr(0, equals);
      const std::string value = attribute.substr(equals + 1);
      // Anything this build does not recognise is left at its default rather
      // than rejected, so a competition file can carry newer keys.
      if (key == "camera")
        beat.camera = ParseCamera(value);
      else if (key == "overlay")
        beat.overlay = ParseOverlay(value);
    }

    timeline.beats.push_back(beat);
  }

  return !timeline.beats.empty();
}

Timeline Default() {
  // The running order of a PES pre-match: the choreographed walkout first,
  // following the players out; the anthems on the line; then the wide shots
  // that the lineup graphics sit over; the team pictures; and away to
  // kickoff. Durations are sized to the imported choreography, whose walkout
  // cycle runs about sixteen seconds.
  Timeline timeline;
  timeline.beats = {
      MakeBeat("walkout", 16.0f, Camera::Walkout, Overlay::None),
      MakeBeat("anthems", 14.0f, Camera::Lineup, Overlay::None),
      MakeBeat("wide_home", 9.0f, Camera::Aerial, Overlay::FormationHome),
      MakeBeat("wide", 4.0f, Camera::Aerial, Overlay::None),
      MakeBeat("wide_away", 9.0f, Camera::Aerial, Overlay::FormationAway),
      MakeBeat("team_picture_home", 7.0f, Camera::Lineup, Overlay::None),
      MakeBeat("team_picture_away", 7.0f, Camera::Lineup, Overlay::None),
      MakeBeat("to_kickoff", 6.0f, Camera::Entrance, Overlay::None),
  };
  return timeline;
}

Timeline Rescale(const Timeline& timeline, float seconds) {
  const float total = timeline.TotalSeconds();
  if (seconds <= 0.0f || total <= 0.0f) return timeline;

  Timeline scaled = timeline;
  const float factor = seconds / total;
  for (Beat& beat : scaled.beats) beat.seconds *= factor;
  return scaled;
}

float EntranceProgress(const Timeline& timeline, const State& state) {
  float totalEntrance = 0.0f;
  for (const Beat& beat : timeline.beats)
    if (beat.camera == Camera::Entrance) totalEntrance += beat.seconds;
  if (totalEntrance <= 0.0f) return 0.0f;

  // Past the end of the sequence the camerawork has played out in full.
  if (state.finished || state.beatIndex < 0) return state.finished ? 1.0f : 0.0f;

  float consumed = 0.0f;
  for (int i = 0; i < state.beatIndex && i < (int)timeline.beats.size(); i++)
    if (timeline.beats[i].camera == Camera::Entrance) consumed += timeline.beats[i].seconds;

  const Beat& current = timeline.beats[state.beatIndex];
  if (current.camera == Camera::Entrance) consumed += state.beatT * current.seconds;

  const float progress = consumed / totalEntrance;
  return progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
}

State At(const Timeline& timeline, float elapsedSeconds) {
  State state;
  if (timeline.beats.empty()) {
    state.finished = true;
    return state;
  }

  float cursor = 0.0f;
  for (size_t i = 0; i < timeline.beats.size(); i++) {
    const Beat& beat = timeline.beats[i];
    const float end = cursor + beat.seconds;
    if (elapsedSeconds < end) {
      state.beatIndex = (int)i;
      const float into = std::max(0.0f, elapsedSeconds - cursor);
      state.beatT = beat.seconds > 0.0f ? std::min(1.0f, into / beat.seconds) : 0.0f;
      state.camera = beat.camera;
      state.overlay = beat.overlay;

      if (beat.overlay != Overlay::None) {
        // Cross-fade at both ends, inside the beat's own window. A beat too
        // short for two full fades still peaks part-way up rather than
        // flashing at nothing.
        const float remaining = std::max(0.0f, end - elapsedSeconds);
        const float fade = std::min(kOverlayFadeSeconds, beat.seconds * 0.5f);
        float alpha = 1.0f;
        if (fade > 0.0f) {
          alpha = std::min(alpha, into / fade);
          alpha = std::min(alpha, remaining / fade);
        }
        state.overlayAlpha = std::max(0.0f, std::min(1.0f, alpha));
      }
      return state;
    }
    cursor = end;
  }

  state.finished = true;
  return state;
}

}  // namespace PrematchTimeline
