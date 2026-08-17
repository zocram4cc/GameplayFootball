#include "cutscenereport.hpp"

#include <algorithm>
#include <cstdio>

namespace CutsceneViewer {

TrackExtent MeasureTrack(const blunted::CamTrack& track) {
  TrackExtent extent;
  extent.frames = track.GetFrameCount();
  if (extent.frames <= 0)
    return extent;

  bool first = true;
  // Sampling by frame index rather than reading the vector keeps this to the
  // public interface; Sample clamps outside the range, so every index is valid.
  for (int i = 0; i < extent.frames; ++i) {
    const blunted::CamTrackFrame frame = track.Sample(static_cast<float>(i));
    const float x = frame.position[0], y = frame.position[1], z = frame.position[2];
    if (first) {
      extent.minX = extent.maxX = x;
      extent.minY = extent.maxY = y;
      extent.minZ = extent.maxZ = z;
      first = false;
      continue;
    }
    extent.minX = std::min(extent.minX, x);
    extent.maxX = std::max(extent.maxX, x);
    extent.minY = std::min(extent.minY, y);
    extent.maxY = std::max(extent.maxY, y);
    extent.minZ = std::min(extent.minZ, z);
    extent.maxZ = std::max(extent.maxZ, z);
  }
  // "Static" to within a centimetre: PES ships plenty of fixed shots, and a
  // fixed shot authored about the origin is the clearest sign of an
  // incident-local pack.
  extent.isStatic = extent.SpanX() < 0.01f && extent.SpanY() < 0.01f &&
                    (extent.maxZ - extent.minZ) < 0.01f;
  return extent;
}

TrackExtent MeasureChoreography(const blunted::EntranceChoreo& choreography) {
  TrackExtent extent;
  bool first = true;
  for (const auto& slot : choreography.GetSlots()) {
    for (const auto& key : slot.keys) {
      extent.frames++;
      if (first) {
        extent.minX = extent.maxX = key.x;
        extent.minY = extent.maxY = key.y;
        first = false;
        continue;
      }
      extent.minX = std::min(extent.minX, key.x);
      extent.maxX = std::max(extent.maxX, key.x);
      extent.minY = std::min(extent.minY, key.y);
      extent.maxY = std::max(extent.maxY, key.y);
    }
  }
  extent.isStatic = extent.SpanX() < 0.01f && extent.SpanY() < 0.01f;
  return extent;
}

std::vector<std::string> Report(
    const std::map<std::string, std::vector<blunted::CamTrack>>& cameraPools,
    const std::map<std::string, std::vector<blunted::EntranceChoreo>>& choreographyPools) {
  std::vector<std::string> lines;

  // Every pool that either side knows about, so an actor-only category is
  // reported rather than silently missing.
  std::vector<std::string> categories;
  for (const auto& entry : cameraPools) categories.push_back(entry.first);
  for (const auto& entry : choreographyPools)
    if (cameraPools.find(entry.first) == cameraPools.end()) categories.push_back(entry.first);
  std::sort(categories.begin(), categories.end());

  for (const std::string& category : categories) {
    const auto cameras = cameraPools.find(category);
    const auto choreographies = choreographyPools.find(category);
    const int cameraCount = cameras == cameraPools.end() ? 0 : (int)cameras->second.size();
    const int choreographyCount =
        choreographies == choreographyPools.end() ? 0 : (int)choreographies->second.size();
    TrackExtent firstTrack;
    if (cameraCount > 0)
      firstTrack = MeasureTrack(cameras->second.front());
    lines.push_back(DescribePool(category, cameraCount, choreographyCount, firstTrack));
    if (choreographyCount > 0) {
      const TrackExtent staging = MeasureChoreography(choreographies->second.front());
      char buffer[160];
      snprintf(buffer, sizeof buffer, "%-18s   staging: %s, radius %.1f m",
               category.c_str(), AnchoringName(ClassifyAnchoring(staging)), staging.MaxRadius());
      lines.push_back(buffer);
    }
  }
  return lines;
}

}  // namespace CutsceneViewer
