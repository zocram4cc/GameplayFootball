#include "goalbug.hpp"

#include "onthepitch/match.hpp"

#include "onthepitch/goaldirector.hpp"

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

// The ribbon sits bottom-LEFT above the home plate, a short plate in the
// scoring team's colour carrying the name alone (VGL 26 day 12 frames at
// 5:10:11).
constexpr float kPlateWidth = 26.0f;
constexpr float kPlateHeight = 4.6f;
constexpr float kPlateY = 84.0f;
constexpr float kCrestHeight = 4.4f;
constexpr float kTextHeight = 2.6f;
constexpr float kSubHeight = 2.0f;
constexpr float kPadX = 1.0f;

// The cut PES makes between its two graphics follows the montage itself
// (GoalSequence::Shot): the score bug comes up a beat after the goal and holds
// through the tracking shot, the scorer's card replaces it on the cut to the
// tight close-up, and the wide of the mob carries neither - which is what the
// reference shows at 0:07 and 0:11.

const Vector3 kTextColor(255, 255, 255);
const Vector3 kSubColor(186, 200, 224);
const Vector3 kOutline(6, 10, 24);

}  // namespace

Gui2GoalBug::Gui2GoalBug(Gui2WindowManager* windowManager, const std::string& name, Match* match)
    : Gui2View(windowManager, name, 0, 0, 100, 100), match(match) {}

Gui2GoalBug::~Gui2GoalBug() {}

Gui2GoalBug::Stage Gui2GoalBug::StageAt(unsigned long celebration_ms,
                                        unsigned long celebrationLength_ms) const {
  // PES 2021 (VGL 26 day 12, 5:09:59): ONE graphic, the scorer's name ribbon,
  // rising on the two-shot about four seconds after the goal and staying
  // through the mob; the stand cuts and the replay are clean. The score bug
  // and the scorer card of the older reference are not what this broadcast
  // draws.
  if (celebrationLength_ms == 0 || celebration_ms > celebrationLength_ms) return Stage::None;
  if (celebration_ms < GoalSequence::kRibbonIn_ms) return Stage::None;
  // On the director's walk the ribbon rides the middle beats - the
  // celebration, the hug, the approach - not the montage's shot machine.
  if (!match->GetGoalBeatsEmpty()) {
    switch (match->GoalWalkPhase()) {
      case GoalDirector::Phase::Celebrate:
      case GoalDirector::Phase::Hug:
      case GoalDirector::Phase::Approach:
        return Stage::Scorer;
      default:
        return Stage::None;
    }
  }
  switch (GoalSequence::ShotAt(celebration_ms, celebrationLength_ms)) {
    case GoalSequence::Shot::TwoShot:
    case GoalSequence::Shot::MobClose:
    case GoalSequence::Shot::MobWide:
      return Stage::Scorer;
    default:
      break;
  }
  return Stage::None;
}

void Gui2GoalBug::Init() {
  const float x = 2.0f;

  // AddView before LoadImage, and never Hide/Show a freshly built image - the
  // same two traps Gui2Banner::Init documents. Visibility is alpha here too:
  // captions carry it, the plate and crests are shown or hidden as a set.
  plate = new Gui2Image(windowManager, GetName() + "_plate", x, kPlateY, kPlateWidth, kPlateHeight);
  this->AddView(plate);
  plate->LoadImage("media/ui/pes/formation_header.png");
  plate->Show();  // once; visibility moves the container afterwards

  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  for (int i = 0; i < 2; i++) {
    const float crestX =
        i == 0 ? x + kPadX : x + kPlateWidth - kPadX - crestWidth;
    crest[i] = new Gui2Image(windowManager, GetName() + "_crest" + int_to_str(i), crestX,
                             kPlateY + (kPlateHeight - kCrestHeight) * 0.5f, crestWidth,
                             kCrestHeight);
    this->AddView(crest[i]);
    crest[i]->LoadImage(match->GetTeam(i)->GetTeamData()->GetLogoUrl());
    crest[i]->Show();  // once; see ShowStage
    crest[i]->GetPosition(crestHomeX[i], crestHomeY[i]);
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
  // Never Hide/Show the images: cycling visibility on a freshly created
  // Gui2Image leaves it permanently blank (banner.cpp). They are shown once
  // at Init; the whole view moves off-screen when there is nothing to say.
  // The score bug carries both crests, the scorer's card only his own: the
  // unwanted crest is blanked by moving it off its plate, not by hiding it.
  this->SetPosition(0.0f, visible ? 0.0f : 200.0f);
  for (int i = 0; i < 2; i++) {
    if (!crest[i]) continue;
    const bool wanted = visible && (next == Stage::Score ||
                                    i == std::max(0, match->GetLastGoalTeamID()));
    float cx, cy;
    crest[i]->GetPosition(cx, cy);
    crest[i]->SetPosition(wanted ? crestHomeX[i] : -50.0f, wanted ? crestHomeY[i] : cy);
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

  // The ribbon: number and name, nothing else. Height, age and the day's
  // tally were the older reference's card; the 2021 broadcast shows the name.
  const std::string name = int_to_str(squadNumber) + "  " + data->GetLastName();

  leftText->SetCaption(name);
  centreText->SetCaption(" ");
  rightText->SetCaption(" ");
  subText->SetCaption(" ");

  const float x = 2.0f;
  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  float unused, y;
  leftText->GetPosition(unused, y);
  leftText->SetPosition(x + kPadX * 2.0f + crestWidth, y);
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
