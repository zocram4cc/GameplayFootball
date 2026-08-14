// Runs the penalty shootout during e_MatchPhase_Penalties: picks takers, rolls
// each kick through PenaltyShootout, narrates the outcome and decides the match.
// See TECHNICAL_ROADMAP.md section 3C.

#ifndef _HPP_PENALTY_SHOOTOUT_CONTROLLER
#define _HPP_PENALTY_SHOOTOUT_CONTROLLER

#include <string>

#include "base/math/vector3.hpp"
#include "penaltyshootout.hpp"

class Match;
class Player;

class PenaltyShootoutController {
public:
  PenaltyShootoutController(Match* match);

  // Sets up a fresh shootout. Safe to call repeatedly; only the first call for a
  // given shootout takes effect.
  void Start();
  void Reset();

  // Called once per match tick while the match is in the penalties phase.
  void Process();

  bool IsStarted() const { return started; }
  bool IsFinished() const { return state.phase == PenaltyShootout::e_Phase_Finished; }
  int GetWinner() const { return PenaltyShootout::GetWinner(state); }
  const PenaltyShootout::State& GetState() const { return state; }
  // Kicks converted so far, for the scoreboard.
  int GetScore(int teamID) const { return state.score[teamID == 1 ? 1 : 0]; }

  // Placement the current taker is going for, used by his controller so the
  // shot animation is aimed where his stats put it.
  bool IsKickLive() const { return state.phase == PenaltyShootout::e_Phase_Execution; }
  float GetAimY() const { return aimY; }
  float GetAimPower() const { return aimPower; }
  // Every kick in the shootout is taken at the same end, as in a real shootout.
  float GetGoalX() const;

protected:
  Player* SelectTaker(int teamID);
  // Places ball, taker, keeper and the waiting players for the next kick.
  void SetUpKick();
  void BeginKick();
  // Puts the defending keeper on the line of the shootout goal.
  void AnchorKeeper();
  // Taker in the area, defending keeper in goal, everyone else on halfway.
  void PositionPlayers();
  PenaltyShootout::KickObservation ObserveBall();
  void FinishKick(PenaltyShootout::e_Outcome outcome);
  void AnnounceOutcome();
  std::string GetScoreLine() const;
  blunted::Vector3 GetPenaltySpot() const;
  blunted::Vector3 GetGoalCentre() const;

  Match* match;
  PenaltyShootout::State state;

  bool started;
  unsigned long nextEventTime_ms;
  // Which players have already taken a kick this round, per team.
  unsigned int takenMask[2];
  unsigned long kickStartTime_ms;
  // Placement and weight of the current kick.
  float aimY;
  float aimPower;
  // Match score at the start of the shootout; shootout kicks do not change it.
  int baseScore[2];
  bool keeperTouchedBall;
  // Whether the keeper has already been hauled back to this goal for this kick.
  bool keeperHauledBack;
  // Whether the taker has actually struck the ball yet. Until he has, the ball
  // sitting still on the spot must not be read as a kick that has finished.
  bool ballStruck;
  // Outcome rolled from the two players' stats before the kick is taken.
  PenaltyShootout::e_Outcome plannedOutcome;
  // Side of the pitch the shootout is held at (-1 or 1), drawn at the start.
  int shootoutEndSide;
  Player* currentTaker;
};

#endif
