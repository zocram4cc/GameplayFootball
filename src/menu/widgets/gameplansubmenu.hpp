// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MENU_GAMEPLAN_SUBMENU
#define _HPP_MENU_GAMEPLAN_SUBMENU

#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/widgets/menu.hpp"
#include "utils/gui2/widgets/slider.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

// Where the button column and every sub-menu sit in the game plan's grid: beside the
// pitch, not below it. Shared so the page and the sub-menus cannot disagree - they
// did, and a mismatched cell is a fatal in Gui2View::RemoveView.
constexpr int kGamePlanNavRow = 0;
constexpr int kGamePlanNavColumn = 1;

class GamePlanSubMenu : public Gui2View {
public:
  GamePlanSubMenu(Gui2WindowManager* windowManager, Gui2View* parentFocus, Gui2Grid* mainGrid,
                  const std::string& name);
  virtual ~GamePlanSubMenu();

  void OnClose();

  Gui2Button* AddButton(const std::string& buttonName, const std::string& buttonCaption, int row,
                        int column, Vector3 color);
  Gui2Slider* AddSlider(const std::string& sliderName, const std::string& sliderCaption, int row,
                        int column);

  // returns first toggled button in grid
  Gui2Button* GetToggledButton(Gui2Button* except);

  Gui2Grid* GetGrid() { return grid; }

  const std::vector<Gui2Button*>& GetAllButtons() { return allButtons; }

  void ProcessWindowingEvent(WindowingEvent* event);

protected:
  Gui2Grid* grid;
  Gui2Grid* mainGrid;
  // One close per submenu, however many escapes arrive in the same frame.
  bool closing = false;  // root menu's grid
  Gui2View* parentFocus;

  std::vector<Gui2Button*> allButtons;  // cache for GetToggledbutton()
};

#endif
