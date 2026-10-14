// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MATCH
#define _HPP_MATCH

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "../data/matchanalytics.hpp"
#include "../data/matchdata.hpp"
#include "AIsupport/mentalimage.hpp"
#include "ball.hpp"
#include "gameplaytuning.hpp"
#include "matchsetup.hpp"
#include "matchprogression.hpp"
#include "pesstamina.hpp"
#include "pessetpiecetaker.hpp"
#include "pessetpieceteam.hpp"
#include "pessetpieceteam.hpp"
#include "officials.hpp"
#include "pesstoppage.hpp"
#include "peskickerror.hpp"
#include "penaltyshootoutcontroller.hpp"
#include "player/humanoid/animcollection.hpp"
#include "referee.hpp"
#include "scene/scene3d/scene3d.hpp"
#include "substitutions.hpp"
#include "team.hpp"

struct PlayerBounce {
  Player* opp;
  float force;
};

class Match {
public:
  // One simulation tick, the engine's fixed physics step (GF main.cpp
  // "physics_frametime_ms" default).
  static constexpr unsigned long kTick_ms = 10;

  // `config` replaces the process-wide configuration (GetConfiguration()) for
  // the life of this match. Seeds the shared random engines from setup.seed, so
  // a second Match in the same process replays the first given the same seed.
  Match(MatchData* matchData, const MatchSetup& setup, const Properties& config);
  ~Match();

  int GetScore(int teamID) { return matchData->GetGoalCount(teamID); }
  Ball* GetBall() { return ball.get(); }
  Team* GetTeam(int teamID) { return teams[teamID].get(); }
  Player* GetPlayer(int playerID);
  void GetAllTeamPlayers(int teamID, std::vector<Player*>& players);
  void GetActiveTeamPlayers(int teamID, std::vector<Player*>& players);
  void GetOfficialPlayers(std::vector<PlayerBase*>& players);

  std::shared_ptr<AnimCollection> GetAnimCollection() { return anims; }
  std::shared_ptr<Scene3D> GetScene3D() { return scene3D; }
  const pesrig::Rig& GetRig() const { return *rig; }

  const MentalImage* GetMentalImage(int history_ms);
  // As above, but keeps the image alive for as long as the caller holds it.
  // Anyone who CACHES an image across frames has to take this: the vector is
  // emptied on every reset (half time, a restart, teardown) and a raw pointer
  // into it dangles the moment that happens.
  std::shared_ptr<const MentalImage> GetMentalImageOwned(int history_ms);
  void UpdateLatestMentalImageBallPredictions();

  void ResetSituation(const Vector3& focusPos);
  // drops every player's cached mental image before the match frees them
  void InvalidateCachedMentalImages();

  void SetMatchPhase(e_MatchPhase newMatchPhase);
  e_MatchPhase GetMatchPhase() const { return matchPhase; }

  void StartPlay() { inPlay = true; }
  void StopPlay() { inPlay = false; }
  bool IsInPlay() const { return inPlay; }
  // How long the celebration has been running (onthepitch/goalcelebration.hpp).
  unsigned long GetGoalScoredTimer() const { return goalScoredTimer; }
  Player* GetLastGoalScorer() const { return lastGoalScorer; }

  void StartSetPiece() { inSetPiece = true; }
  void StopSetPiece() { inSetPiece = false; }
  bool IsInSetPiece() const { return inSetPiece; }
  Referee* GetReferee() { return referee.get(); }
  Officials* GetOfficials() { return officials.get(); }
  const RefereeBuffer& GetRefereeBuffer() { return referee->GetBuffer(); };

  // Whether the ball is in the net is where the ball is, not whether anyone is
  // still calling it a goal: the netting in Ball::Process is gated on it. Only a
  // reset clears it, by which time the ball is dead.
  void SetGoalScored(bool onOff) { goalScored = onOff; }
  bool IsGoalScored() const { return goalScored; }
  void SetScore(int teamID, int goals);
  // Drops every cached reference to a player who has just left the pitch.
  void ReplacePlayerReferences(Player* playerOut, Player* playerIn);
  int GetLastGoalTeamID() const { return lastGoalTeamID; }

  // The dead-ball window a struck penalty leaves open: the taker's own
  // conversion (direct, or an immediate follow-up on the rebound) still
  // counts as a penalty goal, but a normal phase of play starting later must
  // not. Referee::Process arms this the instant the penalty taker's kick
  // releases; the goal-scoring check in Match::Process consumes it.
  static constexpr unsigned long penaltyGoalWindow_ms = 6000;
  void SetPendingPenaltyTaken(int teamID, Player* taker) {
    pendingPenaltyTeamID = teamID;
    pendingPenaltyTaker = taker;
    pendingPenaltyDeadline_ms = actualTime_ms + penaltyGoalWindow_ms;
  }
  bool IsPendingPenaltyGoal(int scoringTeamID, Player* scorer) const {
    return pendingPenaltyTeamID == scoringTeamID && pendingPenaltyTaker != nullptr &&
           pendingPenaltyTaker == scorer && actualTime_ms <= pendingPenaltyDeadline_ms;
  }
  void ClearPendingPenalty() {
    pendingPenaltyTeamID = -1;
    pendingPenaltyTaker = nullptr;
  }
  void SetLastTouchTeamID(int id, e_TouchType touchType = e_TouchType_Intentional_Kicked) {
    lastTouchTeamIDs[touchType] = id;
    lastTouchTeamID = id;
    referee->BallTouched();
  }
  int GetLastTouchTeamID(e_TouchType touchType) const { return lastTouchTeamIDs[touchType]; }
  int GetLastTouchTeamID() const { return lastTouchTeamID; }
  Team* GetLastTouchTeam(e_TouchType touchType) {
    if (lastTouchTeamIDs[touchType] != -1)
      return teams[lastTouchTeamIDs[touchType]].get();
    else
      return 0;
  }
  Team* GetLastTouchTeam() {
    if (lastTouchTeamID != -1)
      return teams[lastTouchTeamID].get();
    else
      return teams[0].get();
  }
  Player* GetLastTouchPlayer(e_TouchType touchType) {
    if (GetLastTouchTeam(touchType))
      return GetLastTouchTeam(touchType)->GetLastTouchPlayer(touchType);
    else
      return 0;
  }
  Player* GetLastTouchPlayer() {
    if (GetLastTouchTeam())
      return GetLastTouchTeam()->GetLastTouchPlayer();
    else
      return 0;
  }
  float GetLastTouchBias(int decay_ms, unsigned long time_ms = 0) {
    if (GetLastTouchTeam())
      return GetLastTouchTeam()->GetLastTouchBias(decay_ms, time_ms);
    else
      return 0;
  }
  bool IsBallInGoal() const { return ballIsInGoal; }

  signed int GetBestPossessionTeamID();
  Player* GetDesignatedPossessionPlayer() { return designatedPossessionPlayer; }

  // Read by the match report, so a match that stood still shows up in the
  // batch output. Zero is the only good answer.
  unsigned long GetStalemateCount() const { return stalemateCount; }

  // Whether the man on the ball must give it up: nothing has happened for long
  // enough that keeping it is no longer an option.
  bool ShouldReleaseBall() const {
    return stalemateRecovery == GameplayTuning::StalemateRecovery::releaseBall;
  }
  Player* GetBallRetainer() { return ballRetainer; }
  void SetBallRetainer(Player* retainer) { ballRetainer = retainer; }

  float GetAveragePossessionSide(int time_ms) const {
    return possessionSideHistory->GetAverage(time_ms);
  }

  // PES 2021's match clock (docs/re/pes-re-tactics-clock.md): a u32 percentage
  // of the running period's game-minutes, at 960 ticks per game-minute. The
  // Match always has one: the constants are exe-derived and required.
  const pes::PesClockConstants& GetPesClockConstants() const { return *pesClockConstants; }
  // PES 2021's ball physics constants (docs/re/pes-re-ball-contact.md 6): the
  // `ball.o` record and the image values, required like the clock's.
  const pes::PesBallConstants& GetPesBallConstants() const { return *pesBallConstants; }
  const pes::JudgeConstants& GetPesJudgeConstants() const { return pesJudgeConstants; }
  // PES 2021's stamina model (docs/re/pes-re-stamina-drain.md): its constants,
  // read from the install's exe, required like the clock's and the ball's.
  const pes::StaminaConstants& GetPesStaminaConstants() const { return *pesStaminaConstants; }
  // PES 2021's restart timings (docs/re/pes-re-foul-choreography.md 9).
  // PES 2021's kick execution error (docs/re/pes-re-kick-scatter.md).
  const pes::KickErrorConstants& GetPesKickErrorConstants() const {
    return pesKickErrorConstants;
  }
  const pes::StoppageConstants& GetPesStoppageConstants() const {
    return pesStoppageConstants;
  }
  // PES 2021's ball-carrier decision layer (docs/re/pes-re-carrier-ai.md).
  const pes::CarrierConstants& GetPesCarrierConstants() const { return pesCarrierConstants; }
  // PES 2021's open-play defensive shape (docs/re/pes-re-defensive-shape.md 6,
  // 7, 14): the line ramp, the team length and width and the marking arithmetic.
  const pes::DefensiveShapeConstants& GetPesDefensiveShapeConstants() const {
    return pesDefensiveShapeConstants;
  }
  // PES 2021's engaging-defender decisions (docs/re/pes-re-defender-decisions.md):
  // the reaction chooser's table and the gates its entries read.
  const pes::DefenderDecisionConstants& GetPesDefenderDecisionConstants() const {
    return pesDefenderDecisionConstants;
  }
  // PES 2021's CPU strategy decider (docs/re/pes-re-match-management.md 1).
  const pes::StrategyConstants& GetPesStrategyConstants() const { return pesStrategyConstants; }
  // PES 2021's team sheet defaults and concepts (docs/re/pes-re-tactics.md 2).
  const pes::TeamConceptConstants& GetPesTeamConceptConstants() const { return pesTeamConceptConstants; }
  // PES 2021's generator constants (docs/re/pes-re-rng.md 1): the values the
  // per-decision generators are built from. The match generator itself is
  // `rngSource` and is not shared: PES builds a throw-away generator for each
  // deterministic roll (the carrier's plan rolls, report 4.4).
  const pes::RngConstants& GetPesRngConstants() const { return rngSource->constants; }
  // The seed this match was set up with: what any per-match generator derived
  // from it must be seeded from (see `matchSeed`).
  unsigned int GetMatchSeed() const { return matchSeed; }
  // PES 2021's goalkeeper deflect point and post-catch distribution target
  // (docs/re/pes-re-keeper-port.md).
  const pes::KeeperConstants& GetPesKeeperConstants() const { return pesKeeperConstants; }
  // PES 2021's set-piece taker wait (docs/re/pes-re-setpiece-choreography.md 9
  // item 2): the frame-rate multiples the AI holds a restart for, read from the
  // install's exe. Required like the rest.
  const pes::SetPieceTakerConstants& GetPesSetPieceTakerConstants() const {
    return pesSetPieceTakerConstants;
  }
  // PES 2021's set-piece team AI (docs/re/pes-re-setpiece-choreography.md 11):
  // the jog budget, the wall and the free-kick taker waterfall. Required.
  const pes::SetPieceTeamConstants& GetPesSetPieceTeamConstants() const {
    return pesSetPieceTeamConstants;
  }
  const pes::PesClock& GetPesClock() const { return *pesClock; }
  pes::PesClock& GetPesClock() { return *pesClock; }
  // Advances the match clock by elapsed game-seconds, frozen while play is
  // stopped (report 3.3: a stoppage adds no time).
  void AdvanceMatchSeconds(float gameSeconds);

  unsigned long GetMatchTime_ms() const { return matchTime_ms; }
  unsigned long GetActualTime_ms() const { return actualTime_ms; }
  // The simulation clock: ticks processed so far times kTick_ms. Everything
  // that GF timed on the wall clock is timed on this instead.
  unsigned long GetSimTime_ms() const { return iterations * kTick_ms; }

  void GameOver();
  bool IsGameOver() const { return gameOver; }

  // Player IDs phase per-player work ((time + id * 10) % 100), so they are
  // numbered per match: a process-wide counter made a second match diverge.
  int AllocatePlayerID() { return nextPlayerID++; }

  // One simulation tick: AI, physics, rules, and the pose of every body for
  // the collision tests of the next tick.
  void Process();

  boost::intrusive_ptr<Node> GetDynamicNode() { return dynamicNode; }

  MatchData* GetMatchData() { return matchData; }

  MatchAnalytics::ShotTally& GetShotTally() { return shotTally; }
  const MatchAnalytics::ShotTally& GetShotTally() const { return shotTally; }
  const MatchAnalytics::Heatmap& GetTeamBallHeatmap(int teamID) const {
    return teamBallHeatmap[teamID];
  }
  // The match clock in whole minutes, as a scoreboard shows it.
  int GetMatchMinute() const { return (int)(matchTime_ms / 60000); }

  PenaltyShootoutController* GetPenaltyShootout() { return penaltyShootout.get(); }

  Substitutions::State& GetSubstitutionState() { return substitutionState; }
  // Play stopped, in a match that has actually started.
  bool IsSubstitutionWindow() const;
  // Requests a substitution for `teamID`; returns the rule check result and
  // queues the swap when it is accepted (executed at the end of the tick).
  Substitutions::e_Result RequestSubstitution(int teamID, Player* playerOut, Player* playerIn);
  // Whether the CPU manager may adapt this team's tactics and use its bench
  // (MatchSetup::aiManagerRuns).
  bool AIManagerRunsTeam(int teamID) const { return aiManagerRuns[teamID]; }

  float GetMatchDurationFactor() const { return matchDurationFactor; }
  // The match length in minutes, PES's match-length factor F (report 2).
  float GetMatchDurationMinutes() const { return matchDurationMinutes; }
  float GetMatchDifficulty() const { return matchDifficulty; }

  std::vector<Vector3>& GetAnimPositionCache(Animation* anim) {
    // The cache is built from the animation collection, so a clip that never
    // joined it has no entry.
    auto cached = animPositionCache.find(anim);
    if (cached != animPositionCache.end()) return cached->second;
    static std::vector<Vector3> empty;
    return empty;
  }

  // The ball's position and velocity entering this tick, taken before
  // `ball->Process()` and therefore before this tick's collisions.
  const Vector3& GetBallPositionBeforeTick() const { return previousBallPos; }
  const Vector3& GetBallMomentumBeforeTick() const { return previousBallMomentum; }

  // FNV-1a over (tick, ball position, every active player's position), folded
  // in every kDeterminismSampleTicks ticks (matchreport.hpp).
  uint64_t GetDeterminismHash() const { return determinismHash; }

protected:
  // PES's match generator and the adapter GF's random()/fastrandom() draw from.
  // Declared first so it is destroyed last: players still draw while the
  // teams are torn down. ~Match makes it current for that teardown, and its
  // destructor hands the source back to whichever match was current before
  // (a test may build the next match before this one dies).
  struct RngSource : blunted::RandomSource {
    explicit RngSource(const pes::RngConstants& k) : constants(k), rng(constants) {}
    ~RngSource() override {
      blunted::SetRandomSource(restoreTo == this ? nullptr : restoreTo);
    }
    float Unit(int stream) override;
    pes::RngConstants constants;
    pes::Random rng;
    blunted::RandomSource* restoreTo = nullptr;
  };
  std::unique_ptr<RngSource> rngSource;

  // PES 2021's injury roll for one contact (docs/re/pes-re-stamina-drain.md 6).
  void InjureFromContact(Player* victim, Player* attacker);

  bool BallTeleportedThisTick() const;
  bool CheckForGoal(signed int side);

  void CalculateBestPossessionTeamID();
  void UpdateBallHeatmap();
  // Lets the teams use their bench (AIManager). Callers must be inside
  // IsSubstitutionWindow - the decision is made during a stoppage.
  void ProcessAutoSubstitutions();
  void ExecutePendingSubstitutions();
  void CheckHumanoidCollisions();
  void CheckHumanoidCollision(Player* p1, Player* p2, std::vector<PlayerBounce>& p1Bounce,
                              std::vector<PlayerBounce>& p2Bounce);
  // PES's contact reaction (docs/re/pes-re-contact.md): one side of a contact,
  // the tackle leg hits, the balance gauge, and the trip they issue.
  bool ResolvePesContact(Player* self, Player* opp, pes::ContactPart selfPart,
                         pes::ContactPart oppPart);
  void CheckTackleLegContact(Player* tackler, Player* victim);
  void UpdateBalanceGauges(const std::vector<Player*>& players);
  bool TripFromContact(Player* victim, Player* other, int tripType);
  std::map<int, int> pesBalanceGauge;      // player id -> ap+0x2820
  std::vector<Player*> pesContactReacted;  // reacted this tick
  void CheckBallCollisions();
  // Poses every body for this tick (GF's put phase, run in-line).
  void UpdateBodies();

  std::shared_ptr<Scene3D> scene3D;
  const pesrig::Rig* rig;

  MatchData* matchData;
  const bool aiManagerRuns[2];

  std::unique_ptr<PenaltyShootoutController> penaltyShootout;

  MatchAnalytics::ShotTally shotTally;
  MatchAnalytics::Heatmap teamBallHeatmap[2];

  // The ball's state entering this tick, kept so a keeper's touch can be judged
  // on where the ball was going before the touch stopped it. See `Process`.
  blunted::Vector3 previousBallMomentum;

  // True while the ball is behind the goal line, so one arrival is counted once
  // rather than once per tick the ball spends rattling in the net.
  bool goalLineCrossingOpen = false;

  Substitutions::State substitutionState;
  struct PendingSubstitution {
    int teamID;
    Player* playerOut;
    Player* playerIn;
  };
  std::vector<PendingSubstitution> pendingSubstitutions;
  std::unique_ptr<Team> teams[2];

  std::unique_ptr<Officials> officials;

  boost::intrusive_ptr<Node> dynamicNode;

  std::shared_ptr<AnimCollection> anims;

  std::unique_ptr<Ball> ball;

  std::vector<std::shared_ptr<MentalImage>>
      mentalImages;  // [index] == index * 10 ms ago ([0] == now)

  unsigned long iterations = 0;
  int nextPlayerID = 0;
  // The seed this match was set up with (MatchSetup::seed). Kept so per-match
  // generators can be seeded from it: a static or a surviving instance would
  // make a second match in the same process replay the first one's stream.
  unsigned int matchSeed = 0;
  unsigned long matchTime_ms = 0;
  // Stalemate recovery: how many ticks have forced the ball to go to the
  // nearest man because nobody was playing it, and when that was last printed.
  unsigned long stalemateCount = 0;
  unsigned long stalemateLastRealAction_ms = 0;
  Vector3 stalemateLastBallPosition;
  // What the stalemate policy decided this tick. The controllers read it.
  GameplayTuning::StalemateRecovery stalemateRecovery = GameplayTuning::StalemateRecovery::none;
  unsigned long stalemateLastReport_ms = 0;
  double matchTimeExact_ms = 0.0;
  unsigned long actualTime_ms = 0;
  unsigned long goalScoredTimer = 0;

  // Initialised: Team::GetSide() reads the phase while the constructor activates
  // players, before SetMatchPhase runs; a stale 2nd-half value left on the heap
  // by a previous in-process match mirrored every spawn.
  e_MatchPhase matchPhase = e_MatchPhase_PreMatch;  // then 1st/2nd half, extra time, penalties
  bool inPlay = false;
  bool inSetPiece = false;
  bool goalScored = false;  // true after goal scored, false again after next match state change
  bool ballIsInGoal = false;
  int lastGoalTeamID = 0;
  Player* lastGoalScorer = nullptr;
  int pendingPenaltyTeamID = -1;
  Player* pendingPenaltyTaker = nullptr;
  unsigned long pendingPenaltyDeadline_ms = 0;
  int lastTouchTeamIDs[e_TouchType_SIZE];
  int lastTouchTeamID = -1;
  signed int bestPossessionTeamID = -1;
  Player* designatedPossessionPlayer = nullptr;
  Player* ballRetainer = nullptr;

  bool gameOver = false;

  std::unique_ptr<ValueHistory<float>> possessionSideHistory;

  unsigned int lastBodyBallCollisionTime_ms = 0;

  std::unique_ptr<Referee> referee;

  Vector3 previousBallPos;

  float matchDurationMinutes;
  float matchDurationFactor;
  float matchTimeScale;

  // PES 2021's match clock: its exe-derived constants, owned here, and the
  // clock itself.
  std::unique_ptr<pes::PesClockConstants> pesClockConstants;
  std::unique_ptr<pes::PesBallConstants> pesBallConstants;
  pes::JudgeConstants pesJudgeConstants{};
  pes::KeeperConstants pesKeeperConstants{};
  pes::StoppageConstants pesStoppageConstants{};
  pes::KickErrorConstants pesKickErrorConstants{};
  pes::CarrierConstants pesCarrierConstants{};
  pes::DefenderDecisionConstants pesDefenderDecisionConstants{};
  pes::ContactConstants pesContactConstants{};
  pes::StrategyConstants pesStrategyConstants{};
  pes::TeamConceptConstants pesTeamConceptConstants{};
  pes::SubstitutionConstants pesSubstitutionConstants{};
  pes::SetPieceTakerConstants pesSetPieceTakerConstants{};
  pes::SetPieceTeamConstants pesSetPieceTeamConstants{};
  pes::DefensiveShapeConstants pesDefensiveShapeConstants{};
  std::unique_ptr<pes::PesClock> pesClock;
  std::unique_ptr<pes::StaminaConstants> pesStaminaConstants;

  std::map<Animation*, std::vector<Vector3>> animPositionCache;

  float matchDifficulty;

  uint64_t determinismHash;
};

#endif
