#include "utils/cloth.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace blunted {

namespace {

// Positions rounded to a millimetre, so an edge shared in space is shared here
// too. Imported meshes are unwelded at their UV seams - the same corner appears as
// two vertices - and an unwelded seam read per index looks like two borders, which
// would pin a line straight down the middle of a net panel.
typedef std::pair<std::pair<int, int>, int> Welded;

Welded Weld(const Vector3& v) {
  return Welded(std::pair<int, int>(static_cast<int>(std::lround(v.coords[0] * 1000.0f)),
                                    static_cast<int>(std::lround(v.coords[1] * 1000.0f))),
                static_cast<int>(std::lround(v.coords[2] * 1000.0f)));
}

}  // namespace

void Cloth::Build(const std::vector<Vector3>& restPositions, const std::vector<bool>& fixedPoints,
                  const std::vector<std::pair<int, int>>& linkPairs) {
  rest = restPositions;
  positions = restPositions;
  previous = restPositions;
  fixed = fixedPoints;
  fixed.resize(rest.size(), false);
  links.clear();
  lengths.clear();

  std::set<std::pair<int, int>> seen;
  const int count = static_cast<int>(rest.size());
  for (const std::pair<int, int>& link : linkPairs) {
    int a = link.first, b = link.second;
    if (a == b || a < 0 || b < 0 || a >= count || b >= count) continue;
    if (a > b) std::swap(a, b);
    if (!seen.insert(std::pair<int, int>(a, b)).second) continue;
    const float length = (rest[b] - rest[a]).GetLength();
    // Nothing to correct along, and no direction to do it in.
    if (length < 0.0001f) continue;
    // Two fixed ends can never move, so solving between them is wasted work.
    if (fixed[a] && fixed[b]) continue;
    links.push_back(std::pair<int, int>(a, b));
    lengths.push_back(length);
  }
}

void Cloth::Step(float dt, const Vector3& acceleration, float damping, int iterations) {
  if (positions.empty()) return;
  // A pause, a stall or a load hands over a step of whole seconds. Verlet squares
  // the step, so one of those throws the cloth into the stands and it never comes
  // back. Time is not conserved here: the cloth simply moves slower than the world
  // for that one frame, which nobody can see.
  dt = std::min(dt, kMaxStep_s);
  if (dt <= 0.0f) return;

  const Vector3 gravityStep = acceleration * (dt * dt);
  for (size_t i = 0; i < positions.size(); i++) {
    if (fixed[i]) {
      positions[i] = rest[i];
      previous[i] = rest[i];
      continue;
    }
    const Vector3 velocity = (positions[i] - previous[i]) * damping;
    previous[i] = positions[i];
    positions[i] += velocity + gravityStep;
  }
  Relax(iterations);
}

void Cloth::Relax(int iterations) {
  for (int pass = 0; pass < iterations; pass++) {
    for (size_t i = 0; i < links.size(); i++) {
      const int a = links[i].first, b = links[i].second;
      Vector3 delta = positions[b] - positions[a];
      const float distance = delta.GetLength();
      if (distance < 0.0001f) continue;
      // Half the error onto each free end; all of it onto the free one when its
      // partner is held.
      const float error = (distance - lengths[i]) / distance;
      const bool freeA = !fixed[a], freeB = !fixed[b];
      const float share = (freeA && freeB) ? 0.5f : 1.0f;
      if (freeA) positions[a] += delta * (error * share);
      if (freeB) positions[b] -= delta * (error * share);
    }
  }
}

void Cloth::Push(const Vector3& centre, float radius) {
  if (radius <= 0.0f) return;
  for (size_t i = 0; i < positions.size(); i++) {
    if (fixed[i]) continue;
    Vector3 delta = positions[i] - centre;
    const float distance = delta.GetLength();
    if (distance >= radius) continue;
    // A point exactly on the centre has no way out; push it along the cloth's own
    // rest direction, which for netting is out of the goal.
    if (distance < 0.0001f) {
      delta = (rest[i] - centre);
      if (delta.GetLength() < 0.0001f) continue;
    }
    positions[i] = centre + delta.GetNormalizedTo(radius);
  }
}

void Cloth::Reset() {
  positions = rest;
  previous = rest;
}

float Cloth::Speed() const {
  float worst = 0.0f;
  for (size_t i = 0; i < positions.size(); i++) {
    if (fixed[i]) continue;
    worst = std::max(worst, (positions[i] - previous[i]).GetLength());
  }
  return worst;
}

float Cloth::Displacement() const {
  float worst = 0.0f;
  for (size_t i = 0; i < positions.size(); i++) {
    worst = std::max(worst, (positions[i] - rest[i]).GetLength());
  }
  return worst;
}

std::vector<std::pair<int, int>> LinksFromTriangles(const std::vector<int>& indices) {
  std::set<std::pair<int, int>> seen;
  std::vector<std::pair<int, int>> links;
  for (size_t f = 0; f + 2 < indices.size(); f += 3) {
    const int corner[3] = {indices[f], indices[f + 1], indices[f + 2]};
    for (int e = 0; e < 3; e++) {
      int a = corner[e], b = corner[(e + 1) % 3];
      if (a == b) continue;
      if (a > b) std::swap(a, b);
      if (seen.insert(std::pair<int, int>(a, b)).second) {
        links.push_back(std::pair<int, int>(a, b));
      }
    }
  }
  return links;
}

std::vector<bool> BorderVertices(const std::vector<Vector3>& rest,
                                 const std::vector<int>& indices) {
  // Counted by welded position, so a seam does not read as two borders.
  std::map<std::pair<Welded, Welded>, int> uses;
  for (size_t f = 0; f + 2 < indices.size(); f += 3) {
    const int corner[3] = {indices[f], indices[f + 1], indices[f + 2]};
    for (int e = 0; e < 3; e++) {
      const int a = corner[e], b = corner[(e + 1) % 3];
      if (a < 0 || b < 0 || a >= static_cast<int>(rest.size()) ||
          b >= static_cast<int>(rest.size()))
        continue;
      Welded wa = Weld(rest[a]), wb = Weld(rest[b]);
      if (wa == wb) continue;
      if (wb < wa) std::swap(wa, wb);
      uses[std::pair<Welded, Welded>(wa, wb)]++;
    }
  }

  std::vector<bool> border(rest.size(), false);
  for (size_t f = 0; f + 2 < indices.size(); f += 3) {
    const int corner[3] = {indices[f], indices[f + 1], indices[f + 2]};
    for (int e = 0; e < 3; e++) {
      const int a = corner[e], b = corner[(e + 1) % 3];
      if (a < 0 || b < 0 || a >= static_cast<int>(rest.size()) ||
          b >= static_cast<int>(rest.size()))
        continue;
      Welded wa = Weld(rest[a]), wb = Weld(rest[b]);
      if (wa == wb) continue;
      if (wb < wa) std::swap(wa, wb);
      if (uses[std::pair<Welded, Welded>(wa, wb)] == 1) {
        border[a] = true;
        border[b] = true;
      }
    }
  }
  return border;
}

std::vector<bool> VerticesOnPlane(const std::vector<Vector3>& rest, int axis, float at,
                                  float tolerance) {
  std::vector<bool> on(rest.size(), false);
  if (axis < 0 || axis > 2) return on;
  for (size_t i = 0; i < rest.size(); i++) {
    on[i] = std::fabs(rest[i].coords[axis] - at) <= tolerance;
  }
  return on;
}

void UnionInto(std::vector<bool>& into, const std::vector<bool>& other) {
  const size_t count = std::min(into.size(), other.size());
  for (size_t i = 0; i < count; i++) into[i] = into[i] || other[i];
}

std::vector<bool> Both(const std::vector<bool>& a, const std::vector<bool>& b) {
  std::vector<bool> both(a.size(), false);
  const size_t count = std::min(a.size(), b.size());
  for (size_t i = 0; i < count; i++) both[i] = a[i] && b[i];
  return both;
}

std::vector<bool> VerticesNearAxis(const std::vector<Vector3>& rest, const Vector3& point,
                                   const Vector3& axis, float radius) {
  std::vector<bool> held(rest.size(), false);
  const Vector3 direction = axis.GetNormalized(Vector3(0, 0, 1));
  for (size_t i = 0; i < rest.size(); i++) {
    const Vector3 offset = rest[i] - point;
    const Vector3 along = direction * offset.GetDotProduct(direction);
    held[i] = (offset - along).GetLength() <= radius;
  }
  return held;
}

}  // namespace blunted
