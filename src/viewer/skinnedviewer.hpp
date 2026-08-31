#ifndef VIEWER_SKINNEDVIEWER_HPP
#define VIEWER_SKINNEDVIEWER_HPP

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/math/vector3.hpp"
#include "onthepitch/player/humanoid/humanoidbase.hpp"
#include "onthepitch/player/humanoid/skinweights.hpp"
#include "onthepitch/player/humanoid/skinning.hpp"
#include "scene/scene3d/scene3d.hpp"
#include "utils/animation.hpp"
#include "onthepitch/player/humanoid/handrig.hpp"

namespace blunted {

class ViewerSkinnedModel {
 public:
    struct Joint {
    boost::intrusive_ptr<Node> node;
    Vector3 origPos;
    Quaternion origOrientation;
    Vector3 position;
    Quaternion orientation;
  };

  ViewerSkinnedModel() = default;
  ~ViewerSkinnedModel();

  bool Load(const std::string& modelPath, std::shared_ptr<Scene3D> scene);
  void Pose(Animation* anim, int frame, Vector3 basePos = Vector3(0),
            radian baseYaw = 0, bool noPos = false);
  void PoseChoreo(Animation* anim, int frame, Vector3 pos, radian yaw);

  boost::intrusive_ptr<Node> GetTargetNode() const { return fullbodyTargetNode; }
  boost::intrusive_ptr<Node> GetHumanoidNode() const { return humanoidNode; }
  const std::vector<Joint>& GetJoints() const { return joints; }

 private:
  bool Prepare();
  void UpdateSkin(bool updateSrc);
  static void FillNodeMap(boost::intrusive_ptr<Node> node,
                          std::map<const std::string, boost::intrusive_ptr<Node>>& out);

  boost::intrusive_ptr<Node> humanoidNode;
  boost::intrusive_ptr<Node> fullbodyNode;
  boost::intrusive_ptr<Node> fullbodyTargetNode;
  boost::intrusive_ptr<Node> sourceSkel;
  boost::intrusive_ptr<Node> sourceMesh;
  std::map<const std::string, boost::intrusive_ptr<Node>> nodeMap;

  std::vector<Joint> joints;
  SkinWeights skinWeights;

  struct WeightedBone { int jointID = 0; float weight = 0.0f; };
  struct WeightedVertex { int vertexID = 0; std::vector<WeightedBone> bones; };
  std::vector<std::vector<WeightedVertex>> weightedVerticesVec;
  std::vector<FloatArray> uniqueFullbodyMesh;
  std::vector<int*> uniqueIndicesVec;
  std::vector<Skinning::JointTransform> jointTransforms;

  float zMultiplier = 1.0f;
  HandRig handRig;
  int fullbodySubgeomCount = 0;
  bool loaded = false;
};


// Draws the rig over the skinned body: RGB axes at every joint (X red,
// Y green, Z blue, in the joint's world frame), bone connectors and a root
// trajectory trail (both yellow), all rebuilt per frame from the same nodes
// the skinning reads - so what it shows IS what deforms the mesh. Enabled
// with --debug-skeleton; when off, none of this exists.

}  // namespace blunted

#endif
