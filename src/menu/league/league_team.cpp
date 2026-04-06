#include "league_team.hpp"

#include "../../main.hpp"
#include "menu_smoke.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueTeamPage::LeagueTeamPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_team", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team", 2, 2, 66, 3, "Team Management");
  frame->AddView(title);
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

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_team", 2, 10, 66, 75);
  grid->AddView(btnFormation, 0, 0);
  grid->AddView(btnPlayerSel, 1, 0);
  grid->AddView(btnTactics, 2, 0);
  grid->AddView(btnPlayerOvr, 3, 0);
  grid->AddView(btnPlayerDev, 4, 0);
  grid->AddView(btnSetup, 5, 0);
  grid->AddView(btnBack, 6, 0);
  grid->UpdateLayout(0.5);
  frame->AddView(grid);
  grid->Show();

  btnFormation->SetFocus();
  this->Show();
}

LeagueTeamPage::~LeagueTeamPage() {}

void LeagueTeamPage::Process() {
  Gui2Page::Process();

  if (!league_menu_smoke::RouteEnabled("team_overview") || autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kAdvanceDelay_ms) {
    return;
  }

  autoAdvanceTriggered = true;
  printf("[menu-smoke] Team management opening player overview\n");
  GoPage(e_PageID_League_Team_PlayerOverview);
}

void LeagueTeamPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueTeamFormationPage::LeagueTeamFormationPage(Gui2WindowManager* windowManager,
                                                 const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_team_formation", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team_formation", 2, 2, 66, 3, "Formation");
  frame->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT t.name, t.formation_xml FROM teams t, settings s WHERE t.id = s.team_id LIMIT 1");
  if (!result->data.empty()) {
    std::string teamName = result->data.at(0).at(0);
    Gui2Caption* info =
        new Gui2Caption(windowManager, "caption_formation_team", 2, 6, 66, 3,
                        "Team: " + teamName);
    frame->AddView(info);
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
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_team_playersel", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_team_playerselection", 2, 2,
                                        66, 3, "Player Selection");
  frame->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT p.id, p.firstname, p.lastname, p.role FROM players p, teams t, settings s "
      "WHERE p.team_id = t.id AND t.id = s.team_id ORDER BY p.formationorder");
  if (!result->data.empty()) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_playersel", 2, 8, 66, 80);
    int row = 0;
    for (const auto& r : result->data) {
      std::string label = r.at(1) + " " + r.at(2) + " (" + r.at(3) + ")";
      Gui2Button* btn = new Gui2Button(windowManager, "btn_player_" + r.at(0), 0, 0, 86, 2.5, label);
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    frame->AddView(grid);
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
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_team_tactics", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team_tactics", 2, 2, 66, 3, "Team Tactics");
  frame->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT t.name, t.tactics_xml FROM teams t, settings s WHERE t.id = s.team_id LIMIT 1");
  if (!result->data.empty()) {
    std::string teamName = result->data.at(0).at(0);
    Gui2Caption* info =
        new Gui2Caption(windowManager, "caption_tactics_team", 2, 6, 66, 3,
                        "Team: " + teamName);
    frame->AddView(info);
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
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_team_playerovr", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_team_playeroverview", 2, 2,
                                        66, 3, "Player Overview");
  frame->AddView(title);
  title->Show();

  Gui2Caption* header =
      new Gui2Caption(windowManager, "caption_playerovr_header", 2, 6, 66, 2,
                      "Name                  | Role                | Age | Base Stat");
  frame->AddView(header);
  header->Show();

  auto result = GetDB()->Query(
      "SELECT p.firstname, p.lastname, p.role, p.age, p.base_stat FROM players p "
      "JOIN teams t ON p.team_id = t.id JOIN settings s ON t.id = s.team_id "
      "ORDER BY p.formationorder");
  if (!result->data.empty()) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_playerovr", 2, 10, 66, 75);
    int row = 0;
    for (const auto& r : result->data) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%-20s | %-19s | %2s  | %s",
               (r.at(0) + " " + r.at(1)).c_str(), r.at(2).c_str(), r.at(3).c_str(), r.at(4).c_str());
      Gui2Button* btn = new Gui2Button(windowManager, "btn_povr_" + std::to_string(row), 0, 0, 86, 2.5, buf);
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    frame->AddView(grid);
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

void LeagueTeamPlayerOverviewPage::Process() {
  Gui2Page::Process();

  if (!league_menu_smoke::RouteEnabled("team_overview") || autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kQuitDelay_ms) {
    return;
  }

  autoAdvanceTriggered = true;
  printf("[menu-smoke] Team player overview reached successfully\n");
  GetMenuTask()->QuitGame();
}

LeagueTeamPlayerDevelopmentPage::LeagueTeamPlayerDevelopmentPage(Gui2WindowManager* windowManager,
                                                                    const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_team_playerdev", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_team_playerdevelopment", 2, 2,
                                        66, 3, "Player Development");
  frame->AddView(title);
  title->Show();

  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_playerdev_info", 2, 6, 66, 4,
                      "Player growth is processed at the end of each season. "
                      "Younger players grow faster; older players decline.");
  frame->AddView(info);
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
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_team_setup", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_team_setup", 2, 2, 66, 3, "Team Setup");
  frame->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT t.name, l.name FROM teams t JOIN leagues l ON t.league_id = l.id "
      "JOIN settings s ON t.id = s.team_id LIMIT 1");
  if (!result->data.empty()) {
    std::string label = "Team: " + result->data.at(0).at(0) + " | League: " + result->data.at(0).at(1);
    Gui2Caption* info = new Gui2Caption(windowManager, "caption_setup_info", 2, 6, 66, 3, label);
    frame->AddView(info);
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
