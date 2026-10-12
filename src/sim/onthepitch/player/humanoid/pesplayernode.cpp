#include "pesplayernode.hpp"

#include <string_view>
#include <vector>

#include "base/geometry/trianglemeshutils.hpp"
#include "rig/rig.hpp"
#include "scene/objects/geometry.hpp"

using namespace blunted;

namespace {

// Bounding boxes, in metres and GF space, of the collision meshes GF's
// data/media/objects/players/player.object attaches to these nodes: the
// MESH_VERTEX extents of data/media/objects/players/models/<mesh>.ase at GF
// commit efd50d91 (GF's ASE loader uses the vertices as stored). The ball and
// the tackle tests read nothing of a body part but this box and the node it
// hangs from (Match::CheckBallCollisions, Match::CheckHumanoidCollision), and
// AnimCollection reads only the node position.
struct CollisionBox {
  float min[3];
  float max[3];
};
constexpr CollisionBox kFootBox = {{-0.0412f, -0.1607f, -0.1139f}, {0.0419f, 0.0961f, -0.0001f}};
constexpr CollisionBox kHeadBox = {{-0.0881f, -0.1302f, -0.0448f}, {0.0887f, 0.1060f, 0.2872f}};
constexpr CollisionBox kLowerArmBox = {{-0.0393f, -0.0519f, -0.3414f}, {0.0387f, 0.0404f, 0.0359f}};
constexpr CollisionBox kLowerLegBox = {{-0.0590f, -0.0494f, -0.4972f}, {0.0604f, 0.0846f, 0.0448f}};
constexpr CollisionBox kPelvisBox = {{-0.1958f, -0.1131f, -0.0622f}, {0.1959f, 0.1271f, 0.2318f}};
constexpr CollisionBox kTrunkBox = {{-0.2290f, -0.1300f, -0.0820f}, {0.2290f, 0.1170f, 0.5250f}};
constexpr CollisionBox kUpperArmBox = {{-0.0590f, -0.0700f, -0.3400f}, {0.0590f, 0.0710f, -0.0440f}};
constexpr CollisionBox kUpperLegBox = {{-0.1013f, -0.0894f, -0.4543f}, {0.1013f, 0.1051f, 0.0784f}};

// Node -> geometry name and box, as player.object pairs them.
struct CollisionPart {
  std::string_view node;
  const char* geometry;
  const CollisionBox* box;
};
constexpr CollisionPart kCollisionParts[] = {
    {"body", "pelvis", &kPelvisBox},
    {"left_thigh", "left_upperleg", &kUpperLegBox},
    {"left_knee", "left_lowerleg", &kLowerLegBox},
    {"left_ankle", "left_foot", &kFootBox},
    {"right_thigh", "right_upperleg", &kUpperLegBox},
    {"right_knee", "right_lowerleg", &kLowerLegBox},
    {"right_ankle", "right_foot", &kFootBox},
    {"middle", "trunk", &kTrunkBox},
    {"head", "head", &kHeadBox},
    {"left_shoulder", "left_upperarm", &kUpperArmBox},
    {"left_elbow", "left_lowerarm", &kLowerArmBox},
    {"right_shoulder", "right_upperarm", &kUpperArmBox},
    {"right_elbow", "right_lowerarm", &kLowerArmBox},
};

// One triangle spanning the box: min, max, min. GeometryData's AABB is the
// extent of its triangle positions (GetTriangleMeshAABB), and a vertex carries
// GetTriangleMeshElementCount() attribute triples.
constexpr int kBoxTriangleVertices = 3;
constexpr int kCoords = 3;

boost::intrusive_ptr<Geometry> MakeCollisionGeometry(const CollisionPart& part) {
  const int dataSize = kBoxTriangleVertices * kCoords * GetTriangleMeshElementCount();
  float* vertices = new float[dataSize]();
  const float* corners[kBoxTriangleVertices] = {part.box->min, part.box->max, part.box->min};
  for (int v = 0; v < kBoxTriangleVertices; v++)
    for (int c = 0; c < kCoords; c++) vertices[v * kCoords + c] = corners[v][c];

  boost::intrusive_ptr<Resource<GeometryData>> data(new Resource<GeometryData>(part.geometry));
  data->GetResource()->AddTriangleMesh(vertices, dataSize, {});

  boost::intrusive_ptr<Geometry> geometry(new Geometry(part.geometry));
  geometry->SetGeometryData(data);
  return geometry;
}

}  // namespace

boost::intrusive_ptr<Node> BuildPlayerNode(const pesrig::Rig& rig) {
  boost::intrusive_ptr<Node> player(new Node("player"));
  player->SetLocalMode(e_LocalMode_Absolute);

  std::vector<boost::intrusive_ptr<Node>> joints;
  joints.reserve(rig.bones.size());
  for (const pesrig::RigBone& bone : rig.bones) {
    boost::intrusive_ptr<Node> joint(new Node(bone.gfName));
    // Fox (Y up, +Z forward) to GF (Z up): (x, y, z) -> (x, -z, y).
    joint->SetPosition(Vector3(bone.animLocalPos.x, -bone.animLocalPos.z, bone.animLocalPos.y));
    joint->SetRotation(QUATERNION_IDENTITY);
    for (const CollisionPart& part : kCollisionParts)
      if (part.node == bone.gfName) joint->AddObject(MakeCollisionGeometry(part));
    (bone.parent < 0 ? player : joints.at(bone.parent))->AddNode(joint);
    joints.push_back(joint);
  }
  return player;
}
