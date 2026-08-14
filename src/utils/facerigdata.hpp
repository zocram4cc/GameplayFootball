// FaceRig data: skf_* weight maps (tools/pes21_import/face_weights.py) and
// translation-only pose blending. PES's facial rig is muscle-translation
// dominant, so v1 deforms vertices by weighted bone translations - no
// pivots needed. The engine-side FaceRig feeds PoseOffsets into the head
// geometry each time the expression state changes.

#ifndef _HPP_UTILS_FACERIGDATA
#define _HPP_UTILS_FACERIGDATA

#include <array>
#include <istream>
#include <map>
#include <string>
#include <vector>

namespace blunted {

struct FaceRigWeight {
  int bone = 0;          // index into FaceRigData::bones
  float weight = 0.0f;
};

struct FaceRigVertex {
  std::array<float, 3> position{};
  std::vector<FaceRigWeight> weights;
};

class FaceRigData {
public:
  bool Load(std::istream& in);

  // Per-vertex offsets for a pose given as bone-name -> translation,
  // scaled by blend (0..1). Bones absent from the pose contribute nothing.
  std::vector<std::array<float, 3>> PoseOffsets(
      const std::map<std::string, std::array<float, 3>>& pose,
      float blend) const;

  std::vector<std::string> bones;                  // skf_* names, index order
  std::vector<std::array<float, 3>> bonePivots;
  std::vector<FaceRigVertex> vertices;

private:
  std::map<std::string, int> boneIndex;
};

}  // namespace blunted

#endif
