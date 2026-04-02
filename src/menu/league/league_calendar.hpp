#ifndef _HPP_MENU_LEAGUE_CALENDAR
#define _HPP_MENU_LEAGUE_CALENDAR

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/widgets/pulldown.hpp"
#include "utils/gui2/windowmanager.hpp"

#include "../pagefactory.hpp"

using namespace blunted;

class LeagueCalendarPage : public Gui2Page {
public:
  LeagueCalendarPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueCalendarPage();

protected:
  void RefreshFixtures();
  void GoBack();

  Gui2Pulldown* leagueFilterPulldown;
  std::string m_selectedLeagueID;
};

#endif
