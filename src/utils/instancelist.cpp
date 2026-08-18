#include "utils/instancelist.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace InstanceList {

std::vector<Placement> Parse(const std::string& text) {
  std::vector<Placement> places;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    const std::string::size_type hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    std::istringstream values(line);
    Placement place;
    if (!(values >> place.x)) continue;
    if (!(values >> place.y)) continue;
    if (!(values >> place.z)) continue;
    if (!(values >> place.yaw)) place.yaw = 0.0f;
    places.push_back(place);
  }
  return places;
}

std::vector<Placement> Load(const std::string& path) {
  std::ifstream file(path.c_str());
  if (!file.good()) return std::vector<Placement>();
  std::ostringstream contents;
  contents << file.rdbuf();
  return Parse(contents.str());
}

Bounds Extent(const std::vector<Placement>& places) {
  Bounds bounds;
  if (places.empty()) return bounds;
  bounds.valid = true;
  bounds.low[0] = bounds.high[0] = places[0].x;
  bounds.low[1] = bounds.high[1] = places[0].y;
  bounds.low[2] = bounds.high[2] = places[0].z;
  for (const Placement& place : places) {
    const float coords[3] = {place.x, place.y, place.z};
    for (int axis = 0; axis < 3; ++axis) {
      if (coords[axis] < bounds.low[axis]) bounds.low[axis] = coords[axis];
      if (coords[axis] > bounds.high[axis]) bounds.high[axis] = coords[axis];
    }
  }
  return bounds;
}

}  // namespace InstanceList
