// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "gameover.hpp"

#include <algorithm>

#include <cmath>
#include <ctime>

#include "../../data/matchanalytics.hpp"
#include "../../data/matchhistory.hpp"
#include "../career/career_database.hpp"
#include "../pagefactory.hpp"
#include "main.hpp"
#include "../../remotecontrolmode.hpp"
#include "utils/gui2/events.hpp"
#include "utils/localization.hpp"

using namespace blunted;

namespace {

constexpr unsigned long kMenuSmokeQuitDelay_ms = 1000;

bool MenuSmokeFullMatchEnabled() {
  return GetConfiguration()->GetBool("menu_smoke_test_full_match", false);
}

}  // namespace

GameOverPage::GameOverPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      match(nullptr),
      pageCreatedTime_ms(EnvironmentManager::GetInstance().GetTime_ms()),
      autoQuitTriggered(false) {
  match = GetGameTask()->GetMatch();
  if (!match) {
    return;
  }
  match->Pause(true);
  // The same card as half time. PRESENTATION_SPEC 3.4 calls for one template
  // for both breaks, and statsoverlay.hpp says so too, but this page had kept
  // its own: a menu frame, its own score caption, and five hand-laid rows of
  // the same six numbers - two screens to maintain that a viewer cannot tell
  // apart (owner, 05-09). The crests, the possession bar and the heatmap come
  // with the card; only the title and the actions differ.
  //
  // Lighter than a menu panel: the closing ceremony - this ground's crowd, the
  // winners, the walk over, the team photo - plays behind it, as on the
  // broadcast.
  card = new Gui2StatsOverlay(windowManager, match, "gameover_card");
  this->AddView(card);
  card->SetTitle(Localization::GetInstance().Translate("gameover_full_time") + "   " +
                 int_to_str(match->GetMatchData()->GetGoalCount(0)) + " - " +
                 int_to_str(match->GetMatchData()->GetGoalCount(1)));
  card->UpdateStats();
  card->Show();

  // The action bar in the band under the card, laid out exactly as the
  // half-time page's (phasemenu.cpp).
  float cardX, cardY, cardW, cardH;
  card->GetPosition(cardX, cardY);
  card->GetSize(cardW, cardH);
  const float barHeight = 5.5f;
  const float barY = std::min(cardY + cardH + 1.5f, 100.0f - barHeight - 1.5f);
  const float barWidth = cardW * 0.7f;
  const float barX = (100.0f - barWidth) * 0.5f;
  Gui2Image* bar =
      new Gui2Image(windowManager, "gameover_actionbar", barX, barY, barWidth, barHeight);
  this->AddView(bar);
  bar->LoadImage("media/ui/pes/banner_panel.png");
  bar->Show();

  const float buttonWidth = barWidth * 0.46f;
  const float buttonHeight = barHeight * 0.64f;
  const float buttonY = barY + (barHeight - buttonHeight) * 0.5f;
  const float gap = barWidth * 0.04f;
  Gui2Button* buttonHistory =
      new Gui2Button(windowManager, "button_gameover_history", barX + gap, buttonY, buttonWidth,
                     buttonHeight, Localization::GetInstance().Translate("gameover_match_history"));
  buttonOkay = new Gui2Button(windowManager, "button_gameover_ok",
                              barX + barWidth - gap - buttonWidth, buttonY, buttonWidth,
                              buttonHeight, Localization::GetInstance().Translate("gameover_continue"));
  this->AddView(buttonHistory);
  this->AddView(buttonOkay);
  buttonHistory->Show();
  buttonOkay->Show();
  buttonOkay->sig_OnClick.connect([this](...) { GoMainMenu(); });
  buttonHistory->sig_OnClick.connect([this](...) {
    Properties props;
    CreatePage((int)e_PageID_MatchHistory, props);
  });

  buttonOkay->SetFocus();

  // Auto-save match result to history. Guarded so re-entering this page (e.g.
  // returning from Match History) does not append a duplicate record.
  if (!match->GetMatchData()->IsHistorySaved()) {
    match->GetMatchData()->SetHistorySaved(true);

    float poss1 = match->GetMatchData()->GetPossessionTime_ms(0);
    float poss2 = match->GetMatchData()->GetPossessionTime_ms(1);
    float totalPoss = poss1 + poss2;

    MatchHistoryEntry entry;
    entry.id = 0;

    time_t now = time(nullptr);
    char tsbuf[32];
    std::tm localTime = {};
    if (blunted::GetLocalTime(now, localTime) &&
        strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", &localTime) > 0) {
      entry.timestamp = tsbuf;
    } else {
      entry.timestamp = "1970-01-01 00:00:00";
    }

    entry.team1_name = match->GetTeam(0)->GetTeamData()->GetName();
    entry.team2_name = match->GetTeam(1)->GetTeamData()->GetName();
    entry.score1 = match->GetMatchData()->GetGoalCount(0);
    entry.score2 = match->GetMatchData()->GetGoalCount(1);
    entry.match_time_ms = (int)match->GetMatchTime_ms();
    entry.possession1_pct = (totalPoss > 0) ? poss1 / totalPoss * 100.0f : 50.0f;
    entry.possession2_pct = (totalPoss > 0) ? poss2 / totalPoss * 100.0f : 50.0f;
    entry.shots1 = match->GetMatchData()->GetShots(0);
    entry.shots2 = match->GetMatchData()->GetShots(1);
    entry.shots_on_target1 = match->GetMatchData()->GetShotsOnTarget(0);
    entry.shots_on_target2 = match->GetMatchData()->GetShotsOnTarget(1);
    entry.passes1 = match->GetMatchData()->GetPassAttempts(0);
    entry.passes2 = match->GetMatchData()->GetPassAttempts(1);
    entry.passes_completed1 = match->GetMatchData()->GetPassesCompleted(0);
    entry.passes_completed2 = match->GetMatchData()->GetPassesCompleted(1);
    entry.fouls1 = match->GetMatchData()->GetFouls(0);
    entry.fouls2 = match->GetMatchData()->GetFouls(1);

    MatchHistory::EnsureTable();
    MatchHistory::SaveMatch(entry);
  }

  this->Show();
}

GameOverPage::~GameOverPage() {}

namespace {

int PossessionPercent(MatchData* matchData, int teamID) {
  const float mine = static_cast<float>(matchData->GetPossessionTime_ms(teamID));
  const float total = mine + static_cast<float>(matchData->GetPossessionTime_ms(1 - teamID));
  return total > 0.0f ? static_cast<int>(std::round(mine / total * 100.0f)) : 50;
}

}  // namespace

void GameOverPage::Process() {
  Gui2Page::Process();

  // The closing ceremony plays behind this page, so a run that wants to see it says
  // how long to hold before quitting ("menu_smoke_gameover_hold_ms").
  const unsigned long hold_ms = static_cast<unsigned long>(std::max(
      0, GetConfiguration()->GetInt("menu_smoke_gameover_hold_ms",
                                    (int)kMenuSmokeQuitDelay_ms)));
  if (!autoQuitTriggered && MenuSmokeFullMatchEnabled() &&
      EnvironmentManager::GetInstance().GetTime_ms() >= pageCreatedTime_ms + hold_ms) {
    autoQuitTriggered = true;
    printf("[menu-smoke] Full match complete: %s %i - %i %s\n",
           match->GetTeam(0)->GetTeamData()->GetName().c_str(),
           match->GetMatchData()->GetGoalCount(0), match->GetMatchData()->GetGoalCount(1),
           match->GetTeam(1)->GetTeamData()->GetName().c_str());
    // Balance line: shots, shots on target and expected goals per side, so the
    // feel of a match ("offensive, flowing, 15-20 shots") can be measured rather
    // than guessed at.
    MatchData* matchData = match->GetMatchData();
    const int passes1 = matchData->GetPassAttempts(0);
    const int passes2 = matchData->GetPassAttempts(1);
    const int passAccuracy1 = passes1 > 0 ? (matchData->GetPassesCompleted(0) * 100) / passes1 : 0;
    const int passAccuracy2 = passes2 > 0 ? (matchData->GetPassesCompleted(1) * 100) / passes2 : 0;
    const int cleanPct1 = passes1 > 0 ? (matchData->GetCleanCompletions(0) * 100) / passes1 : 0;
    const int cleanPct2 = passes2 > 0 ? (matchData->GetCleanCompletions(1) * 100) / passes2 : 0;
    printf("[balance-passing] passes %i-%i | accuracy %i%%-%i%% | clean %i%%-%i%% | clearances %i-%i\n",
           passes1, passes2, passAccuracy1, passAccuracy2, cleanPct1, cleanPct2,
           matchData->GetClearances(0), matchData->GetClearances(1));
#ifndef NDEBUG
    // Questionable-play deny list, debug-only: no quality guarantee should depend on
    // somebody counting frames by hand.
    printf("[deny-list] pass-to-opponent %i-%i | gk-lost %i-%i | own-third-giveaway %i-%i | bad-plays %i\n",
           matchData->GetBadPassToOpponent(0), matchData->GetBadPassToOpponent(1),
           matchData->GetGoalkeeperLost(0), matchData->GetGoalkeeperLost(1),
           matchData->GetOwnThirdGiveaway(0), matchData->GetOwnThirdGiveaway(1),
           matchData->GetBadPlayTotal());
    // Failure breakdown: where the incomplete passes actually went.
    printf("[pass-fail] intercept %i-%i out %i-%i trap %i-%i\n",
           matchData->GetPassFailIntercept(0), matchData->GetPassFailIntercept(1),
           matchData->GetPassFailOutOfBounds(0), matchData->GetPassFailOutOfBounds(1),
           matchData->GetPassFailBadTrap(0), matchData->GetPassFailBadTrap(1));
    // Touches that never reached RecordBallTouch while a pass was pending:
    // the sink the [pass-fail] breakdown cannot see. hostile = an opponent's
    // body killed our pass in flight; kinds are interfere/deflect/slide/
    // collision/keeper.
    printf("[pass-ghost] friendly %i/%i/%i/%i/%i-%i/%i/%i/%i/%i | hostile %i/%i/%i/%i/%i-%i/%i/%i/%i/%i\n",
           matchData->GetGhostTouch(0, 0, 0), matchData->GetGhostTouch(0, 0, 1),
           matchData->GetGhostTouch(0, 0, 2), matchData->GetGhostTouch(0, 0, 3),
           matchData->GetGhostTouch(0, 0, 4), matchData->GetGhostTouch(0, 1, 0),
           matchData->GetGhostTouch(0, 1, 1), matchData->GetGhostTouch(0, 1, 2),
           matchData->GetGhostTouch(0, 1, 3), matchData->GetGhostTouch(0, 1, 4),
           matchData->GetGhostTouch(1, 0, 0), matchData->GetGhostTouch(1, 0, 1),
           matchData->GetGhostTouch(1, 0, 2), matchData->GetGhostTouch(1, 0, 3),
           matchData->GetGhostTouch(1, 0, 4), matchData->GetGhostTouch(1, 1, 0),
           matchData->GetGhostTouch(1, 1, 1), matchData->GetGhostTouch(1, 1, 2),
           matchData->GetGhostTouch(1, 1, 3), matchData->GetGhostTouch(1, 1, 4));
    printf("[pass-gk] %i-%i\n", matchData->GetPassGoalkeeperCatch(0),
           matchData->GetPassGoalkeeperCatch(1));
    printf("[pass-restart] %i-%i\n", matchData->GetPassRestart(0),
           matchData->GetPassRestart(1));
    // Chosen pass-length distribution and support-web width for the whole
    // match, next to the failure breakdown: selection and execution on one
    // card.
    printf("[pass-dist] bands %i/%i/%i/%i/%i/%i | rms %.1fm-%.1fm | web %.1fm-%.1fm\n",
           matchData->GetPassDistanceBand(0, 0), matchData->GetPassDistanceBand(0, 1),
           matchData->GetPassDistanceBand(0, 2), matchData->GetPassDistanceBand(0, 3),
           matchData->GetPassDistanceBand(0, 4), matchData->GetPassDistanceBand(0, 5),
           matchData->GetPassDistanceMeanRms_m(0), matchData->GetPassDistanceMeanRms_m(1),
           matchData->GetSupportWebWidthMean_m(0), matchData->GetSupportWebWidthMean_m(1));
#endif
    printf(
        "[balance] shots %i-%i | on target %i-%i | xg %.2f-%.2f | goals %i-%i | possession "
        "%i%%-%i%%\n",
        matchData->GetShots(0), matchData->GetShots(1), matchData->GetShotsOnTarget(0),
        matchData->GetShotsOnTarget(1), MatchAnalytics::GetExpectedGoals(match->GetShotTally(), 0),
        MatchAnalytics::GetExpectedGoals(match->GetShotTally(), 1), matchData->GetGoalCount(0),
        matchData->GetGoalCount(1), PossessionPercent(matchData, 0),
        PossessionPercent(matchData, 1));
    if (RemoteControlMode::IsActive()) {
      // The rig lives on: back to the waiting page for the next schedule. The
      // launch keys the schedule wrote are cleared so the main menu does not
      // smoke-drive itself into a rematch on the way.
      GetConfiguration()->SetBool("menu_smoke_test_full_match", false);
      printf("[remote-control] match over, returning to the waiting page\n");
      GoMainMenu();
      return;
    }
    printf("[menu-smoke] Full-match verification succeeded, quitting test run\n");
    EnvironmentManager::GetInstance().SignalQuit();
  }
}

void GameOverPage::ProcessWindowingEvent(WindowingEvent* event) {
  if (event->IsEscape()) {
    // The match is over; ESC should return to the main menu like "Continue"
    // rather than walking back into the (now finished) match/game flow.
    GoMainMenu();
    return;
  } else {
    event->Ignore();
  }
}

void GameOverPage::GoRematch() {
  windowManager->GetPagePath()->Clear();

  GetGameTask()->Action(e_GameTaskMessage_StopMatch);
  GetGameTask()->Action(e_GameTaskMessage_StartMatch);

  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_Game, properties, 0);
  delete this;
}

void GameOverPage::GoMainMenu() {
  // Preserve the finished 3D match in career bookkeeping before leaving the game flow.
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
  if (resumeCareer) {
    GetConfiguration()->SetBool("career_resume_hub", true);
  }
  this->Exit();
  GetMenuTask()->SetMenuAction(e_MenuAction_Menu);
  delete this;
}
