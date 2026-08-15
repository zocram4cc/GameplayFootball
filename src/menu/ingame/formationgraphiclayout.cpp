#include "formationgraphiclayout.hpp"

#include <algorithm>
#include <cmath>

using blunted::Vector3;
using blunted::clamp;

namespace FormationGraphicLayout {

namespace {

// Panel body-area margins (percent) so icons never touch the panel edges.
constexpr float kMinPercent = 8.0f;
constexpr float kMaxPercent = 92.0f;
constexpr float kSpan = (kMaxPercent - kMinPercent) * 0.5f;  // 42
constexpr float kMid = (kMaxPercent + kMinPercent) * 0.5f;   // 50

// Entrance schedule timing (see ComputeDisplayState).
constexpr unsigned long kHold_ms = 5000;
constexpr unsigned long kFade_ms = 600;
constexpr unsigned long kGap_ms = 400;
constexpr unsigned long kTailClear_ms = 2500;
constexpr unsigned long kMinLeadIn_ms = 2000;

}  // namespace

PanelPoint MapPosition(const Vector3& databasePosition) {
  // x: -1 (own goal) .. +1 (forward) maps to y: bottom (100) .. top (0).
  // y: -1 (RB flank) .. +1 (LB flank) maps to x: left .. right (arbitrary but
  // consistent choice; only relative spread matters for the schematic).
  const float x = clamp(databasePosition.coords[0], -1.0f, 1.0f);
  const float y = clamp(databasePosition.coords[1], -1.0f, 1.0f);

  PanelPoint p;
  p.yPercent = kMid - x * kSpan;
  p.xPercent = kMid + y * kSpan;
  return p;
}

RoleZone ZoneForRole(e_PlayerRole role) {
  if (role == e_PlayerRole_GK) return RoleZone::Goalkeeper;
  if (role == e_PlayerRole_CF) return RoleZone::Forward;
  return RoleZone::Outfield;
}

int SquadNumberForSlot(int formationSlotIndex) { return formationSlotIndex + 1; }

std::vector<Connection> BuildConnections(const std::vector<Vector3>& databasePositions) {
  std::vector<PanelPoint> points;
  points.reserve(databasePositions.size());
  for (const Vector3& pos : databasePositions) points.push_back(MapPosition(pos));

  std::vector<Connection> connections;
  const int n = static_cast<int>(points.size());
  for (int i = 0; i < n; i++) {
    // Connect to the nearest teammate in the *next* line forward, not just
    // the laterally-nearest player anywhere further forward (which could
    // skip straight from the back line to an aligned striker). Primary key:
    // smallest forward (depth) gap, i.e. the immediately next line; ties on
    // that broken by lateral distance, then by index for determinism.
    int best = -1;
    float bestDy = 0.0f;
    float bestDx = 0.0f;
    for (int j = 0; j < n; j++) {
      if (j == i) continue;
      // "More advanced" == strictly smaller yPercent (closer to the forward
      // arc at the top of the panel).
      const float dy = points[i].yPercent - points[j].yPercent;
      if (dy <= 0.0f) continue;
      const float dx = std::fabs(points[j].xPercent - points[i].xPercent);
      if (best == -1 || dy < bestDy || (dy == bestDy && dx < bestDx) ||
          (dy == bestDy && dx == bestDx && j < best)) {
        best = j;
        bestDy = dy;
        bestDx = dx;
      }
    }
    if (best != -1) connections.push_back({i, best});
  }
  return connections;
}

DisplayState ComputeDisplayState(unsigned long elapsed_ms, unsigned long entranceDuration_ms) {
  const unsigned long perTeam = kHold_ms;  // fade in/out happen inside this window
  const unsigned long needed = kMinLeadIn_ms + perTeam + kGap_ms + perTeam + kTailClear_ms;
  if (entranceDuration_ms < needed || elapsed_ms >= entranceDuration_ms)
    return DisplayState{-1, 0.0f};

  // Anchor both windows against the end of the entrance, leaving
  // kTailClear_ms free for the final live-pitch/referee shots.
  const unsigned long team1End = entranceDuration_ms - kTailClear_ms;
  const unsigned long team1Start = team1End - perTeam;
  const unsigned long team0End = team1Start - kGap_ms;
  const unsigned long team0Start = team0End - perTeam;

  auto windowAlpha = [&](unsigned long start, unsigned long end) -> float {
    if (elapsed_ms < start || elapsed_ms > end) return -1.0f;  // outside
    const unsigned long sinceStart = elapsed_ms - start;
    const unsigned long untilEnd = end - elapsed_ms;
    float a = 1.0f;
    if (sinceStart < kFade_ms) a = std::min(a, sinceStart / (float)kFade_ms);
    if (untilEnd < kFade_ms) a = std::min(a, untilEnd / (float)kFade_ms);
    return clamp(a, 0.0f, 1.0f);
  };

  float a = windowAlpha(team0Start, team0End);
  if (a >= 0.0f) return DisplayState{0, a};
  a = windowAlpha(team1Start, team1End);
  if (a >= 0.0f) return DisplayState{1, a};
  return DisplayState{-1, 0.0f};
}

}  // namespace FormationGraphicLayout
