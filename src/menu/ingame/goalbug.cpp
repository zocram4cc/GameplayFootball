#include "goalbug.hpp"

#include <algorithm>
#include <cmath>

#include "../../data/playerdata.hpp"
#include "../../data/teamdata.hpp"
#include "../../onthepitch/goalsequence.hpp"
#include "../../onthepitch/match.hpp"
#include "../../onthepitch/player/player.hpp"
#include "../../onthepitch/team.hpp"
#include "formationgraphiclayout.hpp"
#include "utils/localization.hpp"
#include "utils/gui2/windowmanager.hpp"

namespace blunted {
namespace {

// Centred low in frame, above the bottom edge where the player plates live -
// where PES puts both bugs (reference frames at 0:07 and 0:11).
constexpr float kPlateWidth = 34.0f;
constexpr float kPlateHeight = 6.4f;
constexpr float kPlateY = 78.0f;
constexpr float kCrestHeight = 4.4f;
constexpr float kTextHeight = 2.6f;
constexpr float kSubHeight = 2.0f;
constexpr float kPadX = 1.0f;

// The cut PES makes between its two graphics follows the montage itself
// (GoalSequence::Shot): the score bug comes up a beat after the goal and holds
// through the tracking shot, the scorer's card replaces it on the cut to the
// tight close-up, and the wide of the mob carries neither - which is what the
// reference shows at 0:07 and 0:11.
constexpr unsigned long kScoreIn_ms = 1500;

const Vector3 kTextColor(255, 255, 255);
const Vector3 kSubColor(186, 200, 224);
const Vector3 kOutline(6, 10, 24);

}  // namespace

Gui2GoalBug::Gui2GoalBug(Gui2WindowManager* windowManager, const std::string& name, Match* match)
    : Gui2View(windowManager, name, 0, 0, 100, 100), match(match) {}

Gui2GoalBug::~Gui2GoalBug() {}

Gui2GoalBug::Stage Gui2GoalBug::StageAt(unsigned long celebration_ms,
                                        unsigned long celebrationLength_ms) {
  if (celebrationLength_ms == 0 || celebration_ms > celebrationLength_ms) return Stage::None;
  if (celebration_ms < kScoreIn_ms) return Stage::None;  // still on the live camera
  switch (GoalSequence::ShotAt(celebration_ms, celebrationLength_ms)) {
    case GoalSequence::Shot::Tracking:
      return Stage::Score;
    case GoalSequence::Shot::Tight:
      return Stage::Scorer;
    case GoalSequence::Shot::Group:
      break;
  }
  return Stage::None;  // the mob is played clean
}

void Gui2GoalBug::Init() {
  const float x = (100.0f - kPlateWidth) * 0.5f;

  // AddView before LoadImage, and never Hide/Show a freshly built image - the
  // same two traps Gui2Banner::Init documents. Visibility is alpha here too:
  // captions carry it, the plate and crests are shown or hidden as a set.
  plate = new Gui2Image(windowManager, GetName() + "_plate", x, kPlateY, kPlateWidth, kPlateHeight);
  this->AddView(plate);
  plate->LoadImage("media/ui/pes/formation_header.png");

  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  for (int i = 0; i < 2; i++) {
    const float crestX =
        i == 0 ? x + kPadX : x + kPlateWidth - kPadX - crestWidth;
    crest[i] = new Gui2Image(windowManager, GetName() + "_crest" + int_to_str(i), crestX,
                             kPlateY + (kPlateHeight - kCrestHeight) * 0.5f, crestWidth,
                             kCrestHeight);
    this->AddView(crest[i]);
    crest[i]->LoadImage(match->GetTeam(i)->GetTeamData()->GetLogoUrl());
  }

  auto caption = [&](const std::string& suffix, float y, float height) {
    Gui2Caption* c = new Gui2Caption(windowManager, GetName() + suffix, x, y, kPlateWidth, height,
                                     " ");
    c->SetColor(kTextColor);
    c->SetOutlineColor(kOutline);
    this->AddView(c);
    c->Show();
    return c;
  };

  const float textY = kPlateY + kPlateHeight * 0.22f;
  leftText = caption("_left", textY, kTextHeight);
  centreText = caption("_centre", textY, kTextHeight);
  rightText = caption("_right", textY, kTextHeight);
  subText = caption("_sub", kPlateY + kPlateHeight * 0.62f, kSubHeight);
  subText->SetColor(kSubColor);

  ShowStage(Stage::None);
  ApplyZOrder();
}

void Gui2GoalBug::ShowStage(Stage next) {
  const bool visible = next != Stage::None;
  if (plate) {
    if (visible)
      plate->Show();
    else
      plate->Hide();
  }
  for (int i = 0; i < 2; i++) {
    if (!crest[i]) continue;
    // The score bug carries both crests, the scorer's card only his own.
    const bool wanted = visible && (next == Stage::Score ||
                                    i == std::max(0, match->GetLastGoalTeamID()));
    if (wanted)
      crest[i]->Show();
    else
      crest[i]->Hide();
  }
  if (!visible) {
    leftText->SetCaption(" ");
    centreText->SetCaption(" ");
    rightText->SetCaption(" ");
    subText->SetCaption(" ");
  }
}

void Gui2GoalBug::FillScore() {
  // Both names either side, the score in the middle - and read live, so the
  // scoreline ticks over under the scorer exactly as PES's does.
  const std::string home = match->GetTeam(0)->GetTeamData()->GetName();
  const std::string away = match->GetTeam(1)->GetTeamData()->GetName();
  leftText->SetCaption(home);
  rightText->SetCaption(away);
  centreText->SetCaption(int_to_str(match->GetMatchData()->GetGoalCount(0)) + " - " +
                         int_to_str(match->GetMatchData()->GetGoalCount(1)));
  subText->SetCaption(" ");

  const float x = (100.0f - kPlateWidth) * 0.5f;
  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  float unused, y;
  leftText->GetPosition(unused, y);
  leftText->SetPosition(x + kPadX * 2.0f + crestWidth, y);
  rightText->SetPosition(
      x + kPlateWidth - kPadX * 2.0f - crestWidth - rightText->GetTextWidthPercent(), y);
  centreText->SetPosition(50.0f - centreText->GetTextWidthPercent() * 0.5f, y);
}

void Gui2GoalBug::FillScorer() {
  Player* scorer = match->GetLastGoalScorer();
  if (!scorer || !scorer->GetPlayerData()) {
    ShowStage(Stage::None);
    return;
  }
  PlayerData* data = scorer->GetPlayerData();

  // The engine has no shirt numbers: the squad slot is what the lineup panel
  // and the player plate show, so this agrees with them (playerhud.cpp).
  int squadNumber = 0;
  {
    std::vector<Player*> activePlayers;
    match->GetTeam(std::max(0, match->GetLastGoalTeamID()))->GetActivePlayers(activePlayers);
    for (unsigned int i = 0; i < activePlayers.size(); i++)
      if (activePlayers.at(i) == scorer) {
        squadNumber = FormationGraphicLayout::SquadNumberForSlot(static_cast<int>(i));
        break;
      }
  }

  const std::string name = int_to_str(squadNumber) + " " + data->GetLastName();
  const int cm = (int)std::round(data->GetHeight() * 100.0f);
  std::string detail = int_to_str(cm) + "cm";
  const int age = data->GetAge();
  if (age != MatchPressure::unknownAge) detail += "   Age " + int_to_str(age);

  leftText->SetCaption(name);
  centreText->SetCaption(" ");
  rightText->SetCaption(detail);
  subText->SetCaption(Localization::GetInstance().Translate("goal_bug_goals_today") + "   " +
                      int_to_str(match->GetGoalsToday(scorer)));

  const float x = (100.0f - kPlateWidth) * 0.5f;
  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  float unused, y;
  leftText->GetPosition(unused, y);
  leftText->SetPosition(x + kPadX * 2.0f + crestWidth, y);
  rightText->SetPosition(x + kPlateWidth - kPadX * 2.0f - rightText->GetTextWidthPercent(), y);
  subText->GetPosition(unused, y);
  subText->SetPosition(50.0f - subText->GetTextWidthPercent() * 0.5f, y);
}

void Gui2GoalBug::Process() {
  Gui2View::Process();

  const Stage want = match->IsGoalScored()
                         ? StageAt(match->GetGoalScoredTimer(), match->GetCelebrationLength_ms())
                         : Stage::None;

  // Content is refreshed every frame in the Score stage because the scoreline
  // changes while it is on air; the scorer's card is filled once per stage.
  if (want != stage) {
    stage = want;
    ShowStage(want);
    if (want == Stage::Scorer) FillScorer();
    ApplyZOrder();
  }
  if (stage == Stage::Score) FillScore();
}

void Gui2GoalBug::ApplyZOrder() {
  const int base = GetZPriority();
  if (plate) plate->SetZPriority(base);
  for (int i = 0; i < 2; i++)
    if (crest[i]) crest[i]->SetZPriority(base + 1);
  for (Gui2Caption* c : {leftText, centreText, rightText, subText})
    if (c) c->SetZPriority(base + 2);
}

void Gui2GoalBug::SetRecursiveZPriority(int prio) {
  Gui2View::SetRecursiveZPriority(prio);
  ApplyZOrder();
}

}  // namespace blunted
