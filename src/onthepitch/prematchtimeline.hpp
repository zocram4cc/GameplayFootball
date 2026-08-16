// The pre-match presentation timeline (docs/PRESENTATION_SPEC.md section 1).
//
// PES does not simply cut its entrance camerawork together and start the
// match: it stages a sequence of authored beats - stadium establishing
// shots, each team's walkout, a lineup graphic per side, a wide pitch hold,
// player and referee close-ups - and only kicks off once that sequence has
// run. Which beats, how long each holds, and which overlay rides on top of
// it differ per competition, which is why this is data rather than code.
//
// A timeline lives in a plain text file (simple, editable formats, like the
// rest of the pack):
//
//     # media/presentation/default.timeline
//     beat stadium 4.5 camera=orbit
//     beat walkout_home 22 camera=entrance
//     beat lineup_home 8.5 camera=hold overlay=formation_home
//
// The file is picked per competition - "presentation_timeline" names one
// explicitly, otherwise <presentation_dir>/<entrance_id>.timeline is tried
// and then default.timeline - so a competition ships its own pacing and its
// own overlays alongside its own entrance camerawork family. With nothing
// installed, Default() is used, so a bare checkout still gets the full
// sequence.
//
// Everything here is pure: no Gui2, no Match, no file system. Match owns the
// loading and the camera work; Gui2FormationGraphic asks it what overlay is
// on air. Unit-tested in tests/menu/prematch_timeline_test.cpp.

#ifndef _HPP_ONTHEPITCH_PREMATCHTIMELINE
#define _HPP_ONTHEPITCH_PREMATCHTIMELINE

#include <iosfwd>
#include <string>
#include <vector>

namespace PrematchTimeline {

// Where the camera is during a beat.
enum class Camera {
  Entrance,  // the imported PES entrance camerawork, advancing across beats
  Orbit,     // the authored slow orbit of the stands
  Aerial,    // the live-play aerial camera, held still
  Hold,      // stay wherever the previous beat left the camera
  // Both of these frame the choreographed cast rather than the stadium, so
  // they work in any venue - the imported camerawork is authored per stadium
  // and simply films the wrong place in one it was not made for.
  Walkout,   // low broadcast angle, panning along the line of players
  Lineup,    // static, head-on, holding the whole line in frame
};

// What is drawn over it.
enum class Overlay {
  None,
  FormationHome,
  FormationAway,
};

struct Beat {
  std::string name;
  float seconds = 0.0f;
  Camera camera = Camera::Entrance;
  Overlay overlay = Overlay::None;
  // Which piece of authored PES camerawork this beat is filmed with, as a
  // token matched against the camtrack file names under the entrance root.
  //
  // PES does not ship one entrance camera per competition: each ent_<id>
  // family is a different SHOT, and the file names say which - passage01 is
  // the tunnel, anth the anthems, circle_home and center the team picture,
  // aerial the wide. A beat names the shot it wants and gets the authored
  // camerawork for it; empty means run the family's shots in order, as
  // before.
  std::string shot;
};

struct Timeline {
  std::vector<Beat> beats;
  float TotalSeconds() const;
};

// Parses the text format above. Blank lines and '#' comments are skipped, as
// are lines that are not a well-formed `beat`, so a competition file can
// carry keys a future version understands without breaking this one.
// Returns false when no beat at all could be read.
bool Parse(std::istream& in, Timeline& timeline);

// The built-in sequence, used when no file is installed: the shot list from
// PRESENTATION_SPEC.md section 1, at the durations observed there.
Timeline Default();

// Stretches or squeezes every beat proportionally so the whole timeline runs
// for `seconds` ("intro_cutscene_seconds" overrides the file's own pacing).
// A non-positive target, or an empty timeline, is returned unchanged.
Timeline Rescale(const Timeline& timeline, float seconds);

struct State {
  int beatIndex = -1;
  float beatT = 0.0f;  // 0..1 through the current beat
  Camera camera = Camera::Aerial;
  Overlay overlay = Overlay::None;
  float overlayAlpha = 0.0f;  // cross-fade at each end of an overlay's beat
  bool finished = false;      // past the end: kick off
};

// Where the sequence is `elapsedSeconds` in.
State At(const Timeline& timeline, float elapsedSeconds);

// How far through the imported entrance camerawork the sequence should be,
// counting only the beats that asked for Camera::Entrance. Beats filmed some
// other way (a stadium orbit, a lineup graphic over the aerial) do not
// consume camera track they are not showing. 0 when no entrance beat has run
// yet, or when the timeline has none at all.
float EntranceProgress(const Timeline& timeline, const State& state);

// How long an overlay takes to fade in and out inside its own beat.
constexpr float kOverlayFadeSeconds = 0.6f;

}  // namespace PrematchTimeline

#endif
