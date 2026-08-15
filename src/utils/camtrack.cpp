#include "camtrack.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace blunted {

namespace {

std::array<float, 3> Cross(const std::array<float, 3>& a,
                           const std::array<float, 3>& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}

float Length(const std::array<float, 3>& v) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

bool Normalize(std::array<float, 3>& v) {
  float len = Length(v);
  if (len < 1e-6f) return false;
  for (int c = 0; c < 3; c++) v[c] /= len;
  return true;
}

// quaternion (x,y,z,w) from a right-handed orthonormal camera basis
// (columns: X = right, Y = up, Z = -forward), Shepperd's method
std::array<float, 4> QuatFromBasis(const std::array<float, 3>& x,
                                   const std::array<float, 3>& y,
                                   const std::array<float, 3>& z) {
  std::array<float, 4> q{};
  float trace = x[0] + y[1] + z[2];
  if (trace > 0.0f) {
    float s = std::sqrt(trace + 1.0f) * 2.0f;
    q[3] = 0.25f * s;
    q[0] = (y[2] - z[1]) / s;
    q[1] = (z[0] - x[2]) / s;
    q[2] = (x[1] - y[0]) / s;
  } else if (x[0] > y[1] && x[0] > z[2]) {
    float s = std::sqrt(1.0f + x[0] - y[1] - z[2]) * 2.0f;
    q[3] = (y[2] - z[1]) / s;
    q[0] = 0.25f * s;
    q[1] = (y[0] + x[1]) / s;
    q[2] = (z[0] + x[2]) / s;
  } else if (y[1] > z[2]) {
    float s = std::sqrt(1.0f + y[1] - x[0] - z[2]) * 2.0f;
    q[3] = (z[0] - x[2]) / s;
    q[0] = (y[0] + x[1]) / s;
    q[1] = 0.25f * s;
    q[2] = (z[1] + y[2]) / s;
  } else {
    float s = std::sqrt(1.0f + z[2] - x[0] - y[1]) * 2.0f;
    q[3] = (x[1] - y[0]) / s;
    q[0] = (z[0] + x[2]) / s;
    q[1] = (z[1] + y[2]) / s;
    q[2] = 0.25f * s;
  }
  return q;
}

}  // namespace

std::array<float, 3> CamTrackForward(const std::array<float, 4>& q) {
  // rotate local (0, 0, -1) by q
  return {-2.0f * (q[0] * q[2] + q[3] * q[1]),
          -2.0f * (q[1] * q[2] - q[3] * q[0]),
          -(1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]))};
}

CamTrackFrame RetargetCamTrackFrame(const CamTrackFrame& frame,
                                    const std::array<float, 3>& target,
                                    float minDistance,
                                    float subjectHalfHeight) {
  CamTrackFrame out = frame;

  // aim line camera -> target; degenerate (camera at the target) backs out
  // opposite the authored view axis
  std::array<float, 3> aim = {target[0] - out.position[0],
                              target[1] - out.position[1],
                              target[2] - out.position[2]};
  if (!Normalize(aim)) {
    aim = CamTrackForward(frame.rotation);
  }

  // minimum-distance clamp: push straight back along the aim line
  float distance = std::sqrt(
      (target[0] - out.position[0]) * (target[0] - out.position[0]) +
      (target[1] - out.position[1]) * (target[1] - out.position[1]) +
      (target[2] - out.position[2]) * (target[2] - out.position[2]));
  if (distance < minDistance) {
    for (int c = 0; c < 3; c++)
      out.position[c] = target[c] - aim[c] * minDistance;
    distance = minDistance;
  }

  // roll-free look-at: world up Z, fall back to Y when aiming straight up
  // or down
  std::array<float, 3> right = Cross(aim, {0.0f, 0.0f, 1.0f});
  if (!Normalize(right)) {
    right = Cross(aim, {0.0f, 1.0f, 0.0f});
    Normalize(right);
  }
  std::array<float, 3> up = Cross(right, aim);
  out.rotation =
      QuatFromBasis(right, up, {-aim[0], -aim[1], -aim[2]});

  // widen PES's telephoto only as much as the actual subject distance needs
  const float pi = 3.14159265358979f;
  float neededFov =
      2.0f * std::atan2(subjectHalfHeight, distance) * (180.0f / pi);
  out.fov = std::max(frame.fov, neededFov);

  // never near-clip the subject (some tracks ship a 35m near override)
  out.near = std::min(frame.near, std::max(0.1f, distance * 0.5f));
  // and never far-clip it either
  out.far = std::max(frame.far, distance + 50.0f);

  return out;
}

bool CamTrack::Load(std::istream& in) {
  frames.clear();
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::stringstream tokens(line);
    std::string field;
    std::vector<float> values;
    while (std::getline(tokens, field, ',')) {
      char* end = nullptr;
      float v = std::strtof(field.c_str(), &end);
      if (end == field.c_str()) { values.clear(); break; }
      values.push_back(v);
    }
    if (values.size() < 11) continue;
    CamTrackFrame frame;
    frame.position = {values[1], values[2], values[3]};
    frame.rotation = {values[4], values[5], values[6], values[7]};
    frame.fov = values[8];
    frame.near = values[9];
    frame.far = values[10];
    frames.push_back(frame);
  }
  return !frames.empty();
}

CamTrackFrame CamTrack::Sample(float frame) const {
  if (frames.empty()) return CamTrackFrame();
  if (frame <= 0.0f) return frames.front();
  if (frame >= frames.size() - 1) return frames.back();

  int i = (int)frame;
  float t = frame - i;
  const CamTrackFrame& a = frames[i];
  const CamTrackFrame& b = frames[i + 1];

  CamTrackFrame out;
  for (int c = 0; c < 3; c++)
    out.position[c] = a.position[c] + (b.position[c] - a.position[c]) * t;

  // nlerp with hemisphere fix
  float dot = 0.0f;
  for (int c = 0; c < 4; c++) dot += a.rotation[c] * b.rotation[c];
  float sign = dot < 0.0f ? -1.0f : 1.0f;
  float lengthSq = 0.0f;
  for (int c = 0; c < 4; c++) {
    out.rotation[c] = a.rotation[c] + (b.rotation[c] * sign - a.rotation[c]) * t;
    lengthSq += out.rotation[c] * out.rotation[c];
  }
  float invLen = lengthSq > 0.0f ? 1.0f / std::sqrt(lengthSq) : 1.0f;
  for (int c = 0; c < 4; c++) out.rotation[c] *= invLen;

  out.fov = a.fov + (b.fov - a.fov) * t;
  out.near = a.near + (b.near - a.near) * t;
  out.far = a.far + (b.far - a.far) * t;
  return out;
}

}  // namespace blunted
