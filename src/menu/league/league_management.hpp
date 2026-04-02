#ifndef _HPP_MENU_LEAGUE_MANAGEMENT
#define _HPP_MENU_LEAGUE_MANAGEMENT

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/windowmanager.hpp"

#include "../pagefactory.hpp"

using namespace blunted;

class LeagueManagementPage : public Gui2Page {
public:
  LeagueManagementPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueManagementPage();

protected:
  void GoPage(e_PageID pageID);
};

class LeagueManagementContractsPage : public Gui2Page {
public:
  LeagueManagementContractsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueManagementContractsPage();

protected:
};

class LeagueManagementTransfersPage : public Gui2Page {
public:
  LeagueManagementTransfersPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueManagementTransfersPage();

protected:
};

#endif
