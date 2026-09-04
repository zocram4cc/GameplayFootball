// A standalone model viewer: one model, nothing else.
//
// Troubleshooting a model through a running match is the wrong tool. A match loads a
// stadium, a crowd, 22 players, the cutscene pools and a whole presentation before
// you can look at one mesh, and the debug bench is bolted onto
// Match::UpdateIngameCamera. This loads the model and stops.
//
// It uses the engine's own ASE loader on purpose. A viewer with its own parser would
// show what its parser thinks, not what the game sees, and the whole point is to find
// out why an imported model is missing geometry.
//
//   gfviewer <model.ase> [--shots N] [--out DIR] [--fov D] [--pitch R] [--wireframe]
//   gfviewer --cutscene <pack.chor> [--camtrack <track>] [--body <model>] --shots N --out DIR
//
// With --shots it writes N stills round a turntable and exits, which is what a
// headless machine needs; without, it opens a window and orbits with the mouse.
//
// --cutscene plays a PES choreography the way the match does - every slot cast on a
// skinned body, posed by its own clip at its own phase, and the camera driven by the
// .camtrack that filmed it - and spreads the N stills across its whole length. That is
// how a celebration whose actors lie down, or whose second half never comes, is
// looked at without waiting for a goal.
//
// The stills go to --out as raw RGBA at the context size, through the same recording
// path the game records a showcase through, so ffmpeg turns them into PNGs:
//
//   ffmpeg -f rawvideo -pixel_format rgba -video_size 1280x720 -i out.raw shot%02d.png
//
// The file holds N frames, occasionally N+1, and the first of them is sometimes empty:
// the recording stream opens against the swap chain rather than in step with it. A
// frame with no variance in it is one of those and can be dropped.

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <tuple>

#include "utils/cloth.hpp"
#include <vector>
#include "utils/camtrack.hpp"
#include "utils/entrancechoreo.hpp"
#include "viewer/skinnedviewer.hpp"

#include "base/log.hpp"
#include "base/math/vector3.hpp"
#include "main.hpp"
#include "framework/scheduler.hpp"
#include "managers/environmentmanager.hpp"
#include "types/iusertask.hpp"
#include "framework/tasksequence.hpp"
#include "blunted.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "managers/scenemanager.hpp"
#include "managers/systemmanager.hpp"
#include "scene/objectfactory.hpp"
#include "scene/objects/camera.hpp"
#include "scene/objects/geometry.hpp"
#include "scene/objects/light.hpp"
#include "scene/scene2d/scene2d.hpp"
#include "scene/scene3d/scene3d.hpp"
#include "systems/audio/audio_system.hpp"
#include "systems/graphics/graphics_system.hpp"
#include "systems/graphics/rendering/opengl_renderer3d.hpp"
#include "utils/modelinventory.hpp"
#include "utils/objectloader.hpp"
#include "utils/viewercamera.hpp"

using namespace blunted;

// The application context the engine expects a host to provide. main.cpp owns these
// for the game; a viewer is a different host, so it owns its own. They are the only
// reason this file is longer than it looks: the engine reaches for a configuration, a
// scene and a set of debug helpers from anywhere, and a second executable has to
// answer for all of them.
//
// Splitting them out of main.cpp into a shared app context is the better shape, and
// the right moment for it is when EDIT mode needs the same set - not in the middle of
// a session, where touching the game's entry point buys a risk and no capability.
namespace {

std::shared_ptr<Scene3D> viewerScene3D;
std::shared_ptr<Scene2D> viewerScene2D;
GraphicsSystem* viewerGraphics = nullptr;
Properties* viewerConfig = new Properties();
std::vector<IHIDevice*> viewerControllers;

}  // namespace

Properties* GetConfiguration() { return viewerConfig; }
std::shared_ptr<Scene2D> GetScene2D() { return viewerScene2D; }
std::shared_ptr<Scene3D> GetScene3D() { return viewerScene3D; }
GraphicsSystem* GetGraphicsSystem() { return viewerGraphics; }
Database* GetDB() { return nullptr; }
std::shared_ptr<MenuTask> GetMenuTask() { return std::shared_ptr<MenuTask>(); }
const std::vector<IHIDevice*>& GetControllers() { return viewerControllers; }
void AddGamepad(int, int) {}
void RemoveGamepad(int) {}
std::string GetActiveSaveDirectory() { return "."; }
e_DebugMode GetDebugMode() { return e_DebugMode_Off; }
bool SuperDebug() { return false; }
bool Verbose() { return false; }
bool IsReleaseVersion() { return true; }

// The debug helpers. A viewer draws the model and nothing else, so these are stubs
// rather than geometry nobody asked to see.
boost::intrusive_ptr<Geometry> GetGreenDebugPilon() { return boost::intrusive_ptr<Geometry>(); }
boost::intrusive_ptr<Geometry> GetBlueDebugPilon() { return boost::intrusive_ptr<Geometry>(); }
boost::intrusive_ptr<Geometry> GetYellowDebugPilon() { return boost::intrusive_ptr<Geometry>(); }
boost::intrusive_ptr<Geometry> GetRedDebugPilon() { return boost::intrusive_ptr<Geometry>(); }
boost::intrusive_ptr<Geometry> GetSmallDebugCircle1() { return boost::intrusive_ptr<Geometry>(); }
boost::intrusive_ptr<Geometry> GetSmallDebugCircle2() { return boost::intrusive_ptr<Geometry>(); }
boost::intrusive_ptr<Geometry> GetLargeDebugCircle() { return boost::intrusive_ptr<Geometry>(); }
void SetGreenDebugPilon(const Vector3&) {}
void SetBlueDebugPilon(const Vector3&) {}
void SetYellowDebugPilon(const Vector3&) {}
void SetRedDebugPilon(const Vector3&) {}
boost::intrusive_ptr<Image2D> GetDebugOverlay() { return boost::intrusive_ptr<Image2D>(); }
void GetDebugOverlayCoord(Match*, const Vector3&, int&, int&) {}

namespace {

struct Options {
  std::string model;
  std::string out = "viewer_shots";
  int shots = 0;
  float fov = 35.0f;
  float pitch = 0.25f;
  bool wireframe = false;
  // Drive the cloth simulation instead of a turntable, and shoot a ball through
  // it. The point is to see whether a surface actually moves: the netting, the
  // corner flags and the pennant all run the same Cloth, and a still frame of
  // any of them looks the same whether it is simulating or frozen.
  bool cloth = false;
  // Which meshes are the cloth, by a substring of their texture's name - the
  // same test the match uses to tell netting from woodwork.
  std::string clothTexture = "goalnetting";
  // Play a PES choreography instead of showing a model: the .chor, the camera
  // that filmed it (a sibling named after it when not given) and the body every
  // actor is cast on.
  std::string cutscene;
  std::string camtrack;
  std::string body = "media/objects/players/models/fullbody.ase";
  // PES's camera as authored, without the match's re-aim at the primary actor.
  bool authoredCamera = false;
  // Play one clip on the model instead of turning it: the whole point of a
  // viewer for a rig defect ("dragging verts on cutscenes" - owner, 04-09) is a
  // model under animation, which a turntable of the bind pose cannot show. This
  // is the mode AGENTS.md's health check calls for
  // (--anim media/animations/straight.anim.util must render a perfect T-pose).
  std::string anim;
  // Treat the model as authored in the rig's own pose rather than PES's render
  // bind: no authoring->bind bake. Every installed 4cc body tears with the bake
  // and none without it (skin_probe.py), so this is how to see which pose a
  // model is really authored in.
  bool noBake = false;
  // A head-and-shoulders still, framed on the top of the model and shot from
  // the front: what a lineup card wants. A pack normally ships portrait art,
  // and where it does that art wins - this is for a squad whose pack does not
  // (or is no longer on disk), so the card has a face instead of a blank.
  bool portrait = false;
};

Options Parse(int argc, const char** argv) {
  Options options;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    const bool hasNext = i + 1 < argc;
    if (arg == "--shots" && hasNext) options.shots = atoi(argv[++i]);
    else if (arg == "--out" && hasNext) options.out = argv[++i];
    else if (arg == "--fov" && hasNext) options.fov = atof(argv[++i]);
    else if (arg == "--pitch" && hasNext) options.pitch = atof(argv[++i]);
    else if (arg == "--wireframe") options.wireframe = true;
    else if (arg == "--cloth") options.cloth = true;
    else if (arg == "--cloth-texture" && hasNext) options.clothTexture = argv[++i];
    else if (arg == "--cutscene" && hasNext) options.cutscene = argv[++i];
    else if (arg == "--camtrack" && hasNext) options.camtrack = argv[++i];
    else if (arg == "--body" && hasNext) options.body = argv[++i];
    else if (arg == "--authored-camera") options.authoredCamera = true;
    else if (arg == "--anim" && hasNext) options.anim = argv[++i];
    else if (arg == "--no-bake") options.noBake = true;
    else if (arg == "--portrait") options.portrait = true;
    else if (!arg.empty() && arg[0] != '-') options.model = arg;
  }
  return options;
}

// Every mesh in the loaded node, read back out of the engine's own geometry so the
// inventory describes what the game holds rather than what a text parse guessed.
std::vector<ModelInventory::Mesh> ReadMeshes(boost::intrusive_ptr<Node> node) {
  std::vector<ModelInventory::Mesh> out;
  std::list<boost::intrusive_ptr<Geometry>> geoms;
  node->GetObjects<Geometry>(e_ObjectType_Geometry, geoms, true);
  for (auto& geom : geoms) {
    boost::intrusive_ptr<Resource<GeometryData>> data = geom->GetGeometryData();
    if (!data) continue;
    std::vector<MaterializedTriangleMesh>& parts = data->GetResource()->GetTriangleMeshesRef();
    for (size_t p = 0; p < parts.size(); p++) {
      ModelInventory::Mesh mesh;
      mesh.name = geom->GetName() + ":" + int_to_str((int)p);
      const float* verts = parts[p].vertices;
      const int floats = parts[p].verticesDataSize;
      mesh.vertices = ModelInventory::ReadPositions(verts, floats, GetTriangleMeshElementCount());
      if (mesh.vertices.empty()) continue;
      // The engine holds these unwelded: three vertices to a triangle, in order.
      for (int t = 0; t + 2 < (int)mesh.vertices.size(); t += 3)
        mesh.faces.push_back({t, t + 1, t + 2});
      out.push_back(mesh);
    }
  }
  return out;
}

// ObjectLoader reads the little XML wrapper an imported model ships beside its
// mesh, not the mesh itself - and handed a raw .ase it walks off the end of a
// parse that never matched and takes the process with it. Troubleshooting an
// import is exactly when you have the .ase path in your hand and not the
// wrapper's, so accept either: given a mesh, write the wrapper it is missing.
//
// The wrapper names its geometry relatively, so it has to sit in the mesh's own
// directory. Returns the path to load, and sets `scratch` when one was written
// so the caller can take it away again.
std::string ResolveModelPath(const std::string& model, std::string& scratch) {
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

// A surface, built as cloth and shot at.
//
// Whether the netting, the corner flags and the pennant actually simulate is not
// answerable from a still: a frozen cloth and a settling one look the same in one
// frame. This builds the same Cloth the match builds, from the same meshes picked
// by the same texture test, drives a ball through it and lets the frames show
// what happens.
struct ClothRig {
  Cloth cloth;
  std::vector<float*> corners;   // into the live vertex buffer
  std::vector<int> weld;         // which simulated point each corner follows
  std::list<boost::intrusive_ptr<Geometry>> geometry;
  Vector3 low, high;             // the surface's own bounds, for aiming the ball

  bool Build(boost::intrusive_ptr<Node> node, const std::string& textureMark) {
    node->GetObjects<Geometry>(e_ObjectType_Geometry, geometry, true);
    std::map<std::tuple<int, int, int>, int> welded;
    std::vector<Vector3> rest;
    std::vector<int> faces;
    for (boost::intrusive_ptr<Geometry>& geom : geometry) {
      std::vector<MaterializedTriangleMesh>& meshes =
          geom->GetGeometryData()->GetResource()->GetTriangleMeshesRef();
      for (unsigned int m = 0; m < meshes.size(); m++) {
        if (!meshes.at(m).material.diffuseTexture) continue;
        if (meshes.at(m).material.diffuseTexture->GetIdentString().find(textureMark) ==
            std::string::npos)
          continue;
        const int floats = meshes.at(m).verticesDataSize / GetTriangleMeshElementCount();
        for (int i = 0; i + 8 < floats; i += 9) {
          for (int c = 0; c < 3; c++) {
            const int at = i + c * 3;
            const Vector3 p(meshes.at(m).vertices[at + 0], meshes.at(m).vertices[at + 1],
                            meshes.at(m).vertices[at + 2]);
            const std::tuple<int, int, int> key(std::lround(p.coords[0] * 1000.0f),
                                                std::lround(p.coords[1] * 1000.0f),
                                                std::lround(p.coords[2] * 1000.0f));
            std::map<std::tuple<int, int, int>, int>::iterator found = welded.find(key);
            if (found == welded.end()) {
              found = welded.insert(std::make_pair(key, (int)rest.size())).first;
              rest.push_back(p);
            }
            corners.push_back(&(meshes.at(m).vertices[at]));
            weld.push_back(found->second);
            faces.push_back(found->second);
          }
        }
      }
    }
    if (rest.empty()) return false;

    low = rest[0];
    high = rest[0];
    for (const Vector3& p : rest) {
      for (int a = 0; a < 3; a++) {
        low.coords[a] = std::min(low.coords[a], p.coords[a]);
        high.coords[a] = std::max(high.coords[a], p.coords[a]);
      }
    }
    // The match's own rule for a goal net, restated on this model's bounds: tied
    // along its mouth, pegged down all round its foot, carried along its rear top
    // edge (match.cpp, PrepareGoalNetting). Everything between is free to billow.
    //
    // Per side, because goals.ase holds both of them. Measured over the pair, the
    // "mouth" plane lands 57 m away at the far goal and pins almost nothing that
    // is really tied to anything, and the net then falls for as long as you watch
    // it - 3.0 m and still going after sixty frames.
    std::vector<bool> held(rest.size(), false);
    for (int side = 0; side < 2; side++) {
      std::vector<Vector3> half;
      std::vector<int> back;
      for (unsigned int i = 0; i < rest.size(); i++) {
        if ((rest[i].coords[0] < 0.0f) != (side == 0)) continue;
        half.push_back(rest[i]);
        back.push_back(i);
      }
      if (half.empty()) continue;
      Vector3 lo = half[0], hi = half[0];
      for (const Vector3& p : half) {
        for (int a = 0; a < 3; a++) {
          lo.coords[a] = std::min(lo.coords[a], p.coords[a]);
          hi.coords[a] = std::max(hi.coords[a], p.coords[a]);
        }
      }
      const bool negative = side == 0;
      const float mouthX = negative ? hi.coords[0] : lo.coords[0];
      const float backX = negative ? lo.coords[0] : hi.coords[0];
      std::vector<bool> mine = VerticesOnPlane(half, 0, mouthX, 0.02f);
      UnionInto(mine, VerticesOnPlane(half, 2, lo.coords[2], 0.02f));
      UnionInto(mine, Both(VerticesOnPlane(half, 0, backX, 0.02f),
                           VerticesOnPlane(half, 2, hi.coords[2], 0.02f)));
      for (unsigned int i = 0; i < mine.size(); i++)
        if (mine[i]) held[back[i]] = true;
      // Aim the ball at this goal rather than across the pitch at the other one.
      if (side == 0) {
        low = lo;
        high = hi;
      }
    }
    cloth.Build(rest, held, LinksFromTriangles(faces));
    std::cout << "cloth: " << rest.size() << " point(s), "
              << std::count(held.begin(), held.end(), true) << " held, "
              << corners.size() << " corner(s)" << std::endl;
    return true;
  }

  void Write() {
    const std::vector<Vector3>& points = cloth.Positions();
    if (points.empty()) return;
    for (unsigned int i = 0; i < corners.size(); i++) {
      const Vector3& p = points[weld[i]];
      corners[i][0] = p.coords[0];
      corners[i][1] = p.coords[1];
      corners[i][2] = p.coords[2];
    }
    for (boost::intrusive_ptr<Geometry>& geom : geometry) geom->OnUpdateGeometryData(false);
  }
};

}  // namespace

// Drives the turntable and stops the run.
//
// The scheduler runs until something signals quit, so a viewer that wants N frames
// and then an exit has to be the thing that counts them. Sitting in the graphics
// sequence's Put phase means one call per presented frame, which is exactly the
// unit being counted.
class TurntableTask : public IUserTask {
 public:
  TurntableTask(boost::intrusive_ptr<Node> cameraNode, boost::intrusive_ptr<Camera> camera,
                const ViewerCamera::Shot& shot, int frames, const std::string& out)
      : cameraNode(cameraNode), camera(camera), shot(shot), frames(frames), out(out) {}

  void GetPhase() override {}
  void ProcessPhase() override {}

  // Presents before the first one worth keeping. The pipeline holds several frames in
  // flight, and those land in the recording after it opens: at five, a --shots 1 run
  // recorded nothing but background and --shots 3 gave one drawn frame of four. Twelve
  // is past the deepest staleness measured, so every recorded frame has the model in
  // it - which is what a one-shot audit of ninety-three models needs.
  static const int kWarmupFrames = 12;

  // And at least this long on the clock, because the geometry loads asynchronously.
  static const unsigned long kWarmupMilliseconds = 1500;

  // Frames the recorder writes before a live one lands. Measured: a four-shot run put
  // its first live frame in file position four.
  static const int kRecorderLeadIn = 4;

  void PutPhase() override {
    // Warm up by the clock as well as by frame count. The frame count alone was not
    // enough: the model's geometry loads on worker threads, so the early frames are
    // an empty scene and a one-shot run recorded nothing but background. Whichever
    // gate is later wins.
    if (warmupStart_ms == 0)
      warmupStart_ms = EnvironmentManager::GetInstance().GetTime_ms();
    const bool framesReady = warmup >= kWarmupFrames;
    const bool clockReady =
        EnvironmentManager::GetInstance().GetTime_ms() - warmupStart_ms >= kWarmupMilliseconds;
    if (!framesReady || !clockReady) {
      warmup++;
      // Recording starts once the pipeline is drawing the model, so the file holds
      // exactly the shots asked for rather than the warm-up as well.
      if (framesReady && clockReady)
        StartFrameRecording(out);
      return;
    }
    if (!recording) {
      recording = true;
      StartFrameRecording(out);
      return;
    }
    ViewerCamera::Shot turned = shot;
    turned.yaw = ViewerCamera::TurntableYaw(shot, drawn, frames > 0 ? frames : 1);
    const std::array<float, 3> eye = ViewerCamera::Position(turned);
    cameraNode->SetPosition(Vector3(eye[0], eye[1], eye[2]));
    // Split the way the match camera splits it: the node carries the yaw about Z and
    // the camera object the pitch about X, a quarter turn off because a camera looks
    // down its own -z (Match::UpdateIngameCamera).
    Quaternion yaw;
    yaw.SetAngleAxis(turned.yaw, Vector3(0, 0, 1));
    cameraNode->SetRotation(yaw);
    Quaternion pitch;
    pitch.SetAngleAxis(0.5f * pi - turned.pitch, Vector3(1, 0, 0));
    camera->SetRotation(pitch);
    drawn++;
    // The recorder writes the presented buffer, which lags the scene by a few frames:
    // the first frames in the file are the empty scene from before the model was
    // drawn, however long the warm-up. So draw the lead-in as well and let the caller
    // take the last `frames`; the count is printed rather than left to be discovered.
    if (frames > 0 && drawn >= frames + kRecorderLeadIn)
      EnvironmentManager::GetInstance().SignalQuit();
  }

  std::string GetName() const override { return "turntable"; }

  int Drawn() const { return drawn; }

 private:
  boost::intrusive_ptr<Node> cameraNode;
  boost::intrusive_ptr<Camera> camera;
  ViewerCamera::Shot shot;
  int frames = 0;
  int drawn = 0;
  int warmup = 0;
  unsigned long warmupStart_ms = 0;
  bool recording = false;
  std::string out;
};

// Plays one clip on one body: shots spread over the clip, the camera turning a
// little between them so a defect that only shows from one side still lands in
// the sheet. A turntable of the bind pose cannot show a skinning fault - the
// owner's "dragging verts on cutscenes" is a posed-frame defect - and the match
// is a poor instrument for it (one player among twenty-two, at broadcast
// distance, for a second).
class AnimTask : public IUserTask {
 public:
  AnimTask(ViewerSkinnedModel* body, Animation* clip, boost::intrusive_ptr<Node> cameraNode,
           boost::intrusive_ptr<Camera> camera, const ViewerCamera::Shot& shot, int frames,
           const std::string& out)
      : body(body), clip(clip), cameraNode(cameraNode), camera(camera), shot(shot),
        frames(frames), out(out) {}

  void GetPhase() override {}
  void ProcessPhase() override {}

  void PutPhase() override {
    if (warmupStart_ms == 0) warmupStart_ms = EnvironmentManager::GetInstance().GetTime_ms();
    const bool framesReady = warmup >= TurntableTask::kWarmupFrames;
    const bool clockReady = EnvironmentManager::GetInstance().GetTime_ms() - warmupStart_ms >=
                            TurntableTask::kWarmupMilliseconds;
    if (!framesReady || !clockReady) {
      warmup++;
      Pose(0);
      if (framesReady && clockReady) StartFrameRecording(out);
      return;
    }
    if (!recording) {
      recording = true;
      StartFrameRecording(out);
      return;
    }
    const int steps = std::max(1, frames);
    const int last = std::max(0, clip->GetFrameCount() - 1);
    Pose(steps > 1 ? last * std::min(drawn, steps - 1) / (steps - 1) : 0);
    ViewerCamera::Shot turned = shot;
    // Half a turn over the run, not a full one: the back of a body says less
    // about a skin than three quarters round the front does.
    turned.yaw = shot.yaw + pi * (steps > 1 ? (float)std::min(drawn, steps - 1) / (steps - 1) : 0.0f);
    const std::array<float, 3> eye = ViewerCamera::Position(turned);
    cameraNode->SetPosition(Vector3(eye[0], eye[1], eye[2]));
    Quaternion yaw;
    yaw.SetAngleAxis(turned.yaw, Vector3(0, 0, 1));
    cameraNode->SetRotation(yaw);
    Quaternion pitch;
    pitch.SetAngleAxis(0.5f * pi - turned.pitch, Vector3(1, 0, 0));
    camera->SetRotation(pitch);
    drawn++;
    if (frames > 0 && drawn >= frames + TurntableTask::kRecorderLeadIn)
      EnvironmentManager::GetInstance().SignalQuit();
  }

  std::string GetName() const override { return "anim"; }

 private:
  void Pose(int frame) {
    // The root's own travel is dropped: a clip that walks 30 m would leave the
    // frame, and the question here is the skin, not the locomotion.
    body->Pose(clip, frame, Vector3(0), 0, true);
    // AGENTS.md, the viewer trap: Animation::Apply invalidates spatial caches
    // through the "player" node, which is a dummy leaf here, so nothing applies
    // unless the humanoid is invalidated by hand.
    body->GetHumanoidNode()->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  }

  ViewerSkinnedModel* body = nullptr;
  Animation* clip = nullptr;
  boost::intrusive_ptr<Node> cameraNode;
  boost::intrusive_ptr<Camera> camera;
  ViewerCamera::Shot shot;
  int frames = 0;
  int drawn = 0;
  int warmup = 0;
  unsigned long warmupStart_ms = 0;
  bool recording = false;
  std::string out;
};

// Holds the camera still and shoots a ball through the cloth.
//
// The camera does not turn: a turntable and a moving cloth are impossible to tell
// apart in a contact sheet, and the whole question is whether the surface moves.
// The ball crosses the mouth over the run, pushing the net ahead of it the way the
// match does (Cloth::Push, match.cpp UpdateGoalNetting), and the frames after it
// has gone through show whether the net comes back.
class ClothShotTask : public IUserTask {
 public:
  ClothShotTask(ClothRig* rig, boost::intrusive_ptr<Node> cameraNode,
                boost::intrusive_ptr<Camera> camera, const ViewerCamera::Shot& shot,
                int frames, const std::string& out)
      : rig(rig), cameraNode(cameraNode), camera(camera), shot(shot), frames(frames),
        out(out) {}

  // Placed once and left there. A turntable and a moving cloth are impossible to
  // tell apart in a contact sheet, so the camera holds still and only the net moves.
  void PlaceCamera() {
    ViewerCamera::Shot from = shot;
    // Round in front of the goalmouth rather than square on the side, so the ball
    // comes towards the camera and the billow is across the frame.
    from.yaw = 0.9f;
    const std::array<float, 3> eye = ViewerCamera::Position(from);
    cameraNode->SetPosition(Vector3(eye[0], eye[1], eye[2]));
    Quaternion yaw;
    yaw.SetAngleAxis(from.yaw, Vector3(0, 0, 1));
    cameraNode->SetRotation(yaw);
    Quaternion pitch;
    pitch.SetAngleAxis(0.5f * pi - from.pitch, Vector3(1, 0, 0));
    camera->SetRotation(pitch);
  }

  void GetPhase() override {}
  void ProcessPhase() override {}

  void PutPhase() override {
    // Every tick: the scheduler resets nothing, but placing it once during warm-up
    // and never again put the camera back at the origin for the first live frames.
    PlaceCamera();
    if (warmupStart_ms == 0) warmupStart_ms = EnvironmentManager::GetInstance().GetTime_ms();
    const bool ready =
        warmup >= TurntableTask::kWarmupFrames &&
        EnvironmentManager::GetInstance().GetTime_ms() - warmupStart_ms >=
            TurntableTask::kWarmupMilliseconds;
    if (!ready) {
      warmup++;
      return;
    }
    if (!recording) {
      recording = true;
      StartFrameRecording(out);
      // Let it take up its own sag before the ball arrives, exactly as the match
      // does, so what the shot shows is the shot and not the settling.
      for (int i = 0; i < 60; i++)
        rig->cloth.Step(0.01f, Vector3(0, 0, -6.0f), 0.97f, 4);
      rig->Write();
      return;
    }

    // The ball's flight: in through the mouth, across the goal, out the back. Timed
    // off the frame counter rather than the clock so the run is the same every time.
    const float t = frames > 0 ? (float)drawn / (float)frames : 0.0f;
    const float travel = std::min(1.0f, t * 2.2f);  // through by halfway, then watch
    const bool negative = rig->low.coords[0] < 0.0f;
    const float mouthX = negative ? rig->high.coords[0] : rig->low.coords[0];
    const float backX = negative ? rig->low.coords[0] : rig->high.coords[0];
    const Vector3 ball(mouthX + (backX - mouthX) * travel,
                       (rig->low.coords[1] + rig->high.coords[1]) * 0.5f,
                       rig->low.coords[2] + (rig->high.coords[2] - rig->low.coords[2]) * 0.45f);
    if (travel < 1.0f) rig->cloth.Push(ball, 0.11f);
    rig->cloth.Step(0.016f, Vector3(0, 0, -6.0f), 0.97f, 4);
    rig->Write();
    if (drawn % 10 == 0)
      std::cout << "  frame " << drawn << " ball x " << ball.coords[0] << " sag "
                << (int)(rig->cloth.Displacement() * 1000) << " mm" << std::endl;
    drawn++;
    if (frames > 0 && drawn >= frames + TurntableTask::kRecorderLeadIn)
      EnvironmentManager::GetInstance().SignalQuit();
  }

  std::string GetName() const override { return "clothshot"; }

 private:
  ClothRig* rig;
  boost::intrusive_ptr<Node> cameraNode;
  boost::intrusive_ptr<Camera> camera;
  ViewerCamera::Shot shot;
  int frames = 0;
  int drawn = 0;
  int warmup = 0;
  unsigned long warmupStart_ms = 0;
  bool recording = false;
  std::string out;
};

// One PES choreography, played the way the match plays it and nothing else around it.
//
// Every slot is cast on a skinned body and posed by its own clip through
// EntranceChoreo::Sample - the same call the match makes for its cutscene cast, so a
// phase offset, a loop or a mark that is wrong here is wrong in the match too. The
// camera is the .camtrack that filmed the performance, applied the way
// Match::UpdateIngameCamera applies it: position on the node, rotation on the camera.
// Without one the camera frames the cast from the side, which is what a choreography
// PES never shot (the offsides) gets in a match as well.
//
// The N shots are spread evenly over the whole thing - the longer of the camera and
// the slowest actor to finish his first cycle - so the last still is the end of the
// performance, and "the second half never comes" is a question the sheet answers.
struct CutsceneActor {
  const ChoreoSlot* slot = nullptr;
  std::unique_ptr<ViewerSkinnedModel> body;
  std::unique_ptr<Animation> clip;
};

class CutsceneTask : public IUserTask {
 public:
  CutsceneTask(const EntranceChoreo* choreo, std::vector<CutsceneActor>* cast,
               const CamTrack* track, boost::intrusive_ptr<Node> cameraNode,
               boost::intrusive_ptr<Camera> camera, const ViewerCamera::Shot& fallback,
               float duration_ms, int frames, bool authoredCamera, const std::string& out)
      : choreo(choreo), cast(cast), track(track), cameraNode(cameraNode), camera(camera),
        fallback(fallback), duration_ms(duration_ms), frames(frames),
        authoredCamera(authoredCamera), out(out) {}

  void GetPhase() override {}
  void ProcessPhase() override {}

  void PutPhase() override {
    // The same warm-up the turntable needs, for the same reasons (TurntableTask).
    if (warmupStart_ms == 0) warmupStart_ms = EnvironmentManager::GetInstance().GetTime_ms();
    const bool framesReady = warmup >= TurntableTask::kWarmupFrames;
    const bool clockReady = EnvironmentManager::GetInstance().GetTime_ms() - warmupStart_ms >=
                            TurntableTask::kWarmupMilliseconds;
    if (!framesReady || !clockReady) {
      warmup++;
      Pose(0.0f);
      if (framesReady && clockReady) StartFrameRecording(out);
      return;
    }
    if (!recording) {
      recording = true;
      StartFrameRecording(out);
      return;
    }
    const int steps = std::max(1, frames);
    // The last shot lands on the last moment, not one step short of it.
    const float t_ms = steps > 1 ? duration_ms * std::min(drawn, steps - 1) / (steps - 1) : 0.0f;
    Pose(t_ms);
    drawn++;
    if (frames > 0 && drawn >= frames + TurntableTask::kRecorderLeadIn)
      EnvironmentManager::GetInstance().SignalQuit();
  }

  std::string GetName() const override { return "cutscene"; }

 private:
  void Pose(float t_ms) {
    Vector3 primary;
    bool havePrimary = false;
    for (CutsceneActor& actor : *cast) {
      Vector3 position;
      radian yaw = 0.0f;
      int animFrame = 0;
      // Choreography keys sit on the 10 ms frame grid the match ticks on.
      choreo->Sample(*actor.slot, t_ms / 10.0f, position, yaw, animFrame);
      actor.body->PoseChoreo(actor.clip.get(), animFrame, position, yaw);
      if (!havePrimary || actor.slot->role == e_ChoreoRole_Primary) {
        primary = position;
        havePrimary = true;
      }
    }
    if (track && track->GetFrameCount() > 0) {
      // 30 fps on the camera side, as canm_to_camtrack.py wrote it - and by the
      // montage timeline, not by row: a goal track is several cuts concatenated,
      // each numbered from its own start, and Sample() blends across the cut points
      // (Match::UpdateIngameCamera says the same, at length).
      CamTrackFrame frame = track->SampleTimeline(t_ms * 0.03f);
      // What the match does with a goal camera, so the viewer predicts the match:
      // the authored aim is a couple of degrees off the choreography's own mark on
      // a lens of a few degrees, and the match re-aims at the primary actor's head
      // with the same guard (Match::UpdateIngameCamera, RetargetCamTrackFrame).
      // --authored-camera shows PES's aim untouched, which is how that offset was
      // measured in the first place.
      if (!authoredCamera && havePrimary)
        frame = RetargetCamTrackFrame(
            frame, {primary.coords[0], primary.coords[1], primary.coords[2] + 1.0f}, 1.5f, 0.15f);
      cameraNode->SetPosition(Vector3(frame.position[0], frame.position[1], frame.position[2]));
      cameraNode->SetRotation(QUATERNION_IDENTITY);
      Quaternion rotation;
      rotation.Set(frame.rotation[0], frame.rotation[1], frame.rotation[2], frame.rotation[3]);
      camera->SetRotation(rotation);
      camera->SetFOV(frame.fov);
      camera->SetCapping(std::max(0.1f, frame.nearPlane), frame.farPlane);
      return;
    }
    const std::array<float, 3> eye = ViewerCamera::Position(fallback);
    cameraNode->SetPosition(Vector3(eye[0], eye[1], eye[2]));
    Quaternion yaw;
    yaw.SetAngleAxis(fallback.yaw, Vector3(0, 0, 1));
    cameraNode->SetRotation(yaw);
    Quaternion pitch;
    pitch.SetAngleAxis(0.5f * pi - fallback.pitch, Vector3(1, 0, 0));
    camera->SetRotation(pitch);
    camera->SetFOV(fallback.fov);
  }

  const EntranceChoreo* choreo;
  std::vector<CutsceneActor>* cast;
  const CamTrack* track;
  boost::intrusive_ptr<Node> cameraNode;
  boost::intrusive_ptr<Camera> camera;
  ViewerCamera::Shot fallback;
  float duration_ms;
  int frames = 0;
  int drawn = 0;
  int warmup = 0;
  unsigned long warmupStart_ms = 0;
  bool recording = false;
  bool authoredCamera = false;
  std::string out;
};

// Loads a choreography, its camera and its cast, and plays it to `out`. Returns the
// process exit code; everything it made is torn down before it returns.
// One model, one clip: the health check AGENTS.md asks for, and the instrument
// for any reported skinning defect.
int PlayAnim(const Options& options, std::shared_ptr<Scene3D> scene3D) {
  if (options.noBake)
    ViewerSkinnedModel::authoringPose = "media/animations/straight.anim.util";
  Animation clip;
  clip.Load(options.anim);
  if (clip.GetFrameCount() <= 0) {
    std::cout << "could not read animation " << options.anim << "\n";
    return 2;
  }
  ViewerSkinnedModel body;
  if (!body.Load(options.model, scene3D, "anim")) {
    std::cout << "could not cast a body from " << options.model << "\n";
    return 2;
  }
  body.Pose(&clip, 0, Vector3(0), 0, true);
  body.GetHumanoidNode()->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  const AABB bounds = body.GetTargetNode()->GetAABB();
  std::cout << options.model << " playing " << options.anim << ": " << clip.GetFrameCount()
            << " frames, body " << (bounds.maxxyz.coords[2] - bounds.minxyz.coords[2])
            << " m tall at frame 0\n";

  const ViewerCamera::Shot shot = ViewerCamera::Frame(
      {bounds.minxyz.coords[0] - 0.3f, bounds.minxyz.coords[1] - 0.3f, bounds.minxyz.coords[2]},
      {bounds.maxxyz.coords[0] + 0.3f, bounds.maxxyz.coords[1] + 0.3f, bounds.maxxyz.coords[2]},
      options.fov);

  boost::intrusive_ptr<Camera> camera = boost::static_pointer_cast<Camera>(
      ObjectFactory::GetInstance().CreateObject("camera", e_ObjectType_Camera));
  scene3D->CreateSystemObjects(camera);
  camera->Init();
  camera->SetFOV(shot.fov);
  camera->SetCapping(0.1f, 200.0f);
  boost::intrusive_ptr<Node> cameraNode(new Node("cameraNode"));
  cameraNode->AddObject(camera);
  scene3D->AddNode(cameraNode);

  boost::intrusive_ptr<Light> light = boost::static_pointer_cast<Light>(
      ObjectFactory::GetInstance().CreateObject("light", e_ObjectType_Light));
  scene3D->CreateSystemObjects(light);
  light->SetColor(Vector3(1.0f, 1.0f, 1.0f));
  light->SetRadius(60.0f);
  light->SetType(e_LightType_Point);
  light->SetShadow(false);
  boost::intrusive_ptr<Node> lightNode(new Node("lightNode"));
  lightNode->AddObject(light);
  lightNode->SetPosition(Vector3(-4.0f, -4.0f, 8.0f));
  scene3D->AddNode(lightNode);

  std::shared_ptr<IUserTask> driver(new AnimTask(&body, &clip, cameraNode, camera, shot,
                                                 options.shots > 0 ? options.shots : 8,
                                                 options.out));
  // Paced like the cutscene mode, and for the same measured reason: an unpaced
  // skinned body draws faster than the 60 Hz recorder samples.
  std::shared_ptr<TaskSequence> sequence(new TaskSequence("viewer", 40, true));
  sequence->AddUserTaskEntry(driver, e_TaskPhase_Put);
  sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Get);
  sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Process);
  sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Put);
  GetScheduler()->RegisterTaskSequence(sequence);
  Run();
  StopFrameRecording();
  std::cout << "drew frames to " << options.out << "; the first "
            << TurntableTask::kRecorderLeadIn << " are lead-in\n";
  lightNode->Exit();
  lightNode.reset();
  cameraNode->Exit();
  cameraNode.reset();
  return 0;
}

int PlayCutscene(const Options& options, std::shared_ptr<Scene3D> scene3D) {
  std::ifstream chorFile(options.cutscene);
  EntranceChoreo choreo;
  if (!chorFile.good() || !choreo.Load(chorFile)) {
    std::cout << "could not read choreography " << options.cutscene << "\n";
    return 2;
  }
  const std::filesystem::path chorPath(options.cutscene);
  const std::filesystem::path dir = chorPath.parent_path();

  // The camera: named, or the sibling whose name shares the most with the
  // choreography's. PES pairs them by a common stem with different tails -
  // goal_2018_run_30_banzai.chor / goal_2018_run_30_banzai_Z_fromL.camtrack,
  // tu_full_01_glad_pl_away.chor / tu_full_01_glad_cam.camtrack - so a plain
  // prefix test found the first and missed the second. Ties go to the first name
  // in order, which is the "_L"/"_fromL" angle where there are two.
  std::string camtrackPath = options.camtrack;
  if (camtrackPath.empty()) {
    const std::string stem = chorPath.stem().string();
    std::vector<std::string> candidates;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
      if (entry.path().extension() == ".camtrack") candidates.push_back(entry.path().string());
    std::sort(candidates.begin(), candidates.end());
    size_t best = 0;
    for (const std::string& candidate : candidates) {
      const std::string other = std::filesystem::path(candidate).stem().string();
      size_t shared = 0;
      while (shared < stem.size() && shared < other.size() && stem[shared] == other[shared]) shared++;
      // Enough of a name to mean the same pack, not just the same category.
      if (shared > best && shared >= 8) {
        best = shared;
        camtrackPath = candidate;
      }
    }
  }
  CamTrack track;
  bool haveTrack = false;
  if (!camtrackPath.empty()) {
    std::ifstream trackFile(camtrackPath);
    haveTrack = trackFile.good() && track.Load(trackFile) && track.GetFrameCount() > 0;
    if (!haveTrack) std::cout << "could not read camera " << camtrackPath << "\n";
  }

  // The cast. Each slot is its own body so no two actors share a vertex buffer.
  std::vector<CutsceneActor> cast;
  float castEnd_ms = 0.0f;
  for (const ChoreoSlot& slot : choreo.GetSlots()) {
    CutsceneActor actor;
    actor.slot = &slot;
    actor.clip.reset(new Animation());
    const std::string clipPath = (dir / slot.animFile).string();
    if (!std::filesystem::exists(clipPath)) {
      std::cout << "slot " << slot.slot << ": no clip at " << clipPath << "\n";
      continue;
    }
    actor.clip->Load(clipPath);
    actor.body.reset(new ViewerSkinnedModel());
    if (!actor.body->Load(options.body, scene3D, "actor" + int_to_str(slot.slot))) {
      std::cout << "slot " << slot.slot << ": could not cast a body from " << options.body << "\n";
      return 2;
    }
    // One cycle at the match's 10 ms grid, after the slot's own entrance.
    castEnd_ms = std::max(castEnd_ms, (slot.phaseFrames + slot.cycleFrames) * 10.0f);
    std::cout << "slot " << slot.slot << "  " << slot.animFile << "  " << actor.clip->GetFrameCount()
              << " frames, phase " << slot.phaseFrames << ", cycle " << slot.cycleFrames
              << (slot.loop ? ", loops" : "") << "\n";
    cast.push_back(std::move(actor));
  }
  if (cast.empty()) {
    std::cout << "nobody to cast: " << options.cutscene << " names no clip that exists\n";
    return 2;
  }
  const float cameraEnd_ms = haveTrack ? track.GetTimelineFrameCount() / 30.0f * 1000.0f : 0.0f;
  const float duration_ms = std::max(castEnd_ms, cameraEnd_ms);
  std::cout << options.cutscene << ": " << cast.size() << " actor(s), camera "
            << (haveTrack ? camtrackPath : std::string("none - framing the cast")) << ", "
            << cameraEnd_ms / 1000.0f << " s of camera, " << castEnd_ms / 1000.0f
            << " s of choreography\n";

  // Where the cast stands at the start, for the fallback framing and the lamp.
  Vector3 low(1e9f, 1e9f, 0.0f), high(-1e9f, -1e9f, 2.0f);
  for (const CutsceneActor& actor : cast) {
    for (const ChoreoKey& key : actor.slot->keys) {
      low.coords[0] = std::min(low.coords[0], key.x);
      low.coords[1] = std::min(low.coords[1], key.y);
      high.coords[0] = std::max(high.coords[0], key.x);
      high.coords[1] = std::max(high.coords[1], key.y);
    }
  }
  const ViewerCamera::Shot fallback = ViewerCamera::Frame(
      {low.coords[0] - 1.0f, low.coords[1] - 1.0f, 0.0f},
      {high.coords[0] + 1.0f, high.coords[1] + 1.0f, 2.0f}, options.fov);

  boost::intrusive_ptr<Camera> camera = boost::static_pointer_cast<Camera>(
      ObjectFactory::GetInstance().CreateObject("camera", e_ObjectType_Camera));
  scene3D->CreateSystemObjects(camera);
  camera->Init();
  camera->SetFOV(fallback.fov);
  camera->SetCapping(0.2f, 400.0f);
  boost::intrusive_ptr<Node> cameraNode(new Node("cameraNode"));
  cameraNode->AddObject(camera);
  scene3D->AddNode(cameraNode);

  // A high lamp over the stage, the way the sun sits over a pitch.
  boost::intrusive_ptr<Light> light = boost::static_pointer_cast<Light>(
      ObjectFactory::GetInstance().CreateObject("light", e_ObjectType_Light));
  scene3D->CreateSystemObjects(light);
  light->SetColor(Vector3(1.0f, 1.0f, 1.0f));
  light->SetRadius(120.0f);
  light->SetType(e_LightType_Point);
  light->SetShadow(false);
  boost::intrusive_ptr<Node> lightNode(new Node("lightNode"));
  lightNode->AddObject(light);
  lightNode->SetPosition(Vector3((low.coords[0] + high.coords[0]) * 0.5f - 8.0f,
                                (low.coords[1] + high.coords[1]) * 0.5f - 8.0f, 18.0f));
  scene3D->AddNode(lightNode);

  if (options.shots > 0) {
    std::shared_ptr<IUserTask> driver(new CutsceneTask(&choreo, &cast, haveTrack ? &track : nullptr,
                                                       cameraNode, camera, fallback, duration_ms,
                                                       options.shots, options.authoredCamera,
                                                       options.out));
    // Paced, unlike the turntable: the recorder samples the presented frame at 60 Hz
    // and a skinned cast draws fast enough that an unpaced run put three frames of
    // sixteen in the file. Forty milliseconds a frame keeps every shot.
    std::shared_ptr<TaskSequence> sequence(new TaskSequence("viewer", 40, true));
    sequence->AddUserTaskEntry(driver, e_TaskPhase_Put);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Get);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Process);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Put);
    GetScheduler()->RegisterTaskSequence(sequence);
    Run();
    StopFrameRecording();
    std::cout << "drew frames to " << options.out << "; the first " << TurntableTask::kRecorderLeadIn
              << " are pipeline lead-in, the last " << options.shots << " span 0 to "
              << duration_ms / 1000.0f << " s\n";
    sequence.reset();
  }

  lightNode->Exit();
  lightNode.reset();
  cameraNode->Exit();
  cameraNode.reset();
  cast.clear();
  return 0;
}

int main(int argc, const char** argv) {
  const Options options = Parse(argc, argv);
  if (options.model.empty() && options.cutscene.empty()) {
    std::cout << "gfviewer <model.ase> [--anim CLIP] [--shots N] [--out DIR] [--fov D] [--pitch R]\n"
              << "gfviewer --cutscene <pack.chor> [--camtrack T] [--body M] --shots N --out DIR\n";
    return 1;
  }

  // The engine wants its run tree as the working directory, the same as the game.
  if (std::filesystem::exists("data") && std::filesystem::exists("data/media"))
    std::filesystem::current_path("data");

  GetConfiguration()->LoadFile("football.config");
  // A viewer has no use for a match's frame budget or its audio.
  GetConfiguration()->Set("audio_volume", 0.0f);
  Initialize(*GetConfiguration());

  SystemManager* systemManager = SystemManager::GetInstancePtr();
  GraphicsSystem* graphics = new GraphicsSystem();
  systemManager->RegisterSystem("GraphicsSystem", graphics);
  graphics->Initialize(*GetConfiguration());

  viewerGraphics = graphics;
  viewerScene2D = std::shared_ptr<Scene2D>(new Scene2D("scene2D", *GetConfiguration()));
  SceneManager::GetInstance().RegisterScene(viewerScene2D);
  std::shared_ptr<Scene3D> scene3D(new Scene3D("scene3D"));
  viewerScene3D = scene3D;
  SceneManager::GetInstance().RegisterScene(scene3D);

  if (!options.cutscene.empty() || !options.anim.empty()) {
    const int code = options.anim.empty() ? PlayCutscene(options, scene3D)
                                          : PlayAnim(options, scene3D);
    viewerScene3D.reset();
    scene3D.reset();
    viewerScene2D.reset();
    delete viewerConfig;
    viewerConfig = nullptr;
    Exit();
    return code;
  }

  ObjectLoader loader;
  std::string scratchWrapper;
  const std::string modelPath = ResolveModelPath(options.model, scratchWrapper);
  boost::intrusive_ptr<Node> node = loader.LoadObject(scene3D, modelPath);
  if (!node) {
    std::cout << "could not load " << modelPath << "\n";
    if (!scratchWrapper.empty()) std::filesystem::remove(scratchWrapper);
    return 2;
  }

  // LoadObject builds the node and its graphics counterparts but leaves it to the
  // caller to put in the scene. Without this the model is loaded, framed, measured -
  // and never in the render set, which is a turntable of empty frames.
  scene3D->AddNode(node);

  const std::vector<ModelInventory::Mesh> meshes = ReadMeshes(node);
  const ModelInventory::Report report = ModelInventory::Describe(meshes, 0.15f);
  std::cout << options.model << ": " << report.meshes.size() << " mesh(es), "
            << report.totalVertices << " vertices, " << report.totalFaces << " faces";
  if (report.emptyMeshes) std::cout << ", " << report.emptyMeshes << " EMPTY";
  if (report.duplicateMeshes) std::cout << ", " << report.duplicateMeshes << " stray shell(s)";
  std::cout << "\n";
  for (const auto& mesh : report.meshes) {
    std::cout << "   " << mesh.name << "  v" << mesh.vertices << " f" << mesh.faces
              << "  median edge " << mesh.medianEdge;
    if (mesh.empty) std::cout << "  EMPTY";
    if (!mesh.duplicateOf.empty()) std::cout << "  shell of " << mesh.duplicateOf;
    if (mesh.tooCoarseForCut)
      std::cout << "  the 0.15 m cut is only " << mesh.cutRatio << "x its median";
    std::cout << "\n";
  }

  // The model's own bounds decide the shot, which is why a viewer needs no
  // configuration to frame something it has never seen.
  const AABB bounds = node->GetAABB();
  ClothRig clothRig;
  if (options.cloth && !clothRig.Build(node, options.clothTexture)) {
    std::cout << "no mesh in " << options.model << " wears a texture named '"
              << options.clothTexture << "' - nothing to simulate\n";
    return 2;
  }
  // Framed on the cloth rather than the whole model when there is one: goals.ase
  // holds both goals 115 m apart, and a shot that takes in the pair puts the net
  // being hit a few pixels across.
  const std::array<float, 3> framedLow =
      options.cloth ? std::array<float, 3>{clothRig.low.coords[0], clothRig.low.coords[1],
                                           clothRig.low.coords[2]}
                    : std::array<float, 3>{bounds.minxyz.coords[0], bounds.minxyz.coords[1],
                                           bounds.minxyz.coords[2]};
  const std::array<float, 3> framedHigh =
      options.cloth ? std::array<float, 3>{clothRig.high.coords[0], clothRig.high.coords[1],
                                           clothRig.high.coords[2]}
                    : std::array<float, 3>{bounds.maxxyz.coords[0], bounds.maxxyz.coords[1],
                                           bounds.maxxyz.coords[2]};
  ViewerCamera::Shot shot = ViewerCamera::Frame(framedLow, framedHigh, options.fov);
  if (options.portrait) {
    // The top quarter of the model, from the front. A quarter rather than a
    // measured head: these characters run from a chibi Wario to a three-metre
    // mech, and no head joint is going to describe both - what a card needs is
    // "the top of him, big enough to recognise".
    const float height = framedHigh[2] - framedLow[2];
    const float band = height * 0.26f;
    // A box round the head, not the model's full width: in a T-pose the arms
    // are the widest thing about him and Frame() sizes on the largest span, so
    // taking only the top of the bounds still framed the whole wingspan.
    //
    // Centred on where the geometry actually is rather than on the middle of
    // the box: a model that carries a prop beside its head (the /vn/ squad is a
    // painted plane with a star floating next to it) has its box centre out in
    // the air between the two, and the portrait framed the gap.
    float cx = (framedLow[0] + framedHigh[0]) * 0.5f;
    float cy = (framedLow[1] + framedHigh[1]) * 0.5f;
    double sumX = 0.0, sumY = 0.0;
    long counted = 0;
    for (const ModelInventory::Mesh& mesh : meshes) {
      for (const std::array<float, 3>& vertex : mesh.vertices) {
        if (vertex[2] < framedHigh[2] - band) continue;
        sumX += vertex[0];
        sumY += vertex[1];
        counted++;
      }
    }
    if (counted > 0) {
      cx = (float)(sumX / counted);
      cy = (float)(sumY / counted);
    }
    const std::array<float, 3> headLow = {cx - band * 0.55f, cy - band * 0.55f,
                                          framedHigh[2] - band};
    const std::array<float, 3> headHigh = {cx + band * 0.55f, cy + band * 0.55f, framedHigh[2]};
    shot = ViewerCamera::Frame(headLow, headHigh, options.fov);
    shot.pitch = 0.0f;
  }

  boost::intrusive_ptr<Camera> camera = boost::static_pointer_cast<Camera>(
      ObjectFactory::GetInstance().CreateObject("camera", e_ObjectType_Camera));
  scene3D->CreateSystemObjects(camera);
  camera->Init();
  camera->SetFOV(shot.fov);
  // Near and far from the model's own size. Without capping the planes are
  // whatever the camera defaulted to and the model falls outside them, which is
  // a turntable of blank frames (MenuScene sets its own for the same reason).
  camera->SetCapping(shot.distance * 0.02f, shot.distance * 20.0f);
  boost::intrusive_ptr<Node> cameraNode(new Node("cameraNode"));
  cameraNode->AddObject(camera);
  scene3D->AddNode(cameraNode);

  // Something has to light it, or every shot is a silhouette. One lamp over the
  // camera's shoulder is what a turntable wants; the model's own size sets how far
  // out it sits, so a boot and a stadium prop are both lit the same way.
  boost::intrusive_ptr<Light> light = boost::static_pointer_cast<Light>(
      ObjectFactory::GetInstance().CreateObject("light", e_ObjectType_Light));
  scene3D->CreateSystemObjects(light);
  light->SetColor(Vector3(1.0f, 1.0f, 1.0f));
  light->SetRadius(shot.distance * 8.0f);
  light->SetType(e_LightType_Point);
  light->SetShadow(false);
  boost::intrusive_ptr<Node> lightNode(new Node("lightNode"));
  lightNode->AddObject(light);
  lightNode->SetPosition(Vector3(shot.target[0], shot.target[1] - shot.distance,
                                shot.target[2] + shot.distance));
  scene3D->AddNode(lightNode);

  {
    // What the graphics task will actually be offered. A model that is loaded but
    // not in the scene, or in it and disabled, draws nothing, and those look alike
    // from outside.
    std::list<boost::intrusive_ptr<Object>> all;
    scene3D->GetObjects(all);
    int geoms = 0, enabled = 0;
    for (auto& object : all) {
      if (object->GetObjectType() != e_ObjectType_Geometry) continue;
      geoms++;
      if (object->IsEnabled()) enabled++;
    }
    std::cout << "framed at " << shot.distance << " m; " << geoms
              << " geometry object(s) in the scene, " << enabled << " enabled\n";
  }


  if (options.shots > 0) {
    // The same path the game records a showcase through: every presented frame goes
    // to a fifo or a file, and the caller turns it into stills. A viewer that wrote
    // its own PNGs would be a second answer to a question already answered.
    std::shared_ptr<IUserTask> driver;
    if (options.cloth)
      driver = std::shared_ptr<IUserTask>(
          new ClothShotTask(&clothRig, cameraNode, camera, shot, options.shots, options.out));
    else
      driver = std::shared_ptr<IUserTask>(
          new TurntableTask(cameraNode, camera, shot, options.shots, options.out));
    std::shared_ptr<TaskSequence> sequence(new TaskSequence("viewer", 0, true));
    // The order the game's own graphics sequence uses: the user task writes the
    // camera in its Put phase, then the graphics Get phase reads it and enqueues the
    // view. Getting first renders the frame before the camera has been placed.
    sequence->AddUserTaskEntry(driver, e_TaskPhase_Put);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Get);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Process);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Put);
    GetScheduler()->RegisterTaskSequence(sequence);

    Run();
    StopFrameRecording();
    std::cout << "drew frames to " << options.out
              << "; the first 4 are pipeline lead-in, the last " << options.shots
              << " are the shots\n";
    sequence.reset();
  }

  // Teardown, in the order the game's own does it. A geometry destroyed while the
  // graphics system still observes it aborts with "Observer(s) still present at
  // destruction time", which is how this exited before it drew anything.
  lightNode->Exit();
  lightNode.reset();
  cameraNode->Exit();
  cameraNode.reset();
  node->Exit();
  node.reset();
  // The wrapper written for a raw .ase is scratch, not part of the import.
  if (!scratchWrapper.empty()) std::filesystem::remove(scratchWrapper);
  viewerScene3D.reset();
  scene3D.reset();
  viewerScene2D.reset();
  delete viewerConfig;
  viewerConfig = nullptr;
  Exit();
  return 0;
}
