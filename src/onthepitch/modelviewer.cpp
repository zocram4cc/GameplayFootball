#include "modelviewer.hpp"

#include <algorithm>
#include <cmath>

namespace blunted {

bool ModelViewerIsRunning(const ModelViewerSettings& settings, unsigned long time_ms) {
  if (!settings.IsEnabled()) return false;
  return time_ms < (unsigned long)(settings.seconds * 1000.0f);
}

Vector3 ModelViewerCameraPosition(const ModelViewerSettings& settings,
                                  const Vector3& centre, unsigned long time_ms) {
  const float angle = time_ms * 0.0004f;
  return centre + Vector3(std::sin(angle) * settings.radius,
                          -std::cos(angle) * settings.radius, 0.35f);
}

namespace {
// a clip is never allowed zero length, or the bench would divide by it
float ClipMillis(const ModelViewerSettings& settings) {
  return std::max(0.5f, settings.clipSeconds) * 1000.0f;
}
}  // namespace

int ModelViewerClipIndex(const ModelViewerSettings& settings, unsigned long time_ms,
                         int playlistSize) {
  if (playlistSize <= 0) return -1;
  return (int)(time_ms / (unsigned long)ClipMillis(settings)) % playlistSize;
}

int ModelViewerClipFrame(const ModelViewerSettings& settings, unsigned long time_ms,
                         int clipFrameCount) {
  if (clipFrameCount <= 0) return 0;
  const unsigned long into_ms = time_ms % (unsigned long)ClipMillis(settings);
  return (int)(into_ms / 10) % clipFrameCount;  // 10 ms animation frames
}

bool ModelViewerAccepts(const std::string& filter, const std::string& name) {
  return filter.empty() || name.find(filter) != std::string::npos;
}

}  // namespace blunted
