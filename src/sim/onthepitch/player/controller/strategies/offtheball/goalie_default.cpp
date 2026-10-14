// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "goalie_default.hpp"

#include "simcontext.hpp"
#include "../../../../gameplaytuning.hpp"
#include "data/playingstyles.hpp"
#include "base/geometry/triangle.hpp"
#include "onthepitch/setpiecelaws.hpp"
#include "onthepitch/pesrandom.hpp"

GoalieDefaultStrategy::GoalieDefaultStrategy(ElizaController* controller) : Strategy(controller) {
  name = "goalie default";

  ballBoundForGoal_ycoord = 0.0f;
}

GoalieDefaultStrategy::~GoalieDefaultStrategy() {}

int GoalieDefaultStrategy::KeeperShuffleDraw() {
  if (!keeperShuffle) {
    // Created and seeded from THIS match's seed, per Match: the generator's
    // constants are the exe's own, the state starts from their defaults, and
    // the keeper's own id keeps the two keepers' streams apart. A static or an
    // instance that outlived the match would make a second match in the same
    // process replay the first one's stream.
    keeperShuffle = std::make_unique<pes::Random>(match->GetPesRngConstants());
    // Slot 1 is the one Range0 draws, so the seed goes there.
    keeperShuffle->SetSeed(pes::kRange0Slot,
                           match->GetMatchSeed() +
                               static_cast<uint32_t>(player->GetID()) * 2654435761u);
  }
  return keeperShuffle->Range0(6);
}

Vector3 CalculateBestAchievableTarget(Player* player, const Vector3& pos1, float time1_sec,
                                      const Vector3& pos2, float time2_sec) {
  float stepsPerMeter = 4.0f;
  float stepSize = 1.0f / clamp(pos1.GetDistance(pos2) * stepsPerMeter, 1.0f, 20.0f);

  for (float percentage = 0.0f; percentage <= 1.0f; percentage += stepSize) {
    Vector3 checkPos = pos1 + (pos2 - pos1) * percentage;
    float checkTime_sec = time1_sec + (time2_sec - time1_sec) * percentage;

    int maxTime_ms = -1;
    unsigned int timeNeeded_ms =
        AI_GetTimeNeededForDistance_ms(player->GetPosition(), player->GetMovement(), checkPos,
                                       player->GetMaxVelocity(), false /*precise*/, maxTime_ms,
                                       false /*debug*/)
            .optimistic_ms;
    if (timeNeeded_ms * 0.001f <= checkTime_sec) {
      return checkPos;
      break;
    }
  }

  return pos2;
}

Vector3 GoalieDefaultStrategy::InterceptTarget(const MentalImage* mentalImage) {
  // The intercept of a ball bound for goal, as before: the point on the
  // ball-to-goal line the keeper can reach in time. PES's 10.5.1 replaces the
  // walking with KP_SAVING_MOVE's record, which is not ported here.
  Line ballToGoal;
  ballToGoal.SetVertex(0, mentalImage->GetBallPrediction(10).Get2D());
  float minGoalLineDist = 0.4f;
  Vector3 ballOverGoalLinePos =
      Vector3(pitchHalfW * team->GetSide(), ballBoundForGoal_ycoord, 0);
  ballOverGoalLinePos +=
      (ballToGoal.GetVertex(0) - ballOverGoalLinePos).GetNormalized(0) * minGoalLineDist;
  ballToGoal.SetVertex(1, ballOverGoalLinePos);
  float u = 0.0f;
  ballToGoal.GetDistanceToPoint(player->GetPosition() + player->GetMovement() * 0.05f, u);

  float u_at_1sec = 0.0f;
  ballToGoal.GetDistanceToPoint(mentalImage->GetBallPrediction(1010).Get2D(), u_at_1sec);

  bool should_gk_run_towards_the_goal = false;
  if (u_at_1sec > 1e-4) {
    float time_to_reach_gk = u / u_at_1sec;
    Vector3 ball_position_at_gk = mentalImage->GetBallPrediction(10 + 1000 * time_to_reach_gk);
    if (ball_position_at_gk.coords[2] > 2.5) {
      should_gk_run_towards_the_goal = true;
    }
  }

  u = clamp(u, 0.0f, 1.0f);

  Vector3 targetPos;
  if (should_gk_run_towards_the_goal) {
    targetPos = ballOverGoalLinePos;
  } else {
    targetPos = ballToGoal.GetVertex(0) + (ballToGoal.GetVertex(1) - ballToGoal.GetVertex(0)) * u;
    targetPos.coords[2] = 0.0;
  }
  targetPos.coords[0] = clamp(targetPos.coords[0], -pitchHalfW + 0.2f,
                              pitchHalfW - 0.2f);  // not very useful to stand behind line
  return targetPos;
}

void GoalieDefaultStrategy::RequestInput(const MentalImage* mentalImage, Vector3& direction,
                                         float& velocity) {
  // PES 2021's open-play goalkeeper position (docs/re/pes-re-goalkeeper.md
  // 10.2, ActionKeeperBasePosition::exec 0x1409dc040): he stands on the line
  // from 1 m behind his own goal centre to the predicted ball, at a depth that
  // is the minimum of the style cap, 25 m behind the defensive line, the
  // 4 -> 13 m ball-distance ramp, the ball distance and 15 m - with the
  // 2 .. 4.16 m floor - and a lateral shuffle scaled by GK Awareness. With the
  // ball he steps up as a sweeper instead. The post-to-post angle bisector this
  // replaces was a different model: PES uses one atan2 from R and no bisector
  // (10.0's positive kill of the hypothesis).
  //
  // PES GK Awareness decides how far ahead he reads the ball;
  // PlayerData defaults gk_awareness from mental_defensivepositioning for
  // profiles written before the GK attributes existed.
  Vector3 ballPos =
      mentalImage
          ->GetBallPrediction(GameplayTuning::GetKeeperAnticipation_ms(
                                  CastPlayer()->GetStat("gk_awareness")) +
                              CastPlayer()->GetTimeNeededToGetToBall_ms() * 0.2f)
          .Get2D();

  CalculateIfBallIsBoundForGoal(mentalImage);
  Vector3 targetPos;

  if (IsBallBoundForGoal()) {
    // A ball on its way in: shoot heading for goal is KP_SAVING_MOVE
    // (10.5.1), which walks to the intercept record. This engine's
    // bound-for-goal intercept is kept for that part.
    targetPos = InterceptTarget(mentalImage);
  } else {
    // In play, his team's possession decides the mode (PES: T+0x255, read as
    // the best-possession team here).
    const bool inPossession =
        match->GetBestPossessionTeamID() == team->GetID();
    // The two lines: PES reads the team's defensive-line reference and the
    // offside line, both in his own frame. GF's offside trap and rule are the
    // same surfaces.
    const int defend = team->GetSide();
    const int dir = -defend;
    const pes::KeeperPositionInput in{
        ballPos.coords[0],
        ballPos.coords[1],
        player->GetPosition().coords[0],
        player->GetPosition().coords[1],
        defend,
        inPossession,
        dir * team->GetController()->GetOffsideTrapX(),
        dir * AI_GetOffsideLine(match, mentalImage, team->GetID()),
        // PES's style word bits 19/20 are the Offensive/Defensive GK styles.
        CastPlayer()->GetPlayerData()->GetPlayingStyle() ==
            PlayingStyles::Player::OffensiveGoalkeeper,
        CastPlayer()->GetPlayerData()->GetPlayingStyle() ==
            PlayingStyles::Player::DefensiveGoalkeeper,
        CastPlayer()->GetStat("gk_awareness"),
        // The shuffle's Range0(6) on slot 1 of this keeper's own generator.
        KeeperShuffleDraw(),
    };
    const pes::KeeperFieldMarks marks{pitchHalfW,
                                      goalHalfWidth,
                                      pitchHalfW - SetPieceLaws::kGoalAreaDepth,
                                      SetPieceLaws::kGoalAreaHalfWidth,
                                      pitchHalfW - SetPieceLaws::kAreaDepth,
                                      SetPieceLaws::kAreaHalfWidth};
    const pes::KeeperSpot spot =
        pes::PesKeeperBasePosition(in, marks, match->GetPesKeeperConstants());
    targetPos = Vector3(spot.x, spot.z, 0.0f);
  }

  // The 2.25 m dead zone: inside it he stands still rather than shuffling on
  // the spot (10.2.1's dispatcher tail).
  const float toTarget = (targetPos - player->GetPosition()).GetLength();
  if (toTarget * toTarget < 2.25f) {
    direction = Vector3(0, 0, 0);
    velocity = 0.0f;
    return;
  }

  const float maxVelocity = sprintVelocity;
  direction = targetPos - player->GetPosition();
  velocity = clamp(direction.GetLength() * distanceToVelocityMultiplier, idleVelocity, maxVelocity);
  direction.Normalize(player->GetDirectionVec());
}

void GoalieDefaultStrategy::CalculateIfBallIsBoundForGoal(const MentalImage* mentalImage) {
  ballBoundForGoal = false;
  bool intersect = false;
  ballBoundForGoal_ycoord = 0;

  int side = team->GetSide();

  float panic = GameplayTuning::GetKeeperGoalMouthPanic(CastPlayer()->GetStat("gk_awareness"));
  if (mentalImage->GetBallPrediction(4000).coords[0] * side > pitchHalfW &&
      (player->GetPosition() - mentalImage->GetBallPrediction(250)).GetLength() <
          32.0f) {  // only if ball is close enough (cpu optimization)

    /* 3d version
        Line line;
        line.SetVertex(0, mentalImage->GetBallPrediction(40));
        line.SetVertex(1, mentalImage->GetBallPrediction(4000));
        //line.SetVertex(1, mentalImage->GetBallPrediction(0) + match->GetBall()->GetMovement() *
       800); Triangle goal1; goal1.SetVertex(0, Vector3((pitchHalfW - 0.0) * side,  3.7 * panic,
       0)); goal1.SetVertex(1, Vector3((pitchHalfW - 0.0) * side, -3.7 * panic, 0));
        goal1.SetVertex(2, Vector3((pitchHalfW - 0.0) * side,  3.7 * panic, 2.5 * panic));
        goal1.SetNormals(Vector3(-side, 0, 0));
        Triangle goal2;
        goal2.SetVertex(0, Vector3((pitchHalfW - 0.0) * side, -3.7 * panic, 0));
        goal2.SetVertex(1, Vector3((pitchHalfW - 0.0) * side, -3.7 * panic, 2.5 * panic));
        goal2.SetVertex(2, Vector3((pitchHalfW - 0.0) * side,  3.7 * panic, 2.5 * panic));
        goal2.SetNormals(Vector3(-side, 0, 0));

        //match->SetDebugPilon(Vector3(55 * side, 3.66, 2.44));
        //match->SetDebugPilon2(line.GetVertex(1));

        Vector3 intersectVec;
        intersect = goal1.IntersectsLine(line, intersectVec);
        if (!intersect) {
          intersect = goal2.IntersectsLine(line, intersectVec);
        }
    */

    // Walk the physics prediction until the ball crosses the goal line. Unlike
    // the old 2D projection, this rejects trajectories travelling over the bar.
    const float goalLineX = pitchHalfW * side;
    const unsigned int finalPrediction_ms = ballPredictionSize_ms - 10;
    unsigned int previousTime_ms = 0;
    Vector3 previousPosition = mentalImage->GetBallPrediction(previousTime_ms);
    while (previousTime_ms < finalPrediction_ms) {
      const unsigned int predictionTime_ms = std::min(previousTime_ms + 50U, finalPrediction_ms);
      const Vector3 predictedPosition = mentalImage->GetBallPrediction(predictionTime_ms);
      const bool crossedGoalLine = previousPosition.coords[0] * side < pitchHalfW &&
                                   predictedPosition.coords[0] * side >= pitchHalfW;
      if (crossedGoalLine) {
        const float xTravel = predictedPosition.coords[0] - previousPosition.coords[0];
        const float crossingBias =
            fabs(xTravel) > 0.0001f
                ? clamp((goalLineX - previousPosition.coords[0]) / xTravel, 0.0f, 1.0f)
                : 1.0f;
        const Vector3 goalLinePosition =
            previousPosition + (predictedPosition - previousPosition) * crossingBias;
        intersect = GameplayTuning::IsGoalMouthThreat(goalLinePosition.coords[1],
                                                      goalLinePosition.coords[2], goalHalfWidth,
                                                      goalHeight, panic);
        if (intersect)
          ballBoundForGoal_ycoord = goalLinePosition.coords[1];
        break;
      }
      previousTime_ms = predictionTime_ms;
      previousPosition = predictedPosition;
    }
    ballBoundForGoal = intersect;
  }
}
