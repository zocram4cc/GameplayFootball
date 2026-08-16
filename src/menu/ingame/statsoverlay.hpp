// In-match statistics overlay (docs/PRESENTATION_SPEC.md section 3.4): a
// centred card with the two teams' crests and tags in a header, a two-column
// stat table under it, and a ball heatmap at the foot. Toggled by
// Match::ToggleStatsOverlay().
//
// Every row is home value / label / away value, laid out against three fixed
// columns rather than being packed into one string, so the numbers line up
// down the card instead of drifting with the label's length.
#ifndef _HPP_GUI2_VIEW_STATSOVERLAY
#define _HPP_GUI2_VIEW_STATSOVERLAY

#include <string>
#include <vector>

#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

using namespace blunted;
class Match;

class Gui2StatsOverlay : public Gui2View {
public:
  Gui2StatsOverlay(Gui2WindowManager* windowManager, Match* match);
  virtual ~Gui2StatsOverlay() = default;

  void UpdateStats();
  virtual void Redraw() {}

  // Gui2Task resets the whole tree's z-priority every frame; the card's own
  // stacking has to be re-applied on top of that - see
  // Gui2View::SetRecursiveZPriority.
  virtual void SetRecursiveZPriority(int prio);

protected:
  struct StatRow {
    Gui2Caption* home = nullptr;
    Gui2Caption* label = nullptr;
    Gui2Caption* away = nullptr;
    Gui2Image* bar = nullptr;  // only the possession row has one
  };

  // Adds one label row at `y`, returning it so UpdateStats can fill it in.
  StatRow AddRow(const std::string& label, float y, bool withBar);
  // Right-aligns the home value and left-aligns the away one against the
  // label column, then centres the label itself.
  void SetRowValues(StatRow& row, const std::string& home, const std::string& away);
  void DrawPossessionBar(float homeFraction);
  void DrawHeatmap();
  void ApplyZOrder();

  Match* match;

  Gui2Image* panelBg = nullptr;
  Gui2Image* headerBg = nullptr;
  Gui2Image* crest[2] = {nullptr, nullptr};
  Gui2Caption* teamTag[2] = {nullptr, nullptr};
  Gui2Caption* title = nullptr;

  std::vector<StatRow> rows;
  Gui2Caption* heatmapLabel = nullptr;
  Gui2Image* heatmap = nullptr;

  // column geometry, in percent, relative to this view
  float labelLeft = 0.0f, labelWidth = 0.0f;
  float valueMargin = 0.0f;
  float rowTextHeight = 0.0f;
};

#endif
