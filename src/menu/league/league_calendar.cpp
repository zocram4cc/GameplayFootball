#include "league_calendar.hpp"

#include "../../main.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueCalendarPage::LeagueCalendarPage(Gui2WindowManager* windowManager,
                                       const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_calendar", 10, 3, 80, 3, "Calendar / Fixtures");
  this->AddView(title);
  title->Show();

  Gui2Caption* filterLabel =
      new Gui2Caption(windowManager, "caption_cal_filter", 10, 7, 20, 2.5, "Filter by League:");
  this->AddView(filterLabel);
  filterLabel->Show();

  leagueFilterPulldown =
      new Gui2Pulldown(windowManager, "pulldown_cal_league", 30, 7, 30, 3);
  leagueFilterPulldown->AddEntry("All Leagues", "all");

  auto leaguesResult = GetDB()->Query("SELECT id, name FROM leagues ORDER BY name");
  for (const auto& row : leaguesResult->data) {
    leagueFilterPulldown->AddEntry(row.at(1), row.at(0));
  }
  m_selectedLeagueID = "all";
  leagueFilterPulldown->sig_OnChange.connect([this](Gui2Pulldown* pd) {
    m_selectedLeagueID = pd->GetSelected();
    RefreshFixtures();
  });
  this->AddView(leagueFilterPulldown);
  leagueFilterPulldown->Show();

  RefreshFixtures();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_cal_back", 30, 90, 40, 3, "Back to Dashboard");
  btnBack->sig_OnClick.connect([this](...) { GoBack(); });
  this->AddView(btnBack);
  btnBack->Show();

  this->SetFocus();
  this->Show();
}

LeagueCalendarPage::~LeagueCalendarPage() {}

void LeagueCalendarPage::RefreshFixtures() {
  std::string query =
      "SELECT c.timestamp, t1.name, t2.name, l.name "
      "FROM calendar c "
      "JOIN teams t1 ON c.team1_id = t1.id "
      "JOIN teams t2 ON c.team2_id = t2.id "
      "JOIN leagues l ON c.competition_id = l.id ";
  if (m_selectedLeagueID != "all") {
    query += "WHERE c.competition_id = " + m_selectedLeagueID + " ";
  }
  query += "ORDER BY c.timestamp LIMIT 40";

  auto result = GetDB()->Query(query);
  Gui2Caption* header =
      new Gui2Caption(windowManager, "caption_cal_header", 5, 12, 90, 2,
                      "Date              | Home                | Away                | League");
  this->AddView(header);
  header->Show();

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_cal", 5, 15, 90, 68);
  int row = 0;
  for (const auto& r : result->data) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%-17s | %-19s | %-19s | %s",
             r.at(0).c_str(), r.at(1).c_str(), r.at(2).c_str(), r.at(3).c_str());
    Gui2Button* btn = new Gui2Button(windowManager, "btn_fixture_" + std::to_string(row), 0, 0, 86, 2.5, buf);
    grid->AddView(btn, row++, 0);
  }
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();
}

void LeagueCalendarPage::GoBack() {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Forward), properties, 0);
  delete this;
}
