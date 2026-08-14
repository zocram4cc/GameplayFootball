#include "foulseverity.hpp"

#include <algorithm>
#include <cmath>

namespace FoulSeverity {

namespace {

float Clamp(float value, float lo, float hi) {
  return std::max(lo, std::min(value, hi));
}

}  // namespace

float Score(const Contact& contact) {
  const float timingError = Clamp(contact.timingError, 0.0f, 1.0f);
  // Beyond two metres from the ball the challenge is as bad as it gets.
  const float ballDistance = Clamp(contact.ballDistance_m / 2.0f, 0.0f, 1.0f);
  const float fromBehind = Clamp(contact.fromBehind, 0.0f, 1.0f);

  switch (contact.tackleType) {
    case 1:
      // Light contact: a nibble at the heels or a tug. Never a foul on its
      // own, but from behind and nowhere near the ball it climbs to a card.
      return 0.6f + fromBehind * 0.5f + ballDistance * 0.5f;

    case 2:
      // A standing challenge that puts the man down starts above the foul
      // threshold - PES's static challenges take no contact reduction - and
      // the back-and-off-the-ball terms carry it into the cards.
      return 1.05f + fromBehind * 0.5f + ballDistance * 0.25f;

    default: {
      // Sliding tackle: the historical formula, verbatim. Without touch data
      // (a slide that never even played at the ball) the base is a foul.
      float score = 1.0f;
      if (contact.hasTouchData)
        score = std::pow(timingError, 0.7f) * 0.5f + ballDistance * 0.5f;
      return score + fromBehind;
    }
  }
}

}  // namespace FoulSeverity
