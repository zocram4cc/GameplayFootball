// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "gameover.hpp"

#include "menuicons.hpp"

#include <algorithm>

#include <cmath>
#include <ctime>
#include <filesystem>

#include "../../data/matchanalytics.hpp"
#include "../../data/matchhistory.hpp"
#include "../../data/matchstatsexport.hpp"
#include "../../onthepitch/entrancecast.hpp"
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

int PossessionPercent(MatchData* matchData, int teamID);

// The engine anchors its working directory on the tree that holds databases/
// (<repo>/data - see AnchorWorkingDirectory in main.cpp), so a bare
// "data/stats" config value resolves under the CURRENT directory - which
// after that anchor already IS <repo>/data, producing <repo>/data/data/stats.
// Anchor against the repo root instead, the same root every other
// documented generated-file path (e.g. MatchHistory) is meant to sit under.
std::filesystem::path ResolveStatsDir(const std::string& configured) {
  namespace fs = std::filesystem;
  fs::path root = fs::current_path();
  if (fs::exists(root / "databases") && fs::exists(root.parent_path() / "data"))
    root = root.parent_path();
  return root / configured;
}

std::string TeamDisplayName(TeamData* team) {
  const std::string shortName = team->GetShortName();
  return shortName.empty() ? team->GetName() : shortName;
}

// Assembles the frozen match-stats document from MatchData/MatchAnalytics -
// the source of truth already accumulated on the pitch. See
// src/data/matchstatsexport.hpp for the schema and the wikitext this feeds.
MatchStatsDocument BuildMatchStatsDocument(Match* match) {
  MatchData* matchData = match->GetMatchData();

  std::tm localTime = {};
  char dateBuf[16] = "1970-01-01";
  char timeBuf[8] = "000000";
  if (blunted::GetLocalTime(time(nullptr), localTime)) {
    strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &localTime);
    strftime(timeBuf, sizeof(timeBuf), "%H%M%S", &localTime);
  }

  TeamData* homeData = match->GetTeam(0)->GetTeamData();
  TeamData* awayData = match->GetTeam(1)->GetTeamData();

  MatchStatsDocument document;
  document.date = dateBuf;
  document.matchId = std::string(dateBuf) + "-" + std::to_string(homeData->GetDatabaseID()) +
                     "-" + std::to_string(awayData->GetDatabaseID()) + "-" + timeBuf;
  document.competition = "Friendly";
  document.stadium = EntranceCast::StadiumToken(GetConfiguration()->Get("stadium_object", ""));
  document.durationMinutes =
      static_cast<int>(std::round(GetConfiguration()->GetReal("match_duration_minutes", 0.0f)));

  MatchStatsTeam teams[2];
  for (int i = 0; i < 2; ++i) {
    TeamData* teamData = i == 0 ? homeData : awayData;
    teams[i].databaseID = teamData->GetDatabaseID();
    teams[i].name = TeamDisplayName(teamData);
    teams[i].score = matchData->GetGoalCount(i);
    teams[i].shots = matchData->GetShots(i);
    teams[i].shotsOnTarget = matchData->GetShotsOnTarget(i);
    teams[i].saves = matchData->GetSaves(i);
    teams[i].corners = matchData->GetCorners(i);
    teams[i].fouls = matchData->GetFouls(i);
    teams[i].offsides = matchData->GetOffsides(i);
    teams[i].possessionPercent = PossessionPercent(matchData, i);
    teams[i].passes = matchData->GetPassAttempts(i);
    teams[i].passesCompleted = matchData->GetPassesCompleted(i);
    if (teams[i].passes > 0)
      teams[i].passAccuracyPercent = static_cast<int>(
          std::round(teams[i].passesCompleted * 100.0f / teams[i].passes));
    teams[i].expectedGoals = MatchAnalytics::GetExpectedGoals(match->GetShotTally(), i);
  }

  for (const MatchData::Event& event : matchData->GetEvents()) {
    if (event.teamID != 0 && event.teamID != 1) continue;
    switch (event.kind) {
      case MatchData::Event::Goal:
        teams[event.teamID].goals.push_back(
            {event.minute, event.text, /*ownGoal=*/false, event.penalty});
        break;
      case MatchData::Event::OwnGoal:
        teams[event.teamID].goals.push_back(
            {event.minute, event.text, /*ownGoal=*/true, /*penalty=*/false});
        break;
      case MatchData::Event::YellowCard:
        teams[event.teamID].cards.push_back({event.minute, event.text, MatchCardEvent::Yellow});
        break;
      case MatchData::Event::RedCard:
        teams[event.teamID].cards.push_back({event.minute, event.text, MatchCardEvent::Red});
        break;
      default:
        break;
    }
  }

  document.teams = {teams[0], teams[1]};
  return document;
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
  card->SetTitle(MenuIcons::BreakTitle(Localization::GetInstance().Translate("gameover_full_time"),
                                       MenuIcons::Clock(match->GetMatchTime_ms()),
                                       match->GetMatchData()->GetGoalCount(0),
                                       match->GetMatchData()->GetGoalCount(1)));
  // Where the paged cards sit, so full time lands where the pause menu was.
  float cardX0, cardY0;
  card->GetPosition(cardX0, cardY0);
  card->SetPosition(cardX0, 4.0f);
  card->UpdateStats();
  card->Show();

  // PES shows this card ALONE, with a Confirm prompt, and the actions live on
  // the result page that follows (VGL 26 day 12, 5:15:08 then 5:15:13;
  // docs/VGL26_DAY12_REFERENCE.md §3). The five buttons used to sit under the
  // card here, which made one screen of PES's two.
  float cardX, cardY, cardW, cardH;
  card->GetPosition(cardX, cardY);
  card->GetSize(cardW, cardH);
  const float promptY = cardY + cardH + MenuIcons::kCardToBarGap;
  buttonOkay = new Gui2Button(windowManager, "button_gameover_ok", (100.0f - 24.0f) * 0.5f,
                              promptY, 24.0f, 3.2f,
                              Localization::GetInstance().Translate("gameover_confirm"));
  buttonOkay->SetQuietFocus(true);
  this->AddView(buttonOkay);
  buttonOkay->Show();
  buttonOkay->sig_OnClick.connect([this](...) { GoResult(); });
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

    const MatchStatsDocument statsDocument = BuildMatchStatsDocument(match);
    const std::string statsDir =
        ResolveStatsDir(GetConfiguration()->Get("stats_dir", "data/stats/")).string();
    std::string statsError;
    if (!WriteMatchStatsFiles(statsDocument, statsDir, statsError))
      printf("[match-stats] failed to write match stats: %s\n", statsError.c_str());
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
    // Failure breakdown: where the incomplete passes actually went. Release-safe
    // so the accuracy target can be worked on in the build the batches run.
    printf("[pass-fail] intercept %i-%i out %i-%i trap %i-%i\n",
           matchData->GetPassFailIntercept(0), matchData->GetPassFailIntercept(1),
           matchData->GetPassFailOutOfBounds(0), matchData->GetPassFailOutOfBounds(1),
           matchData->GetPassFailBadTrap(0), matchData->GetPassFailBadTrap(1));
#ifndef NDEBUG
    // Questionable-play deny list, debug-only: no quality guarantee should depend on
    // somebody counting frames by hand.
    printf("[deny-list] pass-to-opponent %i-%i | gk-lost %i-%i | own-third-giveaway %i-%i | bad-plays %i\n",
           matchData->GetBadPassToOpponent(0), matchData->GetBadPassToOpponent(1),
           matchData->GetGoalkeeperLost(0), matchData->GetGoalkeeperLost(1),
           matchData->GetOwnThirdGiveaway(0), matchData->GetOwnThirdGiveaway(1),
           matchData->GetBadPlayTotal());
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
        // Saves are in here because they are the lever between on-target and
        // goals: on-target minus saves minus goals is what the keeper never
        // touched, and a keeper stopping 85% of the target is a different
        // problem from a side that cannot hit it (the stats card shows the same
        // number, so this is telemetry, not instrumentation).
        "[balance] shots %i-%i | on target %i-%i | saves %i-%i | xg %.2f-%.2f | goals %i-%i | "
        // `crossings` is the same question asked of the ball's own positions
        // instead of the launch sum: how many shots actually reached the
        // goal-line plane, and how many of those inside the posts. The
        // projection is clamped by the band it checks, so only this column can
        // confirm the band.
        "crossings %i-%i (in frame %i-%i) | "
        // Crossings with no shot in the flight window (a pass or a deflection
        // reaching the line): the audit that `goals <= in frame` holds.
        "crossings-no-shot %i-%i (in frame %i-%i) | "
        // Football's own definition: a shot is on target when it went in or the
        // keeper had to stop it. The saves above are gated on the ball actually
        // heading inside the frame, so this is not the projection restated - and
        // it is the column to compare against the ~20-30% a real match sees.
        "on target (goals+saves) %i-%i | "
        // The gate's audit: keeper touches that were NOT saves because the ball
        // was not heading inside the frame. If this is ~0 the gate is rejecting
        // nothing and the column above is just "any keeper touch".
        "keeper collections %i-%i | "
        "possession %i%%-%i%%\n",
        matchData->GetShots(0), matchData->GetShots(1), matchData->GetShotsOnTarget(0),
        matchData->GetShotsOnTarget(1), matchData->GetSaves(0), matchData->GetSaves(1),
        MatchAnalytics::GetExpectedGoals(match->GetShotTally(), 0),
        MatchAnalytics::GetExpectedGoals(match->GetShotTally(), 1), matchData->GetGoalCount(0),
        matchData->GetGoalCount(1), matchData->GetGoalLineCrossings(0),
        matchData->GetGoalLineCrossings(1), matchData->GetGoalLineCrossingsOnTarget(0),
        matchData->GetGoalLineCrossingsOnTarget(1), matchData->GetGoalLineCrossingsUnattributed(0),
        matchData->GetGoalLineCrossingsUnattributed(1),
        matchData->GetGoalLineCrossingsUnattributedOnTarget(0),
        matchData->GetGoalLineCrossingsUnattributedOnTarget(1),
        // Crossed: `saves[i]` are the saves team i's keeper MADE, so team i's
        // own shots on target are its goals plus the saves the OTHER keeper made.
        matchData->GetGoalCount(0) + matchData->GetSaves(1),
        matchData->GetGoalCount(1) + matchData->GetSaves(0), matchData->GetKeeperCollections(0),
        matchData->GetKeeperCollections(1), PossessionPercent(matchData, 0),
        PossessionPercent(matchData, 1));
    // Shot quality, the other half of the balance question: where the shots
    // came from (bands: inside 11 m, the rest of the box, just outside, long
    // range) and, for the ones the projection calls off target, which way they
    // missed. `^[balance` is what the batch runner greps, so this rides along
    // with the line above.
    printf("[balance-shots] bands %i/%i/%i/%i-%i/%i/%i/%i | mean %.1fm-%.1fm | "
           "wide %i-%i | over %i-%i | short %i-%i\n",
           matchData->GetShotDistanceBand(0, 0), matchData->GetShotDistanceBand(0, 1),
           matchData->GetShotDistanceBand(0, 2), matchData->GetShotDistanceBand(0, 3),
           matchData->GetShotDistanceBand(1, 0), matchData->GetShotDistanceBand(1, 1),
           matchData->GetShotDistanceBand(1, 2), matchData->GetShotDistanceBand(1, 3),
           matchData->GetShotDistanceMean_m(0), matchData->GetShotDistanceMean_m(1),
           matchData->GetShotMiss(0, 0), matchData->GetShotMiss(1, 0),
           matchData->GetShotMiss(0, 1), matchData->GetShotMiss(1, 1),
           matchData->GetShotMiss(0, 2), matchData->GetShotMiss(1, 2));
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
    // The match is over; ESC is Confirm here too - on to the result page,
    // which is where leaving lives.
    GoResult();
    return;
  } else {
    event->Ignore();
  }
}

void GameOverPage::GoResult() {
  // Confirm: on to the result page, over the same closing ceremony. This page
  // stays underneath so the smoke run's completion line is still printed.
  Properties props;
  CreatePage((int)e_PageID_Result, props);
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
