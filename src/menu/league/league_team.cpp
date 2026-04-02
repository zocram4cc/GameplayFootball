#include "league_team.hpp"

#include "../../main.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueTeamPage::LeagueTeamPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team", 20, 5, 60, 3, "Team Management");
  this->AddView(title);
  title->Show();

  Gui2Button* btnFormation = new Gui2Button(windowManager, "btn_team_formation", 0, 0, 60, 3, "Formation");
  Gui2Button* btnPlayerSel = new Gui2Button(windowManager, "btn_team_playersel", 0, 0, 60, 3, "Player Selection");
  Gui2Button* btnTactics = new Gui2Button(windowManager, "btn_team_tactics", 0, 0, 60, 3, "Tactics");
  Gui2Button* btnPlayerOvr = new Gui2Button(windowManager, "btn_team_playerovr", 0, 0, 60, 3, "Player Overview");
  Gui2Button* btnPlayerDev = new Gui2Button(windowManager, "btn_team_playerdev", 0, 0, 60, 3, "Player Development");
  Gui2Button* btnSetup = new Gui2Button(windowManager, "btn_team_setup", 0, 0, 60, 3, "Team Setup");
  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_team_back", 0, 0, 60, 3, "Back to Dashboard");

  btnFormation->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team_Formation); });
  btnPlayerSel->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team_PlayerSelection); });
  btnTactics->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team_Tactics); });
  btnPlayerOvr->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team_PlayerOverview); });
  btnPlayerDev->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team_PlayerDevelopment); });
  btnSetup->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Team_Setup); });
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Forward); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_team", 20, 14, 60, 60);
  grid->AddView(btnFormation, 0, 0);
  grid->AddView(btnPlayerSel, 1, 0);
  grid->AddView(btnTactics, 2, 0);
  grid->AddView(btnPlayerOvr, 3, 0);
  grid->AddView(btnPlayerDev, 4, 0);
  grid->AddView(btnSetup, 5, 0);
  grid->AddView(btnBack, 6, 0);
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  btnFormation->SetFocus();
  this->Show();
}

LeagueTeamPage::~LeagueTeamPage() {}

void LeagueTeamPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueTeamFormationPage::LeagueTeamFormationPage(Gui2WindowManager* windowManager,
                                                 const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team_formation", 10, 5, 80, 3, "Formation");
  this->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT t.name, t.formation_xml FROM teams t, settings s WHERE t.id = s.team_id LIMIT 1");
  if (!result->data.empty()) {
    std::string teamName = result->data.at(0).at(0);
    Gui2Caption* info =
        new Gui2Caption(windowManager, "caption_formation_team", 10, 12, 80, 3,
                        "Team: " + teamName);
    this->AddView(info);
    info->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_formation_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Team), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueTeamFormationPage::~LeagueTeamFormationPage() {}

LeagueTeamPlayerSelectionPage::LeagueTeamPlayerSelectionPage(Gui2WindowManager* windowManager,
                                                               const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_team_playerselection", 10, 3,
                                        80, 3, "Player Selection");
  this->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT p.id, p.firstname, p.lastname, p.role FROM players p, teams t, settings s "
      "WHERE p.team_id = t.id AND t.id = s.team_id ORDER BY p.formationorder");
  if (!result->data.empty()) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_playersel", 10, 10, 80, 75);
    int row = 0;
    for (const auto& r : result->data) {
      std::string label = r.at(1) + " " + r.at(2) + " (" + r.at(3) + ")";
      Gui2Button* btn = new Gui2Button(windowManager, "btn_player_" + r.at(0), 0, 0, 76, 2.5, label);
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_playersel_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Team), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  this->SetFocus();
  this->Show();
}

LeagueTeamPlayerSelectionPage::~LeagueTeamPlayerSelectionPage() {}

LeagueTeamTacticsPage::LeagueTeamTacticsPage(Gui2WindowManager* windowManager,
                                              const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team_tactics", 10, 5, 80, 3, "Team Tactics");
  this->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT t.name, t.tactics_xml FROM teams t, settings s WHERE t.id = s.team_id LIMIT 1");
  if (!result->data.empty()) {
    std::string teamName = result->data.at(0).at(0);
    Gui2Caption* info =
        new Gui2Caption(windowManager, "caption_tactics_team", 10, 12, 80, 3,
                        "Team: " + teamName);
    this->AddView(info);
    info->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_tactics_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Team), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueTeamTacticsPage::~LeagueTeamTacticsPage() {}

LeagueTeamPlayerOverviewPage::LeagueTeamPlayerOverviewPage(Gui2WindowManager* windowManager,
                                                            const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_team_playeroverview", 10, 3,
                                        80, 3, "Player Overview");
  this->AddView(title);
  title->Show();

  Gui2Caption* header =
      new Gui2Caption(windowManager, "caption_playerovr_header", 5, 7, 90, 2,
                      "Name                  | Role                | Age | Base Stat");
  this->AddView(header);
  header->Show();

  auto result = GetDB()->Query(
      "SELECT p.firstname, p.lastname, p.role, p.age, p.base_stat FROM players p "
      "JOIN teams t ON p.team_id = t.id JOIN settings s ON t.id = s.team_id "
      "ORDER BY p.formationorder");
  if (!result->data.empty()) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_playerovr", 5, 10, 90, 72);
    int row = 0;
    for (const auto& r : result->data) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%-20s | %-19s | %2s  | %s",
               (r.at(0) + " " + r.at(1)).c_str(), r.at(2).c_str(), r.at(3).c_str(), r.at(4).c_str());
      Gui2Button* btn = new Gui2Button(windowManager, "btn_povr_" + std::to_string(row), 0, 0, 86, 2.5, buf);
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_playerovr_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Team), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  this->SetFocus();
  this->Show();
}

LeagueTeamPlayerOverviewPage::~LeagueTeamPlayerOverviewPage() {}

LeagueTeamPlayerDevelopmentPage::LeagueTeamPlayerDevelopmentPage(Gui2WindowManager* windowManager,
                                                                    const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_team_playerdevelopment", 10, 5,
                                        80, 3, "Player Development");
  this->AddView(title);
  title->Show();

  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_playerdev_info", 10, 15, 80, 4,
                      "Player growth is processed at the end of each season. "
                      "Younger players grow faster; older players decline.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_playerdev_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Team), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueTeamPlayerDevelopmentPage::~LeagueTeamPlayerDevelopmentPage() {}

LeagueTeamSetupPage::LeagueTeamSetupPage(Gui2WindowManager* windowManager,
                                         const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team_setup", 10, 5, 80, 3, "Team Setup");
  this->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT t.name, l.name FROM teams t JOIN leagues l ON t.league_id = l.id "
      "JOIN settings s ON t.id = s.team_id LIMIT 1");
  if (!result->data.empty()) {
    std::string label = "Team: " + result->data.at(0).at(0) + " | League: " + result->data.at(0).at(1);
    Gui2Caption* info = new Gui2Caption(windowManager, "caption_setup_info", 10, 15, 80, 3, label);
    this->AddView(info);
    info->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_setup_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Team), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueTeamSetupPage::~LeagueTeamSetupPage() {}
