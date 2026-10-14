// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "match.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "../data/playerskills.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "aimanager.hpp"
#include "base/geometry/line.hpp"
#include "base/geometry/triangle.hpp"
#include "base/log.hpp"
#include "crowdmood.hpp"
#include "matchduration.hpp"
#include "matchreport.hpp"
#include "pesstoppage.hpp"
#include "player/playerofficial.hpp"
#include "simcontext.hpp"

// How many past ticks of mental images the AI may look back over (GF match.cpp).
static constexpr size_t kMentalImageHistory = 30;
// Samples of the possession-side history, one per tick: a minute (GF match.cpp).
static constexpr int kPossessionSideHistoryTicks = 6000;
// A designated chaser is replaced only when the candidate is this much quicker
// to the ball (GF match.cpp).
static constexpr float kDesignatedSwitchTimeRatio = 0.85f;
// The ball counts as having moved when it travels further than this (GF match.cpp).
static constexpr float kStalemateBallMove_m = 0.02f;
// A long stalemate is reported at most this often (GF match.cpp).
static constexpr unsigned long kStalemateReportInterval_ms = 3000;
// A goal-line crossing episode ends once the ball is this far back inside the
// line (GF match.cpp).
static constexpr float kGoalLineReopen_m = 2.0f;
// RE-SHIM: while the referee plays his special animation after a card, the
// restart is held this far ahead. NOT a PES value: 9.8 records that no PES step
// waits on the referee's JUDGE action at all (cards live inside their scene,
// which this engine does not have), so this is GF's own way of not cutting the
// card ceremony off. Bounded by Referee::AlterSetPiecePrepareTime's cap.
static constexpr unsigned long kRefereeSpecialHold_ms = 1000;
// Foul types 2 (yellow card) and 3 (red card), see Referee::CheckFoul.
static constexpr int kFoulTypeYellow = 2;
static constexpr int kFoulTypeRed = 3;

float Match::RngSource::Unit(int stream) {
  // ponytail: GF's two generators map to PES slots by convention (random() ->
  // slot 2, fastrandom() -> slot 0), not by PES's per-system draw sites; each
  // system takes PES's own slot as it is ported (docs/re/pes-re-rng.md 3).
  constexpr int SLOT_FOR_STREAM[] = {2, 0};
  return rng.Unit(SLOT_FOR_STREAM[stream]);
}

Match::Match(MatchData* matchData, const MatchSetup& setup, const Properties& config)
    : rig(setup.rig), matchData(matchData), aiManagerRuns{setup.aiManagerRuns[0], setup.aiManagerRuns[1]} {
  // Before anything below draws a number: the anim collection and the wind do.
  // PES seeds its match generator once, at kick-off, with one value t as
  // slots t, t+1, t+2 (docs/re/pes-re-rng.md 2.1); MatchSetup::seed is t.
  if (setup.pesRngConstants == nullptr)
    throw std::runtime_error(
        "PES RNG constants missing: read them from the install with "
        "pes::LoadRngConstants into MatchSetup::pesRngConstants");
  rngSource = std::make_unique<RngSource>(*setup.pesRngConstants);
  pes::SeedAtKickoff(rngSource->rng, setup.seed);
  SetRandomSource(rngSource.get());
  *GetConfiguration() = config;

  Log(e_Notice, "Match", "Match", "Starting Match");

  assert(matchData != 0);
  assert(rig != nullptr);

  scene3D = std::make_shared<Scene3D>("match");

  matchDurationMinutes = static_cast<float>(setup.durationMinutes);
  matchDurationFactor = MatchDurationFactorFromMinutes(matchDurationMinutes);
  matchTimeScale = std::max(1.0f, setup.timeScale);
  matchDifficulty = GetConfiguration()->GetReal("match_difficulty", _default_Difficulty);

  // PES 2021's clock (docs/re/pes-re-tactics-clock.md 7.2): every constant is
  // read from the install's own PES2021.exe by the caller. Required, like the
  // rig - there is no fallback rig either - so a match without them cannot be
  // built and this refuses rather than running on invented numbers.
  if (setup.pesClockConstants == nullptr)
    throw std::runtime_error(
        "PES match-clock constants missing: read them from the install's PES2021.exe with "
        "pes::LoadPesClockConstants into MatchSetup::pesClockConstants");
  pesClockConstants = std::make_unique<pes::PesClockConstants>(*setup.pesClockConstants);
  pesClock = std::make_unique<pes::PesClock>(*pesClockConstants);

  // PES 2021's ball physics (docs/re/pes-re-ball-contact.md): every constant
  // comes from the install - the `ball.o` record of constant_match.bin and the
  // image values that live in the exe and nowhere else. Required like the
  // clock's: the ball's flight, bounce and rolling are PES's, and there is no
  // GF number to fall back on.
  if (setup.pesBallConstants == nullptr)
    throw std::runtime_error(
        "PES ball constants missing: read them from the install with "
        "pes::LoadPesBallConstants into MatchSetup::pesBallConstants");
  pesBallConstants = std::make_unique<pes::PesBallConstants>(*setup.pesBallConstants);
  // PES 2021's restart timings (docs/re/pes-re-foul-choreography.md 9): the
  // foul/offside holds and the beats between the stop and the kick. Required:
  // without them the referee has no length for a stoppage at all.
  if (setup.pesStoppageConstants == nullptr)
    throw std::runtime_error(
        "PES stoppage constants missing: read them from the install with "
        "pes::LoadStoppageConstants into MatchSetup::pesStoppageConstants");
  pesStoppageConstants = *setup.pesStoppageConstants;
  if (setup.pesJudgeConstants == nullptr)
    throw std::runtime_error(
        "PES judge constants missing: read them from the install with "
        "pes::LoadJudgeConstants into MatchSetup::pesJudgeConstants");
  pesJudgeConstants = *setup.pesJudgeConstants;
  // PES 2021's goalkeeper deflect point and post-catch distribution target
  // (docs/re/pes-re-keeper-port.md), exe-derived and required like the rest.
  if (setup.pesKeeperConstants == nullptr)
    throw std::runtime_error(
        "PES keeper constants missing: read them from the install with "
        "pes::LoadKeeperConstants into MatchSetup::pesKeeperConstants");
  pesKeeperConstants = *setup.pesKeeperConstants;
  // PES 2021's stamina model (docs/re/pes-re-stamina-drain.md): every number
  // lives in the image, so a match without the install's constants cannot be
  // built - there is no fallback fatigue to invent.
  // PES 2021's ball-carrier decision layer (docs/re/pes-re-carrier-ai.md).
  if (setup.pesCarrierConstants == nullptr)
    throw std::runtime_error(
        "PES carrier constants missing: read them from the install with "
        "pes::LoadCarrierConstants into MatchSetup::pesCarrierConstants");
  pesCarrierConstants = *setup.pesCarrierConstants;
  // PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6,
  // 7, 14): the line ramp's offsets, the 46/42/32 m lengths, the 15/19/23 m line
  // half-widths and the mark arithmetic. Required like the carrier's: a
  // defending team's whole shape comes out of these.
  if (setup.pesDefensiveShapeConstants == nullptr)
    throw std::runtime_error(
        "PES defensive shape constants missing: read them from the install with "
        "pes::LoadDefensiveShapeConstants into MatchSetup::pesDefensiveShapeConstants");
  pesDefensiveShapeConstants = *setup.pesDefensiveShapeConstants;
  if (setup.pesStaminaConstants == nullptr)
    throw std::runtime_error(
        "PES stamina constants missing: read them from the install with "
        "pes::LoadStaminaConstants into MatchSetup::pesStaminaConstants");
  pesStaminaConstants = std::make_unique<pes::StaminaConstants>(*setup.pesStaminaConstants);
  // PES 2021's set-piece taker wait (docs/re/pes-re-setpiece-choreography.md 9
  // item 2): every number lives in the image, so a match without the install's
  // constants cannot be built - there is no fallback to invent.
  if (setup.pesSetPieceTakerConstants == nullptr)
    throw std::runtime_error(
        "PES set-piece taker constants missing: read them from the install with "
        "pes::LoadSetPieceTakerConstants into MatchSetup::pesSetPieceTakerConstants");
  pesSetPieceTakerConstants = *setup.pesSetPieceTakerConstants;
  // PES 2021's set-piece team AI (docs/re/pes-re-setpiece-choreography.md 11):
  // every number lives in the image, so a match without the install's constants
  // cannot be built - there is no fallback to invent.
  if (setup.pesSetPieceTeamConstants == nullptr)
    throw std::runtime_error(
        "PES set-piece team constants missing: read them from the install with "
        "pes::LoadSetPieceTeamConstants into MatchSetup::pesSetPieceTeamConstants");
  pesSetPieceTeamConstants = *setup.pesSetPieceTeamConstants;

  dynamicNode = boost::intrusive_ptr<Node>(new Node("dynamicNode"));
  scene3D->AddNode(dynamicNode);

  ball = std::make_unique<Ball>(this);

  // animation database

  Log(e_Notice, "Match", "Match", "Loading player animations");

  anims = std::make_shared<AnimCollection>(*rig);
  anims->Load(setup.animationDir);

  // cache animation positions

  const std::vector<Animation*>& animationsTmp = anims->GetAnimations();
  for (unsigned int i = 0; i < animationsTmp.size(); i++) {
    std::vector<Vector3> positions;
    Animation* someAnim = animationsTmp.at(i);
    Quaternion dud;
    Vector3 position;
    for (int frame = 0; frame < someAnim->GetFrameCount(); frame++) {
      someAnim->GetKeyFrame("player", frame, dud, position, false, true);
      position.coords[2] = 0.0f;
      positions.push_back(position);
    }
    animPositionCache.insert(std::pair<Animation*, std::vector<Vector3>>(someAnim, positions));
  }

  // teams

  Log(e_Notice, "Match", "Match", "Creating teams/players");

  teams[0] = std::make_unique<Team>(0, this, matchData->GetTeamData(0));
  teams[1] = std::make_unique<Team>(1, this, matchData->GetTeamData(1));
  teams[0]->InitPlayers();
  teams[1]->InitPlayers();

  std::vector<Player*> activePlayers;
  teams[0]->GetActivePlayers(activePlayers);
  designatedPossessionPlayer = activePlayers.at(0);

  officials = std::make_unique<Officials>(this, anims);

  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchTeamIDs[i] = -1;
  }
  SetMatchPhase(e_MatchPhase_PreMatch);

  referee = std::make_unique<Referee>(this);

  possessionSideHistory = std::make_unique<ValueHistory<float>>(kPossessionSideHistoryTicks);

  penaltyShootout = std::make_unique<PenaltyShootoutController>(this);

  // Weather for this match, from the gameplay settings ("match_weather": 0 dry,
  // 0.5 rain, 1 storm). Wind direction is drawn once per match.
  {
    const float weather = clamp(GetConfiguration()->GetReal("match_weather", 0.0f), 0.0f, 1.0f);
    const float wetness = weather;
    const float windStrength = weather * weather * 6.0f;  // storms bend the ball much more
    const float windAngle = random(0.0f, 2.0f * pi);
    ball->SetWeather(
        Vector3(std::cos(windAngle) * windStrength, std::sin(windAngle) * windStrength, 0.0f),
        wetness);
  }

  // Straight to a shootout: handy for trying penalties out without playing a
  // whole match first ("penalty_shootout_only" in the config).
  if (GetConfiguration()->GetBool("penalty_shootout_only", false)) {
    Log(e_Notice, "Match", "Match", "penalty shootout only: skipping to the shootout");
    SetMatchPhase(e_MatchPhase_Penalties);
    StopPlay();
    StopSetPiece();
  }

  determinismHash = DeterminismHashSeed();

  Log(e_Notice, "Match", "Match", "Done creating match!");
}

Match::~Match() {
  // Teardown draws (players reset their idle frame) come from this match's own
  // generator, not from a newer match's.
  rngSource->restoreTo = GetRandomSource();
  SetRandomSource(rngSource.get());
  InvalidateCachedMentalImages();
  mentalImages.clear();
  possessionSideHistory.reset();
  teams[0]->Exit();
  teams[1]->Exit();
  teams[0].reset();
  teams[1].reset();
  officials.reset();
  ball.reset();
  referee.reset();
  penaltyShootout.reset();
  anims.reset();
  animPositionCache.clear();
  delete matchData;
  scene3D->DeleteNode(dynamicNode);
  dynamicNode.reset();
  scene3D->Exit();
}

Player* Match::GetPlayer(int playerID) {
  for (int t = 0; t < 2; t++) {
    for (unsigned int p = 0; p < teams[t]->GetAllPlayers().size(); p++) {
      if (teams[t]->GetAllPlayers().at(p)->GetID() == playerID) {
        return teams[t]->GetAllPlayers().at(p).get();
      }
    }
  }

  assert(1 == 2);  // shouldn't be here ;)
  return 0;
}

void Match::GetAllTeamPlayers(int teamID, std::vector<Player*>& players) {
  teams[teamID]->GetAllPlayers(players);
}

void Match::GetActiveTeamPlayers(int teamID, std::vector<Player*>& players) {
  teams[teamID]->GetActivePlayers(players);
}

void Match::GetOfficialPlayers(std::vector<PlayerBase*>& players) {
  officials->GetPlayers(players);
}

std::shared_ptr<const MentalImage> Match::GetMentalImageOwned(int history_ms) {
  if (mentalImages.empty()) return nullptr;

  int index = int(round((float)history_ms / 10.0));
  if (index >= (signed int)mentalImages.size()) index = mentalImages.size() - 1;
  if (index < 0) index = 0;

  mentalImages.at(index)->SetTimeStampNeg_ms(index * 10.0f);
  return mentalImages.at(index);
}

const MentalImage* Match::GetMentalImage(int history_ms) {
  // No images yet (the first frames of a match) or none left (teardown): the
  // clamp below would ask for element -1 and hand back a bogus image, whose
  // match pointer the caller then reads through.
  if (mentalImages.empty()) return nullptr;

  int index = int(round((float)history_ms / 10.0));
  if (index >= (signed int)mentalImages.size())
    index = mentalImages.size() - 1;
  if (index < 0)
    index = 0;

  mentalImages.at(index)->SetTimeStampNeg_ms(index * 10.0f);

  return mentalImages.at(index).get();
}

void Match::UpdateLatestMentalImageBallPredictions() {
  if (mentalImages.size() > 0)
    mentalImages.at(0)->UpdateBallPredictions();
}

void Match::InvalidateCachedMentalImages() {
  for (int teamID = 0; teamID < 2; teamID++) {
    if (!teams[teamID]) continue;
    std::vector<Player*> squad;
    teams[teamID]->GetAllPlayers(squad);
    for (Player* player : squad)
      if (player && player->CastHumanoid()) player->CastHumanoid()->InvalidateMentalImage();
  }
  // the referee and his assistants run humanoids of their own
  if (officials) {
    PlayerOfficial* crew[3] = {officials->GetReferee(), officials->GetLinesmanNorth(),
                               officials->GetLinesmanSouth()};
    for (PlayerOfficial* official : crew)
      if (official && official->CastHumanoid())
        official->CastHumanoid()->InvalidateMentalImage();
  }
}

void Match::AdvanceMatchSeconds(float gameSeconds) {
  if (!pesClock) return;
  // Report 3.3: the ClockFSM does not run while the state byte is a frozen
  // one, so a stoppage stops the clock instead of adding to it.
  pesClock->SetFrozen(!IsInPlay() || IsInSetPiece());
  pesClock->Advance(gameSeconds);
}

void Match::ResetSituation(const Vector3& focusPos) {
  SetBallRetainer(nullptr);
  SetGoalScored(false);
  // Players cache the mental image they last read; the vector below is about
  // to be emptied, so drop those pointers before the memory goes.
  InvalidateCachedMentalImages();
  mentalImages.clear();
  goalScored = false;
  ballIsInGoal = false;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeamID = -1;

  mentalImages.clear();
  possessionSideHistory->Clear();

  lastBodyBallCollisionTime_ms = 0;

  ball->ResetSituation(focusPos);
  teams[0]->ResetSituation(focusPos);
  teams[1]->ResetSituation(focusPos);
}

void Match::SetMatchPhase(e_MatchPhase newMatchPhase) {
  matchPhase = newMatchPhase;
  matchData->RecordPassRestart();
  // PES's clock is one counter over the whole match, so a phase change never
  // rewinds it - only the denominator (the phase's own period end) changes.
  if (pesClock) pesClock->SetPhase(newMatchPhase);
  if (matchPhase == e_MatchPhase_1stHalf)
    matchTime_ms = 0;
  else if (matchPhase == e_MatchPhase_2ndHalf)
    matchTime_ms = 2700000;
  else if (matchPhase == e_MatchPhase_1stExtraTime)
    matchTime_ms = 5400000;
  else if (matchPhase == e_MatchPhase_2ndExtraTime)
    matchTime_ms = 6300000;
  else if (matchPhase == e_MatchPhase_Penalties)
    matchTime_ms = 7200000;

  matchTimeExact_ms = static_cast<double>(matchTime_ms);


  if (matchPhase == e_MatchPhase_2ndHalf || matchPhase == e_MatchPhase_2ndExtraTime) {
    // PES's break bonus (docs/re/pes-re-stamina-drain.md 3.4): once at the
    // whistle, 9000 * F plus the fitness tier, clamped to the tank. The old
    // GF "relax 5%" is replaced by it. Period 2 is the half-time break, 4 the
    // extra-time break; this engine's phase names carry the same two.
    const int period = (matchPhase == e_MatchPhase_2ndHalf) ? 2 : 4;
    for (int teamID = 0; teamID < 2; teamID++) {
      std::vector<Player*> squad;
      GetAllTeamPlayers(teamID, squad);
      for (Player* player : squad) {
        pes::StaminaState& st = player->GetStamina();
        st.cur = pes::BreakBonus(*pesStaminaConstants, st, period, 1,
                                 matchDurationMinutes);
      }
    }
#ifndef NDEBUG
    // [pass-dist] per-half instrumentation, printed at the break: the chosen
    // pass-length distribution and the support-web width each team played
    // with, so pass selection and execution can be told apart when accuracy
    // moves.
    for (int teamID = 0; teamID < 2; teamID++) {
      const MatchData* md = GetMatchData();
      printf(
          "[pass-dist] half1 t%i %s | bands %i/%i/%i/%i/%i/%i | rms %.1fm | web "
          "%.1fm\n",
          teamID, teams[teamID]->GetTeamData()->GetName().c_str(),
          md->GetPassDistanceBand(teamID, 0), md->GetPassDistanceBand(teamID, 1),
          md->GetPassDistanceBand(teamID, 2), md->GetPassDistanceBand(teamID, 3),
          md->GetPassDistanceBand(teamID, 4), md->GetPassDistanceBand(teamID, 5),
          md->GetPassDistanceMeanRms_m(teamID), md->GetSupportWebWidthMean_m(teamID));
    }
#endif
  }

  // Ends have just changed, so put both teams back out on the ones they swapped to.
  // The referee prepares its kickoff at prepareTime and fires the phase change from
  // the same tick, in that order - so the marks it computed belonged to the ends the
  // teams were leaving, and nothing re-prepared afterwards. The second half kicked
  // off with everyone standing wherever the whistle caught him, on the wrong side of
  // a pitch that had turned round underneath him.
  if (MatchProgression::SwapsEnds(matchPhase)) {
    ResetSituation(Vector3(0, 0, 0));
    if (referee) referee->PrepareSetPiece(e_SetPiece_KickOff);
  }
}

void Match::SetScore(int teamID, int goals) {
  matchData->SetGoalCount(teamID, goals);
}

signed int Match::GetBestPossessionTeamID() {
  return bestPossessionTeamID;
}

void Match::GameOver() {
  gameOver = true;
}

bool Match::IsSubstitutionWindow() const {
  // Not merely "play is stopped": before kick-off play is stopped too. There is
  // no pre-match entrance in the headless sim.
  return Substitutions::IsSubstitutionWindow(IsInPlay(), matchPhase != e_MatchPhase_PreMatch,
                                             false);
}

Substitutions::e_Result Match::RequestSubstitution(int teamID, Player* playerOut,
                                                   Player* playerIn) {
  Team* team = GetTeam(teamID);
  const Substitutions::SquadView squad = team->DescribeSwap(playerOut, playerIn);
  const Substitutions::e_Result result =
      Substitutions::Validate(substitutionState, teamID, squad, IsSubstitutionWindow());
  if (result != Substitutions::e_Result_Accepted)
    return result;

  // executed at the end of the tick (ExecutePendingSubstitutions)
  pendingSubstitutions.push_back({teamID, playerOut, playerIn});
  return Substitutions::e_Result_Accepted;
}

void Match::ExecutePendingSubstitutions() {
  std::vector<PendingSubstitution> pending;
  pending.swap(pendingSubstitutions);
  for (const auto& sub : pending) {
    Team* team = GetTeam(sub.teamID);
    // conditions may have changed since the request; re-validate quietly
    const Substitutions::SquadView squad =
        team->DescribeSwap(sub.playerOut, sub.playerIn);
    if (Substitutions::Validate(substitutionState, sub.teamID, squad,
                                IsSubstitutionWindow()) != Substitutions::e_Result_Accepted)
      continue;
    if (!team->Substitute(sub.playerOut, sub.playerIn))
      continue;
    Substitutions::Commit(substitutionState, sub.teamID);
    matchData->AddEvent(
        MatchData::Event::Substitution, sub.teamID, GetMatchMinute(),
        (sub.playerOut && sub.playerOut->GetPlayerData() ? sub.playerOut->GetPlayerData()->GetLastName() : "") +
            " -> " +
            (sub.playerIn && sub.playerIn->GetPlayerData() ? sub.playerIn->GetPlayerData()->GetLastName() : ""));
    std::string out = sub.playerOut ? sub.playerOut->GetPlayerData()->GetLastName() : "";
    std::string in = sub.playerIn ? sub.playerIn->GetPlayerData()->GetLastName() : "";
    Log(e_Notice, "Match", "ExecutePendingSubstitutions",
        "substitution, team " + int_to_str(sub.teamID) + ": in " + in + ", out " + out);
  }
}

void Match::ProcessAutoSubstitutions() {
  // Only at stoppages during normal play, and no more than one decision per
  // second. A shootout is not a stoppage to make substitutions in, and neither
  // is anything before kick-off.
  if (!IsSubstitutionWindow() || actualTime_ms % 1000 != 0)
    return;
  if (matchPhase == e_MatchPhase_Penalties || gameOver)
    return;

  for (int teamID = 0; teamID < 2; teamID++) {
    // A team without the CPU manager keeps its bench as it is.
    if (!AIManagerRunsTeam(teamID))
      continue;
    if (Substitutions::GetRemaining(substitutionState, teamID) <= 0)
      continue;

    Team* team = GetTeam(teamID);

    std::vector<Player*> squadPlayers;
    team->GetAllPlayers(squadPlayers);

    std::vector<AIManager::SubstitutionCandidate> squad;
    std::vector<Player*> considered;
    squad.reserve(squadPlayers.size());
    Player* goalie = team->GetGoalie();
    for (Player* player : squadPlayers) {
      if (!player->IsActive() && team->HasBeenSubstituted(player->GetID()))
        continue;  // already used up
      // Keepers only come off if they are actually injured.
      if (player == goalie && player->GetInjuryLevel() < AIManager::substitutionInjuryLevel)
        continue;

      AIManager::SubstitutionCandidate candidate;
      // The remaining stamina as a fraction of the tank, so the manager's
      // threshold below reads on the same 0..1 scale it always did.
      const pes::StaminaState& st = player->GetStamina();
      candidate.fatigueFactorInv =
          st.max == 0 ? 1.0f : clamp(static_cast<float>(st.cur) / st.max, 0.0f, 1.0f);
      candidate.injuryLevel = static_cast<float>(st.injuryLevel) / 3.0f;
      candidate.isOnPitch = player->IsActive();
      // A super-sub is the bench's first choice, rating aside.
      candidate.averageStat =
          player->GetAverageStat() +
          PlayerSkills::GetSubstituteBonus(player->GetPlayerData()->GetSkills(), player->IsActive());
      squad.push_back(candidate);
      considered.push_back(player);
    }

    AIManager::MatchSituation situation;
    situation.goalDifference =
        matchData->GetGoalCount(teamID) - matchData->GetGoalCount(abs(teamID - 1));
    situation.matchTime_ms = matchTime_ms;
    situation.possessionShare =
        CrowdMood::GetHomeSupportFactor(matchData->GetPossessionFactor_60seconds(), teamID);

    const AIManager::SubstitutionPlan plan = AIManager::PlanSubstitution(
        squad, situation, Substitutions::GetRemaining(substitutionState, teamID));
    if (!plan.wanted)
      continue;

    RequestSubstitution(teamID, considered.at(plan.playerOutIndex),
                        considered.at(plan.playerInIndex));
  }
}

void Match::UpdateBallHeatmap() {
  // One sample a second is plenty for a readable heatmap.
  if (!IsInPlay() || actualTime_ms % 1000 != 0)
    return;
  if (GetLastTouchTeamID() >= 0)
    MatchAnalytics::AddSample(teamBallHeatmap[GetLastTouchTeamID()], ball->Predict(0).Get2D());
}

void Match::Process() {
  if (IsInPlay()) {
    CheckBallCollisions();
  }

  // The shootout owns the pitch during the penalties phase: normal play and
  // the referee's set-piece machinery stay out of the way, otherwise play
  // would simply restart underneath the shootout.
  if (matchPhase == e_MatchPhase_Penalties) {
    penaltyShootout->Process();
  } else {
    referee->Process();
  }

  // ball

  previousBallPos = ball->Predict(0);
  // Momentum as well as position, both taken before the tick's physics and
  // its collisions: a keeper's touch resolves inside `ball->Process()`, so by
  // the time the touch is recorded the ball has already been stopped and its
  // velocity says nothing about where it was going.
  previousBallMomentum = ball->GetMovement();  // m/s
  ball->Process();

  // create mental images for the AI to use

  auto mentalImage = std::make_shared<MentalImage>(this);
  mentalImage->TakeSnapshot();
  mentalImages.insert(mentalImages.begin(), mentalImage);
  if (mentalImages.size() > kMentalImageHistory) {
    mentalImages.pop_back();
  }

  teams[0]->Process();
  teams[1]->Process();
  officials->Process();

  teams[0]->UpdatePossessionStats();
  teams[1]->UpdatePossessionStats();
  CalculateBestPossessionTeamID();

  // Sim seconds since the ball last moved. Measured on actualTime_ms, which
  // runs whether play is live or not: a stall that begins when the match
  // already thought play was not on must still accumulate idle time.
  const Vector3 ballNow = ball->GetPositionBuffer();
  if ((ballNow - stalemateLastBallPosition).GetLength() > kStalemateBallMove_m) {
    stalemateLastBallPosition = ballNow;
    stalemateLastRealAction_ms = actualTime_ms;
  }
  stalemateRecovery = GameplayTuning::GetStalemateRecovery(
      static_cast<float>(actualTime_ms - stalemateLastRealAction_ms) / 1000.0f,
      ball->GetMovement().GetLength());

  if (GetBallRetainer() == 0) {
    signed int bestTeamID = GetBestPossessionTeamID();
    if (bestTeamID != -1) {
      Player* candidate = teams[GetBestPossessionTeamID()]->GetDesignatedTeamPossessionPlayer();
      if (candidate != GetDesignatedPossessionPlayer()) {
        unsigned int designatedTime =
            GetDesignatedPossessionPlayer()->GetTimeNeededToGetToBall_ms();
        unsigned int candidateTime = candidate->GetTimeNeededToGetToBall_ms();
        float timeRating = (float)(candidateTime + 10) / (float)(designatedTime + 10);
        if (timeRating < kDesignatedSwitchTimeRatio)
          designatedPossessionPlayer = candidate;
      }
    } else {
      // just stick with current team
      designatedPossessionPlayer = teams[GetDesignatedPossessionPlayer()->GetTeamID()]
                                       ->GetDesignatedTeamPossessionPlayer();
    }
  } else {
    designatedPossessionPlayer = GetBallRetainer();
  }

  // Nobody has touched the ball for seconds and it is lying there, and the
  // designation above picks the quickest player of ONE team: if that man is
  // still out of reach he queues no command at all (elizacontroller.cpp's
  // one-second gate), every other player holds his shape, and the pitch
  // stands still while the half runs out. Under a stalemate the ball therefore
  // goes to whoever can actually reach it, across BOTH teams.
  if (GetBallRetainer() == 0 && stalemateRecovery != GameplayTuning::StalemateRecovery::none) {
    Player* closest = nullptr;
    int closestTime_ms = 0;
    for (int side = 0; side < 2; side++) {
      for (const std::unique_ptr<Player>& candidate : teams[side]->GetAllPlayers()) {
        if (!candidate->IsActive()) continue;
        const int time_ms = candidate->GetTimeNeededToGetToBall_ms();
        if (!closest || time_ms < closestTime_ms) {
          closest = candidate.get();
          closestTime_ms = time_ms;
        }
      }
    }
    if (closest) {
      designatedPossessionPlayer = closest;
      stalemateCount++;
      if (stalemateLastReport_ms + kStalemateReportInterval_ms < actualTime_ms) {
        stalemateLastReport_ms = actualTime_ms;
        // The referee's buffer is the whole question when play is off: an
        // armed restart nobody is taking, and an unarmed one the recovery in
        // referee.cpp is supposed to catch.
        const RefereeBuffer& buf = GetReferee()->GetBuffer();
        printf("[stalemate] %.1fs idle: inPlay %d setPiece %d | ball %.2f m/s at "
               "%.1f,%.1f,%.1f | buf active %d prepared %d started %d piece %d "
               "taker %s start-in %.1fs | forcing %s (%d) from %.1f m\n",
               static_cast<float>(actualTime_ms - stalemateLastRealAction_ms) / 1000.0f,
               (int)inPlay, (int)inSetPiece, ball->GetMovement().GetLength(), ballNow.coords[0],
               ballNow.coords[1], ballNow.coords[2], (int)buf.active, (int)buf.prepared,
               (int)buf.started, (int)buf.desiredSetPiece,
               buf.taker ? buf.taker->GetPlayerData()->GetLastName().c_str() : "none",
               buf.startTime > actualTime_ms ? (buf.startTime - actualTime_ms) / 1000.0f : -1.0,
               closest->GetPlayerData()->GetLastName().c_str(), closest->GetID(),
               (closest->GetPosition() - ballNow).GetLength());
      }
    }
  }

  CheckHumanoidCollisions();

  // time

  if (IsInPlay() && !IsInSetPiece()) {
    const double gameTick_ms = MatchDurationGameTimeFromRealMilliseconds(
        static_cast<double>(kTick_ms), matchDurationMinutes, matchTimeScale);
    matchTimeExact_ms += gameTick_ms;
    matchTime_ms = static_cast<unsigned long>(matchTimeExact_ms);
    // PES 2021's clock (docs/re/pes-re-tactics-clock.md) counts the same game
    // time, so a period ends on both clocks together.
    AdvanceMatchSeconds(static_cast<float>(gameTick_ms / 1000.0));
  }
  actualTime_ms += kTick_ms;
  if (IsGoalScored())
    goalScoredTimer += kTick_ms;
  else
    goalScoredTimer = 0;

  if (IsInPlay() && !IsInSetPiece())
    GetMatchData()->AddPossessionTime_10ms(designatedPossessionPlayer->GetTeamID());

  // Measure the arrival, not the launch: the ball's own two positions either
  // side of the goal-line plane, tick by tick. `previousBallPos` is this
  // tick's pre-physics sample and `ball->Predict(0)` the post-physics one, so
  // the pair straddles the step no matter what the ball did in it. Every
  // crossing is counted, shot or not. A teleport is not an arrival: it is
  // detected by the same predicate `CheckForGoal` uses.
  if (IsInPlay() && !BallTeleportedThisTick()) {
    const Vector3 arrivedAt = ball->Predict(0);
    const std::array<float, 3> fromBefore = {previousBallPos.coords[0], previousBallPos.coords[1],
                                             previousBallPos.coords[2]};
    const std::array<float, 3> fromNow = {arrivedAt.coords[0], arrivedAt.coords[1],
                                          arrivedAt.coords[2]};
    // The SAME plane the goal test uses.
    const GameplayTuning::GoalLineCrossing cross = GameplayTuning::FindGoalLineCrossing(
        fromBefore, fromNow, pitchHalfW + GameplayTuning::kGoalPlaneOffset_m);
    if (cross.crossed) {
      // Once per episode: a ball in the net sits on the plane and the netting
      // clamp nudges it back and forth across it every tick. A new episode
      // needs the ball back in play, two metres inside the line.
      if (!goalLineCrossingOpen) {
        goalLineCrossingOpen = true;
        // The goal on `crossedSide` is attacked by whichever team is not on it.
        const int crossedSide = (std::fabs(arrivedAt.coords[0]) > std::fabs(previousBallPos.coords[0]))
                                    ? (arrivedAt.coords[0] > 0.0f ? 1 : -1)
                                    : (previousBallPos.coords[0] > 0.0f ? 1 : -1);
        const int attacker = (teams[0]->GetSide() == crossedSide) ? 1 : 0;
        const bool inFrame = GameplayTuning::CrossedInsideGoalFrame(cross, goalHalfWidth, goalHeight);
        if (matchData->ShotInFlight())
          matchData->AddGoalLineCrossing(matchData->GetShotTeamID(), inFrame);
        else
          matchData->AddGoalLineCrossingWithoutShot(attacker, inFrame);
      }
    } else if (std::fabs(arrivedAt.coords[0]) < pitchHalfW - kGoalLineReopen_m) {
      goalLineCrossingOpen = false;
    }
  }

  // check for goals

  // The goal line is checked in every phase - the shootout needs to know
  // whether the ball actually crossed it, since that is what its tally is
  // built on. What the penalties phase suppresses is only the *scoring*
  // below: the shootout keeps its own count and the match score is frozen.
  const bool shootoutOwnsScoring = matchPhase == e_MatchPhase_Penalties;
  bool t1goal = CheckForGoal(teams[0]->GetSide());
  bool t2goal = CheckForGoal(teams[1]->GetSide());
  if (t1goal)
    ballIsInGoal = true;
  if (t2goal)
    ballIsInGoal = true;

  if (IsInPlay() && !shootoutOwnsScoring) {
    if (t1goal) {
      matchData->SetGoalCount(teams[1]->GetID(), matchData->GetGoalCount(1) + 1);
      goalScored = true;
      lastGoalTeamID = teams[1]->GetID();
      teams[1]->GetController()->UpdateTactics();
    }
    if (t2goal) {
      matchData->SetGoalCount(teams[0]->GetID(), matchData->GetGoalCount(0) + 1);
      goalScored = true;
      lastGoalTeamID = teams[0]->GetID();
      teams[0]->GetController()->UpdateTactics();
    }
    if (t1goal || t2goal) {
      // find out who scored
      bool ownGoal = true;
      if (GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == GetLastGoalTeamID() ||
          GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == GetLastGoalTeamID())
        ownGoal = false;

      if (!ownGoal) {
        lastGoalScorer = teams[GetLastGoalTeamID()]->GetLastTouchPlayer();
        const bool isPenaltyGoal = IsPendingPenaltyGoal(GetLastGoalTeamID(), lastGoalScorer);
        matchData->AddEvent(MatchData::Event::Goal, GetLastGoalTeamID(), GetMatchMinute(),
                            lastGoalScorer ? lastGoalScorer->GetPlayerData()->GetLastName() : "",
                            isPenaltyGoal);
      } else {
        lastGoalScorer = teams[abs(GetLastGoalTeamID() - 1)]->GetLastTouchPlayer();
        matchData->AddEvent(MatchData::Event::OwnGoal, GetLastGoalTeamID(), GetMatchMinute(),
                            lastGoalScorer ? lastGoalScorer->GetPlayerData()->GetLastName() : "");
      }
      ClearPendingPenalty();
    }
  }

  // average possession side

  if (IsInPlay()) {
    if (GetBestPossessionTeamID() >= 0) {
      float sideValue = 0;
      sideValue += (GetTeam(0)->GetFadingTeamPossessionAmount() - 0.5f) * GetTeam(0)->GetSide();
      sideValue += (GetTeam(1)->GetFadingTeamPossessionAmount() - 0.5f) * GetTeam(1)->GetSide();
      possessionSideHistory->Insert(sideValue);
    }
  }

  // A card: while the referee plays his special animation the restart waits
  // for him.
  const RefereeBuffer& refereeBuffer = GetReferee()->GetBuffer();
  const int foulType = GetReferee()->GetCurrentFoulType();
  if (refereeBuffer.active && (foulType == kFoulTypeYellow || foulType == kFoulTypeRed) &&
      refereeBuffer.stopTime < GetActualTime_ms() - kRefereeSpecialHold_ms &&
      refereeBuffer.prepareTime > GetActualTime_ms() &&
      officials->GetReferee()->GetCurrentFunctionType() == e_FunctionType_Special)
    referee->AlterSetPiecePrepareTime(GetActualTime_ms() + kRefereeSpecialHold_ms);

  // A substitution is a stoppage event. The decision belongs to the window the
  // rules allow it in (IsSubstitutionWindow), not to every tick of open play.
  if (IsSubstitutionWindow())
    ProcessAutoSubstitutions();
  ExecutePendingSubstitutions();
  UpdateBallHeatmap();

  UpdateBodies();

  iterations++;
  if (iterations % kDeterminismSampleTicks == 0)
    determinismHash = FoldDeterminism(determinismHash, *this);
}

void Match::UpdateBodies() {
  teams[0]->PreparePutBuffers(actualTime_ms);
  teams[1]->PreparePutBuffers(actualTime_ms);
  officials->PreparePutBuffers(actualTime_ms);

  teams[0]->FetchPutBuffers(actualTime_ms);
  teams[1]->FetchPutBuffers(actualTime_ms);
  officials->FetchPutBuffers(actualTime_ms);

  teams[0]->Put();
  teams[1]->Put();
  officials->Put();

  GetDynamicNode()->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
}

void Match::ReplacePlayerReferences(Player* playerOut, Player* playerIn) {
  if (designatedPossessionPlayer == playerOut)
    designatedPossessionPlayer = playerIn;
  if (ballRetainer == playerOut)
    ballRetainer = playerIn;
  if (lastGoalScorer == playerOut)
    lastGoalScorer = nullptr;
  // The mental images hold snapshots of a humanoid that no longer exists.
  InvalidateCachedMentalImages();
  mentalImages.clear();
  bestPossessionTeamID = -1;
}

// PES 2021's injury roll for one contact
// (docs/re/pes-re-stamina-drain.md 6): the victim's severity accumulates from
// both players' speeds, the attacker's action state and the approach angle,
// then the player's injury resistance decides how badly it lands. Both
// bodies here are standing tackles, which is PES's attacker state
// "neither sliding" (the 0.5 factor).
void Match::InjureFromContact(Player* victim, Player* attacker) {
  if (victim == nullptr || attacker == nullptr) return;
  // A contact that puts a man on the floor is PES's "victim down" (the
  // e_FunctionType_Trip state this engine's bumps already produce), which
  // doubles the severity's base.
  const bool down = victim->GetCurrentFunctionType() == e_FunctionType_Trip;
  // ponytail: PES reads the injury resistance off the roster byte at
  // PMS+0x70+0x2f, which this engine does not load. The middle tier (B, the
  // 50-point roll) is what the report ships; 0.20's low outfield gk_* stats
  // make the third tier the obvious next step when that byte does load.
  constexpr int kInjuryResistance = 1;
  // ponytail: the approach angle is the attacker's heading against the line
  // to the victim; this engine's contact already carries both directions, so
  // the victim's own heading stands in for the pair.
  const float toAttackerAngle =
      victim->GetAngle() - (attacker->GetPosition() - victim->GetPosition()).GetAngle2D();
  pes::RecordContact(*pesStaminaConstants, &victim->GetStamina(), kInjuryResistance, down,
                     victim->GetMovement().GetLength() * 3.6f,
                     attacker->GetMovement().GetLength() * 3.6f,
                     attacker->GetCurrentFunctionType() == e_FunctionType_Sliding,
                     attacker->GetCurrentFunctionType() == e_FunctionType_Sliding,
                     static_cast<float>(toAttackerAngle) * 180.0f / pi, &rngSource->rng);
}

bool Match::BallTeleportedThisTick() const {
  // A teleported ball did not fly there: keeper retain sticks it onto a body
  // part, placements and stages move it by hand, all while play continues.
  // The segment from the pre-tick sample to the placed position can sweep
  // through the goal mouth and award a goal with the visible ball elsewhere,
  // credited to whoever touched last (showcase t=742.4: banner fired on a
  // ball at a dribbler's feet 10 m out). Physics moves at most speed x 10 ms
  // a tick - under half a metre even at the hardest shot - so a segment past
  // a metre is a placement, not flight. Shared by the goal check and the
  // goal-line crossing census, which otherwise disagree on exactly this case.
  constexpr float kTeleportSegment_m = 1.0f;
  return (ball->Predict(0) - previousBallPos).GetLength() > kTeleportSegment_m;
}

bool Match::CheckForGoal(signed int side) {
  if (BallTeleportedThisTick())
    return false;
  if (fabs(ball->Predict(10).coords[0]) < pitchHalfW - 1.0)
    return false;

  Line line;
  line.SetVertex(0, previousBallPos);
  line.SetVertex(1, ball->Predict(0));

  Triangle goal1;
  goal1.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 0));
  goal1.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal1.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal1.SetNormals(Vector3(-side, 0, 0));
  Triangle goal2;
  goal2.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal2.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 2.5f));
  goal2.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal2.SetNormals(Vector3(-side, 0, 0));

  // match->SetDebugPilon(Vector3(55 * side, 3.66, 2.44));
  // match->SetDebugPilon2(line.GetVertex(1));

  Vector3 intersectVec;
  bool intersect = goal1.IntersectsLine(line, intersectVec);
  if (!intersect) {
    intersect = goal2.IntersectsLine(line, intersectVec);
  }
  // extra check: ball could have gone 'in' via the side netting, if line begin == inside pitch, but
  // outside of post, and line end == in goal. disallow!
  if (fabs(previousBallPos.coords[1]) > 3.7 &&
      fabs(previousBallPos.coords[0]) > pitchHalfW - lineHalfW - 0.11)
    return false;

  if (intersect)
    return true;
  else
    return false;
}

void Match::CalculateBestPossessionTeamID() {
  if (GetBallRetainer() != 0) {
    int retainTeamID = GetBallRetainer()->GetTeamID();
    bestPossessionTeamID = retainTeamID;
  } else {
    int bestTime_ms[2] = {100000, 100000};
    for (int teamID = 0; teamID < 2; teamID++) {
      bestTime_ms[teamID] = teams[teamID]->GetTimeNeededToGetToBall_ms();
    }

    if (bestTime_ms[0] < bestTime_ms[1])
      bestPossessionTeamID = 0;
    else if (bestTime_ms[0] > bestTime_ms[1])
      bestPossessionTeamID = 1;
    else if (bestTime_ms[0] == bestTime_ms[1])
      bestPossessionTeamID = -1;
  }
}

void Match::CheckHumanoidCollisions() {
  std::vector<Player*> players;

  GetTeam(0)->GetActivePlayers(players);
  GetTeam(1)->GetActivePlayers(players);

  // outer vectors index == players[] index
  std::vector<std::vector<PlayerBounce>> playerBounces;

  // insert an empty entry for every player
  for (unsigned int i1 = 0; i1 < players.size(); i1++) {
    std::vector<PlayerBounce> bounce;
    playerBounces.push_back(bounce);
  }

  // check each combination of humanoids once
  for (unsigned int i1 = 0; i1 < players.size() - 1; i1++) {
    for (unsigned int i2 = i1 + 1; i2 < players.size(); i2++) {
      Vector3 tripVec1, tripVec2;
      CheckHumanoidCollision(players.at(i1), players.at(i2), playerBounces.at(i1),
                             playerBounces.at(i2));
    }
  }

  // do bouncy magic
  for (unsigned int i1 = 0; i1 < players.size(); i1++) {
    float totalForce = 0.0f;

    // if (playerBounces.at(i1).size() > 0) printf("  player %i.. ", players.at(i1)->GetID());

    for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
      const PlayerBounce& bounce = playerBounces.at(i1).at(i2);
      totalForce += bounce.force;
    }

    if (totalForce > 0.0f) {
      // if (playerBounces.at(i1).size() > 0) printf("%f, %f; ", totalBias, multiplier);

      Vector3 bounceVec;
      for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
        const PlayerBounce& bounce = playerBounces.at(i1).at(i2);
        bounceVec += (bounce.opp->GetMovement() - players.at(i1)->GetMovement()) * bounce.force *
                     (bounce.force / totalForce);

        // //SetGreenDebugPilon(bounce.opp->GetPosition() + bounce.opp->GetMovement());
        // if (players.at(i1)->GetTeamID() == 0) {
        //   SetRedDebugPilon(players.at(i1)->GetPosition() + players.at(i1)->GetMovement());
        // } else {
        //   SetGreenDebugPilon(players.at(i1)->GetPosition() + players.at(i1)->GetMovement());
        // }
      }

      // if (playerBounces.at(i1).size() > 0) printf("\n");
      //  okay, accumulated all, now distribute them in normalized fashion
      players.at(i1)->OffsetPosition(bounceVec * 0.01f * 1.0f);
      // printf("moving player %i\n", i1);
    }
  }
}

void Match::CheckHumanoidCollision(Player* p1, Player* p2, std::vector<PlayerBounce>& p1Bounce,
                                   std::vector<PlayerBounce>& p2Bounce) {
  float distanceFactor = 0.72f;
  float bouncePlayerRadius = 0.5f * distanceFactor;
  float similarPlayerRadius = 0.8f * distanceFactor;
  float similarExp = 0.2f;           // 0.8f;
  float similarForceFactor = 0.25f;  // 0.5f would be the full effect

  Vector3 p1pos = p1->GetPosition();
  Vector3 p2pos = p2->GetPosition();

  float distance = (p1pos - p2pos).GetLength();

  Vector3 p1movement = p1->GetMovement();
  Vector3 p2movement = p2->GetMovement();
  assert(p1movement.coords[2] == 0.0f);
  assert(p2movement.coords[2] == 0.0f);

  float bounceBias = 0.0f;
  Vector3 bounceVec;
  float p1backFacing = 0.5f;
  float p2backFacing = 0.5f;

  if (distance < bouncePlayerRadius * 2.0f ||
      distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
    bounceVec = (p1pos - p2pos).GetNormalized(Vector3(0, -1, 0));

    /*
        // skew a bit so the bounce is mostly sideways from possession player - this way, players
       are more likely to walk side by side in ball battles instead of behind each other if (p1 ==
       GetDesignatedPossessionPlayer() || p2 == GetDesignatedPossessionPlayer()) {
          //radian angle = GetDesignatedPossessionPlayer()->GetDirectionVec().GetAngle2D();
          radian angle =
       GetBall()->GetMovement().Get2D().GetNormalized(GetDesignatedPossessionPlayer()->GetDirectionVec()).GetAngle2D();
          bounceVec.Rotate2D(-angle);
          bounceVec.coords[0] *= 0.5f;
          bounceVec.Rotate2D(angle);
          bounceVec.Normalize();
        }
    */

    // back facing
    Vector3 p1facing = p1->GetDirectionVec().GetRotated2D(p1->GetRelBodyAngle() * 0.7f);
    Vector3 p2facing = p2->GetDirectionVec().GetRotated2D(p2->GetRelBodyAngle() * 0.7f);
    p1backFacing = clamp(p1facing.GetDotProduct(bounceVec) * 0.5f + 0.5f, 0.0f,
                         1.0f);  // 0 .. 1 == worst .. best
    p2backFacing = clamp(p2facing.GetDotProduct(-bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f);

    if (distance < bouncePlayerRadius * 2.0f) {
      bounceBias += p1backFacing * 0.8f;
      bounceBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      bounceBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1->TouchPending() && p1->GetCurrentFunctionType() == e_FunctionType_Interfere)
        bounceBias += 0.1f + 0.4f * p1->GetStat("technical_standingtackle");
      if (p1->TouchPending() && p1->GetCurrentFunctionType() == e_FunctionType_Sliding)
        bounceBias += 0.1f + 0.4f * p1->GetStat("technical_slidingtackle");
      if (p2->TouchPending() && p2->GetCurrentFunctionType() == e_FunctionType_Interfere)
        bounceBias -= 0.1f + 0.4f * p2->GetStat("technical_standingtackle");
      if (p2->TouchPending() && p2->GetCurrentFunctionType() == e_FunctionType_Sliding)
        bounceBias -= 0.1f + 0.4f * p2->GetStat("technical_slidingtackle");

      // problem is, once possession is lost (usually directly after ball is touched), bias may turn
      // around the other way. (well, maybe that's not a problem. dunno.) if (p1->HasPossession() ==
      // true) bounceBias -= 0.3f; if (p2->HasPossession() == true) bounceBias += 0.3f;

      if (p1 == GetDesignatedPossessionPlayer())
        bounceBias += 0.4f;
      if (p2 == GetDesignatedPossessionPlayer())
        bounceBias -= 0.4f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor =
            clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) *
            1.0f;  // std::min is cap so difference won't matter if ball is far away (so only used
                   // in battles about the ball)
        bounceBias += ballDistanceDiffFactor;
      }

      bounceBias += p1->GetStat("physical_balance") * 1.0f;
      bounceBias -= p2->GetStat("physical_balance") * 1.0f;

      bounceBias = clamp(bounceBias, -1.0f, 1.0f);
      bounceBias *= 0.5f;

      // convert bounceBias to 0 .. 1 instead of -1 .. 1
      float bounceBias0to1 = bounceBias * 0.5f + 0.5f;
      // bounceBias0to1 = curve(bounceBias0to1, 0.5f); // more binary

      Vector3 offset1 = (p1pos - p2pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) *
                        (1.0f - bounceBias0to1) * 2.0f;
      Vector3 offset2 = (p2pos - p1pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) *
                        bounceBias0to1 * 2.0f;

      // slow down on contact
      /*
      Vector3 averageMomentum = (p1movement + p2movement) * 0.5f;
      offset1 -= averageMomentum * 0.001f;
      offset2 -= averageMomentum * 0.001f;
      */

      // make players snap to the side of opponents (rather, just a bit in front of them too)
      // todo: make less binary, and more based on stats. maybe make this whole push/pull thing a
      // separate system?

      if (GetDesignatedPossessionPlayer() == p2 && p2->HasPossession()) {
        Vector3 p2_leftside =
            p2pos + p2->GetDirectionVec().GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p2_rightside =
            p2pos + p2->GetDirectionVec().GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p1_to_p2_left = (p1pos - p2_leftside).GetLength();
        float p1_to_p2_right = (p1pos - p2_rightside).GetLength();
        Vector3 p2side = p1_to_p2_left < p1_to_p2_right ? p2_leftside : p2_rightside;
        // SetYellowDebugPilon(p2side);
        offset1 +=
            (p2side - p1pos).GetNormalizedMax(0.01f) * p1->GetStat("physical_balance") * 0.3f;
      }

      else if (GetDesignatedPossessionPlayer() == p1 && p1->HasPossession()) {
        Vector3 p1_leftside =
            p1pos + p1->GetDirectionVec().GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p1_rightside =
            p1pos + p1->GetDirectionVec().GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p2_to_p1_left = (p2pos - p1_leftside).GetLength();
        float p2_to_p1_right = (p2pos - p1_rightside).GetLength();
        Vector3 p1side = p2_to_p1_left < p2_to_p1_right ? p1_leftside : p1_rightside;
        // SetRedDebugPilon(p1side);
        offset2 +=
            (p1side - p2pos).GetNormalizedMax(0.01f) * p2->GetStat("physical_balance") * 0.3f;
      }

      // can not bump faster than sprint
      offset1.NormalizeMax(sprintVelocity * 0.01f);
      offset2.NormalizeMax(sprintVelocity * 0.01f);

      p1->OffsetPosition(offset1);
      p2->OffsetPosition(offset2);
    }

    // take over each others movement a bit (precalc phase)

    float similarBias = 0.0f;

    if (similarForceFactor > 0.0f && distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
      float shellDistance = std::max(0.0f, distance - bouncePlayerRadius * 2.0f);

      bool verbose = false;
      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("similarbias: ");

      similarBias += p1backFacing * 0.8f;
      similarBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      similarBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("backfacing: %f; ", similarBias);

      if (p1 == GetDesignatedPossessionPlayer())
        similarBias += 0.6f;
      if (p2 == GetDesignatedPossessionPlayer())
        similarBias -= 0.6f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("designated: %f; ", similarBias);

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor =
            clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) *
            1.0f;  // std::min is cap so difference won't matter if ball is far away (so only used
                   // in battles about the ball)
        similarBias += ballDistanceDiffFactor;
      }

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("ball closeness: %f; ", similarBias);

      similarBias += p1->GetStat("physical_balance") * 1.0f;
      similarBias -= p2->GetStat("physical_balance") * 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("balance stat: %f; ", similarBias);

      similarBias = clamp(similarBias, -1.0f, 1.0f);
      similarBias *= 0.9f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("result: %f\n", similarBias);

      float similarForce = clamp(1.0f - (shellDistance / (similarPlayerRadius * 2.0f)), 0.0f, 1.0f);
      similarForce = pow(similarForce, similarExp);
      similarForce *= similarForceFactor;

      assert(similarForce >= 0.0f && similarForce <= 1.0f);

      float similarBias0to1 = similarBias * 0.5f + 0.5f;

      PlayerBounce player1Bounce;
      player1Bounce.opp = p2;
      player1Bounce.force = similarForce * (1.0f - similarBias0to1);
      p1Bounce.push_back(player1Bounce);

      PlayerBounce player2Bounce;
      player2Bounce.opp = p1;
      player2Bounce.force = similarForce * similarBias0to1;
      p2Bounce.push_back(player2Bounce);
    }

    // u b trippin?

    if (distance < bouncePlayerRadius * 2.0f) {
      bool verbose = false;
      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("trip sensitivity: ");

      float p1sensitivity = 0.0f;
      float p2sensitivity = 0.0f;

      p1sensitivity += (1.0f - p1backFacing) * 1.0f;
      p2sensitivity += (1.0f - p2backFacing) * 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("backfacing: %f - %f; ", p1sensitivity, p2sensitivity);

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      p1sensitivity += NormalizedClamp(p1velocity, idleVelocity, sprintVelocity) * 1.0f;
      p2sensitivity += NormalizedClamp(p2velocity, idleVelocity, sprintVelocity) * 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("velocity: %f - %f; ", p1sensitivity, p2sensitivity);

      if (p1->HasBestPossession() == true)
        p1sensitivity += 1.0f;
      if (p2->HasBestPossession() == true)
        p2sensitivity += 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("haspossession: %f - %f; ", p1sensitivity, p2sensitivity);

      float balanceWeight = 3.0f;
      p1sensitivity += (1.0f - p1->GetStat("physical_balance") * 1.0f) * balanceWeight;
      p2sensitivity += (1.0f - p2->GetStat("physical_balance") * 1.0f) * balanceWeight;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("balance: %f - %f; ", p1sensitivity, p2sensitivity);

      p1sensitivity += clamp(p1->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);
      p2sensitivity += clamp(p2->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("decposoffset: %f - %f", p1sensitivity, p2sensitivity);

      // penetration
      float penetrationWeight = 6.0f;
      float penetration = ((p1->GetPosition() + p1->GetMovement() * 0.03f) -
                           (p2->GetPosition() + p2->GetMovement() * 0.03f))
                              .GetLength();
      // if (p1->GetDebug() || p2->GetDebug()) printf("penetration: %f\n", pow(1.0f -
      // NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f));
      p1sensitivity +=
          pow(1.0f - NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f) *
          penetrationWeight;
      p2sensitivity +=
          pow(1.0f - NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f) *
          penetrationWeight;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("penetration: %f - %f\n", p1sensitivity, p2sensitivity);

      // ball proximity (usually means: stability is less because we sacrifice balance to control
      // the ball)
      float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
      float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
      p1sensitivity += 1.0f - NormalizedClamp(p1BallDistance, 0.0f, 0.7f);
      p2sensitivity += 1.0f - NormalizedClamp(p2BallDistance, 0.0f, 0.7f);

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("ball proximity: %f - %f\n", p1sensitivity, p2sensitivity);

      // divided by elements active
      p1sensitivity /= 5.0f + balanceWeight + penetrationWeight;
      p2sensitivity /= 5.0f + balanceWeight + penetrationWeight;

      float trip0threshold = 0.38f;
      float trip1threshold = 0.48f;
      float trip2threshold = 0.58f;

      if (p1sensitivity > trip0threshold) {
        int tripType = 0;
        if (p1sensitivity > trip1threshold)
          tripType = 1;
        if (p1sensitivity > trip2threshold)
          tripType = 2;
        if (tripType > 0) {
          p1->TripMe((p1->GetMovement() * 0.1f + p2->GetMovement() * 0.06f + bounceVec * 1.0f)
                         .GetNormalized(bounceVec),
                     tripType);
          referee->TripNotice(p1, p2, tripType);
          // foul statistics are counted by the referee at the whistle
          const bool p1WasFit = p1->GetInjuryLevel() < AIManager::substitutionInjuryLevel;
          (void)p1WasFit;
          InjureFromContact(p1, p2);
        }
      }
      if (p2sensitivity > trip0threshold) {
        int tripType = 0;
        if (p2sensitivity > trip1threshold)
          tripType = 1;
        if (p2sensitivity > trip2threshold)
          tripType = 2;
        if (tripType > 0) {
          p2->TripMe((p2->GetMovement() * 0.1f + p1->GetMovement() * 0.06f - bounceVec * 1.0f)
                         .GetNormalized(-bounceVec),
                     tripType);
          referee->TripNotice(p2, p1, tripType);
          // foul statistics are counted by the referee at the whistle
          const bool p2WasFit = p2->GetInjuryLevel() < AIManager::substitutionInjuryLevel;
          (void)p2WasFit;
          InjureFromContact(p2, p1);
        }
      }

    }  // within either bump, similar or trip range
  }

  // check for tackling collisions

  int tackle = 0;
  if ((p1->GetCurrentFunctionType() == e_FunctionType_Sliding ||
       p1->GetCurrentFunctionType() == e_FunctionType_Interfere) &&
      p1->GetFrameNum() > 5 && p1->GetFrameNum() < 28)
    tackle += 1;
  if ((p2->GetCurrentFunctionType() == e_FunctionType_Sliding ||
       p2->GetCurrentFunctionType() == e_FunctionType_Interfere) &&
      p2->GetFrameNum() > 5 && p2->GetFrameNum() < 28)
    tackle += 2;
  if (distance < 2.0f && tackle > 0 && tackle < 3) {  // if tackle is 3, ignore both
    std::list<boost::intrusive_ptr<Geometry>> tacklerObjectList;
    std::list<boost::intrusive_ptr<Geometry>> victimObjectList;
    /*
    if (tackle == 0) { // todo: this way, p1 would have an advantage
      if (p1->GetCurrentFunctionType() == e_FunctionType_Trap ||
          p1->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_LongPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_HighPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_Shot ||
          p1->GetCurrentFunctionType() == e_FunctionType_Interfere) {
        p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
        p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
        p1action = true;
      }
      else if (p2->GetCurrentFunctionType() == e_FunctionType_Trap ||
               p2->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_LongPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_HighPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_Shot ||
               p2->GetCurrentFunctionType() == e_FunctionType_Interfere) {
        p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
        p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
        p2action = true;
      }
    }
    */
    if (tackle == 1) {
      p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
      p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
    }
    if (tackle == 2) {
      p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
      p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
    }

    // iterate through all body parts of tackler
    std::list<boost::intrusive_ptr<Geometry>>::iterator objIter = tacklerObjectList.begin();
    while (objIter != tacklerObjectList.end()) {
      AABB objAABB = (*objIter)->GetAABB();

      // make a tad smaller: AABBs are usually too large.
      objAABB.minxyz += 0.1f;
      objAABB.maxxyz -= 0.1f;

      std::list<boost::intrusive_ptr<Geometry>>::iterator victimIter = victimObjectList.begin();
      while (victimIter != victimObjectList.end()) {
        std::string bodyPartName = (*victimIter)->GetName();
        if (bodyPartName == "left_foot" || bodyPartName == "right_foot" ||
            bodyPartName == "left_lowerleg" || bodyPartName == "right_lowerleg"
            /*bodyPartName == "left_upperleg" || bodyPartName == "right_upperleg"*/) {
          if (objAABB.Intersects((*victimIter)->GetAABB())) {
            // printf("HIT: %s hits %s\n", (*objIter)->GetName().c_str(),
            // (*victimIter)->GetName().c_str());

            if (tackle == 1) {
              if (p1->GetFrameNum() > 10 && p1->GetFrameNum() < p1->GetFrameCount() - 6) {
                Vector3 tripVec = p2->GetDirectionVec();
                int tripType = 3;  // sliding
                if (p1->GetCurrentFunctionType() == e_FunctionType_Interfere)
                  tripType = 1;  // was 2
                p2->TripMe(tripVec, tripType);
                referee->TripNotice(p2, p1, tripType);
              }
            }
            if (tackle == 2) {
              if (p2->GetFrameNum() > 10 && p2->GetFrameNum() < p2->GetFrameCount() - 6) {
                Vector3 tripVec = p1->GetDirectionVec();
                int tripType = 3;  // sliding
                if (p2->GetCurrentFunctionType() == e_FunctionType_Interfere)
                  tripType = 1;  // was 2
                p1->TripMe(tripVec, tripType);
                referee->TripNotice(p1, p2, tripType);
              }
            }
            break;
          }
        }

        victimIter++;
      }

      objIter++;
    }
  }
}

void Match::CheckBallCollisions() {
  // todo: rewrite this function, this SHIT is UNREADABLE!!!111 olololololo

  // printf("%i - %i hihi\n", actualTime_ms, lastBodyBallCollisionTime_ms + 150);
  if (actualTime_ms <= lastBodyBallCollisionTime_ms + 150)
    return;

  std::vector<Player*> players;
  GetTeam(0)->GetActivePlayers(players);
  GetTeam(1)->GetActivePlayers(players);

  std::list<boost::intrusive_ptr<Geometry>> objectList;
  Vector3 bounceVec;
  float bias = 0.0;
  int bounceCount =
      0;  // this shit is shit, average properly in combination with bias or something like that

  // printf("lasttouchbias: %f, isnul?: %s\n", GetLastTouchBias(200), GetLastTouchBias(200) == 0.0f
  // ? "true" : "false");
  for (int i = 0; i < (signed int)players.size(); i++) {
    // A keeper who has been beaten does not block the ball with his body.
    if (players[i]->IsBeatenKeeper())
      continue;

    bool biggestRatio = false;
    int teamID = players[i]->GetTeam()->GetID();

    int touchTimeThreshold_ms = 200;  // 700;
    float oppLastTouchBias = GetTeam(abs(teamID - 1))->GetLastTouchBias(touchTimeThreshold_ms);
    float lastTouchBias = players[i]->GetLastTouchBias(touchTimeThreshold_ms);
    float oppLastTouchBiasLong = GetTeam(abs(teamID - 1))->GetLastTouchBias(1600);

    if (lastTouchBias <= 0.01f &&
        oppLastTouchBias >
            0.01f /* && ballTowardsPlayer*/) {  // cannot collide if opp didn't recently touch ball
                                                // (we would be able to predict ball by then), or if
                                                // player itself already did (to overcome the
                                                // 'perpetuum collision' problem, and to allow for
                                                // 'controlled ball collisions' in humanoid class)

      bool collisionAnim = false;
      if (players[i]->GetCurrentFunctionType() == e_FunctionType_Movement ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Trip ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Sliding ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect)
        collisionAnim = true;
      bool onlyWhenDirectionChangedUnexpectedly = false;
      if (players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect)
        onlyWhenDirectionChangedUnexpectedly = true;

      bool directionChangedUnexpectedly = false;
      if (onlyWhenDirectionChangedUnexpectedly) {
        float unexpectedDistance =
            (GetMentalImage(players[i]->GetController()->GetReactionTime_ms() +
                            players[i]->GetFrameNum() * 10)
                 ->GetBallPrediction(1000) -
             GetBall()->Predict(1000))
                .GetLength();  // mental image from when the anim began
        if (unexpectedDistance > 0.5f)
          directionChangedUnexpectedly = true;
      }
      // todo: this is the temp workaround version
      // if (onlyWhenDirectionChangedUnexpectedly) directionChangedUnexpectedly = true;

      if (Verbose())
        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect) {
          printf("onlyWhenDirectionChangedUnexpectedly: %i, directionChangedUnexpectedly: %i\n",
                 onlyWhenDirectionChangedUnexpectedly, directionChangedUnexpectedly);
        }

      if (collisionAnim && !players[i]->HasUniquePossession() &&
          (onlyWhenDirectionChangedUnexpectedly == directionChangedUnexpectedly)) {
        float boundingBoxSizeOffset =
            -0.1f;  // fake a big AABB for more blocking fun, or a small one for less bouncy bounce
        if (!players[i]->HasPossession())
          boundingBoxSizeOffset += 0.03f;
        else
          boundingBoxSizeOffset -= 0.03f;

        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Sliding ||
            players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere) {
          boundingBoxSizeOffset += 0.1f;
        }
        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect) {
          boundingBoxSizeOffset += 0.2f;
        }
        // A target man shields the ball with his body while holding position
        // (proposal 3A).
        boundingBoxSizeOffset += PlayerSkills::GetShieldingRadiusBonus(
            players[i]->GetPlayerData()->GetSkills(), players[i]->GetMovement().GetLength() < 1.0f);

        if (((players[i]->GetPosition() + Vector3(0, 0, 0.8f)) - ball->Predict(0)).GetLength() <
            2.5f) {  // premature optimization is the root of all evil :D
          players[i]->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, objectList);

          std::list<boost::intrusive_ptr<Geometry>>::iterator objIter = objectList.begin();
          while (objIter != objectList.end()) {
            AABB objAABB = (*objIter)->GetAABB();
            float ballRadius = 0.11f + boundingBoxSizeOffset;
            if (objAABB.Intersects(ball->Predict(0), ballRadius)) {
              if (Verbose())
                printf("HIT: %s\n", (*objIter)->GetName().c_str());

              if (players[i] == players[i]->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
                  GetLastTouchBias(200) < 0.01f) {  // todo: use reaction time stat

                players[i]->TriggerControlledBallCollision();

              } else {
                // todonow: average bouncevec and bias together per hit
                float movementBias = oppLastTouchBias * 0.8f + 0.2f;
                bounceVec += (ball->Predict(0) - (*objIter)->GetDerivedPosition())
                                     .GetNormalized(Vector3(0)) *
                                 movementBias +
                             players[i]->GetMovement() * (1.0f - movementBias);
                bounceCount++;
                players[i]->GetTeam()->SetLastTouchPlayer(players[i], e_TouchType_Accidental);
                Vector3 aabbCenter;
                objAABB.GetCenter(aabbCenter);
                bias += (1.0f - clamp(((ball->Predict(0) - aabbCenter).GetLength() - ballRadius) /
                                          objAABB.GetRadius(),
                                      0.0f, 1.0f)) *
                            0.9f +
                        0.1f;
              }
            }

            objIter++;
          }
        }
      }
    }
  }

  if (bias > 0.0f) {
    bounceVec /= (bounceCount * 1.0f);
    bounceVec.coords[2] *= 0.6f;
    bounceVec.Normalize();
    Vector3 currentMovement = ball->GetMovement();
    Vector3 fullCollisionVec = (bounceVec * 6.0f) +
                               (bounceVec * currentMovement.GetLength() * 0.6f) +
                               (currentMovement * -0.2f);
    bias = clamp(bias, 0.0f, 1.0f);
    bias = bias * 0.5f + 0.5f;
    Vector3 resultVector = fullCollisionVec * bias + currentMovement * (1.0f - bias);
    if (resultVector.GetLength() > currentMovement.GetLength())
      resultVector = resultVector.GetNormalized(0) * currentMovement.GetLength();
    // resultVector = resultVector.GetNormalized(0) * (currentMovement.GetLength() * 0.7f +
    // resultVector.GetLength() * 0.3f); // EXPERIMENT!
    resultVector *= 0.7f;

    ball->Touch(resultVector);
    ball->SetRotation(random(-30, 30), random(-30, 30), random(-30, 30), 0.5f * bias);

    lastBodyBallCollisionTime_ms = actualTime_ms;
  }
}
