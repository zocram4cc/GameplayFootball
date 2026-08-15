// A rig test bench, kept out of the match's own code path.
//
// The viewer parks a chosen player in front of an orbiting camera and walks
// him through the animation collection, so imported bodies and clips can be
// judged in the engine that actually skins them: bad joint weights show up as
// bent or detached limbs, bad root motion as sliding feet.
//
// Config ("debug_model_viewer_seconds" > 0 turns it on):
//   debug_model_viewer_seconds       how long the bench runs
//   debug_model_viewer_player        model directory fragment, e.g. 2hug_k1851
//   debug_model_viewer_anim          clip name fragment, e.g. pes_
//   debug_model_viewer_clip_seconds  seconds per clip
//   debug_model_viewer_radius        orbit radius in metres
//
// The maths is free functions so it can be tested without an engine.

#ifndef _HPP_ONTHEPITCH_MODELVIEWER
#define _HPP_ONTHEPITCH_MODELVIEWER

#include <string>
#include <vector>

#include "base/math/vector3.hpp"

namespace blunted {

struct ModelViewerSettings {
  float seconds = 0.0f;
  std::string playerFilter;
  std::string animFilter;
  float clipSeconds = 4.0f;
  float radius = 3.4f;

  bool IsEnabled() const { return seconds > 0.0f; }
};

// Still within the bench's run time?
bool ModelViewerIsRunning(const ModelViewerSettings& settings, unsigned long time_ms);

// Camera position for the slow orbit around a subject at `centre`.
Vector3 ModelViewerCameraPosition(const ModelViewerSettings& settings,
                                  const Vector3& centre, unsigned long time_ms);

// Which clip of `playlistSize` is on the bench, and which frame of it.
// Returns -1 for an empty playlist.
int ModelViewerClipIndex(const ModelViewerSettings& settings, unsigned long time_ms,
                         int playlistSize);
int ModelViewerClipFrame(const ModelViewerSettings& settings, unsigned long time_ms,
                         int clipFrameCount);

// Does a clip name pass the filter? An empty filter passes everything.
bool ModelViewerAccepts(const std::string& filter, const std::string& name);

}  // namespace blunted

#endif
