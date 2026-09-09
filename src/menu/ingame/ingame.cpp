// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "ingame.hpp"

#include <algorithm>
#include <memory>
#include <cstdio>
#include <vector>

#include "../controllerselect.hpp"
#include "../gameplan.hpp"
#include "../pagefactory.hpp"
#include "../settings.hpp"
#include "main.hpp"
#include "onthepitch/coachmode.hpp"
#include "onthepitch/match.hpp"
#include "menuicons.hpp"
#include "replaymenu.hpp"
#include "statsoverlay.hpp"
#include "utils/localization.hpp"

using namespace blunted;

IngamePage::IngamePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  teamID = pageData.properties->GetInt("teamID", 0);

  Match* match = GetGameTask()->GetMatch();
  match->Pause(true);
  // The card takes the screen; the HUD has nothing to add over it.
  match->SuppressHud(true);

  // PES's pause menu (VGL 26 day 12, 5:10:59; docs/VGL26_DAY12_REFERENCE.md
  // §4): the same stats card as half time, titled "Pause Menu" with the score
  // and the clock, and a bar of icon buttons under it - Game Plan, Replay,
  // Camera Settings, System Settings and so on, B to return. It used to be a
  // tall frame of text buttons in four sections; half time and pause were two
  // different screens of the same thing.
  // Three cards under one header, paged with LB/RB (Q/E): the stat table, each
  // team's ball activity, the match's events. The title carries the score and
  // the clock, and the card's own name after it, so the reader knows which
  // page is up without a tab strip.
  const unsigned long matchTime_ms = match->GetMatchTime_ms();
  const int minute = std::min(90, static_cast<int>(matchTime_ms / 60000));
  const std::string scoreline =
      int_to_str(match->GetScore(0)) + "  " + int_to_str(minute) + ":" +
      (matchTime_ms / 1000 % 60 < 10 ? "0" : "") + int_to_str(matchTime_ms / 1000 % 60) + "  " +
      int_to_str(match->GetScore(1));
  cards = std::make_unique<PagedStatsCards>(
      windowManager, this, match, "pause_card",
      Localization::GetInstance().Translate("ingame_pause") + "   " + scoreline);
  Gui2StatsOverlay* card = cards->Current();

  float cardX, cardY, cardW, cardH;
  card->GetPosition(cardX, cardY);
  card->GetSize(cardW, cardH);
  const float barH = 11.0f;
  const float barY = std::min(cardY + cardH + 1.0f, 100.0f - barH - 4.0f);

  // In coach mode both touchlines are human-run, so each coached team gets its
  // own game plan entry rather than only the team that opened the menu.
  const bool managerDuel = CoachMode::IsManagerDuel(match->GetCoachSetup());
  Localization& text = Localization::GetInstance();
  std::vector<MenuIcons::BarItem> items = {
      {MenuIcons::Icon::GamePlan,
       text.Translate("ingame_game_plan") +
           (managerDuel ? " " + int_to_str(teamID + 1) : std::string())}};
  if (managerDuel)
    items.push_back({MenuIcons::Icon::GamePlan,
                     text.Translate("ingame_game_plan") + " " + int_to_str(2 - teamID)});
  items.push_back({MenuIcons::Icon::Ball, text.Translate("ingame_set_pieces")});
  items.push_back({MenuIcons::Icon::Play, text.Translate("ingame_replay")});
  items.push_back({MenuIcons::Icon::Camera, text.Translate("ingame_camera_settings")});
  items.push_back({MenuIcons::Icon::Substitute, text.Translate("ingame_controller_select")});
  items.push_back({MenuIcons::Icon::Shield, text.Translate("ingame_visual_options")});
  items.push_back({MenuIcons::Icon::Gear, text.Translate("ingame_system_settings")});
  items.push_back({MenuIcons::Icon::Back, text.Translate("ingame_forfeit_match")});
  const float barW = std::min(11.0f, (92.0f - (items.size() - 1) * 0.8f) / items.size());
  std::vector<MenuIcons::IconButton> bar =
      MenuIcons::MakeIconBar(windowManager, this, "pause_bar", barY, barW, barH, items);

  size_t i = 0;
  bar[i++].button->sig_OnClick.connect([this](...) { GoGamePlan(); });
  if (managerDuel) {
    const int opponentID = abs(teamID - 1);
    bar[i++].button->sig_OnClick.connect(
        [this, opponentID](...) { GoGamePlanForTeam(opponentID); });
  }
  bar[i++].button->sig_OnClick.connect([this](...) { GoSetPieceEditor(); });
  bar[i++].button->sig_OnClick.connect([this](...) { GoReplay(); });
  bar[i++].button->sig_OnClick.connect([this](...) { GoCameraSettings(); });
  bar[i++].button->sig_OnClick.connect([this](...) { GoControllerSelect(); });
  bar[i++].button->sig_OnClick.connect([this](...) { GoVisualOptions(); });
  bar[i++].button->sig_OnClick.connect([this](...) { GoSystemSettings(); });
  bar[i++].button->sig_OnClick.connect([this](...) { GoPreQuit(); });

  Gui2Caption* hintCaption =
      new Gui2Caption(windowManager, "caption_ingame_hint", 4.0f, std::min(97.5f, barY + barH + 0.4f),
                      40.0f, 2.0f, text.Translate("ingame_hint"));
  this->AddView(hintCaption);
  hintCaption->Show();

  bar[0].button->SetFocus();
  this->Show();
}

IngamePage::~IngamePage() {}

void IngamePage::GoControllerRemap() {
  CreatePage(e_PageID_Controller);
}

void IngamePage::GoGamePlan() {
  GoGamePlanForTeam(teamID);
}

void IngamePage::GoGamePlanForTeam(int gamePlanTeamID) {
  Properties properties;
  properties.Set("teamID", gamePlanTeamID);
  CreatePage(e_PageID_GamePlan, properties);
}

void IngamePage::GoControllerSelect() {
  Properties properties;
  properties.SetBool("isInGame", true);
  CreatePage(e_PageID_ControllerSelect, properties);
}

void IngamePage::GoCameraSettings() {
  CreatePage(e_PageID_Camera);
}

void IngamePage::GoVisualOptions() {
  CreatePage(e_PageID_VisualOptions);
}

void IngamePage::GoSystemSettings() {
  CreatePage(e_PageID_Settings);
}

void IngamePage::GoReplay() {
  CreatePage(e_PageID_Replay);
}

void IngamePage::GoPreQuit() {
  CreatePage(e_PageID_PreQuit);
}

void IngamePage::GoSetPieceEditor() {
  Properties properties;
  properties.Set("teamDatabaseID",
                 GetGameTask()->GetMatch()->GetTeam(teamID)->GetTeamData()->GetDatabaseID());
  CreatePage((int)e_PageID_SetPieceEditor, properties);
}

void IngamePage::ProcessKeyboardEvent(KeyboardEvent* event) {
  // LB/RB page the cards; Q/E are the keyboard's shoulders, as on the game plan.
  if (cards && cards->HandleKey(event)) {
    event->Accept();
    return;
  }
  Gui2Page::ProcessKeyboardEvent(event);
}

void IngamePage::ProcessWindowingEvent(WindowingEvent* event) {
  if (event->IsEscape()) {
    GetMenuTask()->ReleaseAllButtons();
    GetGameTask()->GetMatch()->Pause(false);
    GetGameTask()->GetMatch()->SuppressHud(false);
  }
  Gui2Page::ProcessWindowingEvent(event);
}

PreQuitPage::PreQuitPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_prequit", 25, 40, 50, 20, true);
  this->AddView(frame);
  frame->Show();

  Gui2Caption* restartCaption =
      new Gui2Caption(windowManager, "caption_prequit_info", 0, 0, 44, 3,
                      Localization::GetInstance().Translate("ingame_forfeit_confirm"));
  Gui2Button* okButton = new Gui2Button(windowManager, "button_prequit_ok", 0, 0, 44, 3,
                                        Localization::GetInstance().Translate("ingame_forfeit"));
  Gui2Button* cancelButton =
      new Gui2Button(windowManager, "button_prequit_cancel", 0, 0, 44, 3,
                     Localization::GetInstance().Translate("ingame_continue_match"));
  okButton->sig_OnClick.connect([this](...) { GoMenu(); });
  cancelButton->sig_OnClick.connect([this](...) { GoBack(); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_prequit", 2, 2, 46, 16);

  grid->AddView(restartCaption, 0, 0);
  grid->AddView(okButton, 1, 0);
  grid->AddView(cancelButton, 2, 0);

  grid->UpdateLayout(0.5);

  frame->AddView(grid);
  grid->Show();

  cancelButton->SetFocus();

  this->Show();
}

PreQuitPage::~PreQuitPage() {}

void PreQuitPage::GoMenu() {
  this->Exit();
  GetMenuTask()->SetMenuAction(e_MenuAction_Menu);
  delete this;
}
