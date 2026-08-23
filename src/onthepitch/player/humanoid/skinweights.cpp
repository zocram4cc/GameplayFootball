#include "onthepitch/player/humanoid/skinweights.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace blunted {

std::vector<SkinInfluence> SkinWeights::DecodeVertexColour(const Vector3& colour) {
  // Kept arithmetically identical to the loop this replaced, down to the double
  // literals: a body converted before the sidecar existed has to skin bit for bit
  // as it did, and the weights ride a float attribute.
  float totalWeight = 0.0;
  int jointIDs[3];
  float weights[3];
  for (int c = 0; c < 3; c++) {
    int jointID = floor(colour.coords[c] * 0.1);
    float weight = (colour.coords[c] - jointID * 10.0) / 9.0;
    jointIDs[c] = jointID;
    weights[c] = weight;
    totalWeight += weight;
  }

  std::vector<SkinInfluence> out;
  for (int c = 0; c < 3; c++) {
    if (weights[c] > 0.01f) {
      SkinInfluence influence;
      influence.jointID = jointIDs[c];
      influence.weight = weights[c] / totalWeight;
      out.push_back(influence);
    }
  }
  return out;
}

std::string SkinWeights::SidecarPath(const std::string& aseFilename) {
  const size_t dot = aseFilename.find_last_of('.');
  const size_t slash = aseFilename.find_last_of('/');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    return aseFilename + ".weights";
  return aseFilename.substr(0, dot) + ".weights";
}

void SkinWeights::AddVertexColour(const Vector3& position, const Vector3& colour) {
  // First writer wins, as the colour map has always done: two ASE objects sharing
  // a vertex position agree about it or the seam pass has already failed.
  colours.emplace(position, DecodeVertexColour(colour));
}

namespace {

// "12:0.75" -> (12, 0.75). Returns false on anything else.
bool ParseInfluence(const std::string& token, SkinInfluence& out) {
  const size_t colon = token.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == token.size())
    return false;
  const std::string idText = token.substr(0, colon);
  const std::string weightText = token.substr(colon + 1);
  char* end = nullptr;
  const long id = strtol(idText.c_str(), &end, 10);
  if (end != idText.c_str() + idText.size() || id < 0) return false;
  const float weight = strtof(weightText.c_str(), &end);
  if (end != weightText.c_str() + weightText.size()) return false;
  if (!std::isfinite(weight) || weight <= 0.0f) return false;
  out.jointID = (int)id;
  out.weight = weight;
  return true;
}

bool ParseCoordinate(const std::string& token, float& out) {
  // Parsed the way the ASE loader parses a vertex position - atof, narrowed to
  // float (aseloader.cpp) - and not with strtof. The two round differently in
  // decimal->double->float tie cases, our positions carry seven significant digits
  // against float's six, and the lookup is by exact equality: a coordinate that
  // lands one ULP away from its ASE twin silently costs that vertex its fingers.
  char* end = nullptr;
  const float value = (float)atof(token.c_str());
  strtod(token.c_str(), &end);
  if (end != token.c_str() + token.size() || !std::isfinite(value)) return false;
  out = value;
  return true;
}

}  // namespace

bool SkinWeights::LoadSidecar(const std::string& path) {
  std::ifstream file(path);
  if (!file.good()) return false;

  std::string line;
  if (!std::getline(file, line)) return false;
  // The header is what tells a weight file from anything else that happens to sit
  // beside a model; without it nothing here is trusted.
  // Exactly version 1, as the Python side reads it (read_weights): a prefix match
  // would take "# gfweights 10" for version 1 here while the tools reject it, and a
  // format bump must fail loudly on the older reader, not silently half-parse.
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
  if (line != "# gfweights 1") return false;

  std::map<Vector3, std::vector<SkinInfluence>> loaded;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream tokens(line);
    std::string token;
    float coords[3];
    bool ok = true;
    for (int c = 0; c < 3 && ok; c++) {
      ok = (bool)(tokens >> token) && ParseCoordinate(token, coords[c]);
    }
    if (!ok) continue;  // a rotten line costs its own vertex, not the file

    std::vector<SkinInfluence> influences;
    while (tokens >> token) {
      SkinInfluence influence;
      if (!ParseInfluence(token, influence)) {
        influences.clear();
        break;
      }
      influences.push_back(influence);
    }
    if (influences.empty()) continue;

    // strongest first, at most as many as PES itself weights a vertex to
    std::stable_sort(influences.begin(), influences.end(),
                     [](const SkinInfluence& a, const SkinInfluence& b) {
                       return a.weight > b.weight;
                     });
    if ((int)influences.size() > kMaxSkinInfluences)
      influences.resize(kMaxSkinInfluences);
    float total = 0.0f;
    for (const SkinInfluence& influence : influences) total += influence.weight;
    if (total <= 0.0f) continue;
    for (SkinInfluence& influence : influences) influence.weight /= total;

    loaded.emplace(Vector3(coords[0], coords[1], coords[2]), std::move(influences));
  }

  if (loaded.empty()) return false;
  sidecar = std::move(loaded);
  return true;
}

namespace {

// -> whether the vertex still rides anything after the cut.
bool ClampList(std::vector<SkinInfluence>& influences, int jointCount) {
  std::vector<SkinInfluence> kept;
  float total = 0.0f;
  for (const SkinInfluence& influence : influences) {
    if (influence.jointID >= jointCount) continue;
    kept.push_back(influence);
    total += influence.weight;
  }
  if (kept.empty() || total <= 0.0f) return false;
  for (SkinInfluence& influence : kept) influence.weight /= total;
  influences.swap(kept);
  return true;
}

}  // namespace

void SkinWeights::ClampToJointCount(int jointCount) {
  for (std::map<Vector3, std::vector<SkinInfluence>>::iterator it = sidecar.begin();
       it != sidecar.end();) {
    if (ClampList(it->second, jointCount)) {
      ++it;
      continue;
    }
    // Nothing valid left: the colour path takes over for this vertex, so the entry
    // must go rather than shadow it with an empty list.
    it = sidecar.erase(it);
  }
  for (std::map<Vector3, std::vector<SkinInfluence>>::iterator it = colours.begin();
       it != colours.end(); ++it) {
    if (!ClampList(it->second, jointCount)) {
      // No colour survives either. Every vertex must ride something (humanoidbase
      // asserts it), so the body root carries it rather than nothing.
      it->second.assign(1, SkinInfluence{0, 1.0f});
    }
  }
}

const std::vector<SkinInfluence>* SkinWeights::Find(const Vector3& position) const {
  std::map<Vector3, std::vector<SkinInfluence>>::const_iterator found =
      sidecar.find(position);
  if (found != sidecar.end()) return &found->second;
  found = colours.find(position);
  if (found != colours.end()) return &found->second;
  return nullptr;
}

}  // namespace blunted
