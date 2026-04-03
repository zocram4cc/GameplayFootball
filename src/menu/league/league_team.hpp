#ifndef _HPP_MENU_LEAGUE_TEAM
#define _HPP_MENU_LEAGUE_TEAM

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/windowmanager.hpp"

#include "../pagefactory.hpp"

using namespace blunted;

class LeagueTeamPage : public Gui2Page {
public:
  LeagueTeamPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueTeamPage();
  virtual void Process();

protected:
  void GoPage(e_PageID pageID);
  unsigned long pageCreatedTime_ms;
  bool autoAdvanceTriggered;
};

class LeagueTeamFormationPage : public Gui2Page {
public:
  LeagueTeamFormationPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueTeamFormationPage();

protected:
};

class LeagueTeamPlayerSelectionPage : public Gui2Page {
public:
  LeagueTeamPlayerSelectionPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueTeamPlayerSelectionPage();

protected:
};

class LeagueTeamTacticsPage : public Gui2Page {
public:
  LeagueTeamTacticsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueTeamTacticsPage();

protected:
};

class LeagueTeamPlayerOverviewPage : public Gui2Page {
public:
  LeagueTeamPlayerOverviewPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueTeamPlayerOverviewPage();
  virtual void Process();

protected:
  unsigned long pageCreatedTime_ms;
  bool autoAdvanceTriggered;
};

class LeagueTeamPlayerDevelopmentPage : public Gui2Page {
public:
  LeagueTeamPlayerDevelopmentPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueTeamPlayerDevelopmentPage();

protected:
};

class LeagueTeamSetupPage : public Gui2Page {
public:
  LeagueTeamSetupPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueTeamSetupPage();

protected:
};

#endif
