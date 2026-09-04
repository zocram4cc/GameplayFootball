#include "viewer/skinnedviewer.hpp"

#include <filesystem>
#include <iostream>

#include "base/geometry/trianglemeshutils.hpp"
#include "base/log.hpp"
#include "base/utils.hpp"
#include "gamedefines.hpp"
#include "onthepitch/player/humanoid/jointorder.hpp"
#include "scene/objectfactory.hpp"
#include "scene/objects/geometry.hpp"
#include "utils/animation.hpp"
#include "utils/objectloader.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "scene/resources/geometrydata.hpp"
#include "types/resource.hpp"

namespace blunted {

ViewerSkinnedModel::~ViewerSkinnedModel() {
  for (auto* p : uniqueIndicesVec) delete[] p;
  for (auto& fa : uniqueFullbodyMesh) delete[] fa.data;
  // Exit() takes a node out of the scene and away from the graphics system that
  // observes it; a geometry destroyed while still observed aborts with
  // "Observer(s) still present at destruction time", which is how the viewer
  // died after drawing everything it was asked for. Same order as main().
  if (fullbodyTargetNode) {
    fullbodyTargetNode->Exit();
    fullbodyTargetNode.reset();
    fullbodyNode.reset();
  }
  if (humanoidNode) {
    humanoidNode->Exit();
    humanoidNode.reset();
  }
  if (sourceMesh) {
    sourceMesh->Exit();
    sourceMesh.reset();
  }
  if (sourceSkel) {
    sourceSkel->Exit();
    sourceSkel.reset();
  }
}

void ViewerSkinnedModel::FillNodeMap(
    boost::intrusive_ptr<Node> node,
    std::map<const std::string, boost::intrusive_ptr<Node>>& out) {
  out[node->GetName()] = node;
  std::vector<boost::intrusive_ptr<Node>> children;
  node->GetNodes(children);
  for (auto& child : children) FillNodeMap(child, out);
}

static std::string ResolveForViewer(const std::string& model, std::string& scratch) {
  scratch.clear();
  std::filesystem::path path(model);
  if (path.extension() != ".ase") return model;
  const std::filesystem::path sibling = path.parent_path() / "fullbody.object";
  if (std::filesystem::exists(sibling)) return sibling.string();
  const std::filesystem::path wrapper =
      path.parent_path() / (path.stem().string() + ".gfviewer.object");
  std::ofstream file(wrapper);
  if (!file.good()) return model;
  file << "<object>\n\t<geometry>\n\t\t<filename>" << path.filename().string()
       << "</filename>\n\t\t<name>fullbody</name>\n"
       << "\t\t<position>0, 0, 0</position>\n\t\t<rotation>0, 0, 0, 0</rotation>\n"
       << "\t</geometry>\n</object>\n";
  file.close();
  scratch = wrapper.string();
  return scratch;
}

bool ViewerSkinnedModel::Load(const std::string& modelPath,
                              std::shared_ptr<Scene3D> scene, const std::string& postfix) {
  ObjectLoader loader;
  sourceSkel = loader.LoadObject(scene, "media/objects/players/player.object");
  if (!sourceSkel) {
    std::cout << "skinned viewer: could not load media/objects/players/player.object\n";
    return false;
  }
  humanoidNode = boost::intrusive_ptr<Node>(new Node(*sourceSkel.get(), "", scene));
  humanoidNode->SetLocalMode(e_LocalMode_Absolute);

  std::string scratch;
  const std::string meshPath = ResolveForViewer(modelPath, scratch);
  sourceMesh = loader.LoadObject(scene, meshPath);
  if (!sourceMesh) {
    std::cout << "skinned viewer: could not load " << meshPath << "\n";
    if (!scratch.empty()) std::filesystem::remove(scratch);
    return false;
  }
  fullbodyTargetNode = boost::intrusive_ptr<Node>(new Node("fullbodyTarget"));
  fullbodyTargetNode->SetLocalMode(e_LocalMode_Absolute);
  scene->AddNode(fullbodyTargetNode);
  fullbodyNode =
      boost::intrusive_ptr<Node>(new Node(*sourceMesh.get(), postfix, scene));
  fullbodyNode->SetLocalMode(e_LocalMode_Absolute);
  fullbodyTargetNode->AddNode(fullbodyNode);
  if (!scratch.empty()) std::filesystem::remove(scratch);

  std::string weightsAse = modelPath;
  if (weightsAse.size() > 7 && weightsAse.substr(weightsAse.size() - 7) == ".object") {
    std::filesystem::path p(modelPath);
    std::string base = p.parent_path().string() + "/fullbody_" +
                       p.parent_path().filename().string() + ".ase";
    if (std::filesystem::exists(base))
      weightsAse = base;
    else
      weightsAse = p.parent_path().string() + "/" + p.stem().string() + ".ase";
  } else if (std::filesystem::is_directory(modelPath)) {
    weightsAse = modelPath + "/fullbody_" +
                 std::string(std::filesystem::path(modelPath).filename().c_str()) + ".ase";
  }
  LoadSkinWeights(skinWeights, weightsAse);
  if (skinWeights.VertexColourCount() == 0 && skinWeights.SidecarVertexCount() == 0) {
    std::filesystem::path p(modelPath);
    std::string alt;
    if (p.extension() == ".ase")
      alt = p.string();
    else if (std::filesystem::is_directory(p))
      alt = (p / ("fullbody_" + std::string(p.filename().c_str()) + ".ase")).string();
    if (!alt.empty() && alt != weightsAse) LoadSkinWeights(skinWeights, alt);
  }
  std::cout << "skinned viewer: weights: " << skinWeights.VertexColourCount()
            << " colour verts, " << skinWeights.SidecarVertexCount()
            << " sidecar verts\n";

  FillNodeMap(humanoidNode, nodeMap);
  // Debug: print skeleton nodes
  std::cout << "skinned viewer: skeleton nodes (" << nodeMap.size() << "): ";
  for (auto& kv : nodeMap) std::cout << kv.first << " ";
  std::cout << "\n";
  if (!Prepare()) {
    std::cout << "skinned viewer: Prepare failed\n";
    return false;
  }
  loaded = true;
  return true;
}

std::string ViewerSkinnedModel::authoringPose = "media/animations/base.anim.util";

bool ViewerSkinnedModel::Prepare() {
  Animation* baseAnim = new Animation();
  baseAnim->Load(authoringPose);
  // Ensure base anim joints exist in skeleton (some base anims have fewer joints than the full rig)
  for (size_t i = 0; i < baseAnim->GetNodeAnimations().size(); i++) {
    auto* na = baseAnim->GetNodeAnimations()[i];
    if (nodeMap.find(na->nodeName) == nodeMap.end()) {
      boost::intrusive_ptr<Node> dummy(new Node(na->nodeName));
      dummy->SetLocalMode(e_LocalMode_Absolute);
      humanoidNode->AddNode(dummy);
      nodeMap[na->nodeName] = dummy;
    }
  }
  struct Buf { Animation* anim = nullptr; int frameNum = 0; bool smooth = false; float smoothFactor = 0; Vector3 position; radian orientation = 0; std::map<std::string, BiasedOffset> offsets; };
  Buf buf;
  buf.anim = baseAnim;
  buf.position = Vector3(0);
  buf.orientation = 0;
  buf.anim->Apply(nodeMap, buf.frameNum, 0, buf.smooth, buf.smoothFactor,
                  buf.position, buf.orientation, buf.offsets, 0, false, true);
  // Apply() invalidates spatial caches through the "player" node; in the
  // match that node parents the whole skeleton, here it is a dummy leaf.
  // Without this the base-pose capture below reads stale rest-pose caches
  // and the authoring->bind bake silently degenerates to a no-op.
  humanoidNode->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  std::vector<boost::intrusive_ptr<Node>> dfsNodes;
  humanoidNode->GetNodes(dfsNodes, true);
  std::vector<std::string> dfsNames;
  dfsNames.reserve(dfsNodes.size());
  for (auto& n : dfsNodes) dfsNames.push_back(n->GetName());
  std::vector<boost::intrusive_ptr<Node>> jointsVec;
  jointsVec.reserve(dfsNodes.size());
  for (int idx : JointOrder::Permutation(dfsNames)) jointsVec.push_back(dfsNodes[idx]);
  joints.clear();
  for (size_t i = 0; i < jointsVec.size(); i++) {
    Joint j;
    j.node = jointsVec[i];
    j.origPos = jointsVec[i]->GetDerivedPosition();
    j.origOrientation = jointsVec[i]->GetDerivedRotation();
    joints.push_back(j);
  }
  skinWeights.ClampToJointCount((int)joints.size());
  // HandRig: same library Match uses (media/objects/players/handposes.txt).
  // Without it hands stay in the flat bind pose; with it they match Match's
  // per-frame ChooseHandPose. This is the 1:1 path for finger rig.
  if (handRig.Load("media/objects/players")) handRig.Bind(nodeMap);
  boost::intrusive_ptr<Resource<GeometryData>> fullbodyGeometryData =
      boost::static_pointer_cast<Geometry>(fullbodyNode->GetObject("fullbody"))
          ->GetGeometryData();
  fullbodyGeometryData->resourceMutex.lock();
  std::vector<MaterializedTriangleMesh>& meshes =
      fullbodyGeometryData->GetResource()->GetTriangleMeshesRef();
  fullbodySubgeomCount = meshes.size();
  weightedVerticesVec.clear();
  uniqueFullbodyMesh.clear();
  uniqueIndicesVec.clear();
  for (unsigned int subgeom = 0; subgeom < fullbodySubgeomCount; subgeom++) {
    std::vector<WeightedVertex> wvVec;
    weightedVerticesVec.push_back(wvVec);
    FloatArray meshRef;
    meshRef.data = meshes[subgeom].vertices;
    meshRef.size = meshes[subgeom].verticesDataSize;
    FloatArray uniqueMesh;
    int elementOffset = meshRef.size / GetTriangleMeshElementCount();
    std::vector<std::vector<Vector3>> uniqueVertices;
    std::map<std::pair<Vector3, Vector3>, int> uniqueLookup;
    int* uniqueIndices = new int[elementOffset / 3];
    for (int v = 0; v < elementOffset; v += 3) {
      std::vector<Vector3> elem;
      for (int e = 0; e < GetTriangleMeshElementCount(); e++) {
        elem.push_back(Vector3(meshRef.data[v + e * elementOffset],
                               meshRef.data[v + e * elementOffset + 1],
                               meshRef.data[v + e * elementOffset + 2]));
      }
      auto key = std::make_pair(elem[0], elem[2]);
      auto found = uniqueLookup.find(key);
      int index;
      if (found != uniqueLookup.end())
        index = found->second;
      else {
        uniqueVertices.push_back(elem);
        index = (int)uniqueVertices.size() - 1;
        uniqueLookup[key] = index;
      }
      uniqueIndices[v / 3] = index;
    }
    uniqueMesh.size = uniqueVertices.size() * 3 * GetTriangleMeshElementCount();
    uniqueMesh.data = new float[uniqueMesh.size];
    int uniqueElementOffset = uniqueMesh.size / GetTriangleMeshElementCount();
    for (unsigned int v = 0; v < uniqueVertices.size(); v++) {
      for (int e = 0; e < GetTriangleMeshElementCount(); e++) {
        uniqueMesh.data[v * 3 + e * uniqueElementOffset + 0] =
            uniqueVertices[v][e].coords[0];
        uniqueMesh.data[v * 3 + e * uniqueElementOffset + 1] =
            uniqueVertices[v][e].coords[1];
        uniqueMesh.data[v * 3 + e * uniqueElementOffset + 2] =
            uniqueVertices[v][e].coords[2];
      }
    }
    for (int v = 0; v < uniqueElementOffset; v += 3) {
      Vector3 vertexPos(uniqueMesh.data[v], uniqueMesh.data[v + 1],
                        uniqueMesh.data[v + 2]);
      WeightedVertex wv;
      wv.vertexID = v / 3;
      const std::vector<SkinInfluence>* influences = skinWeights.Find(vertexPos);
      if (!influences) {
        std::cout << "skinned viewer: no weight for " << vertexPos.coords[0] << " "
                  << vertexPos.coords[1] << " " << vertexPos.coords[2] << "\n";
        assert(influences);
      }
      for (auto& inf : *influences) {
        WeightedBone wb;
        wb.jointID = inf.jointID;
        wb.weight = inf.weight;
        wv.bones.push_back(wb);
      }
      // See HumanoidBase::PrepareFullbodyModel: bake at default height then scale
      // The uniqueMesh already holds the author-pose vertex; scaling here keeps the
      // viewer's baked mesh and Match's baked mesh identical.
      uniqueMesh.data[v + 0] *= zMultiplier;
      uniqueMesh.data[v + 1] *= zMultiplier;
      uniqueMesh.data[v + 2] *= zMultiplier;
      weightedVerticesVec[subgeom].push_back(wv);
    }
    uniqueFullbodyMesh.push_back(uniqueMesh);
    uniqueIndicesVec.push_back(uniqueIndices);
    delete[] meshes[subgeom].vertices;
    meshes[subgeom].vertices = new float[uniqueMesh.size];
    memcpy(meshes[subgeom].vertices, uniqueMesh.data,
           uniqueMesh.size * sizeof(float));
    meshes[subgeom].verticesDataSize = uniqueMesh.size;
    meshes[subgeom].indices.clear();
    for (int v = 0; v < elementOffset; v += 3)
      meshes[subgeom].indices.push_back(uniqueIndices[v / 3]);
  }
  fullbodyGeometryData->resourceMutex.unlock();
  static_cast<Geometry*>(fullbodyNode->GetObject("fullbody").get())
      ->OnUpdateGeometryData();
  Animation* straightAnim = new Animation();
  straightAnim->Load("media/animations/straight.anim.util");
  {
    std::map<std::string, BiasedOffset> offsets;
    // Ensure straight anim joints exist
    for (size_t i = 0; i < straightAnim->GetNodeAnimations().size(); i++) {
      auto* na = straightAnim->GetNodeAnimations()[i];
      if (nodeMap.find(na->nodeName) == nodeMap.end()) {
        boost::intrusive_ptr<Node> dummy(new Node(na->nodeName));
        dummy->SetLocalMode(e_LocalMode_Absolute);
        humanoidNode->AddNode(dummy);
        nodeMap[na->nodeName] = dummy;
      }
    }
    straightAnim->Apply(nodeMap, 0, 0, false, 0, Vector3(0), 0, offsets, 0, false,
                        true);
    humanoidNode->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  }
  for (size_t i = 0; i < joints.size(); i++) {
    joints[i].position = joints[i].node->GetDerivedPosition();
    joints[i].orientation =
        (joints[i].node->GetDerivedRotation() *
         joints[i].origOrientation.GetInverse())
            .GetNormalized();
  }
  UpdateSkin(true);
  for (size_t i = 0; i < joints.size(); i++) {
    joints[i].origPos = joints[i].node->GetDerivedPosition();
    joints[i].origOrientation = joints[i].node->GetDerivedRotation().GetNormalized();
  }
  delete straightAnim;
  delete baseAnim;
  return true;
}

void ViewerSkinnedModel::Pose(Animation* anim, int frame, Vector3 basePos,
                              radian baseYaw, bool noPos) {
  // The skeleton the viewer loads (player.object) is the gameplay rig.
  // Cutscene anims sometimes name a joint the gameplay rig does not have
  // (e.g. a face helper). Humanoid would assert; a viewer should just show
  // what it can and keep going so the defect is visible, not hidden behind
  // an abort.
  for (size_t i = 0; i < anim->GetNodeAnimations().size(); i++) {
    auto* na = anim->GetNodeAnimations()[i];
    if (nodeMap.find(na->nodeName) == nodeMap.end()) {
      std::cout << "skinned viewer: missing joint in skeleton: " << na->nodeName
                << " (adding dummy, frame " << frame << ")\n";
      boost::intrusive_ptr<Node> dummy(new Node(na->nodeName));
      dummy->SetLocalMode(e_LocalMode_Absolute);
      humanoidNode->AddNode(dummy);
      nodeMap[na->nodeName] = dummy;
      // Also need a Joint entry for skinning? No, dummy joints have no
      // vertices weighted to them, so they can be ignored for skinning.
      // But the skinning Codexpects joints.size() to match the skeleton's
      // joint count at Prepare time; adding a joint now would desync skin
      // weights. So we just add to nodeMap for Apply, not to joints.
    }
  }
  std::map<std::string, BiasedOffset> offsets;
  // Twelve arguments, not eleven: written short, `noPos` bound to timeDiff_ms
  // and the real noPos defaulted to false, so this path always applied the
  // clip's root travel however it was called (Animation::Apply, animation.hpp).
  anim->Apply(nodeMap, frame, 0, false, 0.0f, basePos, baseYaw, offsets, nullptr, 10, noPos,
              true);
  humanoidNode->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  if (handRig.IsActive()) {
    handRig.Apply(blunted::e_HandPose::Neutral);
    // SetRotation(q, false) skips invalidation; without this the finger
    // nodes skin from a mix of stale and fresh derived transforms.
    humanoidNode->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  }
  UpdateSkin(false);
}

void ViewerSkinnedModel::PoseChoreo(Animation* anim, int frame, Vector3 pos, radian yaw) {
  for (size_t i = 0; i < anim->GetNodeAnimations().size(); i++) {
    auto* na = anim->GetNodeAnimations()[i];
    if (nodeMap.find(na->nodeName) == nodeMap.end()) {
      std::cout << "skinned viewer: missing joint in skeleton (choreo): "
                << na->nodeName << "\n";
      boost::intrusive_ptr<Node> dummy(new Node(na->nodeName));
      dummy->SetLocalMode(e_LocalMode_Absolute);
      humanoidNode->AddNode(dummy);
      nodeMap[na->nodeName] = dummy;
    }
  }
  // Wrapped exactly as Humanoid::SetChoreoPose wraps it. A choreography's path
  // and the clip it plays are different lengths - the path counts on without
  // wrapping - and a frame off the end of the clip reads whatever is past it: a
  // survey of thirty celebrations through this viewer showed coaches lying flat
  // on the grass, and every one of them was this.
  const int frameCount = anim ? anim->GetFrameCount() : 0;
  frame = frameCount > 0 ? ((frame % frameCount) + frameCount) % frameCount : 0;
  std::map<std::string, BiasedOffset> offsets;
  anim->Apply(nodeMap, frame, 0, false, 0.0f, pos, yaw, offsets, nullptr, 0, true, true);
  humanoidNode->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  if (handRig.IsActive()) {
    blunted::e_HandPose hp = blunted::e_HandPose::Neutral;
    if (anim && anim->GetVariable("type") == "special") hp = blunted::e_HandPose::Celebrating;
    handRig.Apply(hp);
    humanoidNode->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  }
  UpdateSkin(false);
}


void ViewerSkinnedModel::UpdateSkin(bool updateSrc) {
  for (size_t i = 0; i < joints.size(); i++) {
    joints[i].orientation =
        (joints[i].node->GetDerivedRotation() *
         joints[i].origOrientation.GetInverse())
            .GetNormalized();
    joints[i].position = joints[i].node->GetDerivedPosition();
  }
  if (jointTransforms.size() != joints.size()) jointTransforms.resize(joints.size());
  for (size_t j = 0; j < joints.size(); j++) {
    jointTransforms[j] = Skinning::MakeJointTransform(
        joints[j].orientation, joints[j].origPos, joints[j].position, zMultiplier);
  }
  boost::intrusive_ptr<Resource<GeometryData>> fullbodyGeometryData =
      boost::static_pointer_cast<Geometry>(fullbodyNode->GetObject("fullbody"))
          ->GetGeometryData();
  fullbodyGeometryData->resourceMutex.lock();
  std::vector<MaterializedTriangleMesh>& meshes =
      fullbodyGeometryData->GetResource()->GetTriangleMeshesRef();
  static const int directionOffsets[3] = {1, 3, 4};
  for (unsigned int subgeom = 0; subgeom < fullbodySubgeomCount; subgeom++) {
    FloatArray& uniqueMesh = uniqueFullbodyMesh[subgeom];
    const std::vector<WeightedVertex>& wverts = weightedVerticesVec[subgeom];
    int uniqueElementOffset = uniqueMesh.size / GetTriangleMeshElementCount();
    float* target = meshes[subgeom].vertices;
    Skinning::JointTransform blended;
    Vector3 result;
    const int directionCount =
        meshes[subgeom].material.normalTexture ? 3 : 1;
    for (int v = 0; v < (int)wverts.size(); v++) {
      const auto& bones = wverts[v].bones;
      const bool blendedInfluences = bones.size() > 1;
      if (blendedInfluences) {
        Skinning::ZeroTransform(blended);
        for (auto& b : bones)
          Skinning::AddWeighted(blended, jointTransforms[b.jointID], b.weight);
      } else {
        blended = jointTransforms[bones[0].jointID];
      }
      const int at = wverts[v].vertexID * 3;
      Skinning::TransformPoint(blended, &uniqueMesh.data[at], result.coords);
      if (updateSrc) memcpy(&uniqueMesh.data[at], result.coords, 3 * sizeof(float));
      memcpy(&target[at], result.coords, 3 * sizeof(float));
      for (int d = 0; d < directionCount; d++) {
        const int atDir = at + uniqueElementOffset * directionOffsets[d];
        Skinning::TransformDirection(blended, &uniqueMesh.data[atDir],
                                     result.coords);
        if (blendedInfluences) result.FastNormalize();
        if (updateSrc) memcpy(&uniqueMesh.data[atDir], result.coords, 3 * sizeof(float));
        memcpy(&target[atDir], result.coords, 3 * sizeof(float));
      }
    }
  }
  fullbodyGeometryData->resourceMutex.unlock();
  static_cast<Geometry*>(fullbodyNode->GetObject("fullbody").get())
      ->OnUpdateGeometryData(false);
}

}  // namespace blunted
