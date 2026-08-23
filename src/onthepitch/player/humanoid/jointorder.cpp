#include "onthepitch/player/humanoid/jointorder.hpp"

#include <map>

namespace JointOrder {

const std::vector<std::string>& BodyJoints() {
  static const std::vector<std::string> body = {
      "body",          "hip",            "left_thigh",     "left_knee",
      "left_ankle",    "right_thigh",    "right_knee",     "right_ankle",
      "middle",        "chest",          "neck",           "head",
      "left_clavicle", "left_shoulder",  "left_elbow",     "left_hand",
      "right_clavicle", "right_shoulder", "right_elbow",   "right_hand"};
  return body;
}

std::vector<int> Permutation(const std::vector<std::string>& dfsNames) {
  const std::vector<std::string>& body = BodyJoints();

  // where each body joint sits in the tree, if it is there at all
  std::map<std::string, int> position;
  for (size_t i = 0; i < dfsNames.size(); i++) {
    // a duplicate name cannot be addressed by number anyway; the first wins
    position.emplace(dfsNames[i], (int)i);
  }

  std::vector<int> out;
  out.reserve(dfsNames.size());
  std::vector<bool> taken(dfsNames.size(), false);
  for (const std::string& name : body) {
    std::map<std::string, int>::const_iterator found = position.find(name);
    if (found == position.end()) continue;
    out.push_back(found->second);
    taken[found->second] = true;
  }
  for (size_t i = 0; i < dfsNames.size(); i++) {
    if (!taken[i]) out.push_back((int)i);
  }
  return out;
}

}  // namespace JointOrder
