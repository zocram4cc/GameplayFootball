#include "league_forward.hpp"

#include "../../league/leaguecode.hpp"
#include "../../main.hpp"
#include "menu_smoke.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueForwardPage::LeagueForwardPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  auto result = GetDB()->Query(
      "SELECT managername, timestamp FROM settings LIMIT 1");
  std::string mgrName = "Manager";
  std::string dateStr = "";
  if (!result->data.empty()) {
    if (result->data.at(0).size() > 0) mgrName = result->data.at(0).at(0);
    if (result->data.at(0).size() > 1) dateStr = result->data.at(0).at(1);
  }

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_forward", 20, 5, 60, 3, "League Dashboard");
  this->AddView(title);
  title->Show();

  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_forward_info", 20, 9, 60, 2,
                      "Manager: " + mgrName + " | Date: " + dateStr);
  this->AddView(info);
  info->Show();

  Gui2Button* btnTeam = new Gui2Button(windowManager, "btn_forward_team", 0, 0, 60, 3, "Team Management");
  Gui2Button* btnCalendar = new Gui2Button(windowManager, "btn_forward_calendar", 0, 0, 60, 3, "Calendar / Fixtures");
  Gui2Button* btnStandings = new Gui2Button(windowManager, "btn_forward_standings", 0, 0, 60, 3, "Standings");
  Gui2Button* btnManagement = new Gui2Button(windowManager, "btn_forward_management", 0, 0, 60, 3, "Management");
  Gui2Button* btnInbox = new Gui2Button(windowManager, "btn_forward_inbox", 0, 0, 60, 3, "Inbox");
  Gui2Button* btnSystem = new Gui2Button(windowManager, "btn_forward_system", 0, 0, 60, 3, "System");
  Gui2Button* btnLeagueHub = new Gui2Button(windowManager, "btn_forward_league", 0, 0, 60, 3, "Back to League Hub");
  Gui2Button* btnMainMenu = new Gui2Button(windowManager, "btn_forward_mainmenu", 0, 0, 60, 3, "Return to Main Menu");

  btnTeam->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team); });
  btnCalendar->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Calendar); });
  btnStandings->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings); });
  btnManagement->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Management); });
  btnInbox->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Inbox); });
  btnSystem->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_System); });
  btnLeagueHub->sig_OnClick.connect([this](...) { GoPage(e_PageID_League); });
  btnMainMenu->sig_OnClick.connect([this](...) { GoMainMenu(); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_forward", 20, 16, 60, 60);
  grid->AddView(btnTeam, 0, 0);
  grid->AddView(btnCalendar, 1, 0);
  grid->AddView(btnStandings, 2, 0);
  grid->AddView(btnManagement, 3, 0);
  grid->AddView(btnInbox, 4, 0);
  grid->AddView(btnSystem, 5, 0);
  grid->AddView(btnLeagueHub, 6, 0);
  grid->AddView(btnMainMenu, 7, 0);
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  btnTeam->SetFocus();
  this->Show();
}

LeagueForwardPage::~LeagueForwardPage() {}

void LeagueForwardPage::Process() {
  Gui2Page::Process();

  if (!league_menu_smoke::HasRoute() || autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kAdvanceDelay_ms) {
    return;
  }

  autoAdvanceTriggered = true;

  if (league_menu_smoke::RouteEnabled("dashboard")) {
    printf("[menu-smoke] League dashboard reached successfully\n");
    GetMenuTask()->QuitGame();
  } else if (league_menu_smoke::RouteEnabled("inbox")) {
    printf("[menu-smoke] League dashboard opening Inbox\n");
    GoPage(e_PageID_League_Inbox);
  } else if (league_menu_smoke::RouteEnabled("calendar")) {
    printf("[menu-smoke] League dashboard opening Calendar\n");
    GoPage(e_PageID_League_Calendar);
  } else if (league_menu_smoke::RouteEnabled("standings_table")) {
    printf("[menu-smoke] League dashboard opening Standings\n");
    GoPage(e_PageID_League_Standings);
  } else if (league_menu_smoke::RouteEnabled("team_overview")) {
    printf("[menu-smoke] League dashboard opening Team Management\n");
    GoPage(e_PageID_League_Team);
  } else if (league_menu_smoke::RouteEnabled("management")) {
    printf("[menu-smoke] League dashboard opening Management\n");
    GoPage(e_PageID_League_Management);
  } else if (league_menu_smoke::RouteEnabled("management_contracts")) {
    printf("[menu-smoke] League dashboard opening Management\n");
    GoPage(e_PageID_League_Management);
  } else if (league_menu_smoke::RouteEnabled("system_settings")) {
    printf("[menu-smoke] League dashboard opening System\n");
    GoPage(e_PageID_League_System);
  } else {
    printf("[menu-smoke] League dashboard route '%s' is unsupported\n",
           league_menu_smoke::GetRoute().c_str());
    GetMenuTask()->QuitGame();
  }
}

void LeagueForwardPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

void LeagueForwardPage::GoMainMenu() {
  SaveAutosaveToDatabase();

  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_MainMenu), properties, 0);
  delete this;
}
