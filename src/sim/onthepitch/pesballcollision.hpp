#pragma once

// PES 2021's ball response to static objects, here the goal frame:
// FUN_14086d6f0 with one record and a still body (vB = 0),
// docs/re/pes-re-ball-collision.md 3 and 5. Fox space (Y up), metres, m/s,
// rad/s; PES's 1/54 s tick wherever PES writes one.

#include <optional>

#include "onthepitch/pesball.hpp"
#include "onthepitch/pesballphysics.hpp"

namespace pesball {

// The FieldInfo goal values the frame is built from (report 3.2): no goal
// dimension is an exe constant.
struct GoalFrame {
  float lineX_m;              // F0, the goal line's outer edge
  float innerHalfWidth_m;     // F2
  float innerHeight_m;        // F3
  float crossbarThickness_m;  // F15
  float postThickness_m;      // F16
};

struct FrameHit {
  pes::Vec3 surface;  // S, on the frame's surface
  pes::Vec3 normal;   // n, out of the frame
};

// The earliest post or crossbar hit on the segment the ball travelled this
// tick, p0 -> p1, while moving with v. Empty when it missed or was leaving.
std::optional<FrameHit> GoalFrameHit(const GoalFrame& g, float ballRadius, pes::Vec3 p0,
                                     pes::Vec3 p1, pes::Vec3 v);

// The response to one frame hit: velocity, snap, floor guard, caps and spin.
// `recentHit` is PES's simBall+0x50, read before this contact; the caller
// commits RecentHitAfter.
void FrameContact(const pes::PesBallConstants& c, const FrameHit& hit, float recentHit,
                  BallState* s);

// simBall+0x50 after a tick: 1 when the contact slowed the ball, otherwise it
// decays at 5 per second (5/54 per PES tick).
float RecentHitAfter(const pes::PesBallConstants& c, float recentHit, float speedInSq,
                     float speedOutSq, float dt_s);

}  // namespace pesball
