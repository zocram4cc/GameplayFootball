#include "resultpage.hpp"

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
constexpr float kCardX = 20.0f;
constexpr float kCardY = 4.0f;
constexpr float kCardW = 60.0f;
constexpr float kCardH = 70.0f;
constexpr float kHeaderH = 16.0f;
constexpr float kRowH = 3.6f;
constexpr float kRowTextH = 2.6f;
constexpr int kRows = 11;

// The icon bar under the card: five equal buttons across the card's width
// plus a margin either side, captions beneath.
constexpr float kBarY = 78.0f;
constexpr float kButtonH = 9.0f;
constexpr float kCaptionH = 2.6f;

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

  Gui2Image* panel = new Gui2Image(windowManager, "result_panel", kCardX, kCardY, kCardW, kCardH);
  this->AddView(panel);
  panel->LoadImage("media/ui/pes/formation_panel.png");
  panel->Show();
  Gui2Image* header = new Gui2Image(windowManager, "result_header", kCardX, kCardY, kCardW, kHeaderH);
  this->AddView(header);
  header->LoadImage("media/ui/pes/formation_header.png");
  header->Show();

  // Header band: crest and tag at each end, "Full Time" over the score and
  // the clock in the middle.
  const float crestH = kHeaderH * 0.55f;
  const float crestW = windowManager->GetWidthPercentForHeight(crestH, 1.0f);
  for (int i = 0; i < 2; i++) {
    const float crestX = i == 0 ? kCardX + kCardW * 0.10f : kCardX + kCardW * 0.90f - crestW;
    Gui2Image* crest = new Gui2Image(windowManager, "result_crest" + int_to_str(i), crestX,
                                     kCardY + 1.0f, crestW, crestH);
    this->AddView(crest);
    crest->LoadImage(match->GetTeam(i)->GetTeamData()->GetLogoUrl());
    crest->Show();
    Gui2Caption* tag = new Gui2Caption(windowManager, "result_tag" + int_to_str(i), 0,
                                       kCardY + 1.5f + crestH, kCardW * 0.3f, 2.8f,
                                       match->GetTeam(i)->GetTeamData()->GetShortName());
    tag->SetColor(kTitle);
    tag->SetOutlineColor(kOutline);
    this->AddView(tag);
    tag->SetPosition(crestX + crestW * 0.5f - tag->GetTextWidthPercent() * 0.5f,
                     kCardY + 1.5f + crestH);
    tag->Show();
  }
  Gui2Caption* title = new Gui2Caption(windowManager, "result_title", 0, kCardY + 1.2f,
                                       kCardW * 0.4f, 3.4f, text.Translate("gameover_full_time"));
  title->SetColor(kText);
  title->SetOutlineColor(kOutline);
  this->AddView(title);
  title->SetPosition(50.0f - title->GetTextWidthPercent() * 0.5f, kCardY + 1.2f);
  title->Show();
  Gui2Caption* score = new Gui2Caption(
      windowManager, "result_score", 0, kCardY + 5.4f, kCardW * 0.4f, 7.0f,
      int_to_str(md->GetGoalCount(0)) + "   " + int_to_str(md->GetGoalCount(1)));
  score->SetColor(kText);
  score->SetOutlineColor(kOutline);
  this->AddView(score);
  score->SetPosition(50.0f - score->GetTextWidthPercent() * 0.5f, kCardY + 5.4f);
  score->Show();
  const int minutes = (int)(match->GetMatchTime_ms() / 60000);
  Gui2Caption* clock = new Gui2Caption(windowManager, "result_clock", 0, kCardY + 12.4f,
                                       kCardW * 0.3f, 2.6f, int_to_str(minutes) + ":00");
  clock->SetColor(kDim);
  clock->SetOutlineColor(kOutline);
  this->AddView(clock);
  clock->SetPosition(50.0f - clock->GetTextWidthPercent() * 0.5f, kCardY + 12.4f);
  clock->Show();

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
      name->Show();
      Gui2Caption* value = new Gui2Caption(windowManager, id + "_rating", columnX + columnW * 0.80f, y,
                                           columnW * 0.2f, kRowTextH, OneDecimal(rating));
      value->SetColor(kText);
      value->SetOutlineColor(kOutline);
      this->AddView(value);
      value->Show();
      if (rating > best) {
        best = rating;
        bestCaption = value;
      }
      y += kRowH;
    }
  }
  if (bestCaption) bestCaption->SetColor(kTitle);  // the man of the match, in the accent

  // The five icon buttons. PES draws a pictogram on each with the caption
  // beneath; the face here carries one glyph standing in for the pictogram
  // (play, records, ball, shield, back arrow) so the caption below is the
  // only text, as on the reference - the labels overran the buttons when they
  // were drawn on the face.
  const char* keys[5] = {"result_highlights", "result_match_records", "result_rematch",
                         "result_select_team", "result_top_menu"};
  const char* glyphs[5] = {">", "=", "O", "U", "<"};
  const float barW = 90.0f;
  const float barX = 5.0f;
  const float gap = 1.2f;
  const float buttonW = (barW - gap * 4.0f) / 5.0f;
  Gui2Button* buttons[5];
  for (int i = 0; i < 5; i++) {
    const float x = barX + i * (buttonW + gap);
    buttons[i] = new Gui2Button(windowManager, std::string("result_button_") + keys[i], x, kBarY,
                                buttonW, kButtonH, glyphs[i]);
    this->AddView(buttons[i]);
    buttons[i]->Show();
    Gui2Caption* caption = new Gui2Caption(windowManager, std::string("result_caption_") + keys[i],
                                           x, kBarY + kButtonH + 0.4f, buttonW, kCaptionH,
                                           text.Translate(keys[i]));
    caption->SetColor(kDim);
    caption->SetOutlineColor(kOutline);
    this->AddView(caption);
    caption->SetPosition(x + buttonW * 0.5f - caption->GetTextWidthPercent() * 0.5f,
                         kBarY + kButtonH + 0.4f);
    caption->Show();
  }
  buttons[0]->sig_OnClick.connect([this](...) { GoHighlights(); });
  buttons[1]->sig_OnClick.connect([this](...) { GoMatchRecords(); });
  buttons[2]->sig_OnClick.connect([this](...) { GoRematch(); });
  buttons[3]->sig_OnClick.connect([this](...) { GoSelectTeam(); });
  buttons[4]->sig_OnClick.connect([this](...) { GoTopMenu(); });
  buttons[2]->SetFocus();  // Rematch is the one PES lands on

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
