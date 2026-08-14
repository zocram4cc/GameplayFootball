// Law 12 severity scoring, shared by every foul producer. PES 2021 scores
// every contact through one pipeline no matter which animation produced it,
// and specifically refuses to treat standing challenges as innocent; GF's
// ladder used to be reachable only from a sliding tackle
// (docs/RULESET_AUDIT.md gap 3). Pure functions over plain data so the
// judgement is headless-testable; Referee::TripNotice is only a translator.

#ifndef _HPP_FOUL_SEVERITY
#define _HPP_FOUL_SEVERITY

namespace FoulSeverity {

struct Contact {
  // 1 == standing tackle resulting in a light trip (interfere, shirt-pull
  // class), 2 == standing tackle putting the man down, 3 == sliding tackle.
  // Same vocabulary as Referee::TripNotice.
  int tackleType = 3;
  // Sliding tackles with a touch animation carry timing information.
  bool hasTouchData = false;
  float timingError = 0.0f;     // 0..1, 0 = touched the ball on the intended frame
  float ballDistance_m = 0.0f;  // how far from the ball the challenge landed
  float fromBehind = 0.0f;      // 0 = face to face, 1 = square in the back
};

// Severity on the same [0..~2] scale RefereeProfile::GetFoulType judges:
// > 1.0 foul, >= 1.4 caution, >= 2.0 sending-off (standard profile).
float Score(const Contact& contact);

}  // namespace FoulSeverity

#endif
