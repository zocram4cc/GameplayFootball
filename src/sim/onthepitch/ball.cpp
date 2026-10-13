// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "ball.hpp"

#include <algorithm>
#include <cmath>

#include "simcontext.hpp"
#include "ballphysics.hpp"
#include "match.hpp"
#include "pesballphysics.hpp"

Ball::Ball(Match* match) : match(match) {
  // PES 2021's ball physics (docs/re/pes-re-ball-contact.md). The constants come
  // from the install: the `ball.o` record of constant_match.bin and the image
  // values that live in the exe and nowhere else. GF's own bounce (0.62),
  // linear bounce, drag and grass friction had no PES counterpart and are gone -
  // PES has no restitution coefficient and no friction coefficient, only the
  // clamped interpolations over `ball.o`'s mode columns that the Match hands
  // over. The Match refuses to build without them, exactly as it does for the
  // clock constants.
  ballConstants = &match->GetPesBallConstants();

  // Weather defaults to calm and dry; a match/season can override it via config
  // or SetWeather() to bend balls in the wind and skid them on a wet pitch.
  weatherWind = Vector3(GetConfiguration()->GetReal("match_wind_x", 0.0f),
                        GetConfiguration()->GetReal("match_wind_y", 0.0f), 0.0f);
  weatherWetness = clamp(GetConfiguration()->GetReal("match_wetness", 0.0f), 0.0f, 1.0f);

  ballTouchesNet = false;

  CalculatePrediction();
}

Ball::~Ball() {}

void Ball::GetPredictionArray(Vector3* target) {
  std::copy_n(predictions, ballPredictionSize_ms / 10, target);
}

Vector3 Ball::GetMovement() {
  // meters / sec
  return momentum;
}

void Ball::Touch(const Vector3& target) {
  if (positionBuffer.coords[2] < 0.11f)
    positionBuffer.coords[2] = 0.11f;

  SetMomentum(target);

  // recalculate prediction
  CalculatePrediction();
  match->UpdateLatestMentalImageBallPredictions();

  match->GetTeam(0)->UpdatePossessionStats();
  match->GetTeam(1)->UpdatePossessionStats();
}

void Ball::SetPosition(const Vector3& target) {
  positionBuffer.Set(target);
  momentum.Set(0);
  SetRotation(0, 0, 0, 1.0);
  ballPosHistory.clear();
  previousMomentum = momentum;
  previousPosition = positionBuffer;
}

void Ball::SetMomentum(const Vector3& target) {
  momentum.Set(target);
  CalculatePrediction();
}

void Ball::SetWeather(const Vector3& wind, float wetness) {
  weatherWind = wind;
  weatherWetness = clamp(wetness, 0.0f, 1.0f);
  CalculatePrediction();
}

void Ball::SetRotation(radian x, radian y, radian z,
                       float bias) {  // radians per second for each axis
  // GF's per-axis rotation becomes PES's raw spin vector: PES stores spin raw,
  // with rad/s = raw * spinScale (docs/re/pes-re-officials-ball.md 5). `bias` is
  // the blend towards the new spin, the same slerp GF applied.
  const pes::PesBallConstants& pc = *ballConstants;
  // GF (x, y, z) -> Fox (x, z, -y): the inverse of the skeleton's
  // Fox (x, y, z) -> GF (x, -z, y) (pesplayernode.cpp), so a GF height maps to a
  // Fox height and a GF lateral to a Fox lateral with the opposite sign.
  const pes::Vec3 target(x * 0.001f, z * 0.001f, -y * 0.001f);

  radian angle = 0.0f;
  const float w = rotation_ms.elements[3];
  const float half = std::acos(clamp(w, -1.0f, 1.0f));
  const float sinHalf = std::sin(half);
  pes::Vec3 current(0.0f, 0.0f, 0.0f);
  if (sinHalf > 1e-6f) {
    const Vector3 axis(rotation_ms.elements[0] / sinHalf, rotation_ms.elements[1] / sinHalf,
                       rotation_ms.elements[2] / sinHalf);
    const float spinRad = 2.0f * half;
    current = pes::Vec3(axis.coords[0], axis.coords[2], -axis.coords[1]) * (spinRad /
              pc.image.spinScale);
  }
  const pes::Vec3 blended = current * (1.0f - bias) + target * bias;
  // Store the blended spin in the quaternion GF keeps, so the render path and
  // the prediction read the same state.
  const float spinRad =
      std::sqrt(blended.x * blended.x + blended.y * blended.y + blended.z * blended.z) *
      pc.image.spinScale;
  if (spinRad > 1e-6f) {
    const float inv = 1.0f / spinRad;
    const Vector3 axisFox(blended.x * inv, blended.y * inv, blended.z * inv);
    const Vector3 axisGf(axisFox.coords[0], -axisFox.coords[2], axisFox.coords[1]);
    rotation_ms.SetAngleAxis(spinRad * 0.001f, axisGf);
  } else {
    rotation_ms = QUATERNION_IDENTITY;
  }

  CalculatePrediction();
}

void Ball::SetRotation(const Vector3& rot, float bias) {  // radians per second for each axis
  SetRotation(rot.coords[0], rot.coords[1], rot.coords[2], bias);
}

BallSpatialInfo Ball::CalculatePrediction() {
  Vector3 newMomentum;
  Quaternion newRotation_ms;

  // fill predictions

  Vector3 nextPos = positionBuffer;
  Quaternion nextOrientation = orientationBuffer;
  Vector3 momentumPredict = momentum;

  predictions[0] = nextPos;

  bool woodwork_enabled = true;
  bool netting_enabled = true;

  // GF's tick: Match::kTick_ms = 10 ms, this loop's fixed step.
  const float timeStep = 0.01f;

  ballTouchesNet = false;

  // PES 2021's ball physics (docs/re/pes-re-ball-contact.md). The state is
  // carried in Fox space (Y up) because that is where every rule in the report
  // is written; GF's ball is Z-up, so the conversion happens at the boundary
  // only. The mapping is the inverse of the skeleton's Fox (x, y, z) ->
  // GF (x, -z, y) (pesplayernode.cpp): GF -> Fox is (x, y, z) -> (x, z, -y), so
  // GF's height lands on Fox's height and GF's lateral on Fox's lateral with the
  // opposite sign.
  const pes::PesBallConstants& pc = *ballConstants;
  pesball::BallState fox;
  fox.position = pes::Vec3(nextPos.coords[0], nextPos.coords[2], -nextPos.coords[1]);
  fox.velocity = pes::Vec3(momentumPredict.coords[0], momentumPredict.coords[2],
                           -momentumPredict.coords[1]);
  // GF's rotation is a quaternion per GF axis; PES's spin is a raw vector whose
  // magnitude is |w| / spinScale. Take the axis-angle out of GF's quaternion and
  // hand PES the raw spin it corresponds to.
  // w = elements[3]; the axis-angle form is angle = 2*acos(w), axis = (x,y,z)/sin(angle/2).
  {
    const float w = rotation_ms.elements[3];
    const float half = std::acos(clamp(w, -1.0f, 1.0f));
    const float sinHalf = std::sin(half);
    const float spinRad = 2.0f * half;  // radians per second
    if (sinHalf > 1e-6f && spinRad > 1e-6f) {
      const Vector3 axis(rotation_ms.elements[0] / sinHalf, rotation_ms.elements[1] / sinHalf,
                         rotation_ms.elements[2] / sinHalf);
      fox.spinRaw = pes::Vec3(axis.coords[0], axis.coords[2], -axis.coords[1]) * (spinRad /
                    pc.image.spinScale);
    }
  }

  // PES applies its per-tick factors once per 1/54 s and integrates its
  // accelerations over that step. GF ticks every 10 ms, so every per-tick keep
  // factor is converted with <func>KeepFactorPerGfTick</func> (a factor k per
  // PES tick becomes k^(0.01*54) per GF tick) and the accelerations are
  // integrated with dt = timeStep. Both are applied inside the pesball calls.

  for (unsigned int predictTime_ms = int(timeStep * 1000.0f);
       predictTime_ms < ballPredictionSize_ms; predictTime_ms += int(timeStep * 1000.0f)) {
    const bool firstTime = (predictTime_ms == 10);

    if (pesball::IsResting(pc, fox)) {
      // Report 2.1: a resting ball skips the flight function entirely. Nothing
      // moves, and PES's at-rest path recomputes the velocity as the finite
      // difference of the position, which is zero here.
    } else if (fox.position.y <= pc.image.ballRadius) {
      // Report 2.3: ground contact - the snap, the vertical response, the
      // tangential rewrite, the spin rebuild and the stop gate. PES integrates
      // no position on a contact tick: its own loop re-tests next tick and
      // lets the *flight* branch carry the ball forward on the new velocity.
      // This predictor runs one 10 ms step per outcome, so it does the same here
      // - the contact rewrites the velocity and then the position follows,
      // which is what PES's next-tick flight step does with it.
      pesball::ContactTick(pc, &fox, contactTicks, true, false);
      ++contactTicks;
      fox.position = pes::Vec3(fox.position.x + fox.velocity.x * timeStep,
                               fox.position.y + fox.velocity.y * timeStep,
                               fox.position.z + fox.velocity.z * timeStep);
    } else {
      // Report 6.3: flight - gravity, drag with the drag-crisis band and
      // Magnus, integrated with dt = 0.01 s.
      pesball::FlightTick(pc, &fox, timeStep, false);
    }

    // Fox -> GF, the skeleton's own mapping (pesplayernode.cpp).
    nextPos = Vector3(fox.position.x, -fox.position.z, fox.position.y);
    momentumPredict = Vector3(fox.velocity.x, -fox.velocity.z, fox.velocity.y);

    float ballRadius = pc.image.ballRadius;
    float postRadius = 0.07f;

    // woodwork

    if (firstTime && woodwork_enabled) {
      // posts

      if (nextPos.coords[2] < goalHeight + ballRadius + postRadius &&
          (nextPos.Get2D().GetAbsolute() - Vector3(pitchHalfW, goalHalfWidth, 0)).GetLength() <
              ballRadius + postRadius) {
        Vector3 normal;

        if (nextPos.coords[0] < 0) {
          // left side of pitch
          if (nextPos.coords[1] < 0) {
            // 'lower' side of pitch
            normal = (nextPos.Get2D() - Vector3(-pitchHalfW, -goalHalfWidth, 0))
                         .GetNormalized(Vector3(1, 0, 0));
            float nextPosZ = nextPos.coords[2];
            nextPos = Vector3(-pitchHalfW, -goalHalfWidth, 0) + normal * (postRadius + ballRadius);
            nextPos.coords[2] = nextPosZ;

          } else {
            // 'upper' side of pitch
            normal = (nextPos.Get2D() - Vector3(-pitchHalfW, goalHalfWidth, 0))
                         .GetNormalized(Vector3(1, 0, 0));
            float nextPosZ = nextPos.coords[2];
            nextPos = Vector3(-pitchHalfW, goalHalfWidth, 0) + normal * (postRadius + ballRadius);
            nextPos.coords[2] = nextPosZ;
          }
        } else {
          // right side of pitch
          if (nextPos.coords[1] < 0) {
            // 'lower' side of pitch
            normal = (nextPos.Get2D() - Vector3(pitchHalfW, -goalHalfWidth, 0))
                         .GetNormalized(Vector3(-1, 0, 0));
            float nextPosZ = nextPos.coords[2];
            nextPos = Vector3(pitchHalfW, -goalHalfWidth, 0) + normal * (postRadius + ballRadius);
            nextPos.coords[2] = nextPosZ;

          } else {
            // 'upper' side of pitch
            normal = (nextPos.Get2D() - Vector3(pitchHalfW, goalHalfWidth, 0))
                         .GetNormalized(Vector3(-1, 0, 0));
            float nextPosZ = nextPos.coords[2];
            nextPos = Vector3(pitchHalfW, goalHalfWidth, 0) + normal * (postRadius + ballRadius);
            nextPos.coords[2] = nextPosZ;
          }
        }

        momentumPredict =
            (momentumPredict.Get2D().GetNormalized(normal) + (normal * 1.1f)).GetNormalized() *
                momentumPredict.Get2D().GetLength() * 0.8f +
            (Vector3(0, 0, 1) * momentumPredict.coords[2]);
        fox.velocity = pes::Vec3(momentumPredict.coords[0], momentumPredict.coords[2],
                                 -momentumPredict.coords[1]);
      }

      // crossbar

      Vector3 nextPosXZ = nextPos * Vector3(1, 0, 1);
      if ((nextPosXZ.GetAbsolute() - Vector3(pitchHalfW, 0, goalHeight)).GetLength() <
              ballRadius + postRadius &&
          fabs(nextPos.coords[1]) < goalHalfWidth + ballRadius + postRadius) {
        Vector3 normal;

        if (nextPos.coords[0] < 0) {
          // left side of pitch
          normal =
              (nextPosXZ - Vector3(-pitchHalfW, 0, goalHeight)).GetNormalized(Vector3(0, 0, 1));
          float nextPosY = nextPos.coords[1];
          nextPos = Vector3(-pitchHalfW, 0, goalHeight) + normal * (postRadius + ballRadius);
          nextPos.coords[1] = nextPosY;

        } else {
          // right side of pitch
          normal =
              (nextPosXZ - Vector3(pitchHalfW, 0, goalHeight)).GetNormalized(Vector3(0, 0, -1));
          float nextPosY = nextPos.coords[1];
          nextPos = Vector3(pitchHalfW, 0, goalHeight) + normal * (postRadius + ballRadius);
          nextPos.coords[1] = nextPosY;
        }

        Vector3 momentumPredictXZ = momentumPredict * Vector3(1, 0, 1);
        momentumPredict =
            (momentumPredictXZ.GetNormalized(normal) + (normal * 1.1f)).GetNormalized() *
                momentumPredictXZ.GetLength() * 0.8f +
            (Vector3(0, 1, 0) * momentumPredict.coords[1]);
        fox.velocity = pes::Vec3(momentumPredict.coords[0], momentumPredict.coords[2],
                                 -momentumPredict.coords[1]);
      }
    }

    // netting

    if (predictTime_ms <= 10 && netting_enabled) {
      GoalNettingConfig nettingConfig;
      nettingConfig.pitchHalfW = pitchHalfW;
      nettingConfig.goalDepth = goalDepth;
      nettingConfig.goalHeight = goalHeight;
      nettingConfig.goalHalfWidth = goalHalfWidth;
      nettingConfig.ballRadius = ballRadius;

      BallPhysicsState nettingState{nextPos, momentumPredict};
      const GoalNettingResult nettingResult = ApplyGoalNettingCollision(
          nettingState, match->IsBallInGoal(), nettingConfig, timeStep);
      nextPos = nettingState.position;
      momentumPredict = nettingState.momentum;
      fox.velocity = pes::Vec3(momentumPredict.coords[0], momentumPredict.coords[2],
                               -momentumPredict.coords[1]);

      if (predictTime_ms == 10 && nettingResult.touchedNet)
        ballTouchesNet = true;
    }

    if (predictTime_ms % 10 == 0) {
      predictions[predictTime_ms / 10] = nextPos;
    }

    if (predictTime_ms == 10) {
      newMomentum = momentumPredict;
      // PES keeps the spin in a raw vector; GF keeps a quaternion. The render
      // orientation is the ball rolling about its spin axis at |w| rad/s.
      const float spinRad =
          std::sqrt(fox.spinRaw.x * fox.spinRaw.x + fox.spinRaw.y * fox.spinRaw.y +
                    fox.spinRaw.z * fox.spinRaw.z) *
          pc.image.spinScale;
      if (spinRad > 1e-6f) {
        const float inv = 1.0f / spinRad;
        const Vector3 axisFox(fox.spinRaw.x * inv, fox.spinRaw.y * inv, fox.spinRaw.z * inv);
        const Vector3 axisGf(axisFox.coords[0], -axisFox.coords[2], axisFox.coords[1]);
        newRotation_ms.SetAngleAxis(spinRad * timeStep, axisGf);
      } else {
        newRotation_ms = QUATERNION_IDENTITY;
      }
      orientPrediction = nextOrientation;
    }
  }

  return BallSpatialInfo(newMomentum, newRotation_ms);
}

Vector3 Ball::GetAveragePosition(unsigned int duration_ms) const {
  std::list<Vector3>::const_reverse_iterator iter = ballPosHistory.rbegin();
  unsigned int total = 0;
  Vector3 averageVec;
  while (iter != ballPosHistory.rend()) {
    averageVec += *iter;
    total++;
    if (total * 10 > duration_ms)
      break;
    iter++;
  }
  if (total > 0)
    averageVec /= total;
  else
    averageVec = Predict(0);
  return averageVec;
}

void Ball::Process() {
  BallSpatialInfo spatialInfo = CalculatePrediction();
  momentum = spatialInfo.momentum;
  rotation_ms = spatialInfo.rotation_ms;

  positionBuffer = Predict(10);
  orientationBuffer = orientPrediction;

  ballPosHistory.push_back(positionBuffer);
  if (ballPosHistory.size() > ballHistorySize_ms)
    ballPosHistory.pop_front();

  previousMomentum = momentum;
  previousPosition = positionBuffer;
}

void Ball::ResetSituation(const Vector3& focusPos) {
  momentum = Vector3(0);
  rotation_ms = QUATERNION_IDENTITY;
  for (unsigned int i = 0; i < ballPredictionSize_ms / 10; i++) {
    predictions[i] = Vector3(focusPos + Vector3(0, 0, 0.11));
  }
  orientPrediction = QUATERNION_IDENTITY;
  ballPosHistory.clear();
  previousMomentum = Vector3(0);
  previousPosition = Vector3(focusPos + Vector3(0, 0, 0.11));
  positionBuffer = Vector3(focusPos + Vector3(0, 0, 0.11));
  orientationBuffer = QUATERNION_IDENTITY;
  ballTouchesNet = false;
}
