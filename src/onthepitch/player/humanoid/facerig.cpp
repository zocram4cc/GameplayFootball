#include "facerig.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

#include "utils/faceanim.hpp"

namespace blunted {

// expression -> the imported PES pose driving it (pack names)
static const std::map<e_FaceExpression, const char*> kPoseFiles = {
    {e_FaceExpression::Neutral, "neut.faceanim"},
    {e_FaceExpression::Happy, "smil_soft.faceanim"},
    {e_FaceExpression::Sad, "loss_brwtrb_mmov_soft.faceanim"},
    {e_FaceExpression::Exert, "pow_brwnit_grit_hard.faceanim"},
};

bool FaceRig::Load(const std::string& modelDir) {
  active = false;
  std::ifstream weights(modelDir + "/faceweights.txt");
  if (!weights.good()) return false;
  if (!data.Load(weights)) return false;

  for (const auto& entry : kPoseFiles) {
    std::ifstream file(modelDir + "/expressions/" + entry.second);
    if (!file.good()) continue;
    FaceAnim anim;
    if (!anim.Load(file)) continue;
    std::map<std::string, std::array<float, 3>> pose;
    for (const auto& track : anim.GetTranslationTracks()) {
      if (!track.second.empty()) {
        const auto& v = track.second.front().values;
        pose[track.first] = {v[0], v[1], v[2]};
      }
    }
    if (!pose.empty()) poses[entry.first] = pose;
  }
  active = poses.size() > 1;  // needs at least neutral + one expression
  return active;
}

void FaceRig::Bind(boost::intrusive_ptr<Geometry> geometry) {
  if (!active) return;
  boundGeometry = geometry;
  meshBindings.assign(data.vertices.size(), {});

  std::vector<MaterializedTriangleMesh>& tmesh =
      geometry->GetGeometryData()->GetResource()->GetTriangleMeshesRef();
  const float epsilon = 1e-4f;
  int bound = 0;
  for (size_t v = 0; v < data.vertices.size(); v++) {
    const auto& p = data.vertices[v].position;
    for (size_t sub = 0; sub < tmesh.size(); sub++) {
      int elements = tmesh[sub].verticesDataSize;
      for (int e = 0; e + 2 < elements; e += 3) {
        float* vp = &tmesh[sub].vertices[e];
        if (std::fabs(vp[0] - p[0]) < epsilon &&
            std::fabs(vp[1] - p[1]) < epsilon &&
            std::fabs(vp[2] - p[2]) < epsilon) {
          meshBindings[v].push_back({(int)sub, e});
          bound++;
        }
      }
    }
  }
  if (bound == 0) active = false;
}

void FaceRig::SetExpression(e_FaceExpression expression) {
  if (!active || expression == current) return;
  auto pose = poses.find(expression);
  if (pose == poses.end()) return;

  // reset to neutral geometry first (undo the current pose), then apply
  auto currentPose = poses.find(current);
  if (currentPose != poses.end()) {
    auto undo = data.PoseOffsets(currentPose->second, -1.0f);
    ApplyOffsets(undo);
  }
  ApplyOffsets(data.PoseOffsets(pose->second, 1.0f));
  current = expression;
}

void FaceRig::ApplyOffsets(const std::vector<std::array<float, 3>>& offsets) {
  if (!boundGeometry) return;
  std::vector<MaterializedTriangleMesh>& tmesh =
      boundGeometry->GetGeometryData()->GetResource()->GetTriangleMeshesRef();
  for (size_t v = 0; v < offsets.size() && v < meshBindings.size(); v++) {
    for (const auto& binding : meshBindings[v]) {
      float* vp = &tmesh[binding.first].vertices[binding.second];
      vp[0] += offsets[v][0];
      vp[1] += offsets[v][1];
      vp[2] += offsets[v][2];
    }
  }
  boundGeometry->OnUpdateGeometryData(false);
}

}  // namespace blunted
