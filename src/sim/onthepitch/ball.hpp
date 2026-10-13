// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_FOOTBALL_ONTHEPITCH_BALL
#define _HPP_FOOTBALL_ONTHEPITCH_BALL

#include "../gamedefines.hpp"
#include "../utils.hpp"
#include "defines.hpp"
#include "pesball.hpp"

using namespace blunted;

class Match;

struct BallSpatialInfo {
  BallSpatialInfo(const Vector3& momentum, const Quaternion& rotation_ms) {
    this->momentum = momentum;
    this->rotation_ms = rotation_ms;
  }
  Vector3 momentum;
  Quaternion rotation_ms;
};

class Ball {
public:
  Ball(Match* match);
  virtual ~Ball();

  inline Vector3 Predict(int predictTime_ms) const {
    unsigned int index = predictTime_ms > 0 ? static_cast<unsigned int>(predictTime_ms) : 0;
    if (index >= ballPredictionSize_ms)
      index = ballPredictionSize_ms - 10;
    index = index / 10;
    return predictions[index];
  }

  void GetPredictionArray(Vector3* target);
  Vector3 GetMovement();
  void Touch(const Vector3& target);
  void SetPosition(const Vector3& target);
  void SetMomentum(const Vector3& target);
  void SetRotation(radian x, radian y, radian z,
                   float bias = 1.0);                      // radians per second for each axis
  void SetRotation(const Vector3& rot, float bias = 1.0);  // radians per second for each axis
  BallSpatialInfo CalculatePrediction();                   // returns momentum in 10ms
  Vector3 GetPositionBuffer() const { return positionBuffer; }

  bool BallTouchesNet() { return ballTouchesNet; }
  Vector3 GetAveragePosition(unsigned int duration_ms) const;

  // Weather control (roadmap 3.8). wind is an acceleration in m/s^2 applied to
  // the airborne ball; wetness is in [0, 1] and makes the pitch slicker.
  void SetWeather(const Vector3& wind, float wetness);
  Vector3 GetWeatherWind() const { return weatherWind; }
  float GetWeatherWetness() const { return weatherWetness; }

  void Process();

  void ResetSituation(const Vector3& focusPos);

protected:
  Vector3 momentum;
  Quaternion rotation_ms;

  Vector3 predictions[ballPredictionSize_ms / 10];
  Quaternion orientPrediction;

  std::list<Vector3> ballPosHistory;
  Vector3 previousMomentum;
  Vector3 previousPosition;

  Vector3 positionBuffer;
  Quaternion orientationBuffer;

  Match* match;

  // PES 2021's ball physics, read from the install by the caller
  // (docs/re/pes-re-ball-contact.md, docs/re/pes-re-ball-constants.md). GF's own
  // bounce, drag and friction coefficients had no PES counterpart and are gone:
  // PES's vertical, tangential and spin response are all clamped interpolations
  // over `ball.o`'s mode columns, not single numbers.
  const pes::PesBallConstants* ballConstants = nullptr;

  // The ball's own contact-tick counter, the input the grounder kick's
  // `int(|x|*1000) mod gounnderprob[mode]` gate runs on.
  unsigned contactTicks = 0;

  Vector3 weatherWind;
  float weatherWetness;

  bool ballTouchesNet;
};

#endif
