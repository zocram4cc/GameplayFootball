#ifndef _HPP_MENU_LEAGUE_STANDINGS
#define _HPP_MENU_LEAGUE_STANDINGS

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/windowmanager.hpp"

#include "../pagefactory.hpp"

using namespace blunted;

class LeagueStandingsPage : public Gui2Page {
public:
  LeagueStandingsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsPage();

protected:
  void GoPage(e_PageID pageID);
};

class LeagueStandingsLeaguePage : public Gui2Page {
public:
  LeagueStandingsLeaguePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsLeaguePage();

protected:
  void GoPage(e_PageID pageID);
};

class LeagueStandingsLeagueTablePage : public Gui2Page {
public:
  LeagueStandingsLeagueTablePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsLeagueTablePage();

protected:
};

class LeagueStandingsLeagueStatsPage : public Gui2Page {
public:
  LeagueStandingsLeagueStatsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsLeagueStatsPage();

protected:
};

class LeagueStandingsNCupPage : public Gui2Page {
public:
  LeagueStandingsNCupPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsNCupPage();

protected:
  void GoPage(e_PageID pageID);
};

class LeagueStandingsNCupTreePage : public Gui2Page {
public:
  LeagueStandingsNCupTreePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsNCupTreePage();

protected:
};

class LeagueStandingsNCupStatsPage : public Gui2Page {
public:
  LeagueStandingsNCupStatsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsNCupStatsPage();

protected:
};

class LeagueStandingsICup1Page : public Gui2Page {
public:
  LeagueStandingsICup1Page(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup1Page();

protected:
  void GoPage(e_PageID pageID);
};

class LeagueStandingsICup1GroupTablePage : public Gui2Page {
public:
  LeagueStandingsICup1GroupTablePage(Gui2WindowManager* windowManager,
                                     const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup1GroupTablePage();

protected:
};

class LeagueStandingsICup1TreePage : public Gui2Page {
public:
  LeagueStandingsICup1TreePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup1TreePage();

protected:
};

class LeagueStandingsICup1StatsPage : public Gui2Page {
public:
  LeagueStandingsICup1StatsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup1StatsPage();

protected:
};

class LeagueStandingsICup2Page : public Gui2Page {
public:
  LeagueStandingsICup2Page(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup2Page();

protected:
  void GoPage(e_PageID pageID);
};

class LeagueStandingsICup2GroupTablePage : public Gui2Page {
public:
  LeagueStandingsICup2GroupTablePage(Gui2WindowManager* windowManager,
                                     const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup2GroupTablePage();

protected:
};

class LeagueStandingsICup2TreePage : public Gui2Page {
public:
  LeagueStandingsICup2TreePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup2TreePage();

protected:
};

class LeagueStandingsICup2StatsPage : public Gui2Page {
public:
  LeagueStandingsICup2StatsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~LeagueStandingsICup2StatsPage();

protected:
};

#endif
