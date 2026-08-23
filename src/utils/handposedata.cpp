#include "utils/handposedata.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace blunted {

namespace {

bool ParseFloat(const std::string& token, float& out) {
  char* end = nullptr;
  const float value = strtof(token.c_str(), &end);
  if (end != token.c_str() + token.size() || !std::isfinite(value)) return false;
  out = value;
  return true;
}

}  // namespace

bool HandPoseData::Load(std::istream& in) {
  poses.clear();
  joints.clear();

  std::string line;
  if (!std::getline(in, line)) return false;
  // The first clause of what stood here compared sixteen characters against a
  // fifteen-character literal - dead for any full-length line. And exactly version
  // 1: a prefix match would read "# gfhandposes 10" as this format.
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  if (line != "# gfhandposes 1") return false;

  HandPose* current = nullptr;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    std::string first;
    fields >> first;
    if (first == "pose") {
      std::string name;
      if (!(fields >> name)) continue;
      current = &poses[name];
      continue;
    }
    if (!current) continue;

    float components[4];
    bool ok = true;
    for (int i = 0; i < 4 && ok; i++) {
      std::string token;
      ok = (bool)(fields >> token) && ParseFloat(token, components[i]);
    }
    if (!ok) continue;  // a rotten line costs its own joint, not the file

    Quaternion rotation(components[0], components[1], components[2], components[3]);
    rotation.Normalize();
    if (current->find(first) == current->end() &&
        std::find(joints.begin(), joints.end(), first) == joints.end()) {
      joints.push_back(first);
    }
    (*current)[first] = rotation;
  }

  // A file that named poses but landed no rotation in any of them is not a pose
  // library, whatever its header says.
  for (const std::pair<const std::string, HandPose>& pose : poses) {
    if (!pose.second.empty()) return true;
  }
  poses.clear();
  joints.clear();
  return false;
}

const HandPose* HandPoseData::Find(const std::string& pose) const {
  std::map<std::string, HandPose>::const_iterator found = poses.find(pose);
  return found == poses.end() ? nullptr : &found->second;
}

void BlendHandPose(HandPose& current, const HandPose& target, float factor) {
  const Quaternion bind(QUATERNION_IDENTITY);

  // every joint the target asks for, whether the hand is already wearing it
  for (const std::pair<const std::string, Quaternion>& want : target) {
    HandPose::iterator have = current.find(want.first);
    if (have == current.end()) {
      current[want.first] = bind.GetSlerped(factor, want.second).GetNormalized();
    } else {
      have->second = have->second.GetSlerped(factor, want.second).GetNormalized();
    }
  }

  // and everything the hand is still wearing that the target does not want
  for (std::pair<const std::string, Quaternion>& worn : current) {
    if (target.find(worn.first) != target.end()) continue;
    worn.second = worn.second.GetSlerped(factor, bind).GetNormalized();
  }
}

}  // namespace blunted
