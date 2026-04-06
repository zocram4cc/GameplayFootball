#include "league_system.hpp"

#include "../../league/leaguecode.hpp"
#include "../../main.hpp"
#include "menu_smoke.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueSystemPage::LeagueSystemPage(Gui2WindowManager* windowManager,
                                   const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_system", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_system", 2, 2, 66, 3, "System");
  frame->AddView(title);
  title->Show();

  Gui2Button* btnSave = new Gui2Button(windowManager, "btn_system_save", 0, 0, 60, 3, "Save");
  Gui2Button* btnSettings = new Gui2Button(windowManager, "btn_system_settings", 0, 0, 60, 3, "Settings");
  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_system_back", 0, 0, 60, 3, "Back to Dashboard");

  btnSave->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_System_Save); });
  btnSettings->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_System_Settings); });
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Forward); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_system", 2, 10, 66, 50);
  grid->AddView(btnSave, 0, 0);
  grid->AddView(btnSettings, 1, 0);
  grid->AddView(btnBack, 2, 0);
  grid->UpdateLayout(0.5);
  frame->AddView(grid);
  grid->Show();

  btnSave->SetFocus();
  this->Show();
}

LeagueSystemPage::~LeagueSystemPage() {}

void LeagueSystemPage::Process() {
  Gui2Page::Process();

  if (!league_menu_smoke::RouteEnabled("system_settings") || autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kAdvanceDelay_ms) {
    return;
  }

  autoAdvanceTriggered = true;
  printf("[menu-smoke] System page opening settings\n");
  GoPage(e_PageID_League_System_Settings);
}

void LeagueSystemPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueSystemSavePage::LeagueSystemSavePage(Gui2WindowManager* windowManager,
                                           const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_save", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_system_save", 2, 2, 66, 3, "Save Game");
  frame->AddView(title);
  title->Show();

  std::string saveDir = GetActiveSaveDirectory();
  Gui2Caption* info = new Gui2Caption(windowManager, "caption_save_info", 2, 10, 66, 6,
                                       "Game saves automatically. Your current save: " + saveDir);
  frame->AddView(info);
  info->Show();

  Gui2Button* btnSave = new Gui2Button(windowManager, "btn_save_manual", 30, 40, 40, 3, "Manual Save");
  btnSave->sig_OnClick.connect([this, windowManager](...) {
    SaveAutosaveToDatabase();
    Gui2Caption* feedback = new Gui2Caption(windowManager, "caption_save_feedback", 2, 30, 66, 3,
                                             "Save successful!");
    frame->AddView(feedback);
    feedback->Show();
  });
  frame->AddView(btnSave);
  btnSave->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_save_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_System), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnSave->SetFocus();
  this->Show();
}

LeagueSystemSavePage::~LeagueSystemSavePage() {}

LeagueSystemSettingsPage::LeagueSystemSettingsPage(Gui2WindowManager* windowManager,
                                                   const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_settings", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_system_settings", 2, 2, 66, 3, "Settings");
  frame->AddView(title);
  title->Show();

  auto result = GetDB()->Query(
      "SELECT s.managername, t.name, s.currency, s.difficulty, s.seasonyear "
      "FROM settings s JOIN teams t ON s.team_id = t.id LIMIT 1");

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_settings", 2, 10, 66, 60);
  int row = 0;

  if (!result->data.empty()) {
    Gui2Caption* lblMgr = new Gui2Caption(windowManager, "caption_set_mgr", 0, 0, 40, 2.5, "Manager Name:");
    Gui2Caption* valMgr = new Gui2Caption(windowManager, "caption_set_mgr_val", 0, 0, 40, 2.5, result->data.at(0).at(0));
    grid->AddView(lblMgr, row, 0);
    grid->AddView(valMgr, row++, 1);

    Gui2Caption* lblTeam = new Gui2Caption(windowManager, "caption_set_team", 0, 0, 40, 2.5, "Team:");
    Gui2Caption* valTeam = new Gui2Caption(windowManager, "caption_set_team_val", 0, 0, 40, 2.5, result->data.at(0).at(1));
    grid->AddView(lblTeam, row, 0);
    grid->AddView(valTeam, row++, 1);

    Gui2Caption* lblCur = new Gui2Caption(windowManager, "caption_set_currency", 0, 0, 40, 2.5, "Currency:");
    Gui2Caption* valCur = new Gui2Caption(windowManager, "caption_set_currency_val", 0, 0, 40, 2.5, result->data.at(0).at(2));
    grid->AddView(lblCur, row, 0);
    grid->AddView(valCur, row++, 1);

    Gui2Caption* lblDiff = new Gui2Caption(windowManager, "caption_set_diff", 0, 0, 40, 2.5, "Difficulty:");
    Gui2Caption* valDiff = new Gui2Caption(windowManager, "caption_set_diff_val", 0, 0, 40, 2.5, result->data.at(0).at(3));
    grid->AddView(lblDiff, row, 0);
    grid->AddView(valDiff, row++, 1);

    Gui2Caption* lblSeason = new Gui2Caption(windowManager, "caption_set_season", 0, 0, 40, 2.5, "Season Year:");
    Gui2Caption* valSeason = new Gui2Caption(windowManager, "caption_set_season_val", 0, 0, 40, 2.5, result->data.at(0).at(4));
    grid->AddView(lblSeason, row, 0);
    grid->AddView(valSeason, row++, 1);
  }
  grid->UpdateLayout(0.5);
  frame->AddView(grid);
  grid->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_settings_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_System), properties, 0);
    delete this;
  });
  frame->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueSystemSettingsPage::~LeagueSystemSettingsPage() {}

void LeagueSystemSettingsPage::Process() {
  Gui2Page::Process();

  if (!league_menu_smoke::RouteEnabled("system_settings") || autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kQuitDelay_ms) {
    return;
  }

  autoAdvanceTriggered = true;
  printf("[menu-smoke] League system settings reached successfully\n");
  GetMenuTask()->QuitGame();
}
