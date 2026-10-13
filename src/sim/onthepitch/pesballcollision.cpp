#include "onthepitch/pesballcollision.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pesball {
namespace {

using pes::Vec3;

Vec3 Sub(Vec3 a, Vec3 b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) {
  return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Normalized(Vec3 a) {
  const float l = Length(a);
  return l > 0.0f ? a * (1.0f / l) : a;
}
float SignOf(float v) { return v < 0.0f ? -1.0f : 1.0f; }

// FUN_140a907c0(A, B, C, R), literal: `a` takes x and z only, `b` and `c` are
// 3-D. The crossbar call packs (x, y, 0), so there `a` misses dy^2 (report 3.2).
int SegmentCircleRoots(Vec3 A, Vec3 B, Vec3 C, float R, float roots[2]) {
  constexpr float SINGLE_ROOT_EPSILON = 1.1920929e-7f;  // FLT_EPSILON, raw 0x140a907c0
  const float a = (A.x - B.x) * (A.x - B.x) + (A.z - B.z) * (A.z - B.z);
  if (a == 0.0f) return 0;
  const Vec3 d = Sub(A, C);
  const float b = Dot(Sub(B, A), d);
  const float disc = b * b - (Dot(d, d) - R * R) * a;
  if (std::fabs(disc) < SINGLE_ROOT_EPSILON) {
    roots[0] = -b / a;
    return 1;
  }
  if (disc <= 0.0f) return 0;
  const float s = std::sqrt(disc);
  roots[0] = (-b - s) / a;
  roots[1] = (s - b) / a;
  return 2;
}

Vec3 Rotate(Vec3 axis, float angle_rad, Vec3 v) {
  const Vec3 a = Normalized(axis);
  const float c = std::cos(angle_rad), s = std::sin(angle_rad);
  return v * c + Cross(a, v) * s + a * (Dot(a, v) * (1.0f - c));
}

// FUN_14086fc60: the normal tilted towards the incoming direction's
// perpendicular, most for a glancing hit (report 3.3).
Vec3 Tilt(const pes::BallImageConstants& k, Vec3 n, Vec3 v, float c) {
  constexpr float RIGHT_ANGLE_DEG = 90.0f;
  const Vec3 w = Normalized(v) * -1.0f;
  const Vec3 axis = Cross(w, n);
  if (Length(axis) == 0.0f) return n;
  const float a = std::acos(std::clamp(Dot(n, w), -1.0f, 1.0f)) * k.degreesPerRadian;
  const float f = (1.0f - std::fabs(RIGHT_ANGLE_DEG - a) / RIGHT_ANGLE_DEG) * c;
  return Rotate(axis, ((1.0f - f) * a + f * RIGHT_ANGLE_DEG) / k.degreesPerRadian, w);
}

}  // namespace

std::optional<FrameHit> GoalFrameHit(const GoalFrame& g, float ballRadius, Vec3 p0, Vec3 p1,
                                     Vec3 v) {
  const float sx = SignOf(p1.x), sz = SignOf(p1.z);
  const float postHalf = g.postThickness_m * 0.5f;
  const float axisX = (g.lineX_m - postHalf) * sx;
  const Vec3 post(axisX, 0.0f, (g.innerHalfWidth_m + postHalf) * sz);
  const Vec3 bar(axisX, g.innerHeight_m + g.crossbarThickness_m * 0.5f, 0.0f);
  const float R = postHalf + ballRadius;
  const Vec3 d = Sub(p1, p0);

  float best = -1.0f;
  bool isPost = true;
  float roots[2];
  for (int i = 0, n = SegmentCircleRoots(Vec3(p0.x, 0, p0.z), Vec3(p1.x, 0, p1.z), post, R, roots);
       i < n; ++i) {
    const float t = roots[i];
    if (t >= 0.0f && t <= 1.0f && p0.y + t * d.y <= g.innerHeight_m + postHalf &&
        (best < 0.0f || t < best))
      best = t;
  }
  for (int i = 0, n = SegmentCircleRoots(Vec3(p0.x, p0.y, 0), Vec3(p1.x, p1.y, 0), bar, R, roots);
       i < n; ++i) {
    const float t = roots[i];
    if (t >= 0.0f && t <= 1.0f && (best < 0.0f || t < best) &&
        std::fabs(p0.z + t * d.z) <= g.innerHalfWidth_m + postHalf) {
      best = t;
      isPost = false;
    }
  }
  if (best < 0.0f) return std::nullopt;

  const Vec3 at = p0 + d * best;
  const Vec3 axisPoint = isPost ? Vec3(post.x, at.y, post.z) : Vec3(bar.x, bar.y, at.z);
  const Vec3 surface = axisPoint + Sub(at, axisPoint) * (postHalf / R);
  Vec3 n = Sub(surface, axisPoint);
  (isPost ? n.y : n.z) = 0.0f;
  n = Normalized(n);
  // Leaving the frame: PES falls back to the mesh record, which this engine
  // does not have; nothing to respond to.
  if (Dot(n, v) >= 0.0f) return std::nullopt;
  return FrameHit{surface, n};
}

void FrameContact(const pes::PesBallConstants& c, const FrameHit& hit, float b, BallState* s) {
  const pes::BallImageConstants& k = c.image;
  const float r = k.ballRadius;
  const Vec3 v = s->velocity;
  const Vec3 n = hit.normal;

  const float T = (1.0f - b) * k.staticRetention + b;
  const float E = (1.0f - b) * k.staticRestitution + b;
  const Vec3 n2 = Dot(n, v) < 0.0f ? Tilt(k, n, v, k.frameTilt) : n;

  // FUN_140875170 for the ball's own reflection (the frame does not move).
  const Vec3 un = n2 * Dot(n2, v);
  const Vec3 u = Sub(v, un) * T + un * -E;
  const Vec3 spinFromBounce = Cross(n2, u) * (1.0f / (2.0f * r * k.pushGain));
  Vec3 vo = u;

  // A front frame hit always snaps the ball out to the surface.
  Vec3 p = hit.surface + n2 * r;
  p.y = std::max(p.y, r);
  vo.y -= k.gravity / k.tickHz * b;
  const float tick_s = 1.0f / k.tickHz;
  if (p.y + vo.y * tick_s < r) {
    const float vy = k.tickHz * (r - p.y - vo.y * tick_s) + vo.y;
    const float horizontal = std::sqrt(std::max(0.0f, Dot(vo, vo) - vy * vy));
    const Vec3 xz = Normalized(Vec3(vo.x, 0.0f, vo.z)) * horizontal;
    vo = Vec3(xz.x, vy, xz.z);
  }

  const float inSq = Dot(v, v);
  if (b > 0.0f && Dot(vo, vo) > inSq && Dot(vo, vo) > k.speedUpGateSq)
    vo = vo * (std::sqrt(inSq) / Length(vo));
  if (Length(vo) >= k.speedCap_mps) vo = vo * (k.speedCap_mps / Length(vo));
  if (std::fabs(vo.y) >= k.verticalCap_mps) vo = vo * (k.verticalCap_mps / std::fabs(vo.y));

  const float approach_kmh = std::fabs(Dot(v, n)) * k.kmhPerMs;
  const float lambda = k.spinBlendBase +
                       k.spinBlendSpan * std::clamp((approach_kmh - k.spinBlendFloor_kmh) *
                                                        k.spinBlendRampPerKmh,
                                                    0.0f, 1.0f);
  const Vec3 w = s->spinRaw * k.spinScale;
  Vec3 wo = w + Sub(spinFromBounce, w) * lambda;
  const float cap_rad = k.spinCap_revPerS * 2.0f * std::numbers::pi_v<float>;
  if (Length(wo) > cap_rad) wo = wo * (cap_rad / Length(wo));

  s->position = p;
  s->velocity = vo;
  s->spinRaw = wo * k.invSpinScale;
}

float RecentHitAfter(const pes::PesBallConstants& c, float b, float speedInSq, float speedOutSq,
                     float dt_s) {
  if (speedOutSq < speedInSq) return 1.0f;
  return std::max(0.0f, b - c.image.recentHitDecayPerS * dt_s);
}

}  // namespace pesball
