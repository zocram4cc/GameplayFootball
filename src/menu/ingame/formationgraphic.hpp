// Pre-match "TV-style" formation graphic (docs/PRESENTATION_SPEC.md section
// 1.1): a semi-transparent panel showing one team's starting XI in formation
// shape plus its substitutes list, shown briefly for each team during the
// match entrance, cross-fading out. Config-gated by
// "prematch_formation_graphic" (default on).
//
// Owned by Match exactly like Gui2ScoreBoard/Gui2StatsOverlay (constructed in
// Match::Match(), added to the same root, torn down in Match::Exit()). Unlike
// those, this widget drives itself: it overrides Process() (called every
// frame via the normal Gui2View child cascade) and reads Match's already-
// public entrance timing (IsInEntrance()/GetEntranceEndTime_ms()/
// GetActualTime_ms()) rather than being explicitly ticked, so no extra
// per-frame wiring in match.cpp is needed beyond construction/show/hide.

#ifndef _HPP_GUI2_VIEW_FORMATIONGRAPHIC
#define _HPP_GUI2_VIEW_FORMATIONGRAPHIC

#include <vector>

#include "scene/objects/image2d.hpp"
#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/bitmaptext.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

using namespace blunted;

class Match;
class TeamData;

namespace blunted {

class Gui2FormationGraphic : public Gui2View {
public:
  Gui2FormationGraphic(Gui2WindowManager* windowManager, const std::string& name, Match* match);
  virtual ~Gui2FormationGraphic();

  // Builds the static chrome (panel/header images, team-tag/subs-header
  // captions). Call once, right after this view has been added to its final
  // parent (root->AddView(...)).
  void Init();

  virtual void Process();

protected:
  struct StarterWidgets {
    Gui2Image* icon = nullptr;
    Gui2BitmapText* number = nullptr;
    Gui2Caption* nickname = nullptr;
  };

  // (Re)builds the whole content for one team's XI + substitutes. Cheap
  // enough to call once per team switch (twice per entrance), not per frame.
  void BuildForTeam(int teamID);
  void ClearDynamicViews();
  void ApplyAlpha(float alpha);

  Match* match;
  bool enabled;

  // Entrance timing: captured lazily the first time the entrance is seen, so
  // no extra Match accessor is needed for "when did the entrance start".
  unsigned long entranceStart_ms = 0;
  unsigned long entranceDuration_ms = 0;

  int builtForTeamID = -2;  // -2: nothing built yet (distinct from the -1 "no team" state)
  float currentAlpha = -1.0f;

  // static chrome (position fixed regardless of which team is showing)
  Gui2Image* panelBg = nullptr;
  Gui2Image* headerBg = nullptr;
  Gui2Caption* teamTagCaption = nullptr;
  Gui2Caption* subsHeaderCaption = nullptr;

  // per-team dynamic content, rebuilt on team switch
  Gui2Image* pitchLines = nullptr;  // tactical-shape lines/goal box/forward arc
  std::vector<StarterWidgets> starters;
  std::vector<Gui2Caption*> subLines;

  // layout, computed once from the widget's own size
  float headerHeight = 0.0f;
  float subsColumnWidth = 0.0f;
  float bodyX = 0.0f, bodyY = 0.0f, bodyWidth = 0.0f, bodyHeight = 0.0f;
};

}  // namespace blunted

#endif
