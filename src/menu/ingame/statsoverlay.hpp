// In-match statistics overlay (docs/PRESENTATION_SPEC.md section 3.4): a
// centred card with the two teams' crests and tags in a header, a two-column
// stat table under it; the ball heatmaps live on the ball activity card. Toggled by
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
#include "data/matchanalytics.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/events.hpp"

using namespace blunted;
class Match;

class Gui2StatsOverlay : public Gui2View {
public:
  // Every widget's surface is fetched from the resource pool by name, so a
  // second card in the same window - the half-time page's - has to carry a
  // name of its own or it takes over the surfaces of the one TAB shows.
  // The pause menu pages through three bodies under the same header (LB/RB):
  // the stat table, each team's ball activity, and the match's events.
  enum class Body { Stats, BallActivity, Events };
  Gui2StatsOverlay(Gui2WindowManager* windowManager, Match* match,
                   const std::string& name = "statsoverlay", Body body = Body::Stats);
  virtual ~Gui2StatsOverlay() = default;

  void UpdateStats();
  virtual void Redraw() {}
  // The header reads "Match Stats" when the card is pulled up during play; the
  // half-time and full-time screens are the same card under their own title
  // (spec section 3.4: "identical template for both").
  void SetTitle(const std::string& text);

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
  // `portrait` draws the pitch short side across, long side down.
  void DrawHeatmap(Gui2Image* target, const MatchAnalytics::Heatmap& data, bool portrait = false);
  void BuildStatsBody(float y);
  void BuildBallActivityBody(float y);
  void BuildEventsBody(float y);
  void UpdateEvents();
  void ApplyZOrder();

  Match* match;

  Gui2Image* panelBg = nullptr;
  Gui2Image* headerBg = nullptr;
  Gui2Image* crest[2] = {nullptr, nullptr};
  Gui2Caption* teamTag[2] = {nullptr, nullptr};
  Gui2Caption* title = nullptr;

  std::vector<StatRow> rows;
  Body body;
  // Ball activity: one map per team with its tag and possession over it.
  Gui2Image* teamHeatmap[2] = {nullptr, nullptr};
  Gui2Caption* teamHeatmapLabel[2] = {nullptr, nullptr};
  // Events: rows reused top-down; home entries on the left, away on the right.
  Gui2Caption* eventsEmpty = nullptr;

  // column geometry, in percent, relative to this view
  float labelLeft = 0.0f, labelWidth = 0.0f;
  float valueMargin = 0.0f;
  float rowTextHeight = 0.0f;
};

// The three cards a break shows under one title, paged with LB/RB (Q/E):
// stats, ball activity, match events. Owned by the page that adds them.
class PagedStatsCards {
public:
  PagedStatsCards(Gui2WindowManager* windowManager, Gui2View* page, Match* match,
                  const std::string& name, const std::string& titlePrefix);
  void Show(int index);
  void Step(int delta) { Show(index + delta); }
  Gui2StatsOverlay* Current() const { return cards[index]; }
  // True when the key was one of the shoulders and the page turned.
  bool HandleKey(KeyboardEvent* event);

private:
  Gui2StatsOverlay* cards[3] = {nullptr, nullptr, nullptr};
  int index = 0;
};

#endif
