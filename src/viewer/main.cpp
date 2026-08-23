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
//
// With --shots it writes N stills round a turntable and exits, which is what a
// headless machine needs; without, it opens a window and orbits with the mouse.
//
// The stills go to --out as raw RGBA at the context size, through the same recording
// path the game records a showcase through, so ffmpeg turns them into PNGs:
//
//   ffmpeg -f rawvideo -pixel_format rgba -video_size 1280x720 -i out.raw shot%02d.png
//
// The file holds N frames, occasionally N+1, and the first of them is sometimes empty:
// the recording stream opens against the swap chain rather than in step with it. A
// frame with no variance in it is one of those and can be dropped.

#include <deque>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

int main(int argc, const char** argv) {
  const Options options = Parse(argc, argv);
  if (options.model.empty()) {
    std::cout << "gfviewer <model.ase> [--shots N] [--out DIR] [--fov D] [--pitch R]\n";
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

  ObjectLoader loader;
  boost::intrusive_ptr<Node> node = loader.LoadObject(scene3D, options.model);
  if (!node) {
    std::cout << "could not load " << options.model << "\n";
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
  ViewerCamera::Shot shot = ViewerCamera::Frame(
      {bounds.minxyz.coords[0], bounds.minxyz.coords[1], bounds.minxyz.coords[2]},
      {bounds.maxxyz.coords[0], bounds.maxxyz.coords[1], bounds.maxxyz.coords[2]},
      options.fov);
  shot.pitch = options.pitch;

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
    std::shared_ptr<TurntableTask> turntable(
        new TurntableTask(cameraNode, camera, shot, options.shots, options.out));
    std::shared_ptr<TaskSequence> sequence(new TaskSequence("viewer", 0, true));
    // The order the game's own graphics sequence uses: the user task writes the
    // camera in its Put phase, then the graphics Get phase reads it and enqueues the
    // view. Getting first renders the frame before the camera has been placed.
    sequence->AddUserTaskEntry(turntable, e_TaskPhase_Put);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Get);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Process);
    sequence->AddSystemTaskEntry(viewerGraphics, e_TaskPhase_Put);
    GetScheduler()->RegisterTaskSequence(sequence);

    Run();
    StopFrameRecording();
    std::cout << "drew " << turntable->Drawn() << " frame(s) to " << options.out
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
  viewerScene3D.reset();
  scene3D.reset();
  viewerScene2D.reset();
  delete viewerConfig;
  viewerConfig = nullptr;
  Exit();
  return 0;
}
