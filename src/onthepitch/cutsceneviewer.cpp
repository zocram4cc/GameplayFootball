#include "cutsceneviewer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace CutsceneViewer {

float TrackExtent::MaxRadius() const {
  const float corners[4][2] = {{minX, minY}, {minX, maxY}, {maxX, minY}, {maxX, maxY}};
  float worst = 0.0f;
  for (const auto& c : corners)
    worst = std::max(worst, std::sqrt(c[0] * c[0] + c[1] * c[1]));
  return worst;
}

Anchoring ClassifyAnchoring(const TrackExtent& extent) {
  if (extent.frames <= 0)
    return Anchoring::Unknown;
  return extent.MaxRadius() <= kIncidentLocalRadius ? Anchoring::IncidentLocal
                                                    : Anchoring::StadiumWorld;
}

const char* AnchoringName(Anchoring anchoring) {
  switch (anchoring) {
    case Anchoring::IncidentLocal: return "incident-local";
    case Anchoring::StadiumWorld: return "stadium-world";
    default: return "unknown";
  }
}

int PackIndexAt(const Settings& settings, float elapsedSeconds, int packCount) {
  if (packCount <= 0)
    return -1;
  const float per = settings.packSeconds > 0.1f ? settings.packSeconds : 0.1f;
  const int step = static_cast<int>(elapsedSeconds / per);
  return step % packCount;
}

bool IsRunning(const Settings& settings, float elapsedSeconds) {
  return elapsedSeconds < settings.seconds;
}

bool PackMatches(const Settings& settings, const std::string& packName) {
  return settings.pack.empty() || packName.find(settings.pack) != std::string::npos;
}

std::string DescribePool(const std::string& category, int cameraPacks, int choreographyPacks,
                         const TrackExtent& firstTrack) {
  char buffer[256];
  if (cameraPacks == 0) {
    // The offside case: staged but not filmed. Worth saying outright, because
    // an empty camera pool used to fall back to another category's shots.
    snprintf(buffer, sizeof buffer, "%-18s camera 0, choreography %d  (actor-only)",
             category.c_str(), choreographyPacks);
    return buffer;
  }
  snprintf(buffer, sizeof buffer,
           "%-18s camera %3d, choreography %3d  first: %d frames, %s, radius %.1f m%s",
           category.c_str(), cameraPacks, choreographyPacks, firstTrack.frames,
           AnchoringName(ClassifyAnchoring(firstTrack)), firstTrack.MaxRadius(),
           firstTrack.isStatic ? ", static" : "");
  return buffer;
}

}  // namespace CutsceneViewer
