#include "facerigdata.hpp"

#include <cstdlib>
#include <sstream>

namespace blunted {

static std::vector<std::string> SplitCommas(const std::string& line) {
  std::vector<std::string> out;
  std::stringstream tokens(line);
  std::string field;
  while (std::getline(tokens, field, ',')) out.push_back(field);
  return out;
}

bool FaceRigData::Load(std::istream& in) {
  bones.clear();
  bonePivots.clear();
  vertices.clear();
  boneIndex.clear();

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    auto fields = SplitCommas(line);
    if (fields[0] == "bone" && fields.size() >= 5) {
      boneIndex[fields[1]] = (int)bones.size();
      bones.push_back(fields[1]);
      bonePivots.push_back({std::strtof(fields[2].c_str(), nullptr),
                            std::strtof(fields[3].c_str(), nullptr),
                            std::strtof(fields[4].c_str(), nullptr)});
    } else if (fields[0] == "v" && fields.size() >= 6) {
      FaceRigVertex vertex;
      vertex.position = {std::strtof(fields[2].c_str(), nullptr),
                         std::strtof(fields[3].c_str(), nullptr),
                         std::strtof(fields[4].c_str(), nullptr)};
      for (size_t i = 5; i < fields.size(); i++) {
        size_t colon = fields[i].find(':');
        if (colon == std::string::npos) continue;
        auto it = boneIndex.find(fields[i].substr(0, colon));
        if (it == boneIndex.end()) continue;
        FaceRigWeight w;
        w.bone = it->second;
        w.weight = std::strtof(fields[i].c_str() + colon + 1, nullptr);
        vertex.weights.push_back(w);
      }
      vertices.push_back(vertex);
    }
  }
  return !bones.empty() && !vertices.empty();
}

std::vector<std::array<float, 3>> FaceRigData::PoseOffsets(
    const std::map<std::string, std::array<float, 3>>& pose,
    float blend) const {
  // bone translations resolved to indices once
  std::vector<std::array<float, 3>> boneOffsets(bones.size(), {0, 0, 0});
  for (const auto& entry : pose) {
    auto it = boneIndex.find(entry.first);
    if (it == boneIndex.end()) continue;
    boneOffsets[it->second] = {entry.second[0] * blend,
                               entry.second[1] * blend,
                               entry.second[2] * blend};
  }

  std::vector<std::array<float, 3>> out(vertices.size(), {0, 0, 0});
  for (size_t v = 0; v < vertices.size(); v++) {
    for (const auto& w : vertices[v].weights) {
      out[v][0] += boneOffsets[w.bone][0] * w.weight;
      out[v][1] += boneOffsets[w.bone][1] * w.weight;
      out[v][2] += boneOffsets[w.bone][2] * w.weight;
    }
  }
  return out;
}

}  // namespace blunted
