#include "camtrack.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

namespace blunted {

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
