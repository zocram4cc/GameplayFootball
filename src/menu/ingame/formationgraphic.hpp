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

#include "formationgraphiclayout.hpp"
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

  // Gui2Task resets the whole tree's z-priority every frame, so the panel's
  // own stacking (plates behind content, numbers over icons) has to be
  // re-applied on top of that reset - see Gui2View::SetRecursiveZPriority.
  virtual void SetRecursiveZPriority(int prio);

protected:
  struct StarterWidgets {
    Gui2Image* icon = nullptr;
    Gui2BitmapText* number = nullptr;
    Gui2Caption* nickname = nullptr;
  };

  // Every image the panel will ever show is created once, up front, and only
  // ever re-pointed afterwards - see the note in Init() on why an image
  // cannot be created later, nor faded down and back up.
  void BuildImages();
  // Points the existing widgets at one team's XI and bench. Cheap enough to
  // call on a team switch (twice per entrance), not per frame.
  void FillForTeam(int teamID);
  void ClearTextViews();
  void ApplyZOrder();
  void ApplyAlpha(float alpha);

  Match* match;
  bool enabled;

  int builtForTeamID = -2;  // -2: nothing built yet (distinct from the -1 "no team" state)
  float currentAlpha = -1.0f;

  // static chrome (position fixed regardless of which team is showing)
  Gui2Image* panelBg = nullptr;
  Gui2Image* headerBg = nullptr;
  Gui2Image* crest[2] = {nullptr, nullptr};  // one per side; only one is shown
  Gui2Caption* teamTagCaption = nullptr;
  Gui2Caption* subsHeaderCaption = nullptr;

  Gui2Image* pitchLines = nullptr;  // tactical-shape lines/goal box/forward arc
  std::vector<StarterWidgets> starters;  // eleven, created once

  // captions, which unlike images can be created and destroyed at any time
  Gui2Caption* formationLabel = nullptr;
  Gui2Caption* formationShape = nullptr;
  std::vector<Gui2Caption*> subLines;

  bool imagesBuilt = false;
  int shownCrest = -1;

  // Panel/pitch/substitutes-column boxes, computed once against the screen's
  // aspect ratio - see FormationGraphicLayout::ComputePanelGeometry.
  FormationGraphicLayout::PanelGeometry geometry;
};

}  // namespace blunted

#endif
