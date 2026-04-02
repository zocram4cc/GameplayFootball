#include "league_forward.hpp"

#include "../../main.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueForwardPage::LeagueForwardPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
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

  btnTeam->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team); });
  btnCalendar->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Calendar); });
  btnStandings->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings); });
  btnManagement->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Management); });
  btnInbox->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Inbox); });
  btnSystem->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_System); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_forward", 20, 16, 60, 60);
  grid->AddView(btnTeam, 0, 0);
  grid->AddView(btnCalendar, 1, 0);
  grid->AddView(btnStandings, 2, 0);
  grid->AddView(btnManagement, 3, 0);
  grid->AddView(btnInbox, 4, 0);
  grid->AddView(btnSystem, 5, 0);
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  btnTeam->SetFocus();
  this->Show();
}

LeagueForwardPage::~LeagueForwardPage() {}

void LeagueForwardPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}
