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

// Measured off the reference, pixel by pixel: implying.fun "Summer 2026 Day 1"
// at 32:09, 1920x1080 (tools used: the blue-channel mask in
// tmp/lower_12.png). PES centres one lens-shaped bar low in frame, and plays
// TWO graphics in order over the celebration:
//
//   score bug   x 24.06%..76.72% (w 52.7%, centred on 50.4%)
//               y 83.70%..90.28% (h 6.7%)
//               crests at the two tips, short names either side of a
//               cream box at x 45.00%..53.12% holding "1 - 1"
//   scorer bug  the same bar at y 83.52%..87.87% (h 4.4%) carrying
//               "<number> <NAME>", the scoring side's crest at the left tip,
//               and a thin dark strip above it reading "Today's Goals <n>"
//
// On air (blue-mask over the clip): score bug 11.75-14.00 s, scorer bug
// 14.75-21.00 s, against a goal at about 10.0 s - so the score comes up ~1.8 s
// after the ball goes in and holds 2.3 s, and the scorer's plate follows after
// a 0.75 s gap and holds ~6 s.
constexpr float kPlateWidth = 52.7f;
constexpr float kPlateX = (100.0f - kPlateWidth) * 0.5f;
constexpr float kPlateY = 83.6f;
constexpr float kScoreHeight = 6.7f;
constexpr float kScorerHeight = 4.4f;
constexpr float kTallyHeight = 2.4f;   // the "Today's Goals n" strip above
constexpr float kCrestHeight = 5.6f;
constexpr float kTextHeight = 3.0f;
constexpr float kSubHeight = 2.2f;
constexpr float kPadX = 0.6f;
// The cream box behind the scoreline, as a fraction of the bar.
constexpr float kScoreBoxWidth = 8.1f;

// The cut PES makes between its two graphics follows the montage itself
// (GoalSequence::Shot): the score bug comes up a beat after the goal and holds
// through the tracking shot, the scorer's card replaces it on the cut to the
// tight close-up, and the wide of the mob carries neither - which is what the
// reference shows at 0:07 and 0:11.

const Vector3 kTextColor(255, 255, 255);
const Vector3 kSubColor(186, 200, 224);
const Vector3 kOutline(6, 10, 24);
const Vector3 kScoreColor(22, 34, 72);

}  // namespace

Gui2GoalBug::Gui2GoalBug(Gui2WindowManager* windowManager, const std::string& name, Match* match)
    : Gui2View(windowManager, name, 0, 0, 100, 100), match(match) {}

Gui2GoalBug::~Gui2GoalBug() {}

Gui2GoalBug::Stage Gui2GoalBug::StageAt(unsigned long celebration_ms,
                                        unsigned long celebrationLength_ms) const {
  // The reference plays both graphics, in order, on its own clock (see the
  // measurements at the top of this file): nothing for the first beat, the
  // score, a breath of nothing, then the scorer for the rest of the walk.
  if (celebrationLength_ms == 0 || celebration_ms > celebrationLength_ms) return Stage::None;
  if (celebration_ms < GoalSequence::kScoreBugIn_ms) return Stage::None;
  if (celebration_ms < GoalSequence::kScoreBugOut_ms) return Stage::Score;
  if (celebration_ms < GoalSequence::kScorerBugIn_ms) return Stage::None;
  // Past that the scorer's plate holds as long as the celebration is on the
  // celebration itself: on the director's walk that is the run, the
  // celebration, the hug and the approach, and not the replay that follows.
  if (!match->GetGoalBeatsEmpty()) {
    switch (match->GoalWalkPhase()) {
      case GoalDirector::Phase::Run:
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
  const float x = kPlateX;

  // AddView before LoadImage, and never Hide/Show a freshly built image - the
  // same two traps Gui2Banner::Init documents. Visibility is alpha here too:
  // captions carry it, the plate and crests are shown or hidden as a set.
  // TWO bars, each built at its own height and never resized: Gui2Image::SetSize
  // resizes the surface and redraws it empty, so a bar that changes height
  // between the two stages simply vanishes (the same family of trap as
  // Hide/Show on a fresh image, which banner.cpp documents). The one that is
  // not wanted is moved off-screen, as the crests are.
  plate = new Gui2Image(windowManager, GetName() + "_plate", x, kPlateY, kPlateWidth, kScoreHeight);
  this->AddView(plate);
  plate->LoadImage("media/ui/pes/formation_header.png");
  plate->Show();  // once; visibility moves the container afterwards

  plateSlim = new Gui2Image(windowManager, GetName() + "_plateslim", x, kPlateY, kPlateWidth,
                            kScorerHeight);
  this->AddView(plateSlim);
  plateSlim->LoadImage("media/ui/pes/formation_header.png");
  plateSlim->Show();

  // The cream box the scoreline sits in, centred on the bar. Off-screen in
  // the scorer stage, like the crest that is not wanted.
  scoreBox = new Gui2Image(windowManager, GetName() + "_scorebox",
                           50.0f - kScoreBoxWidth * 0.5f, kPlateY, kScoreBoxWidth, kScoreHeight);
  this->AddView(scoreBox);
  scoreBox->LoadImage("media/ui/pes/score_box.png");
  scoreBox->Show();

  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  for (int i = 0; i < 2; i++) {
    // At the tips, overhanging the bar a little, as the reference has them.
    const float crestX = i == 0 ? x - crestWidth * 0.35f
                                : x + kPlateWidth - crestWidth * 0.65f;
    crest[i] = new Gui2Image(windowManager, GetName() + "_crest" + int_to_str(i), crestX,
                             kPlateY + (kScoreHeight - kCrestHeight) * 0.5f, crestWidth,
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

  const float textY = kPlateY + (kScoreHeight - kTextHeight) * 0.5f;
  leftText = caption("_left", textY, kTextHeight);
  centreText = caption("_centre", textY, kTextHeight);
  rightText = caption("_right", textY, kTextHeight);
  // "Today's Goals n" rides ABOVE the bar in the scorer stage.
  subText = caption("_sub", kPlateY - kTallyHeight - 0.3f, kSubHeight);
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
  // The bar is taller in the score stage (it holds crests and a score box) and
  // slimmer in the scorer stage, where the tally strip sits above it.
  if (plate) {
    float px, py;
    plate->GetPosition(px, py);
    plate->SetPosition(next == Stage::Score ? kPlateX : -200.0f, kPlateY);
  }
  if (plateSlim) plateSlim->SetPosition(next == Stage::Scorer ? kPlateX : -200.0f, kPlateY);
  // The score box belongs to the score stage only.
  if (scoreBox) {
    float bx, by;
    scoreBox->GetPosition(bx, by);
    scoreBox->SetPosition(next == Stage::Score ? 50.0f - kScoreBoxWidth * 0.5f : -50.0f, by);
  }
  for (int i = 0; i < 2; i++) {
    if (!crest[i]) continue;
    const bool wanted = visible && (next == Stage::Score ||
                                    i == std::max(0, match->GetLastGoalTeamID()));
    float cx, cy;
    crest[i]->GetPosition(cx, cy);
    // The score bug carries both crests at their own tips; the scorer's plate
    // carries only his, and the reference puts it at the LEFT tip whichever
    // side scored.
    const float wantedX = next == Stage::Score ? crestHomeX[i] : crestHomeX[0];
    const float wantedY = next == Stage::Scorer
                              ? kPlateY + (kScorerHeight - kCrestHeight) * 0.5f
                              : crestHomeY[i];
    crest[i]->SetPosition(wanted ? wantedX : -50.0f, wanted ? wantedY : cy);
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
  // Navy on the cream box, white everywhere else on the bar - the reference's
  // only two-colour element.
  centreText->SetColor(kScoreColor);
  centreText->SetOutlineColor(Vector3(230, 228, 220));
  subText->SetCaption(" ");

  // Halfway between the crest at the tip and the score box at the centre, the
  // way the reference sets them.
  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  float unused, y;
  leftText->GetPosition(unused, y);
  const float innerLeft = kPlateX + crestWidth * 0.65f + kPadX;
  const float innerRight = kPlateX + kPlateWidth - crestWidth * 0.65f - kPadX;
  const float boxLeft = 50.0f - kScoreBoxWidth * 0.5f;
  const float boxRight = 50.0f + kScoreBoxWidth * 0.5f;
  leftText->SetPosition((innerLeft + boxLeft) * 0.5f - leftText->GetTextWidthPercent() * 0.5f, y);
  rightText->SetPosition((boxRight + innerRight) * 0.5f - rightText->GetTextWidthPercent() * 0.5f,
                         y);
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

  // Number and name, centred on the bar, with the day's tally on the strip
  // above it - "Today's Goals 1" in the reference, over "13 RAGE".
  const std::string name = int_to_str(squadNumber) + "  " + data->GetLastName();

  leftText->SetCaption(" ");
  rightText->SetCaption(" ");
  centreText->SetColor(kTextColor);
  centreText->SetOutlineColor(kOutline);
  centreText->SetCaption(name);
  subText->SetCaption(Localization::GetInstance().Translate("goal_todays_goals") + " " +
                      int_to_str(match->GetGoalsToday(scorer)));

  float unused, y;
  centreText->GetPosition(unused, y);
  centreText->SetPosition(50.0f - centreText->GetTextWidthPercent() * 0.5f,
                          kPlateY + (kScorerHeight - kTextHeight) * 0.5f);
  subText->SetPosition(50.0f - subText->GetTextWidthPercent() * 0.5f,
                       kPlateY - kTallyHeight - 0.3f);
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
  if (plateSlim) plateSlim->SetZPriority(base);
  if (scoreBox) scoreBox->SetZPriority(base + 1);
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
