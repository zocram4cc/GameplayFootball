// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MENU_GAMEPLAN
#define _HPP_MENU_GAMEPLAN

#include <memory>

#include "../data/teamdata.hpp"
#include "../onthepitch/match.hpp"
#include "data/formations.hpp"
#include "utils/database.hpp"
#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/frame.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/widgets/root.hpp"
#include "utils/gui2/widgets/slider.hpp"
#include "utils/gui2/windowmanager.hpp"
#include "widgets/gameplansubmenu.hpp"
#include "widgets/planmap.hpp"

using namespace blunted;

struct TacticsSlider {
  int id;
  Gui2Slider* widget;
  std::string tacticName;
};

class GamePlanPage : public Gui2Page {
public:
  GamePlanPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~GamePlanPage();

  void OnClose();

  virtual void Process();

  virtual void Deactivate();
  virtual void Reactivate();

  Vector3 GetButtonColor(int id);

  void GoLineupMenu();
  void LineupMenuOnClick(Gui2Button* button);
  void SaveLineup();

  std::string GetFormationCaption() const;
  void ApplyFormationShape(const Formations::Shape& shape);
  void CustomFormationOnChange();
  void GoFormationMenu();
  void FormationMenuOnClick(int formationIndex);

  std::string GetPhilosophyCaption() const;
  void GoPhilosophyMenu();
  void PhilosophyMenuOnClick(int philosophyIndex);

  void GoSubstitutionsMenu();
  void SubstitutionsMenuOnClick(Gui2Button* button);

  void GoTacticsMenu();
  void TacticsMenuOnChange(Gui2Slider* slider, int id);
  void SaveTactics();

protected:
  int teamID;
  unsigned long pageCreatedTime_ms = 0;
  bool uiShotTaken = false;

  Gui2PlanMap* map;
  Gui2Grid* grid;
  Gui2Grid* gridNav;
  Gui2Button* buttonLineup;
  Gui2Button* buttonTactics;
  Gui2Button* buttonFormation;
  Gui2Button* buttonPhilosophy;
  Gui2Button* buttonSubstitutions;

  TeamData* teamData;
  // Owned only when the page is opened before a match exists.
  std::unique_ptr<TeamData> standaloneTeamData;

  GamePlanSubMenu* lineupMenu;
  GamePlanSubMenu* tacticsMenu;
  GamePlanSubMenu* philosophyMenu;
  GamePlanSubMenu* formationMenu;
  Gui2Slider* sliderCustomDefenders = nullptr;
  Gui2Slider* sliderCustomMidfielders = nullptr;
  GamePlanSubMenu* substitutionsMenu;

  std::vector<TacticsSlider> tacticsSliders;

  std::unique_ptr<Database> namedb;
};

#endif
