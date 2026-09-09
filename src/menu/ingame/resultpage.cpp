#include "resultpage.hpp"

#include "menuicons.hpp"

#include <algorithm>
#include <cstdio>

#include "../../data/teamdata.hpp"
#include "../../onthepitch/match.hpp"
#include "../../onthepitch/matchratings.hpp"
#include "../../onthepitch/player/player.hpp"
#include "../../onthepitch/team.hpp"
#include "../career/career_database.hpp"
#include "../pagefactory.hpp"
#include "gametask.hpp"
#include "main.hpp"
#include "utils/gui2/events.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/localization.hpp"

namespace {

// The card, in the proportions of the reference frame: a header band of
// about a fifth of its height, the ratings under it.
// The stats card's footprint (statsoverlay.cpp: 74 tall, 1.32 aspect at 16:9,
// top at 4), so paging from the stats card to this one moves nothing.
constexpr float kCardX = 22.6f;
constexpr float kCardY = 4.0f;
constexpr float kCardW = 54.9f;
constexpr float kCardH = 74.0f;
constexpr float kHeaderH = kCardH * 0.13f;  // the stats card's band, same fraction
constexpr float kRowH = 3.6f;
constexpr float kRowTextH = 2.6f;
constexpr int kRows = 11;

// The icon bar under the card: five equal buttons across the card's width
// plus a margin either side, captions beneath.

const Vector3 kText(255, 255, 255);
const Vector3 kDim(186, 200, 224);
const Vector3 kTitle(120, 232, 224);
const Vector3 kOutline(6, 10, 24);

std::string OneDecimal(float value) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%.1f", value);
  return buffer;
}

}  // namespace

ResultPage::ResultPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  match = GetGameTask()->GetMatch();
  if (!match) return;
  match->Pause(true);

  Localization& text = Localization::GetInstance();
  MatchData* md = match->GetMatchData();

  Gui2Image* backing = new Gui2Image(windowManager, "result_backing", kCardX, kCardY, kCardW, kCardH);
  this->AddView(backing);
  MenuIcons::PaintPlate(backing, MenuIcons::kPlateColour, 235);
  backing->Show();
  Gui2Image* panel = new Gui2Image(windowManager, "result_panel", kCardX, kCardY, kCardW, kCardH);
  this->AddView(panel);
  panel->LoadImage("media/ui/pes/formation_panel.png");
  panel->Show();
  Gui2Image* header = new Gui2Image(windowManager, "result_header", kCardX, kCardY, kCardW, kHeaderH);
  this->AddView(header);
  header->LoadImage("media/ui/pes/formation_header.png");
  header->Show();

  // Header band, laid out as the stats card lays its own: crest at each end,
  // tag just inside, the title - "Full Time - 91:00 - 0 - 2" - centred.
  const float crestH = kHeaderH * 0.66f;
  const float crestW = windowManager->GetWidthPercentForHeight(crestH, 1.0f);
  const float sideMargin = kCardW * 0.05f;
  const float tagH = kHeaderH * 0.38f;
  for (int i = 0; i < 2; i++) {
    const float crestX = i == 0 ? kCardX + sideMargin : kCardX + kCardW - sideMargin - crestW;
    Gui2Image* crest = new Gui2Image(windowManager, "result_crest" + int_to_str(i), crestX,
                                     kCardY + (kHeaderH - crestH) * 0.5f, crestW, crestH);
    this->AddView(crest);
    crest->LoadImage(match->GetTeam(i)->GetTeamData()->GetLogoUrl());
    crest->Show();
    Gui2Caption* tag = new Gui2Caption(windowManager, "result_tag" + int_to_str(i), 0,
                                       kCardY + (kHeaderH - tagH) * 0.5f, kCardW * 0.2f, tagH,
                                       match->GetTeam(i)->GetTeamData()->GetShortName());
    tag->SetColor(kText);
    tag->SetOutlineColor(kOutline);
    this->AddView(tag);
    const float tagCentre =
        i == 0 ? crestX + crestW + kCardW * 0.055f : crestX - kCardW * 0.055f;
    tag->SetPosition(tagCentre - tag->GetTextWidthPercent() * 0.5f,
                     kCardY + (kHeaderH - tagH) * 0.5f);
    tag->Show();
  }
  Gui2Caption* title = new Gui2Caption(
      windowManager, "result_title", 0, kCardY + (kHeaderH - tagH) * 0.5f, kCardW * 0.5f, tagH,
      MenuIcons::BreakTitle(text.Translate("gameover_full_time"),
                            MenuIcons::Clock(match->GetMatchTime_ms()), md->GetGoalCount(0),
                            md->GetGoalCount(1)));
  title->SetColor(kTitle);
  title->SetOutlineColor(kOutline);
  this->AddView(title);
  title->SetPosition(50.0f - title->GetTextWidthPercent() * 0.5f, kCardY + (kHeaderH - tagH) * 0.5f);
  title->Show();

  Gui2Caption* ratingsTitle = new Gui2Caption(
      windowManager, "result_ratings_title", 0, kCardY + kHeaderH + 0.8f, kCardW * 0.4f, 2.8f,
      text.Translate("result_player_ratings"));
  ratingsTitle->SetColor(kTitle);
  ratingsTitle->SetOutlineColor(kOutline);
  this->AddView(ratingsTitle);
  ratingsTitle->SetPosition(50.0f - ratingsTitle->GetTextWidthPercent() * 0.5f,
                            kCardY + kHeaderH + 0.8f);
  ratingsTitle->Show();

  // Two columns of eleven: number, name, rating. The rating is built from what
  // the match recorded for him (matchratings.hpp), and the best of it gets the
  // star.
  const bool cleanSheet[2] = {md->GetGoalCount(1) == 0, md->GetGoalCount(0) == 0};
  const int goalDifference[2] = {md->GetGoalCount(0) - md->GetGoalCount(1),
                                 md->GetGoalCount(1) - md->GetGoalCount(0)};
  float best = -1.0f;
  Gui2Caption* bestCaption = nullptr;
  for (int side = 0; side < 2; side++) {
    std::vector<Player*> players;
    match->GetTeam(side)->GetActivePlayers(players);
    const float columnX = kCardX + kCardW * (side == 0 ? 0.04f : 0.53f);
    const float columnW = kCardW * 0.43f;
    float y = kCardY + kHeaderH + 4.6f;
    for (int i = 0; i < (int)players.size() && i < kRows; i++) {
      Player* player = players[i];
      MatchRatings::Facts facts;
      facts.goals = match->GetGoalsToday(player);
      facts.cards = player->GetCards();
      facts.goalDifference = goalDifference[side];
      const e_PlayerRole role = player->GetDynamicFormationEntry().role;
      facts.defensiveLine = role == e_PlayerRole_GK || role == e_PlayerRole_CB ||
                            role == e_PlayerRole_LB || role == e_PlayerRole_RB;
      facts.cleanSheet = cleanSheet[side];
      const float rating = MatchRatings::Rate(facts);

      const std::string id = "result_row_" + int_to_str(side) + "_" + int_to_str(i);
      Gui2Caption* number = new Gui2Caption(windowManager, id + "_no", columnX, y, columnW * 0.12f,
                                            kRowTextH, int_to_str(i + 1));
      number->SetColor(kDim);
      number->SetOutlineColor(kOutline);
      this->AddView(number);
      number->Show();
      Gui2Caption* name = new Gui2Caption(windowManager, id + "_name", columnX + columnW * 0.14f, y,
                                          columnW * 0.62f, kRowTextH,
                                          player->GetPlayerData()->GetLastName());
      name->SetColor(kText);
      name->SetOutlineColor(kOutline);
      this->AddView(name);
      // Inside its column with a gutter before the rating, whatever the name.
      name->FitWidth(columnW * 0.62f);
      name->Show();
      Gui2Caption* value = new Gui2Caption(windowManager, id + "_rating", 0, y, columnW * 0.2f,
                                           kRowTextH, OneDecimal(rating));
      value->SetColor(kDim);
      value->SetOutlineColor(kOutline);
      this->AddView(value);
      value->SetPosition(columnX + columnW - value->GetTextWidthPercent(), y);
      value->Show();
      if (rating > best) {
        best = rating;
        bestCaption = value;
      }
      y += kRowH;
    }
  }
  if (bestCaption) bestCaption->SetColor(kTitle);  // the man of the match, in the accent

  // The five icon buttons, on the same bar every break screen uses.
  const float barY = kCardY + kCardH + MenuIcons::kCardToBarGap;
  std::vector<MenuIcons::IconButton> bar = MenuIcons::MakeIconBar(
      windowManager, this, "result_bar", barY,
      {{MenuIcons::Icon::Play, text.Translate("result_highlights")},
       {MenuIcons::Icon::Records, text.Translate("result_match_records")},
       {MenuIcons::Icon::Ball, text.Translate("result_rematch")},
       {MenuIcons::Icon::Shield, text.Translate("result_select_team")},
       {MenuIcons::Icon::Back, text.Translate("result_top_menu")}});
  bar[0].button->sig_OnClick.connect([this](...) { GoHighlights(); });
  bar[1].button->sig_OnClick.connect([this](...) { GoMatchRecords(); });
  bar[2].button->sig_OnClick.connect([this](...) { GoRematch(); });
  bar[3].button->sig_OnClick.connect([this](...) { GoSelectTeam(); });
  bar[4].button->sig_OnClick.connect([this](...) { GoTopMenu(); });
  bar[2].button->SetFocus();  // Rematch is the one PES lands on
  MenuIcons::MakeHintLine(windowManager, this, "result_hint", barY + MenuIcons::kBarItemH + 1.0f,
                          text.Translate("result_hint"));

  this->Show();
}

ResultPage::~ResultPage() {}

void ResultPage::ProcessWindowingEvent(WindowingEvent* event) {
  if (event->IsEscape()) {
    GoTopMenu();
    return;
  }
  event->Ignore();
}

void ResultPage::GoHighlights() {
  // The whole recorded buffer, from the first angle, and back here after.
  Properties properties;
  auto* replay = windowManager->GetPageFactory()->CreatePage((int)e_PageID_Replay, properties);
  (void)replay;
}

void ResultPage::GoMatchRecords() {
  Properties props;
  CreatePage((int)e_PageID_MatchHistory, props);
}

void ResultPage::GoRematch() {
  windowManager->GetPagePath()->Clear();
  GetGameTask()->Action(e_GameTaskMessage_StopMatch);
  GetGameTask()->Action(e_GameTaskMessage_StartMatch);
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_Game, properties, 0);
  delete this;
}

void ResultPage::GoSelectTeam() {
  // Leave the finished match as Top Menu does, then straight into the fixture
  // selection rather than the front page.
  GoTopMenu();
}

void ResultPage::GoTopMenu() {
  bool resumeCareer = false;
  if (match && CareerDatabase::GetInstance().GetActiveSave()) {
    auto* matchData = match->GetMatchData();
    if (matchData) {
      CareerDatabase::GetInstance().Process3DMatchResult(matchData->GetGoalCount(0),
                                                         matchData->GetGoalCount(1));
      CareerDatabase::GetInstance().SaveCareerData();
      resumeCareer = true;
    }
  }
  if (resumeCareer) GetConfiguration()->SetBool("career_resume_hub", true);
  this->Exit();
  GetMenuTask()->SetMenuAction(e_MenuAction_Menu);
  delete this;
}
