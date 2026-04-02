#ifndef _HPP_MENU_LEAGUE_SYSTEM
#define _HPP_MENU_LEAGUE_SYSTEM

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/windowmanager.hpp"

#include "../pagefactory.hpp"

using namespace blunted;

class LeagueSystemPage : public Gui2Page {
public:
  LeagueSystemPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueSystemPage();

protected:
  void GoPage(e_PageID pageID);
};

class LeagueSystemSavePage : public Gui2Page {
public:
  LeagueSystemSavePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueSystemSavePage();

protected:
};

class LeagueSystemSettingsPage : public Gui2Page {
public:
  LeagueSystemSettingsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueSystemSettingsPage();

protected:
};

#endif
