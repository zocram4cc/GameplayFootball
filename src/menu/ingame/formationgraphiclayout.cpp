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


// --- Panel proportions, as fractions of the panel itself ---
constexpr float kPanelHeightPercent = 86.0f;
constexpr float kHeaderFraction = 0.085f;
constexpr float kSideMarginFraction = 0.035f;
constexpr float kColumnGapFraction = 0.028f;
constexpr float kTopGapFraction = 0.014f;
constexpr float kBottomMarginFraction = 0.028f;

// --- Formation rows, in pitch-local percent ---
// A depth gap wider than this starts a new line. Formation entries sit in
// [-1, +1], so 0.18 keeps a back four together (its members differ by
// hundredths) while still separating it from the midfield.
constexpr float kLineDepthGap = 0.18f;
constexpr float kTopRowY = 12.0f;     // most advanced line
constexpr float kBottomRowY = 76.0f;  // deepest outfield line
constexpr float kKeeperY = 89.0f;     // alone, in the goal box
constexpr float kMaxHalfSpan = 41.0f;
// The lateral spread (in database units, out of a possible 2) at which a
// line is drawn flank to flank; anything tighter is drawn proportionally
// narrower, so a double pivot does not read as two wing-backs.
constexpr float kFullSpreadRange = 1.5f;

constexpr float kSubsHeaderHeight = 2.6f;
constexpr float kSubsHeaderGap = 1.0f;

}  // namespace

PanelGeometry ComputePanelGeometry(float screenAspectRatio) {
  PanelGeometry g;
  g.panelHeight = kPanelHeightPercent;
  g.panelY = (100.0f - g.panelHeight) * 0.5f;
  g.panelWidth = g.panelHeight * kPanelPixelAspect / screenAspectRatio;
  g.panelX = (100.0f - g.panelWidth) * 0.5f;

  g.headerHeight = g.panelHeight * kHeaderFraction;

  const float sideMargin = g.panelWidth * kSideMarginFraction;
  const float bodyY = g.headerHeight + g.panelHeight * kTopGapFraction;
  const float bodyHeight = g.panelHeight - bodyY - g.panelHeight * kBottomMarginFraction;

  // The pitch is sized first (its portrait shape is fixed) and pinned to the
  // panel's trailing edge; the substitutes column takes whatever is left,
  // which is what keeps the two from ever growing into each other.
  g.pitchHeight = bodyHeight;
  g.pitchWidth = g.pitchHeight * kPitchPixelAspect / screenAspectRatio;
  g.pitchY = bodyY;
  g.pitchX = g.panelWidth - sideMargin - g.pitchWidth;

  g.subsX = sideMargin;
  g.subsY = bodyY;
  g.subsWidth = g.pitchX - g.panelWidth * kColumnGapFraction - g.subsX;
  g.subsHeight = bodyHeight;
  return g;
}

std::vector<PanelPoint> ArrangeFormation(const std::vector<Vector3>& databasePositions,
                                         const std::vector<e_PlayerRole>& roles) {
  const int count = static_cast<int>(databasePositions.size());
  std::vector<PanelPoint> out(count, PanelPoint{50.0f, 50.0f});
  if (count == 0) return out;

  // The keeper is drawn alone in the goal box at the foot of the panel and
  // is kept out of the line clustering: his depth is an outlier that would
  // otherwise drag the back line down with it.
  int keeperIndex = -1;
  for (int i = 0; i < count && i < static_cast<int>(roles.size()); i++) {
    if (roles[i] == e_PlayerRole_GK) {
      keeperIndex = i;
      break;
    }
  }
  if (keeperIndex != -1) out[keeperIndex] = PanelPoint{50.0f, kKeeperY};

  std::vector<int> outfield;
  outfield.reserve(count);
  for (int i = 0; i < count; i++)
    if (i != keeperIndex) outfield.push_back(i);
  if (outfield.empty()) return out;

  // Most advanced first, so line 0 is the forward line.
  std::sort(outfield.begin(), outfield.end(), [&](int a, int b) {
    if (databasePositions[a].coords[0] != databasePositions[b].coords[0])
      return databasePositions[a].coords[0] > databasePositions[b].coords[0];
    return a < b;
  });

  std::vector<std::vector<int>> lines;
  float previousDepth = 0.0f;
  for (int index : outfield) {
    const float depth = databasePositions[index].coords[0];
    if (lines.empty() || previousDepth - depth > kLineDepthGap) lines.push_back({});
    lines.back().push_back(index);
    previousDepth = depth;
  }

  const int lineCount = static_cast<int>(lines.size());
  for (int line = 0; line < lineCount; line++) {
    const float y = lineCount == 1 ? (kTopRowY + kBottomRowY) * 0.5f
                                   : kTopRowY + line * (kBottomRowY - kTopRowY) / (lineCount - 1);

    // Left to right across the panel is database y ascending (see
    // MapPosition), so ordering the row this way preserves who plays where.
    std::vector<int>& row = lines[line];
    std::sort(row.begin(), row.end(), [&](int a, int b) {
      if (databasePositions[a].coords[1] != databasePositions[b].coords[1])
        return databasePositions[a].coords[1] < databasePositions[b].coords[1];
      return a < b;
    });

    const int rowSize = static_cast<int>(row.size());
    if (rowSize == 1) {
      out[row[0]] = PanelPoint{50.0f, y};
      continue;
    }

    const float range =
        databasePositions[row.back()].coords[1] - databasePositions[row.front()].coords[1];
    // Wide enough to keep the row's character, but never so tight that two
    // jerseys (or the nicknames under them) could touch.
    const float required =
        std::min(kMaxHalfSpan, (rowSize - 1) * kMinIconGapPercent * 0.5f);
    const float natural = kMaxHalfSpan * clamp(range / kFullSpreadRange, 0.0f, 1.0f);
    const float halfSpan = clamp(natural, required, kMaxHalfSpan);

    for (int k = 0; k < rowSize; k++) {
      const float t = k / static_cast<float>(rowSize - 1);
      out[row[k]] = PanelPoint{50.0f - halfSpan + t * 2.0f * halfSpan, y};
    }
  }

  return out;
}

float MinHorizontalGap(const std::vector<PanelPoint>& points) {
  float smallest = 100.0f;
  for (size_t i = 0; i < points.size(); i++) {
    for (size_t j = i + 1; j < points.size(); j++) {
      if (std::fabs(points[i].yPercent - points[j].yPercent) > 0.01f) continue;
      smallest = std::min(smallest, std::fabs(points[i].xPercent - points[j].xPercent));
    }
  }
  return smallest;
}

std::string FormationLabel(const std::vector<PanelPoint>& points,
                           const std::vector<e_PlayerRole>& roles) {
  // Rows, deepest first (largest yPercent is nearest the team's own goal).
  std::vector<std::pair<float, int>> rows;
  for (size_t i = 0; i < points.size(); i++) {
    if (i < roles.size() && roles[i] == e_PlayerRole_GK) continue;
    bool merged = false;
    for (auto& row : rows) {
      if (std::fabs(row.first - points[i].yPercent) < 0.01f) {
        row.second++;
        merged = true;
        break;
      }
    }
    if (!merged) rows.push_back({points[i].yPercent, 1});
  }
  std::sort(rows.begin(), rows.end(),
            [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
              return a.first > b.first;
            });

  std::string label;
  for (const auto& row : rows) {
    if (!label.empty()) label += "-";
    label += std::to_string(row.second);
  }
  return label;
}

SubsLayout ComputeSubsLayout(int subCount, float columnHeight) {
  SubsLayout layout;
  layout.headerHeight = kSubsHeaderHeight;
  layout.firstRowY = kSubsHeaderHeight + kSubsHeaderGap;
  layout.rowHeight = kMinSubsRowHeightPercent;

  const float available = columnHeight - layout.firstRowY;
  if (subCount <= 0 || available < kMinSubsRowHeightPercent) return layout;

  layout.rowHeight = clamp(available / subCount, kMinSubsRowHeightPercent,
                           kMaxSubsRowHeightPercent);
  // A bench too long even at the minimum row height loses its tail rather
  // than running off the bottom of the panel, which is what it used to do.
  layout.rowCount =
      std::min(subCount, static_cast<int>(std::floor(available / layout.rowHeight)));
  return layout;
}

float FitTextHeight(float naturalWidthPercent, float naturalHeightPercent, float maxWidthPercent,
                    float minHeightPercent) {
  if (naturalWidthPercent <= 0.0f || naturalWidthPercent <= maxWidthPercent)
    return naturalHeightPercent;
  // A caption's rendered width scales linearly with its height (caption.cpp
  // zooms one surface by h/renderedHeight), so this is an exact fit, not an
  // iteration.
  const float fitted = naturalHeightPercent * maxWidthPercent / naturalWidthPercent;
  return std::max(fitted, minHeightPercent);
}

std::string TruncateToFit(const std::string& text, float maxWidthPercent,
                          const std::function<float(int)>& widthOfFirst) {
  if (text.empty()) return text;
  const int length = static_cast<int>(text.size());
  if (widthOfFirst(length) <= maxWidthPercent) return text;

  // Longest prefix that still leaves room for the ellipsis. The ellipsis is
  // budgeted as one more character of the original text - exact for a
  // monospaced font, close enough for a proportional one, and it costs no
  // extra measurement of a string the caller cannot measure.
  int lo = 1, hi = length - 1, best = 0;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (widthOfFirst(mid + 1) <= maxWidthPercent) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }

  // Never cut a UTF-8 sequence in half.
  auto backOffToBoundary = [&](int at) {
    while (at > 1 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) at--;
    return at;
  };

  if (best <= 0) return text.substr(0, backOffToBoundary(1));
  return text.substr(0, backOffToBoundary(best)) + ".";
}

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
  return BuildConnections(points);
}

std::vector<Connection> BuildConnections(const std::vector<PanelPoint>& points) {
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

}  // namespace FormationGraphicLayout
