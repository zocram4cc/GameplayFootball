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
  // Either sign is enough. How far out a camera sits does not say whose space it is
  // in - PES's `result` shots are 99 m away and pointed at a group standing at the
  // middle, as incident-local as anything - and where it points does not say it
  // either, because a card shot sits five metres out and frames the referee rather
  // than the incident. Measured over the 703 imported tracks, the union is the rule
  // that fits, and it is strictly wider than the radius alone, so nothing that is
  // staged at the incident today stops being staged there.
  const bool local =
      extent.aimsAtOrigin || extent.MaxRadius() <= kIncidentLocalRadius;
  return local ? Anchoring::IncidentLocal : Anchoring::StadiumWorld;
}

bool AnchorsAtIncident(const std::string& category) {
  const std::string head = category.substr(0, category.find('/'));
  // Not "change": PES stages every substitution in stadium coordinates at the
  // halfway line on the bench side - measured over all 30 change stagings, the
  // man coming on starts at (0, -36.5) and the cameras sit behind that touchline
  // ((0.5, -38.2) at 0.7 m) or out on the pitch looking at it ((-2.2, -9) at
  // 5.5 m). Moved to "the incident" they were added to the touchline mark, which
  // put a camera authored 4 m behind the line 38 m behind it, inside the stand.
  return head == "foul" || head == "offside" || head == "goal";
}

std::pair<float, float> SubstitutionMark(float pitchHalfY) { return {0.0f, -pitchHalfY}; }

AssistantMark OffsideAssistantMark(float incidentX, float incidentY, float linesman0Y,
                                   float linesman1Y, float pitchHalfX, float pitchHalfY) {
  AssistantMark mark;
  // Whichever man's own live y shares the incident's sign is on the near
  // touchline and gives it. If both happen to read the same sign (neither is
  // meant to ever cross the halfway line of touch), linesman 0 keeps it
  // rather than leaving the pick undefined.
  const bool linesman0IsNear = (linesman0Y >= 0.0f) == (incidentY >= 0.0f);
  mark.linesman = linesman0IsNear ? 0 : 1;
  const float hisY = mark.linesman == 0 ? linesman0Y : linesman1Y;
  mark.y = hisY >= 0.0f ? pitchHalfY : -pitchHalfY;
  mark.x = incidentX > pitchHalfX ? pitchHalfX
                                  : (incidentX < -pitchHalfX ? -pitchHalfX : incidentX);
  return mark;
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

bool RefereeFollowMayTakeCamera(bool cutscenePlaying) { return !cutscenePlaying; }

}  // namespace CutsceneViewer
