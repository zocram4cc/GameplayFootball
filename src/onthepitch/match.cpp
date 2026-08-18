// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "match.hpp"

#include <fstream>
#include <set>

#include "../data/playertraits.hpp"
#include "../main.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "aimanager.hpp"
#include "base/geometry/triangle.hpp"
#include "base/log.hpp"
#include "coachmode.hpp"
#include "crowdmood.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "matchduration.hpp"
#include "menu/pagefactory.hpp"
#include "menu/startmatch/loadingmatch.hpp"
#include "menu/prematchchoices.hpp"
#include "onthepitch/pitchturf.hpp"
#include "onthepitch/stadiumfar.hpp"
#include "onthepitch/scenelighting.hpp"
#include "onthepitch/camerastandoff.hpp"
#include "onthepitch/staginganchor.hpp"
#include "onthepitch/stadiumsky.hpp"
#include "onthepitch/playerbody.hpp"
#include "player/playerofficial.hpp"
#include "proceduralpitch.hpp"
#include "scene/objectfactory.hpp"
#include "scene/objects/light.hpp"
#include "scene/resources/soundbuffer.hpp"
#include "systems/graphics/rendering/opengl_renderer3d.hpp"
#include "utils/directoryparser.hpp"
#include "modelviewer.hpp"
#include "utils/playermodelmap.hpp"
#include "cutscenereport.hpp"
#include "foulsequence.hpp"
#include "goalsequence.hpp"
#include "utils/splitgeometry.hpp"

// Long enough that a replay fired AFTER the celebration can still reach back
// past the goal to the build-up. See onthepitch/goalsequence.hpp, which owns
// this and the referee's matching restart delay - they were separate once, and
// the goal state was being cleared before the replay could fire.
const unsigned int replaySize_ms = GoalSequence::kReplayBuffer_ms;
// Match::SetReplayCamera's modes, named where they are chosen from.
const int kReplayCameraBehindGoal = 1;
const int kReplayCameraCloseUp = 2;
const unsigned int camPosSize = 150;           // 180; //130
const float excitementEventDecayRate = 0.99f;  // per 10ms tick
const float crowdGainUpdateThreshold = 0.001f;

Match::Match(MatchData* matchData, const std::vector<IHIDevice*>& controllers)
    : matchData(matchData), controllers(controllers) {
  Log(e_Notice, "Match", "Match", "Starting Match");

  _positionLogging = false;

  // shared ptr to menutask, because menutask shouldn't die before match does
  menuTask = GetMenuTask();

  iterations.SetData(0);
  actualTime_ms = 0;
  buf_matchTime_ms = 0;
  buf_actualTime_ms = 0;
  goalScoredTimer = 0;

  replayState->dirty = false;

  resetNetting = false;
  nettingHasChanged = false;

  matchDurationMinutes = kDefaultMatchDurationMinutes;
  if (GetConfiguration()->Exists("match_duration_minutes")) {
    matchDurationMinutes =
        GetConfiguration()->GetReal("match_duration_minutes", kDefaultMatchDurationMinutes);
  } else {
    matchDurationMinutes = MatchDurationMinutesFromLegacySlider(
        GetConfiguration()->GetReal("match_duration", _default_MatchDuration));
  }
  matchDurationFactor = MatchDurationFactorFromMinutes(matchDurationMinutes);
  matchTimeScale = std::max(1.0f, GetConfiguration()->GetReal("menu_smoke_match_time_scale", 1.0f));
  matchDifficulty = GetConfiguration()->GetReal("match_difficulty", _default_Difficulty);

  Log(e_Notice, "Match", "Match", "Creating dynamicNode");

  dynamicNode = boost::intrusive_ptr<Node>(new Node("dynamicNode"));
  GetScene3D()->AddNode(dynamicNode);

  Log(e_Notice, "Match", "Match", "Adding debugpilons");

  dynamicNode->AddObject(GetGreenDebugPilon());
  dynamicNode->AddObject(GetBlueDebugPilon());
  dynamicNode->AddObject(GetYellowDebugPilon());
  dynamicNode->AddObject(GetRedDebugPilon());
  dynamicNode->AddObject(GetSmallDebugCircle1());
  dynamicNode->AddObject(GetSmallDebugCircle2());
  dynamicNode->AddObject(GetLargeDebugCircle());

  // ball

  Log(e_Notice, "Match", "Match", "Creating a ball");

  ball = std::make_unique<Ball>(this);

  // animation database

  Log(e_Notice, "Match", "Match", "Loading player animations");

  anims = std::shared_ptr<AnimCollection>(new AnimCollection(GetScene3D()));
  anims->Load("media/animations");

  // cache animation positions

  Log(e_Notice, "Match", "Match", "Caching animation positions");

  const std::vector<Animation*>& animationsTmp = anims->GetAnimations();
  for (unsigned int i = 0; i < animationsTmp.size(); i++) {
    std::vector<Vector3> positions;
    Animation* someAnim = animationsTmp.at(i);
    Quaternion dud;
    Vector3 position;
    // printf("name: %s\n", someAnim->GetName().c_str());
    for (int frame = 0; frame < someAnim->GetFrameCount(); frame++) {
      someAnim->GetKeyFrame("player", frame, dud, position, false, true);
      position.coords[2] = 0.0f;
      positions.push_back(position);
      // position.Print();
    }
    // printf("\n");
    animPositionCache.insert(std::pair<Animation*, std::vector<Vector3>>(someAnim, positions));
  }

  // full body model template

  Log(e_Notice, "Match", "Match", "Loading fullbody object");

  // the default player body: the imported PES 2021 base model (kit shirt,
  // shorts, socks, arms, face - see tools/pes21_import/pes_base_body.py).
  // "player_body" selects another <name>.object / models/<name>.ase pair;
  // "fullbody" is the migrated legacy low-poly body, and also the fallback
  // when the configured body's files are absent.
  // Both files have to be there: the .object is only a wrapper, and a missing
  // models/<name>.ase is fatal inside the ASE loader rather than something the
  // loader reports back. None of the converted PES assets ship with the
  // repository, so this is the path a fresh clone takes.
  const std::string configuredBody =
      GetConfiguration()->Get("player_body", PlayerBody::kDefaultBody);
  playerBodyName = PlayerBody::Resolve(
      configuredBody, std::filesystem::exists(PlayerBody::ObjectPath(configuredBody)),
      std::filesystem::exists(PlayerBody::ModelPath(configuredBody)));
  if (playerBodyName != configuredBody) {
    Log(e_Warning, "Match", "Match",
        "player_body '" + configuredBody + "' is incomplete (see docs/ASSETS.md), using '" +
            playerBodyName + "'");
  }
  const std::string& bodyName = playerBodyName;

  ObjectLoader loader;
  fullbodyNode = loader.LoadObject(
      GetScene3D(), "media/objects/players/" + bodyName + ".object");

  Log(e_Notice, "Match", "Match", "Fullbody object: getting vertex colors");

  GetVertexColors(colorCoords, "media/objects/players/models/" + bodyName + ".ase");

  designatedPossessionPlayer = 0;

  // teams

  Log(e_Notice, "Match", "Match", "Creating teams/players");

  assert(matchData != 0);

  teams[0] = nullptr;
  teams[1] = nullptr;
  teams[0] = std::make_unique<Team>(0, this, matchData->GetTeamData(0));
  teams[1] = std::make_unique<Team>(1, this, matchData->GetTeamData(1));
  teams[0]->InitPlayers(fullbodyNode, colorCoords);
  teams[1]->InitPlayers(fullbodyNode, colorCoords);

  std::vector<Player*> activePlayers;
  teams[0]->GetActivePlayers(activePlayers);
  designatedPossessionPlayer = activePlayers.at(0);
  ballRetainer = 0;

  // officials

  Log(e_Notice, "Match", "Match", "Creating referee/linesmen models");

  // referees are data too: any PNG with the player kit UV layout works
  std::string kitFilename = GetConfiguration()->Get(
      "referee_kit", "media/objects/players/textures/referee_kit.png");
  boost::intrusive_ptr<Resource<Surface>> kit = ResourceManagerPool::GetInstance()
                                                    .GetManager<Surface>(e_ResourceType_Surface)
                                                    ->Fetch(kitFilename);
  officials = std::make_unique<Officials>(this, fullbodyNode, colorCoords, kit, anims);

  dynamicNode->AddObject(officials->GetYellowCardGeom());
  dynamicNode->AddObject(officials->GetRedCardGeom());

  // camera

  Log(e_Notice, "Match", "Match", "Creating camera objects");

  camera = boost::static_pointer_cast<Camera>(
      ObjectFactory::GetInstance().CreateObject("camera", e_ObjectType_Camera));
  GetScene3D()->CreateSystemObjects(camera);
  camera->Init();

  camera->SetFOV(25);
  cameraNode = boost::intrusive_ptr<Node>(new Node("cameraNode"));
  cameraNode->AddObject(camera);
  cameraNode->SetPosition(Vector3(40, 0, 100));
  GetDynamicNode()->AddNode(cameraNode);

  float introSeconds = GetConfiguration()->GetReal("intro_cutscene_seconds", 0.0f);
  std::string introTrack = GetConfiguration()->Get("intro_cutscene_track", "");

  // PES picks the match entrance by competition: each ent_<id> family is one
  // entrance presentation, and within a family the shots are authored per
  // stadium. "entrance_id" names the family; the stadium the match is played in
  // picks the variant, falling back to any shot in the family when that stadium
  // has none of its own (see docs/PES21_CAMERA_TRACE.md and tools/pes21_import/
  // export_entrances.py). Matches with no competition to derive a family from
  // still get an entrance: the stadium picks a shot across every family, and
  // failing that any installed shot will do. "entrance_id" "none" opts a match
  // out, and an explicit "intro_cutscene_track" still wins.
  std::string entranceID = GetConfiguration()->Get("entrance_id", "");
  const bool entranceDisabled =
      entranceID == "none" ||
      GetConfiguration()->GetBool("penalty_shootout_only", false);
  if (introTrack.empty() && !entranceDisabled) {
    // "media/objects/stadiums/pes_st060/..." -> "st060"
    std::string stadiumToken;
    {
      const std::string stadiumPath =
          GetConfiguration()->Get("stadium_object", "");
      const size_t at = stadiumPath.find("st");
      if (at != std::string::npos && at + 5 <= stadiumPath.size())
        stadiumToken = stadiumPath.substr(at, 5);
    }
    const std::string entranceRoot =
        GetConfiguration()->Get("entrance_dir", "media/cutscenes/ent");
    std::vector<std::string> entranceDirs;
    std::error_code ec;
    if (!entranceID.empty()) {
      entranceDirs.push_back(entranceRoot + "/" + entranceID);
    } else {
      for (const auto& entry : std::filesystem::directory_iterator(entranceRoot, ec))
        if (entry.is_directory()) entranceDirs.push_back(entry.path().string());
      std::sort(entranceDirs.begin(), entranceDirs.end());
    }
    std::vector<std::string> candidates;
    std::vector<std::string> stadiumMatches;
    for (const auto& dir : entranceDirs) {
      for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".camtrack") continue;
        const std::string path = entry.path().string();
        candidates.push_back(path);
        if (!stadiumToken.empty() &&
            entry.path().filename().string().find(stadiumToken) != std::string::npos)
          stadiumMatches.push_back(path);
      }
    }
    const std::vector<std::string>& pick =
        stadiumMatches.empty() ? candidates : stadiumMatches;
    if (!pick.empty()) {
      // PES cuts an entrance together from every shot the family authored, so
      // play them all in order rather than picking one.
      std::vector<std::string> shots = pick;
      std::sort(shots.begin(), shots.end());
      if (!entranceID.empty() || !stadiumMatches.empty()) {
        for (const std::string& shot : shots) {
          std::ifstream shotFile(shot);
          CamTrack track;
          if (shotFile.good() && track.Load(shotFile) && track.GetFrameCount() > 0)
            introShots.push_back(track);
        }
      }
      introTrack = shots.front();
      Log(e_Notice, "Match", "Match",
          "entrance " + (entranceID.empty() ? std::string("(default)") : entranceID) +
              ": " + int_to_str(introShots.size()) + " shot(s), first " + introTrack +
              (stadiumMatches.empty() ? " (no shot for this stadium)" : ""));
    } else {
      Log(e_Warning, "Match", "Match", "no entrance camerawork in " + entranceRoot);
    }
  }

  if (!introTrack.empty()) {
    std::ifstream trackFile(introTrack);
    if (trackFile.good() && introCamTrack.Load(trackFile) && introSeconds <= 0.0f)
      introSeconds = introCamTrack.GetDurationSeconds();
    // a multi-shot entrance runs for as long as all its shots together
    if (introShots.size() > 1 &&
        GetConfiguration()->GetReal("intro_cutscene_seconds", 0.0f) <= 0.0f) {
      introSeconds = 0.0f;
      for (const CamTrack& shot : introShots) introSeconds += shot.GetDurationSeconds();
    }

    // The family's player choreography: PES ships the entrance actors as _pl
    // packs next to the camera packs (exported to .chor + in-place .anim
    // clips by tools/pes21_import/entrance_pl.py). Any .chor in the family
    // directory stages the same entrance, so pick one like the camerawork
    // was picked. With none installed the scripted walk still runs.
    {
      const size_t slash = introTrack.find_last_of('/');
      const std::string familyDir =
          slash == std::string::npos ? std::string(".") : introTrack.substr(0, slash);
      std::vector<std::string> chors;
      std::error_code chorEc;
      for (const auto& entry : std::filesystem::directory_iterator(familyDir, chorEc))
        if (entry.path().extension() == ".chor") chors.push_back(entry.path().string());
      std::sort(chors.begin(), chors.end());
      if (!chors.empty()) {
        const std::string& pick =
            chors[(unsigned int)floor(random(0.0f, chors.size() - 0.001f))];
        std::ifstream chorFile(pick);
        if (chorFile.good() && entranceChoreo.Load(chorFile)) {
          for (const auto& slot : entranceChoreo.GetSlots()) {
            if (entranceClips.count(slot.animFile)) continue;
            const std::string clipPath = familyDir + "/" + slot.animFile;
            if (!std::filesystem::exists(clipPath)) continue;
            auto clip = std::make_shared<Animation>();
            clip->Load(clipPath);
            if (clip->GetFrameCount() >= 2) entranceClips[slot.animFile] = clip;
          }
          Log(e_Notice, "Match", "Match",
              "entrance choreography: " + pick + " (" +
                  int_to_str((int)entranceChoreo.GetSlots().size()) +
                  " actors, " + int_to_str((int)entranceClips.size()) +
                  " clips)");
        }
      }
    }
  }

  // PES goal camerawork pool: any .camtrack in the goal cutscene directory
  {
    std::string goalDir =
        GetConfiguration()->Get("goal_cutscene_dir", "media/cutscenes/goal");
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(goalDir, ec)) {
      if (entry.path().extension() != ".camtrack") continue;
      std::ifstream file(entry.path());
      CamTrack track;
      if (file.good() && track.Load(file)) {
        float meanX = 0.0f;
        int samples = std::min(30, track.GetFrameCount());
        for (int s = 0; s < samples; s++)
          meanX += track.Sample((float)s).position[0];
        goalCamAuthoredSides.push_back(meanX >= 0.0f ? 1 : -1);
        goalCamTracks.push_back(track);
      }
    }
    if (!goalCamTracks.empty())
      Log(e_Notice, "Match", "Match",
          "Loaded " + int_to_str((int)goalCamTracks.size()) +
              " goal camera tracks");
  }

  // stoppage cutscene pools, one directory per PES fixdemo category
  {
    int loadedPools = 0;
    for (const char* category :
         // "offside" is its own category rather than a goal subpool: an
         // offside must never be able to fall back to a goal celebration.
         {"timeup", "change", "foul", "pk", "result", "end", "offside"}) {
      std::string dir = std::string("media/cutscenes/") + category;
      std::error_code ec;
      // A referee's decision is not one shot: the fouls are exported into
      // subdirectories by what the official did (card_yellow, card_red,
      // warning, protest, referee_run, injury, no_card), and goals keep an
      // offside pool. Each subdirectory becomes its own pool named
      // "<category>/<sub>", while the category pool holds everything, so a
      // caller can ask for the precise moment and still fall back.
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".camtrack") continue;
        std::ifstream file(entry.path());
        CamTrack track;
        if (!file.good() || !track.Load(file)) continue;
        cutscenePools[category].push_back(track);
        const std::string parent = entry.path().parent_path().filename().string();
        if (parent != category)
          cutscenePools[std::string(category) + "/" + parent].push_back(track);
        // The post-match pool is flat, with PES's presentation family in the file
        // name rather than in a directory ("result_001_st000_cam1.camtrack"), so
        // give each family a pool of its own too. That is what lets the pre-match
        // screen offer a choice of post-match presentation instead of taking
        // whatever the pool hands over.
        const std::string family =
            PrematchChoices::FamilyFromCamtrackName(entry.path().filename().string());
        if (!family.empty())
          cutscenePools[std::string(category) + "/" + family].push_back(track);
      }
      // The people in shot: PES stages actors alongside the camera, so any
      // .chor exported next to the camerawork joins a matching pool.
      LoadCutsceneChoreo(category, dir);
      if (!cutscenePools[category].empty()) loadedPools++;
    }
    if (loadedPools > 0)
      Log(e_Notice, "Match", "Match",
          "Loaded stoppage cutscene pools for " + int_to_str(loadedPools) +
              " categories");
    // What each pool actually holds, measured rather than assumed
    // ("debug_cutscene_report"). See onthepitch/cutsceneviewer.hpp - camerawork
    // and actor choreography have been confused for one another more than once.
    if (GetConfiguration()->GetBool("debug_cutscene_report", false)) {
      for (const std::string& line : CutsceneViewer::Report(cutscenePools, cutsceneChoreoPools))
        Log(e_Notice, "Match", "CutsceneReport", line);
    }
  }
  // The model viewer is a bench, not a match: hold the kickoff for as long as
  // it runs. That is the same hold the entrance uses, and posing a player is
  // only safe while it is on - during play his own animation machinery owns
  // him and fighting it corrupts the put.
  {
    const float viewerSeconds =
        GetConfiguration()->GetReal("debug_model_viewer_seconds", 0.0f);
    if (viewerSeconds > 0.0f) introSeconds = std::max(introSeconds, viewerSeconds);
  }
  // The presentation timeline: which beats this competition stages before
  // kickoff, and how long each holds (docs/PRESENTATION_SPEC.md section 1).
  // It governs the entrance's length, not the camerawork's own running time -
  // PES holds a stadium shot or a lineup graphic for as long as the
  // presentation says, independent of how much camera track happens to exist.
  if (!entranceDisabled) {
    {
      // "media/objects/stadiums/pes_st060/..." -> "st060", so a shot authored
      // for the stadium being played wins over one that is not.
      const std::string stadiumPath = GetConfiguration()->Get("stadium_object", "");
      const size_t at = stadiumPath.find("st");
      const std::string token = at != std::string::npos && at + 5 <= stadiumPath.size()
                                    ? stadiumPath.substr(at, 5)
                                    : std::string();
      LoadPrematchShots(token);
      LoadPrematchStagingIndex(token);
    }
    prematchTimeline = LoadPrematchTimeline();
    const float configured = GetConfiguration()->GetReal("intro_cutscene_seconds", 0.0f);
    if (configured > 0.0f)
      prematchTimeline = PrematchTimeline::Rescale(prematchTimeline, configured);
    if (!prematchTimeline.beats.empty()) introSeconds = prematchTimeline.TotalSeconds();
  }

  if (introSeconds > 0.0f) {
    entranceSeconds = introSeconds;
    entranceActive = true;
    // Started on the first Process(), not here: the rest of the match load -
    // the stadium above all - still has to happen, and timing the
    // presentation from the constructor would spend most of it behind a
    // loading screen with nothing on the pitch to look at.
    entranceRealStart_ms = 0;
    introCutsceneDuration_ms = (unsigned long)(introSeconds * 1000.0f);
    // Held one tick ahead and pushed forward every Process() while the
    // entrance runs; the referee latches it as the kickoff's prepare time.
    introCutsceneEnd_ms = actualTime_ms + 1000;
    Log(e_Notice, "Match", "Match",
        "pre-match presentation: " + int_to_str((int)prematchTimeline.beats.size()) +
            " beat(s), holding kickoff for " + int_to_str((int)introSeconds) + "s of real time");
  }

  cameraUserZoom = GetConfiguration()->GetReal("camera_zoom", _default_CameraZoom);
  cameraUserHeight = GetConfiguration()->GetReal("camera_height", _default_CameraHeight);
  cameraUserFOV = GetConfiguration()->GetReal("camera_fov", _default_CameraFOV);
  cameraUserAngleFactor =
      GetConfiguration()->GetReal("camera_anglefactor", _default_CameraAngleFactor);

  autoUpdateIngameCamera = true;

  // stadium

  Log(e_Notice, "Match", "Match", "Loading stadium");

  boost::intrusive_ptr<Node> tmpStadiumNode;
  if (!SuperDebug()) {
    // stadiums are data: point this key at any .object under media/objects/
    // stadiums (imported PES stadiums install as pes_<id>/pes_<id>.object)
    std::string stadiumObject = GetConfiguration()->Get(
        "stadium_object", "media/objects/stadiums/test/test.object");
    tmpStadiumNode = loader.LoadObject(GetScene3D(), stadiumObject);
    Log(e_Notice, "Match", "Match", "stadium object loaded");

    // The advertising hoardings are not part of a stadium: PES keeps one ring for
    // every ground (Asset/model/bg/common/bill) and assigns the faces at run time.
    // Imported, they come in here the way the goals do - shared furniture every
    // stadium gets - and their faces carry the ad_placeholder ident, so the
    // randomiser below paints them from media/textures/adboards.
    const std::string adboardsObject =
        GetConfiguration()->Get("adboards_object", "media/objects/stadiums/adboards/adboards.object");
    if (!adboardsObject.empty() && std::filesystem::exists(adboardsObject)) {
      boost::intrusive_ptr<Node> boards = loader.LoadObject(GetScene3D(), adboardsObject);
      if (boards) {
        boards->SetLocalMode(e_LocalMode_Absolute);
        tmpStadiumNode->AddNode(boards);
        Log(e_Notice, "Match", "Match", "adboard ring loaded");
      }
    }

    RandomizeAdboards(tmpStadiumNode);
    Log(e_Notice, "Match", "Match", "adboards randomized");
  }
  if (SuperDebug())
    tmpStadiumNode =
        loader.LoadObject(GetScene3D(), "media/objects/stadiums/test/pitchonly.object");
  std::list<boost::intrusive_ptr<Geometry>> stadiumGeoms;

  // split stadium geometry into multiple geometry objects, for more efficient culling
  tmpStadiumNode->GetObjects<Geometry>(e_ObjectType_Geometry, stadiumGeoms);
  assert(stadiumGeoms.size() != 0);

  stadiumNode = boost::intrusive_ptr<Node>(new Node("stadium"));

  std::list<boost::intrusive_ptr<Geometry>>::iterator iter = stadiumGeoms.begin();
  while (iter != stadiumGeoms.end()) {
    boost::intrusive_ptr<Node> tmpNode = SplitGeometry(GetScene3D(), *iter, 24);
    tmpNode->SetLocalMode(e_LocalMode_Absolute);
    stadiumNode->AddNode(tmpNode);

    iter++;
  }
  Log(e_Notice, "Match", "Match", "stadium geometry split");
  tmpStadiumNode->Exit();
  tmpStadiumNode.reset();

  stadiumNode->SetLocalMode(e_LocalMode_Absolute);
  GetScene3D()->AddNode(stadiumNode);

  // goal netting

  Log(e_Notice, "Match", "Match", "Preparing goal netting");

  goalsNode = loader.LoadObject(GetScene3D(), "media/objects/stadiums/goals.object");
  goalsNode->SetLocalMode(e_LocalMode_Absolute);
  GetScene3D()->AddNode(goalsNode);
  PrepareGoalNetting();

  // optional sky geometry for stadiums that ship their own dome; the default
  // sky is a view-direction gradient in postprocess.frag (no geometry needed),
  // as imported geometry runs through the full lighting pipeline and comes out
  // fogged/desaturated (see docs/TECHNICAL_ROADMAP notes on self-illumination)
  // A converted stadium installs its own dome at <stadium>/sky/sky.object. It has
  // to come through here rather than inside the stadium node: the node's geometry is
  // split into 24 m grid cells for culling, and a 1154 m dome does not survive that
  // - it was never rasterised, which is why the sky was the fallback gradient and
  // the clouds and moons were missing. Loaded here it draws, and it is kept out of
  // the shadow map below.
  std::string skydomeObject = GetConfiguration()->Get("skydome_object", "");
  if (skydomeObject.empty()) {
    const std::string stadiumForSky = GetConfiguration()->Get("stadium_object", "");
    const std::string::size_type slash = stadiumForSky.find_last_of("/\\");
    if (slash != std::string::npos) {
      const std::string candidate = stadiumForSky.substr(0, slash + 1) + "sky/sky.object";
      if (std::filesystem::exists(candidate)) skydomeObject = candidate;
    }
  }
  if (skydomeObject != "" && !SuperDebug() &&
      std::filesystem::exists(skydomeObject)) {
    skydomeNode = loader.LoadObject(GetScene3D(), skydomeObject);
    skydomeNode->SetLocalMode(e_LocalMode_Absolute);
    // the dome encloses the whole shadow volume; letting it into the shadow
    // map would put the entire stadium in shade
    std::list<boost::intrusive_ptr<Geometry>> skydomeGeoms;
    skydomeNode->GetObjects<Geometry>(e_ObjectType_Geometry, skydomeGeoms);
    for (auto& geom : skydomeGeoms) geom->SetCastShadow(false);
    GetScene3D()->AddNode(skydomeNode);
  }

  // The people beside the pitch. A stadium pack does not carry them - PES keeps
  // one copy in its common package and hands it to every ground, and Planet
  // Namek's own staff pack is one of the 48-byte empty overrides - so they are
  // imported separately (tools/pes21_import/stadium_staff.py) and land beside
  // the stadium as staff/staff.object. Loaded outside the stadium node for the
  // same reason as the sky: the node's geometry is split into 24 m cells, and
  // figures scattered along both touchlines do not belong to one.
  std::string staffObject = GetConfiguration()->Get("staff_object", "");
  if (staffObject.empty()) {
    const std::string stadiumForStaff = GetConfiguration()->Get("stadium_object", "");
    const std::string::size_type slash = stadiumForStaff.find_last_of("/\\");
    if (slash != std::string::npos) {
      const std::string candidate = stadiumForStaff.substr(0, slash + 1) + "staff/staff.object";
      if (std::filesystem::exists(candidate)) staffObject = candidate;
    }
  }
  if (!staffObject.empty() && !SuperDebug() && std::filesystem::exists(staffObject)) {
    staffNode = loader.LoadObject(GetScene3D(), staffObject);
    staffNode->SetLocalMode(e_LocalMode_Absolute);
    GetScene3D()->AddNode(staffNode);
    Log(e_Notice, "Match", "Match", "touchline staff: " + staffObject);
  }

  // The furniture around it: corner flags, the fourth official's board, the
  // television cameras, the barrier at the tunnel mouth, the paramedics. A stadium
  // pack ships none of it either - PES keeps one set and gives it to every ground -
  // so it is imported the same way and lands as props/props.object
  // (tools/pes21_import/stadium_props.py). Outside the stadium node for the same
  // reason as the staff: pieces scattered round the whole pitch belong to no one
  // 24 m cell.
  std::string propsObject = GetConfiguration()->Get("props_object", "");
  if (propsObject.empty()) {
    const std::string stadiumForProps = GetConfiguration()->Get("stadium_object", "");
    const std::string::size_type slash = stadiumForProps.find_last_of("/\\");
    if (slash != std::string::npos) {
      const std::string candidate = stadiumForProps.substr(0, slash + 1) + "props/props.object";
      if (std::filesystem::exists(candidate)) propsObject = candidate;
    }
  }
  if (!propsObject.empty() && !SuperDebug() && std::filesystem::exists(propsObject)) {
    propsNode = loader.LoadObject(GetScene3D(), propsObject);
    propsNode->SetLocalMode(e_LocalMode_Absolute);
    GetScene3D()->AddNode(propsNode);
    Log(e_Notice, "Match", "Match", "pitch furniture: " + propsObject);
  }

  // And the crowd in the stands. PES gives every ground the same spectators and
  // says where they sit in the pack's own audi/audiarea.bin - st060 works out to
  // 5,120 seats - so the import writes one spectator per variant and a list of the
  // seats it fills, and the renderer draws one mesh many times
  // (tools/pes21_import/stadium_crowd.py, src/utils/instancelist.hpp). No shadows:
  // five thousand figures in the shadow map buys nothing at that distance.
  std::string crowdObject = GetConfiguration()->Get("crowd_object", "");
  if (crowdObject.empty()) {
    const std::string stadiumForCrowd = GetConfiguration()->Get("stadium_object", "");
    const std::string::size_type slash = stadiumForCrowd.find_last_of("/\\");
    if (slash != std::string::npos) {
      const std::string candidate = stadiumForCrowd.substr(0, slash + 1) + "crowd/crowd.object";
      if (std::filesystem::exists(candidate)) crowdObject = candidate;
    }
  }
  if (!crowdObject.empty() && !SuperDebug() && std::filesystem::exists(crowdObject)) {
    crowdNode = loader.LoadObject(GetScene3D(), crowdObject);
    crowdNode->SetLocalMode(e_LocalMode_Absolute);
    std::list<boost::intrusive_ptr<Geometry>> crowdGeoms;
    crowdNode->GetObjects<Geometry>(e_ObjectType_Geometry, crowdGeoms);
    for (auto& geom : crowdGeoms) geom->SetCastShadow(false);
    GetScene3D()->AddNode(crowdNode);
    Log(e_Notice, "Match", "Match", "crowd: " + crowdObject);
  }

  // What PES carries out for the walkout and takes away again: the flag bearers
  // and their banners at the tunnel mouth, the arch over it, the pennant display
  // on the centre circle, the tunnel itself. Loaded with the ground and dropped
  // the moment the presentation is over, because none of it belongs on a pitch
  // that is about to be played on (tools/pes21_import/stadium_props.py --set
  // entrance).
  std::string entranceProps = GetConfiguration()->Get("entrance_props_object", "");
  if (entranceProps.empty()) {
    const std::string stadiumForEntrance = GetConfiguration()->Get("stadium_object", "");
    const std::string::size_type slash = stadiumForEntrance.find_last_of("/\\");
    if (slash != std::string::npos) {
      const std::string candidate =
          stadiumForEntrance.substr(0, slash + 1) + "entrance/entrance.object";
      if (std::filesystem::exists(candidate)) entranceProps = candidate;
    }
  }
  if (!entranceProps.empty() && !SuperDebug() && entranceSeconds > 0.0f &&
      std::filesystem::exists(entranceProps)) {
    entrancePropsNode = loader.LoadObject(GetScene3D(), entranceProps);
    entrancePropsNode->SetLocalMode(e_LocalMode_Absolute);
    GetScene3D()->AddNode(entrancePropsNode);
    Log(e_Notice, "Match", "Match", "walkout set: " + entranceProps);
  }

  // pitch

  Log(e_Notice, "Match", "Match", "Generating pitch");

  // A converted PES stadium ships its own ground colour beside its .object, so
  // Planet Namek plays on teal blue rather than on GF's green.
  const std::string stadiumObject = GetConfiguration()->Get("stadium_object", "");
  const std::string turfCandidate = PitchTurf::TurfCandidate(stadiumObject);
  bool hasOwnTurf = false;
  if (!turfCandidate.empty()) {
    std::ifstream probe(turfCandidate.c_str(), std::ios::in | std::ios::binary);
    hasOwnTurf = probe.good();
  }
  const std::string grassTexture = PitchTurf::GrassTexturePath(stadiumObject, hasOwnTurf);

  // The stadium's own sky, if the converter sampled it: the gradient in
  // postprocess.frag is painted with these, and the band below the horizon with the
  // horizon colour, so the ground gap stops being a white void (stadiumsky.hpp).
  {
    const std::string skyPath = StadiumSky::SidecarPath(stadiumObject);
    std::string contents;
    if (!skyPath.empty()) {
      std::ifstream file(skyPath.c_str());
      if (file.good())
        contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    const StadiumSky::Colours sky = StadiumSky::Parse(contents);
    const float* fog = StadiumSky::FogColour(sky);
    GetConfiguration()->Set("sky_zenith_r", sky.zenith[0]);
    GetConfiguration()->Set("sky_zenith_g", sky.zenith[1]);
    GetConfiguration()->Set("sky_zenith_b", sky.zenith[2]);
    GetConfiguration()->Set("sky_horizon_r", sky.horizon[0]);
    GetConfiguration()->Set("sky_horizon_g", sky.horizon[1]);
    GetConfiguration()->Set("sky_horizon_b", sky.horizon[2]);
    GetConfiguration()->Set("sky_fog_r", fog[0]);
    GetConfiguration()->Set("sky_fog_g", fog[1]);
    GetConfiguration()->Set("sky_fog_b", fog[2]);
    if (sky.valid)
      Log(e_Notice, "Match", "Match", "stadium sky: zenith and horizon from " + skyPath);
  }

  // Where this ground's sun is, if the pack said so (scenelighting.hpp). Read
  // before the sun is placed, which happens in SetRandomSunParams below.
  {
    const std::string lightingPath = SceneLighting::SidecarPath(stadiumObject);
    std::string contents;
    if (!lightingPath.empty()) {
      std::ifstream file(lightingPath.c_str());
      if (file.good())
        contents.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    stadiumSun = SceneLighting::Parse(contents);
    // What this ground wants of the engine's fog. Namek asks for none, and with
    // the fog taking the horizon's colour that is the difference between rock
    // formations in their own colour and a flat green wash.
    GetConfiguration()->Set("graphics_fog_strength", stadiumSun.fog);
    if (stadiumSun.valid)
      Log(e_Notice, "Match", "Match",
          "stadium sun from " + lightingPath + ": " + real_to_str(stadiumSun.direction[0]) + ", " +
              real_to_str(stadiumSun.direction[1]) + ", " + real_to_str(stadiumSun.direction[2]));
  }

  // How far this stadium's own geometry reaches, if the converter measured it.
  {
    const std::string sidecar = StadiumFar::SidecarPath(stadiumObject);
    if (!sidecar.empty()) {
      std::ifstream file(sidecar.c_str());
      std::string contents;
      if (file.good()) std::getline(file, contents);
      stadiumFarNeeded = StadiumFar::ParseDistance(contents);
      if (stadiumFarNeeded > 0.0f)
        Log(e_Notice, "Match", "Match",
            "stadium reaches " + int_to_str((int)stadiumFarNeeded) + " m; far plane raised");
    }
  }
  if (hasOwnTurf)
    Log(e_Notice, "Match", "Match", "pitch turf: " + grassTexture);

  if (IsReleaseVersion()) {
    GeneratePitch(2048, 1024, 1024, 512, 2048, 1024, grassTexture);
  } else {
    GeneratePitch(1024, 512, 1024, 512, 2048, 1024, grassTexture);
  }

  // sun

  Log(e_Notice, "Match", "Match", "Loading sun object");

  sunNode = loader.LoadObject(GetScene3D(), "media/objects/lighting/generic.object");
  GetDynamicNode()->AddNode(sunNode);
  SetRandomSunParams();

  // human gamers

  Log(e_Notice, "Match", "Match", "Human gamer controller init");

  UpdateControllerSetup();

  // 12th man sound

  Log(e_Notice, "Match", "Match", "Loading crowd sounds");

  // The crowd's own voice. PES keeps its in dt44_all.cpk
  // (common/sound/match/awb/cheers/SE_Cheers_CM_Goal*.awb, HCA streams the import
  // turns into ogg), and its roar belongs on the reaction layer that the mood
  // mixing gains up when the crowd is on its feet. Where the import has landed,
  // that is what plays; otherwise the engine's own two loops, as before.
  const std::string ambientSound =
      GetConfiguration()->Get("crowd_ambient_sound", "media/sounds/crowd01.wav");
  std::string reactionSound = GetConfiguration()->Get("crowd_reaction_sound", "");
  if (reactionSound.empty()) {
    const std::string imported = "imports/pes21/cheers/cheer_goal_00000.ogg";
    reactionSound =
        std::filesystem::exists(imported) ? imported : std::string("media/sounds/crowd02.wav");
  }
  Log(e_Notice, "Match", "Match", "crowd: " + ambientSound + " under " + reactionSound);

  boost::intrusive_ptr<Resource<SoundBuffer>> soundBufferRes =
      ResourceManagerPool::GetInstance()
          .GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)
          ->Fetch(ambientSound, true, true);
  crowd01 = boost::static_pointer_cast<Sound>(
      ObjectFactory::GetInstance().CreateObject("crowd01sound", e_ObjectType_Sound));
  GetScene3D()->CreateSystemObjects(crowd01);
  crowd01->SetSoundBuffer(soundBufferRes);
  crowd01->SetGain(0.0f);
  crowd01->SetLoop(true);
  crowd01->Poke(e_SystemType_Audio);
  GetScene3D()->AddObject(crowd01);

  soundBufferRes = ResourceManagerPool::GetInstance()
                       .GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)
                       ->Fetch(reactionSound, true, true);
  crowd02 = boost::static_pointer_cast<Sound>(
      ObjectFactory::GetInstance().CreateObject("crowd02sound", e_ObjectType_Sound));
  GetScene3D()->CreateSystemObjects(crowd02);
  crowd02->SetSoundBuffer(soundBufferRes);
  crowd02->SetGain(0.0f);
  crowd02->SetLoop(true);
  crowd02->Poke(e_SystemType_Audio);
  GetScene3D()->AddObject(crowd02);

  // per-team chants from the import pack (any format the sound manager
  // loads; the pack ships OGG)
  for (int t = 0; t < 2; t++) {
    std::string chantFile = GetConfiguration()->Get(
        ("team" + int_to_str(t + 1) + "_chant").c_str(), "");
    if (chantFile.empty() || !std::filesystem::exists(chantFile)) continue;
    boost::intrusive_ptr<Resource<SoundBuffer>> chantBuffer =
        ResourceManagerPool::GetInstance()
            .GetManager<SoundBuffer>(e_ResourceType_SoundBuffer)
            ->Fetch(chantFile, true, true);
    teamChant[t] = boost::static_pointer_cast<Sound>(
        ObjectFactory::GetInstance().CreateObject(
            "teamchant" + int_to_str(t), e_ObjectType_Sound));
    GetScene3D()->CreateSystemObjects(teamChant[t]);
    teamChant[t]->SetSoundBuffer(chantBuffer);
    teamChant[t]->SetGain(0.0f);
    teamChant[t]->SetLoop(true);
    teamChant[t]->Poke(e_SystemType_Audio);
    GetScene3D()->AddObject(teamChant[t]);
  }

  // match params

  matchTime_ms = 0;
  matchTimeExact_ms = 0.0;
  pause = false;
  inPlay = false;
  inSetPiece = false;
  goalScored = false;
  ballIsInGoal = false;
  lastGoalTeamID = 0;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeamID = -1;
  SetMatchPhase(e_MatchPhase_PreMatch);

  gameSequenceInfo = GetScheduler()->GetTaskSequenceInfo("game");

  previousProcessTime_ms =
      EnvironmentManager::GetInstance().GetTime_ms() - gameSequenceInfo.startTime_ms;
  previousPutTime_ms =
      EnvironmentManager::GetInstance().GetTime_ms() - gameSequenceInfo.startTime_ms;
  timeSincePreviousProcess_ms = 0;
  timeSincePreviousPut_ms = 0;

  // everybody hates him, this poor bloke

  Log(e_Notice, "Match", "Match", "Creating referee functionality");

  referee = std::make_unique<Referee>(this);

  // GUI

  Log(e_Notice, "Match", "Match", "Creating GUI elements");

  Gui2Root* root = menuTask->GetWindowManager()->GetRoot();

  // Bottom centre, with the two player indicators either side of it: home lower
  // left, away lower right, the way the PES reference lays the lower third out.
  // It used to sit bottom right to stay clear of the pre-match formation panel
  // and the version caption; the panel only shows before kickoff, and the whole
  // match HUD is hidden for the presentation anyway (ShowMatchHud).
  radar = std::make_unique<Gui2Radar>(
      menuTask->GetWindowManager(), "game_radar", 38.0f, 79, 24, 18, this,
      matchData->GetTeamData(0)->GetColor1(), matchData->GetTeamData(0)->GetColor2(),
      matchData->GetTeamData(1)->GetColor1(), matchData->GetTeamData(1)->GetColor2());
  root->AddView(radar.get());
  radar->Show();
  // off / transparent / on, picked in the graphics settings page
  radar->SetTransparentOpacity(GetConfiguration()->GetReal("radar_opacity", 0.55f));
  radar->SetMode(
      Gui2Radar::ParseMode(GetConfiguration()->Get("radar_mode", "transparent")));

  // The player indicators sit in the bottom corners, the way a broadcast puts
  // them: the user's side on the left, the other mirrored on the right.
  for (int side = 0; side < 2; side++) {
    playerHUD[side] = std::make_unique<Gui2PlayerHUD>(
        menuTask->GetWindowManager(), "game_playerhud_" + int_to_str(side),
        side == 0 ? 1.0f : 63.0f, 92.0f, 36.0f, 5.5f, this, side, side == 1);
    root->AddView(playerHUD[side].get());
    playerHUD[side]->Show();
  }

  tacticsDebug = nullptr;
  if (1 == 2) {
    tacticsDebug = std::make_unique<Gui2TacticsDebug>(menuTask->GetWindowManager(),
                                                      "game_tacticsdebug", 22, 1.3f, 56, 26, this);
    root->AddView(tacticsDebug.get());
    tacticsDebug->Show();

    const TeamTactics& tactics = matchData->GetTeamData(0)->GetTactics();
    const map_Properties* userMods = tactics.userProperties.GetProperties();
    map_Properties::const_iterator tacIter = userMods->begin();
    int i = 0;
    while (tacIter != userMods->end()) {
      printf("adding tactical debug item %s (%s)\n", (*tacIter).first.c_str(),
             (*tacIter).second.c_str());
      Vector3 color(sin(i * 0.7f) * 0.5 + 0.5, cos(i * 0.9f) * 0.5 + 0.5,
                    sin(i * 1.1f) * 0.5 + 0.5);
      color = color.GetNormalized(0) * 255;
      color = color * 0.7f + Vector3(255, 255, 255) * 0.3f;
      Vector3 color1 = color * 0.6f;
      Vector3 color2 = color * 0.4f;
      Vector3 color3 = color * 1.0f;
      tacticsDebug->AddEntry((*tacIter).first, color1, color2, color3);
      tacIter++;
      i++;
    }
    tacticsDebug->Redraw();
  }

  scoreboard = std::make_unique<Gui2ScoreBoard>(menuTask->GetWindowManager(), this);
  root->AddView(scoreboard.get());
  scoreboard->Show();

  statsOverlay = std::make_unique<Gui2StatsOverlay>(menuTask->GetWindowManager(), this);
  root->AddView(statsOverlay.get());
  // Normally toggled with TAB (see gamepage.cpp). "debug_stats_overlay_always"
  // holds it up from kickoff instead, so its layout can be judged in a
  // headless capture without having to land a keypress - the same affordance
  // "debug_formation_graphic_always" gives the pre-match panel.
  if (GetConfiguration()->GetBool("debug_stats_overlay_always", false)) {
    statsOverlay->UpdateStats();
    statsOverlay->Show();
  } else {
    statsOverlay->Hide();
  }

  // Pre-match formation graphic (docs/PRESENTATION_SPEC.md 1.1) and in-match
  // lower-third banner (section 4). Both drive their own visibility/fade off
  // Match's already-public state (entrance timing / explicit Show calls) via
  // an overridden Process(), so no further per-frame wiring is needed here.
  // Init() is called after AddView() deliberately, so each widget builds its
  // images/captions once already attached to its final parent.
  formationGraphic =
      std::make_unique<Gui2FormationGraphic>(menuTask->GetWindowManager(), "game_formationgraphic", this);
  root->AddView(formationGraphic.get());
  formationGraphic->Init();

  banner = std::make_unique<Gui2Banner>(menuTask->GetWindowManager(), "game_banner", this);
  root->AddView(banner.get());
  banner->Init();

  // for usage in destructor
  scene3D = GetScene3D();

  // replays

  Log(e_Notice, "Match", "Match", "Initialising replay data array");

  std::list<boost::intrusive_ptr<Spatial>> spatials;
  GetReplaySpatials(spatials);

  std::list<boost::intrusive_ptr<Spatial>>::iterator spatialIter = spatials.begin();
  while (spatialIter != spatials.end()) {
    auto spatial = std::make_unique<ReplaySpatial>(GetReplaySize_ms() / 10);
    spatial->spatial = *spatialIter;
    replay.push_back(std::move(spatial));
    spatialIter++;
  }
  replayBallTouchesNetFrames =
      blunted::circular_buffer<ReplayBallTouchesNetFrame>(GetReplaySize_ms() / 10);

  excitement = 0.0f;
  excitementEventBoost = 0.0f;
  excitementEventTimer_ms = 0;
  crowdAmbientGain = 0.0f;
  crowdReactionGain = 0.0f;

  lastBodyBallCollisionTime_ms = 0;

  gameOver = false;

  possessionSideHistory = std::make_unique<ValueHistory<float>>(6000);

  penaltyShootout = std::make_unique<PenaltyShootoutController>(this);

  // Weather for this match, from the gameplay settings ("match_weather": 0 dry,
  // 0.5 rain, 1 storm). Wind direction is drawn once per match.
  {
    const float weather = clamp(GetConfiguration()->GetReal("match_weather", 0.0f), 0.0f, 1.0f);
    const float wetness = weather;
    const float windStrength = weather * weather * 6.0f;  // storms bend the ball much more
    const float windAngle = random(0.0f, 2.0f * pi);
    ball->SetWeather(
        Vector3(std::cos(windAngle) * windStrength, std::sin(windAngle) * windStrength, 0.0f),
        wetness);
  }

  // Straight to a shootout: handy for trying penalties out without playing a
  // whole match first ("penalty_shootout_only" in the config).
  if (GetConfiguration()->GetBool("penalty_shootout_only", false)) {
    Log(e_Notice, "Match", "Match", "penalty shootout only: skipping to the shootout");
    SetMatchPhase(e_MatchPhase_Penalties);
    StopPlay();
    StopSetPiece();
  }

  Log(e_Notice, "Match", "Match", "Done creating match!");

  // light test

  int maxTestLights = 0;
  if (maxTestLights > 0) {
    std::vector<boost::intrusive_ptr<Light>> lightTest(maxTestLights);
    for (int li = 0; li < maxTestLights; li++) {
      lightTest[li] = boost::static_pointer_cast<Light>(ObjectFactory::GetInstance().CreateObject(
          "testLight #" + int_to_str(li), e_ObjectType_Light));
      scene3D->CreateSystemObjects(lightTest[li]);
      lightTest[li]->SetShadow(false);
      lightTest[li]->SetType(e_LightType_Point);
      lightTest[li]->SetColor(Vector3(sin(li) * 0.5f + 0.5f, sin(li + 0.66f * pi) * 0.5f + 0.5f,
                                      sin(li * 1.33f * pi) * 0.5f + 0.5f));
      lightTest[li]->SetPosition(Vector3(sin(li / (float)maxTestLights * 2 * pi) * 40,
                                         cos(li / (float)maxTestLights * 2 * pi) * 30, 0.5f));
      lightTest[li]->SetRadius(8.0f);
      scene3D->AddObject(lightTest[li]);
    }
  }

  if (_positionLogging)
    positionLogFile.open("positions.log", std::ios::out);

  if (Verbose())
    printf("ready..\n");
  sig_OnCreatedMatch(this);
  if (Verbose())
    printf("set..\n");
  LoadingMatchPage* loadingMatchPage = static_cast<LoadingMatchPage*>(
      menuTask->GetWindowManager()->GetPageFactory()->GetMostRecentlyCreatedPage());
  loadingMatchPage->Close();
  if (Verbose())
    printf("loadingmatchpage closed\n");
}

Match::~Match() {}

void Match::Exit() {
  if (Verbose())
    printf("exiting match.. ");

  if (Verbose())
    printf("\nscene3D tree before match Exit():\n");
  if (Verbose())
    scene3D->PrintTree();

  possessionSideHistory.reset();

  anims.reset();
  teams[0]->Exit();
  teams[1]->Exit();
  teams[0].reset();
  teams[1].reset();
  officials.reset();
  ball.reset();
  referee.reset();
  delete matchData;
  menuTask->SetMatchData(nullptr);

  mentalImages.clear();

  fullbodyNode->Exit();

  fullbodyNode.reset();

  // Exit() removes these from the GUI view tree (root) and cleans up their
  // own children; reset() then deletes each widget exactly once. Without
  // this, root's cascading Exit() (triggered below by menuTask.reset()) would
  // delete them first, leaving these unique_ptrs dangling and causing a
  // heap-use-after-free when Match's destructor runs.
  formationGraphic->Exit();
  formationGraphic.reset();
  banner->Exit();
  banner.reset();

  // remove, don't delete, because main.cpp is owner
  GetDynamicNode()->RemoveObject(GetGreenDebugPilon());
  GetDynamicNode()->RemoveObject(GetBlueDebugPilon());
  GetDynamicNode()->RemoveObject(GetYellowDebugPilon());
  GetDynamicNode()->RemoveObject(GetRedDebugPilon());
  GetDynamicNode()->RemoveObject(GetSmallDebugCircle1());
  GetDynamicNode()->RemoveObject(GetSmallDebugCircle2());
  GetDynamicNode()->RemoveObject(GetLargeDebugCircle());

  scene3D->DeleteNode(GetDynamicNode());
  scene3D->DeleteNode(stadiumNode);
  scene3D->DeleteNode(goalsNode);
  if (skydomeNode) scene3D->DeleteNode(skydomeNode);
  if (staffNode) scene3D->DeleteNode(staffNode);
  if (propsNode) scene3D->DeleteNode(propsNode);
  if (crowdNode) scene3D->DeleteNode(crowdNode);
  if (entrancePropsNode) scene3D->DeleteNode(entrancePropsNode);

  scene3D->DeleteObject(crowd01);
  scene3D->DeleteObject(crowd02);

  radar->Exit();
  radar.reset();

  // Exit() is what detaches a view from the GUI root and deletes its children;
  // the destructor does nothing. Resetting the pointer without it leaves the root
  // holding a dangling view, and tearing the root down later aborts inside
  // Gui2View's destructor.
  for (int side = 0; side < 2; side++) {
    if (!playerHUD[side]) continue;
    playerHUD[side]->Exit();
    playerHUD[side].reset();
  }
  if (tacticsDebug) {
    tacticsDebug->Exit();
    tacticsDebug.reset();
  }

  scoreboard->Exit();
  scoreboard.reset();

  statsOverlay->Exit();
  statsOverlay.reset();

  animPositionCache.clear();

  menuTask.reset();
  if (Verbose())
    printf("remaining tree (should be none):\n");
  if (Verbose())
    scene3D->PrintTree();
  if (Verbose())
    printf("done printing\n");

  if (Verbose())
    printf("done\n");

  /*
    if (missingAnims.size() > 0) {
      printf("*** MISSING ANIMS ***\n");
      std::sort(missingAnims.begin(), missingAnims.end());
      for (unsigned int i = 0; i < missingAnims.size(); i++) {
        //printf("[%i times] dir %f, %f, %f; velo %i; bodydir(abs) %f, %f, %f\n",
    missingAnims.at(i).timesMissed, missingAnims.at(i).outgoingDirection.coords[0],
    missingAnims.at(i).outgoingDirection.coords[1], missingAnims.at(i).outgoingDirection.coords[2],
    missingAnims.at(i).outgoingVelocity, missingAnims.at(i).outgoingBodyDirectionAbs.coords[0],
    missingAnims.at(i).outgoingBodyDirectionAbs.coords[1],
    missingAnims.at(i).outgoingBodyDirectionAbs.coords[2]); printf("[%i times] dir %i; velo %i;
    bodydir(rel) %i; average difference: %i\n", missingAnims.at(i).timesMissed,
    int(round(missingAnims.at(i).outgoingDirection.GetAngle2D(Vector3(0, -1, 0)) / pi * 180.0f)),
    missingAnims.at(i).outgoingVelocity,
    int(round(missingAnims.at(i).outgoingBodyDirection.GetAngle2D(Vector3(0, -1, 0)) / pi *
    180.0f)), int(round(missingAnims.at(i).angleDifference / pi * 180.0f)));
      }
      printf("*********************\n");
      missingAnims.clear();
    }
  */

  if (_positionLogging)
    positionLogFile.close();

  sig_OnExitedMatch(this);
}

void Match::SetRandomSunParams() {
  if (Verbose())
    printf("setting random sun params\n");

  // Time of day chosen before kick-off: 0 day, 0.5 evening, 1 night. A night
  // match is lit by the floodlights, so the sun sits low and dim.
  const float timeOfDay = clamp(GetConfiguration()->GetReal("match_time_of_day", 0.0f), 0.0f, 1.0f);

  float brightness = 1.0f - timeOfDay * 0.45f;

  Vector3 sunPos = Vector3(-1.2f, 0.4f, 1.0f);  // sane default
  float averageHeightMultiplier = 1.3f - timeOfDay * 0.9f;
  // A stadium that shipped its own lighting says where its sun is, and PES's
  // answer is a place, a date and a time rather than a guess - so its shadows
  // fall the same way every kickoff, which is what the broadcast looks like.
  // Without one, the old dice roll.
  const bool haveStadiumSun = stadiumSun.valid;
  if (haveStadiumSun) {
    sunPos = Vector3(stadiumSun.direction[0], stadiumSun.direction[1], stadiumSun.direction[2]);
    sunPos.Normalize();
  } else {
    sunPos = Vector3(clamp(random(-1.7f, 1.7f), -1.0, 1.0), clamp(random(-1.7f, 1.7f), -1.0, 1.0),
                     averageHeightMultiplier);
    sunPos.Normalize();
    if (random(0, 1) > 0.5f && sunPos.coords[1] > 0.25f)
      sunPos.coords[1] = -sunPos.coords[1];  // sun more often on (default) camera side (coming from
                                             // front == clearer lighting on players)
  }
  sunNode->GetObject("sun")->SetPosition(sunPos * 10000.0f);

  float defaultRadius = 1000000.0f;
  float sunRadius = defaultRadius;
  boost::static_pointer_cast<Light>(sunNode->GetObject("sun"))->SetRadius(sunRadius);

  Vector3 sunColorNoon(0.9, 0.8, 1.0);
  sunColorNoon *= 1.4f;
  Vector3 sunColorDusk(1.4, 0.9, 0.7);
  sunColorDusk *= 1.2f;

  float noonBias = pow(NormalizedClamp(sunPos.coords[2], 0.5f, 1.0f), 1.2f);
  // Later in the day the warm dusk tint takes over.
  noonBias *= 1.0f - timeOfDay;
  Vector3 sunColor = sunColorNoon * noonBias + sunColorDusk * (1.0f - noonBias);

  if (!haveStadiumSun) {
    // The jitter is there to keep randomly-lit matches from all looking alike;
    // a ground that says how it is lit should look the same every time.
    Vector3 randomAddition(random(-0.1, 0.1), random(-0.1, 0.1), random(-0.1, 0.1));
    randomAddition *= 1.2f;
    sunColor += randomAddition;
  }

  if (Verbose())
    printf("sunlight noonbias: %f, stadium sun: %s\n", noonBias, haveStadiumSun ? "yes" : "no");

  boost::static_pointer_cast<Light>(sunNode->GetObject("sun"))->SetColor(sunColor * brightness);
}

void Match::RandomizeAdboards(boost::intrusive_ptr<Node> stadiumNode) {
  if (Verbose())
    printf("randomizing adboards..\n");

  // collect texture files

  DirectoryParser parser;
  std::vector<std::string> files;
  parser.Parse("media/textures/adboards", "png", files, false);

  std::vector<boost::intrusive_ptr<Resource<Surface>>> adboardSurfaces;
  for (unsigned int i = 0; i < files.size(); i++) {
    Log(e_Notice, "Match", "RandomizeAdboards", "loading adboard file " + files.at(i));
    adboardSurfaces.push_back(ResourceManagerPool::GetInstance()
                                  .GetManager<Surface>(e_ResourceType_Surface)
                                  ->Fetch(files.at(i)));
  }
  if (Verbose())
    printf("%zu adboards loaded (out of %zu files)\n", adboardSurfaces.size(), files.size());
  if (adboardSurfaces.empty())
    return;

  // collect adboard geoms

  std::list<boost::intrusive_ptr<Geometry>> stadiumGeoms;
  stadiumNode->GetObjects<Geometry>(e_ObjectType_Geometry, stadiumGeoms, true);
  if (Verbose())
    printf("number of stadium objects: %zu\n", stadiumGeoms.size());

  // replace

  std::list<boost::intrusive_ptr<Geometry>>::const_iterator stadiumGeomsIter = stadiumGeoms.begin();
  while (stadiumGeomsIter != stadiumGeoms.end()) {
    boost::intrusive_ptr<Geometry> geomObject = *stadiumGeomsIter;
    assert(geomObject != boost::intrusive_ptr<Object>());
    boost::intrusive_ptr<Resource<GeometryData>> adboardGeom = geomObject->GetGeometryData();

    adboardGeom->resourceMutex.lock();

    std::vector<MaterializedTriangleMesh>& tmesh =
        adboardGeom->GetResource()->GetTriangleMeshesRef();

    bool replacedAny = false;
    for (unsigned int i = 0; i < tmesh.size(); i++) {
      if (tmesh.at(i).material.diffuseTexture != boost::intrusive_ptr<Resource<Surface>>()) {
        std::string identString = tmesh.at(i).material.diffuseTexture->GetIdentString();
        // printf("%s\n", identString.c_str());
        if (identString.find("ad_placeholder") == 0) {
          tmesh.at(i).material.diffuseTexture =
              adboardSurfaces.at(int(floor(random(0, adboardSurfaces.size() - 1.001f))));
          tmesh.at(i).material.specular_amount = 0.2f;
          tmesh.at(i).material.shininess = 0.1f;
          replacedAny = true;
        }
      } else if (Verbose())
        printf("no diffuse texture\n");
    }

    adboardGeom->resourceMutex.unlock();

    // Only the geometry that actually got a new hoarding needs re-uploading.
    // This ran for every object in the stadium whether or not it held an
    // adboard at all; an imported stadium has few adboard meshes and a great
    // many others, so most of those re-uploads were asking the graphics
    // system to redo work for an unchanged mesh.
    if (replacedAny) geomObject->OnUpdateGeometryData();

    stadiumGeomsIter++;
  }
}

void Match::UpdateControllerSetup() {
  // remove current gamers
  teams[0]->DeleteHumanGamers();
  teams[1]->DeleteHumanGamers();

  // add new
  const std::vector<SideSelection> sides = menuTask->GetControllerSetup();
  for (unsigned int i = 0; i < sides.size(); i++) {
    const int controllerID = sides.at(i).controllerID;
    if ((sides.at(i).side == -1 || sides.at(i).side == 1) && controllerID >= 0 &&
        controllerID < static_cast<int>(controllers.size())) {
      int teamID = int(round(sides.at(i).side * 0.5 + 0.5));
      teams[teamID]->AddHumanGamer(controllers.at(controllerID),
                                   (e_PlayerColor)i);  // todo: proper color
      // printf("team id %i, %i\n", teamID, sides.at(i).controllerID);
    }
  }

  // Coach mode: teams without a human on the sticks can still be run from the
  // touchline by a human manager.
  const bool coachModeEnabled = GetConfiguration()->GetBool("coach_mode", false);
  coachSetup = CoachMode::FromHumanGamerCounts(teams[0]->GetHumanGamerCount(),
                                               teams[1]->GetHumanGamerCount(), coachModeEnabled);
}

void Match::SpamMessage(const std::string& msg, int time_ms) {
  ShowBanner(-1, msg, "", time_ms);
}

void Match::ShowBanner(int teamID, const std::string& title, const std::string& subtitle,
                       int time_ms) {
  banner->Show(teamID, title, subtitle, time_ms);
}

Player* Match::GetPlayer(int playerID) {
  for (int t = 0; t < 2; t++) {
    for (unsigned int p = 0; p < teams[t]->GetAllPlayers().size(); p++) {
      if (teams[t]->GetAllPlayers().at(p)->GetID() == playerID) {
        return teams[t]->GetAllPlayers().at(p).get();
      }
    }
  }

  assert(1 == 2);  // shouldn't be here ;)
  return 0;
}

void Match::GetAllTeamPlayers(int teamID, std::vector<Player*>& players) {
  teams[teamID]->GetAllPlayers(players);
}

void Match::GetActiveTeamPlayers(int teamID, std::vector<Player*>& players) {
  teams[teamID]->GetActivePlayers(players);
}

void Match::GetOfficialPlayers(std::vector<PlayerBase*>& players) {
  officials->GetPlayers(players);
}

std::shared_ptr<const MentalImage> Match::GetMentalImageOwned(int history_ms) {
  if (mentalImages.empty()) return nullptr;

  int index = int(round((float)history_ms / 10.0));
  if (index >= (signed int)mentalImages.size()) index = mentalImages.size() - 1;
  if (index < 0) index = 0;

  mentalImages.at(index)->SetTimeStampNeg_ms(index * 10.0f);
  return mentalImages.at(index);
}

const MentalImage* Match::GetMentalImage(int history_ms) {
  // No images yet (the first frames of a match) or none left (teardown): the
  // clamp below would ask for element -1 and hand back a bogus image, whose
  // match pointer the caller then reads through.
  if (mentalImages.empty()) return nullptr;

  int index = int(round((float)history_ms / 10.0));
  if (index >= (signed int)mentalImages.size())
    index = mentalImages.size() - 1;
  if (index < 0)
    index = 0;

  mentalImages.at(index)->SetTimeStampNeg_ms(index * 10.0f);

  return mentalImages.at(index).get();
}

void Match::UpdateLatestMentalImageBallPredictions() {
  if (mentalImages.size() > 0)
    mentalImages.at(0)->UpdateBallPredictions();
}

void Match::LoadPrematchShots(const std::string& stadiumToken) {
  // PES ships each entrance shot as its own ent_<id> family, and the file
  // name says which shot it is. Index them all by the words in the name so a
  // beat can ask for "passage01" (the tunnel) or "anth" (the anthems) and get
  // the camerawork that was authored for it, whichever family it lives in.
  const std::string root =
      GetConfiguration()->Get("entrance_dir", "media/cutscenes/ent");
  std::error_code ec;
  std::vector<std::string> paths;
  for (const auto& dir : std::filesystem::directory_iterator(root, ec)) {
    if (!dir.is_directory()) continue;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path(), ec))
      if (entry.path().extension() == ".camtrack") paths.push_back(entry.path().string());
  }
  std::sort(paths.begin(), paths.end());

  for (const std::string& path : paths) {
    std::string name = std::filesystem::path(path).stem().string();
    // "ent_007_passage01_cmn_cam" -> the words between the family number and
    // the trailing "cam".
    std::vector<std::string> words;
    std::string word;
    for (char c : name + "_") {
      if (c == '_') {
        if (!word.empty()) words.push_back(word);
        word.clear();
      } else {
        word += c;
      }
    }
    const bool matchesStadium =
        !stadiumToken.empty() && name.find(stadiumToken) != std::string::npos;

    std::ifstream file(path);
    CamTrack track;
    if (!file.good() || !track.Load(file) || track.GetFrameCount() == 0) continue;

    // The whole stem is a key as well, so a beat can name a specific pack
    // ("ent_009_st002_cmn_cam") when a shared token would be ambiguous.
    prematchShots.emplace(name, track);
    for (const std::string& token : words) {
      if (token == "ent" || token == "cam" || token.empty()) continue;
      if (token.size() <= 2) continue;              // family numbers
      if (token.rfind("st", 0) == 0) continue;      // stadium tags
      // A shot authored for the stadium being played wins over one that is
      // not; otherwise first in sorted order.
      auto existing = prematchShots.find(token);
      if (existing == prematchShots.end())
        prematchShots.emplace(token, track);
      else if (matchesStadium)
        existing->second = track;
    }
  }
  Log(e_Notice, "Match", "LoadPrematchShots",
      int_to_str((int)prematchShots.size()) + " named entrance shot(s) from " +
          int_to_str((int)paths.size()) + " track(s)");
}

void Match::LoadPrematchStagingIndex(const std::string& stadiumToken) {
  // Same tokenisation as the camerawork, so "passage01" finds both
  // ent_007_passage01_cmn_cam.camtrack and ent_007_passage01_cmn_pl.chor.
  const std::string root =
      GetConfiguration()->Get("entrance_dir", "media/cutscenes/ent");
  std::error_code ec;
  std::vector<std::string> paths;
  for (const auto& dir : std::filesystem::directory_iterator(root, ec)) {
    if (!dir.is_directory()) continue;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path(), ec))
      if (entry.path().extension() == ".chor") paths.push_back(entry.path().string());
  }
  std::sort(paths.begin(), paths.end());

  for (const std::string& path : paths) {
    const std::string name = std::filesystem::path(path).stem().string();
    const bool matchesStadium =
        !stadiumToken.empty() && name.find(stadiumToken) != std::string::npos;
    std::vector<std::string> words;
    std::string word;
    for (char c : name + "_") {
      if (c == '_') {
        if (!word.empty()) words.push_back(word);
        word.clear();
      } else {
        word += c;
      }
    }
    {
      PrematchStaging whole;
      whole.path = path;
      whole.directory = std::filesystem::path(path).parent_path().string();
      prematchStagings.emplace(name, whole);
    }
    for (const std::string& token : words) {
      if (token == "ent" || token == "pl" || token.size() <= 2) continue;
      if (token.rfind("st", 0) == 0) continue;
      auto existing = prematchStagings.find(token);
      if (existing != prematchStagings.end() && !matchesStadium) continue;
      PrematchStaging staging;
      staging.path = path;
      staging.directory = std::filesystem::path(path).parent_path().string();
      prematchStagings[token] = staging;
    }
  }
  Log(e_Notice, "Match", "LoadPrematchStagingIndex",
      int_to_str((int)prematchStagings.size()) + " named staging(s) from " +
          int_to_str((int)paths.size()) + " pack(s)");
}

Match::PrematchStaging* Match::AcquirePrematchStaging(const std::string& shot) {
  if (shot.empty()) return nullptr;
  auto found = prematchStagings.find(shot);
  if (found == prematchStagings.end()) {
    for (auto entry = prematchStagings.begin(); entry != prematchStagings.end(); ++entry) {
      if (entry->first.find(shot) != std::string::npos) {
        found = entry;
        break;
      }
    }
  }
  if (found == prematchStagings.end()) return nullptr;
  PrematchStaging& staging = found->second;
  if (!staging.loaded) {
    staging.loaded = true;
    std::ifstream file(staging.path);
    if (file.good() && staging.choreo.Load(file)) {
      for (const auto& slot : staging.choreo.GetSlots()) {
        if (staging.clips.count(slot.animFile)) continue;
        const std::string clipPath = staging.directory + "/" + slot.animFile;
        if (!std::filesystem::exists(clipPath)) continue;
        auto clip = std::make_shared<Animation>();
        clip->Load(clipPath);
        staging.clips[slot.animFile] = clip;
      }
      Log(e_Notice, "Match", "AcquirePrematchStaging",
          shot + ": " + int_to_str((int)staging.choreo.GetSlots().size()) + " actors, " +
              int_to_str((int)staging.clips.size()) + " clips");
    }
  }
  return staging.choreo.IsLoaded() ? &staging : nullptr;
}

std::string Match::FindPrematchShotName(const std::string& shot) const {
  if (shot.empty()) return "";
  auto found = prematchShots.find(shot);
  if (found != prematchShots.end()) return found->first;
  for (const auto& entry : prematchShots)
    if (entry.first.find(shot) != std::string::npos) return entry.first;
  return "";
}

const CamTrack* Match::FindPrematchShot(const std::string& shot) const {
  if (shot.empty()) return nullptr;
  auto found = prematchShots.find(shot);
  if (found != prematchShots.end()) return &found->second;
  // Fall back to a substring of the pack name, so a beat can ask for
  // "ent_009" and take whichever of that family this stadium installed.
  for (const auto& entry : prematchShots)
    if (entry.first.find(shot) != std::string::npos) return &entry.second;
  return nullptr;
}

PrematchTimeline::Timeline Match::LoadPrematchTimeline() const {
  // Explicit file wins; otherwise the competition's own entrance family names
  // one ("entrance_id" 020 -> 020.timeline), then the shipped default. With
  // none of those installed the built-in sequence is used, so a bare checkout
  // still gets the full pre-match rather than a two-second cut to kickoff.
  const std::string root =
      GetConfiguration()->Get("presentation_dir", "media/presentation");
  std::vector<std::string> candidates;
  const std::string explicitFile = GetConfiguration()->Get("presentation_timeline", "");
  if (!explicitFile.empty()) candidates.push_back(explicitFile);
  const std::string entranceID = GetConfiguration()->Get("entrance_id", "");
  if (!entranceID.empty() && entranceID != "none")
    candidates.push_back(root + "/" + entranceID + ".timeline");
  candidates.push_back(root + "/default.timeline");

  for (const std::string& path : candidates) {
    std::ifstream file(path);
    PrematchTimeline::Timeline timeline;
    if (file.good() && PrematchTimeline::Parse(file, timeline)) {
      Log(e_Notice, "Match", "LoadPrematchTimeline",
          path + ": " + int_to_str((int)timeline.beats.size()) + " beat(s), " +
              int_to_str((int)timeline.TotalSeconds()) + "s");
      return timeline;
    }
  }

  Log(e_Notice, "Match", "LoadPrematchTimeline", "no timeline file; using the built-in sequence");
  return PrematchTimeline::Default();
}

bool Match::GetEntranceCastBounds(Vector3& centre, Vector3& extent) const {
  // Where the two squads currently stand. The imported camerawork is authored
  // per stadium and films the wrong place in any other, so the walkout and
  // lineup shots frame the players themselves - whether they were put on
  // their marks by the choreography or by the scripted walk.
  // While a staging is posing them, the choreography's own positions are the
  // only ones that mean anything: SetChoreoPose moves the model, not the
  // player, so his GetPosition is still his kickoff mark out on the pitch.
  if (choreoBoundsValid) {
    centre = choreoBoundsCentre;
    extent = choreoBoundsExtent;
    return true;
  }

  bool any = false;
  float minX = 0, maxX = 0, minY = 0, maxY = 0;
  for (int teamID = 0; teamID < 2; teamID++) {
    if (!teams[teamID]) continue;
    std::vector<Player*> squad;
    teams[teamID]->GetActivePlayers(squad);
    for (Player* player : squad) {
      if (!player) continue;
      const Vector3 position = player->GetPosition();
      if (!any) {
        minX = maxX = position.coords[0];
        minY = maxY = position.coords[1];
        any = true;
      } else {
        minX = std::min(minX, position.coords[0]);
        maxX = std::max(maxX, position.coords[0]);
        minY = std::min(minY, position.coords[1]);
        maxY = std::max(maxY, position.coords[1]);
      }
    }
  }
  if (!any) return false;
  centre = Vector3((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, 0.0f);
  extent = Vector3(maxX - minX, maxY - minY, 0.0f);
  return true;
}

void Match::SuppressHudForReplay(bool suppressed) {
  hudSuppressedForReplay = suppressed;
  // Nothing of the in-match chrome belongs over a replay; the replay draws its
  // own overlay. Re-applied through the same door the presentation uses, so a
  // replay ending during the walkout does not put the HUD back early.
  ShowMatchHud(!suppressed && !entranceActive);
  Log(e_Notice, "Match", "SuppressHudForReplay", suppressed ? "replay: match HUD hidden"
                                                           : "replay over: match HUD back");
}

void Match::ShowMatchHud(bool visible) {
  // The persistent in-match chrome, hidden for the pre-match presentation and
  // for a replay, and brought back after.
  if (visible && hudSuppressedForReplay) return;
  if (scoreboard) {
    if (visible)
      scoreboard->Show();
    else
      scoreboard->Hide();
  }
  if (radar) {
    if (visible)
      radar->Show();
    else
      radar->Hide();
  }
  for (int side = 0; side < 2; side++) {
    if (!playerHUD[side]) continue;
    if (visible)
      playerHUD[side]->Show();
    else
      playerHUD[side]->Hide();
  }
}

void Match::RememberPrematchCamera() {
  // Camera::Hold shows whatever the beat before it left on screen, so every
  // other beat records where it put the camera.
  heldCameraPosition = cameraNodePosition;
  heldCameraNodeOrientation = cameraNodeOrientation;
  heldCameraOrientation = cameraOrientation;
  heldCameraFOV = cameraFOV;
  heldCameraNear = cameraNearCap;
  heldCameraFar = cameraFarCap;
  heldCameraValid = true;
}

float Match::GetEntranceElapsedSeconds() const {
  if (!entranceActive) return entranceSeconds;
  if (entranceRealStart_ms == 0) return 0.0f;  // not ticking yet
  const unsigned long now = EnvironmentManager::GetInstance().GetTime_ms();
  if (now <= entranceRealStart_ms) return 0.0f;
  return (now - entranceRealStart_ms) * 0.001f;
}

PrematchTimeline::State Match::GetPrematchState() const {
  if (!entranceActive) {
    PrematchTimeline::State done;
    done.finished = true;
    return done;
  }
  return PrematchTimeline::At(prematchTimeline, GetEntranceElapsedSeconds());
}

void Match::GetEntranceSlot(const Player* player, Vector3& position, Vector3& lookAt) const {
  // Both teams line up along the halfway line, one row each, facing the main
  // stand — PES's shape for the walk-out and the anthem. The rows sit a couple
  // of metres either side of the line so the two teams read as two teams, and
  // the keeper takes the outside slot as he does in the real thing.
  const int teamID = player->GetTeamID() == 1 ? 1 : 0;
  const float rowY = teamID == 0 ? -2.2f : 2.2f;

  int slot = 0;
  std::vector<Player*> squad;
  teams[teamID]->GetActivePlayers(squad);
  for (unsigned int i = 0; i < squad.size(); i++) {
    if (squad[i] == player) {
      slot = (int)i;
      break;
    }
  }

  const float spacing = 1.9f;
  const float rowWidth = spacing * std::max(1, (int)squad.size() - 1);
  position = Vector3(-rowWidth * 0.5f + spacing * slot, rowY, 0.0f);
  // Facing the main stand, i.e. down the negative-y touchline.
  lookAt = position + Vector3(0.0f, -30.0f, 0.0f);
}

void Match::BuildEntranceCast() {
  // PES actor slots: 0-10 the home XI with the keeper on 0, 11-21 the away
  // XI likewise, 22+ the officials (not cast yet). Players whose slot the
  // .chor does not stage keep the scripted walk.
  entranceCast.clear();
  const EntranceChoreo& choreo = activeStaging ? activeStaging->choreo : entranceChoreo;
  const std::map<std::string, std::shared_ptr<Animation>>& clips =
      activeStaging ? activeStaging->clips : entranceClips;
  if (!choreo.IsLoaded()) return;

  for (int teamID = 0; teamID < 2; teamID++) {
    std::vector<Player*> squad;
    teams[teamID]->GetActivePlayers(squad);
    const int base = teamID * 11;
    std::vector<Player*> ordered;
    Player* keeper = nullptr;
    for (auto player : squad) {
      if (!keeper && player->GetFormationEntry().role == e_PlayerRole_GK)
        keeper = player;
      else
        ordered.push_back(player);
    }
    if (keeper) ordered.insert(ordered.begin(), keeper);
    for (unsigned int i = 0; i < ordered.size() && i < 11; i++) {
      const ChoreoSlot* slot = choreo.GetSlot(base + (int)i);
      if (!slot) continue;
      auto clip = clips.find(slot->animFile);
      if (clip == clips.end()) continue;
      entranceCast.push_back({ordered[i], slot, clip->second.get()});
    }
  }
}

// The rate PES's camera tracks are authored at (the canm header says 30 in every
// observed clip).
static const float kPrematchShotFrameRate = 30.0f;

// How close a body may come to the lens before the shot is dollied back.
static const float kPrematchLensClearance = 2.2f;

Vector3 Match::ComputeStagingOffset() const {
  // PES authors a walk-on in its own stadium's coordinates, with the cast
  // starting at its tunnel mouth: ent_009_st000 walks from y -48 to y -38, both
  // of them past this pitch's touchline at 36. The motion is worth keeping and
  // only its placement is wrong, so a staging that starts off the field is moved
  // to start just outside the line it comes in over (staginganchor.hpp), and its
  // own ten metres then carry the cast onto the pitch.
  if (entranceCast.empty()) return Vector3(0, 0, 0);
  const EntranceChoreo& choreo = activeStaging ? activeStaging->choreo : entranceChoreo;
  // Anchored on whoever starts nearest the pitch - the head of the column - so
  // the whole group starts outside the line rather than straddling it.
  Vector3 innermost(0, 0, 0);
  bool any = false;
  for (const auto& cast : entranceCast) {
    Vector3 position;
    radian yaw = 0;
    int animFrame = 0;
    // The first frame of that actor's path: a walk-on is placed by where it
    // comes in from, not by where it ends up.
    choreo.Sample(*cast.slot, 0.0f, position, yaw, animFrame);
    if (!any || position.GetLength() < innermost.GetLength()) {
      innermost = position;
      any = true;
    }
  }
  if (!any) return Vector3(0, 0, 0);
  return StagingAnchor::WalkOnOffset(innermost, pitchHalfW, pitchHalfH);
}

void Match::UpdateEntranceChoreo() {
  // "entrance_choreography" "false" falls back to the scripted walk.
  static const bool choreographyEnabled =
      GetConfiguration()->GetBool("entrance_choreography", true);
  if (!choreographyEnabled || !IsInEntrance()) return;

  // Each beat brings its own staging: the tunnel pack walks the squads out,
  // the anthem pack stands them on the line, the circle pack poses them for
  // the team photo. Swapping it restages the cast and restarts its clock, so
  // every pack plays from its own first frame when its beat begins.
  const PrematchTimeline::State beat = GetPrematchState();
  if (beat.beatIndex != stagedBeatIndex) {
    stagedBeatIndex = beat.beatIndex;
    const std::string shot =
        (beat.beatIndex >= 0 && beat.beatIndex < (int)prematchTimeline.beats.size())
            ? prematchTimeline.beats[beat.beatIndex].shot
            : std::string();
    // A beat that names a pack will play it, whichever branch below takes it.
    if (!shot.empty()) stagingHasRun = true;

    std::string wanted = shot;
    // A beat names a family; which of that family's stadium variants plays is
    // decided by the camerawork, and the players it films must be that variant's
    // own - PES authors them as a pair (prematchshotpair.hpp). Resolved apart,
    // the staging came from stadium 000 and the camera from stadium 002, so the
    // lens sat where another ground's tunnel mouth is and spent the entire
    // walk-on inside a player's chest.
    stagedCameraKey.clear();
    if (!shot.empty()) {
      for (const auto& camera : prematchShots) {
        if (camera.first.find(shot) == std::string::npos) continue;
        const std::string paired = PrematchShotPair::StagingForCamera(camera.first);
        if (paired.empty() || prematchStagings.find(paired) == prematchStagings.end()) continue;
        wanted = paired;
        stagedCameraKey = camera.first;
        break;
      }
    }
    if (wanted.empty()) {
      // A beat with no staging of its own, *before* anything has been staged:
      // borrow the first pack the sequence will use and hold its opening frame,
      // so the establishing shots look out over a pitch the squads have not
      // walked onto yet. Once a pack has run, this must not happen again - it put
      // the whole column back at the tunnel mouth, frozen, in the middle of the
      // lineup panels.
      if (!stagingHasRun) {
        for (const auto& other : prematchTimeline.beats)
          if (!other.shot.empty()) {
            if (!activeStaging) wanted = other.shot;
            break;
          }
      }
    }
    PrematchStaging* staging = AcquirePrematchStaging(wanted);
    const bool holdOpeningFrame = shot.empty() && !wanted.empty();

    // A beat that names no staging, once the sequence is under way, lets the
    // cast go: the players walk to their own kickoff marks from wherever the
    // last pack left them. Holding the last pose instead is what made the first
    // whistle teleport everybody - they were still standing in the team picture
    // when the match took them back.
    if (!staging && activeStaging && shot.empty()) {
      activeStaging = nullptr;
      entranceCast.clear();
      choreoBoundsValid = false;
      return;
    }
    if (staging) {
      activeStaging = staging;
      // Holding the opening frame: park the clock on it rather than letting
      // the pack play out under a shot that is not looking at it.
      stagingStartSeconds =
          holdOpeningFrame ? GetEntranceElapsedSeconds() * 2.0f : GetEntranceElapsedSeconds();
      if (holdOpeningFrame) stagingStartSeconds = GetEntranceElapsedSeconds();
      stagingHoldsOpeningFrame = holdOpeningFrame;
      BuildEntranceCast();
      stagingOffset = ComputeStagingOffset();
    } else if (!activeStaging && !stagingHasRun && entranceChoreo.IsLoaded()) {
      // The competition's own pack, for a timeline that stages nothing at all.
      // Not after a pack has run: that hauled the released cast back to the
      // tunnel mouth between beats.
      BuildEntranceCast();
    }
  }

  if (entranceCast.empty()) {
    choreoBoundsValid = false;
    return;
  }

  // The establishing beat looks out over a pitch the squads have not walked onto
  // yet, so nobody is on it. Holding the borrowed pack's opening frame instead put
  // both elevens in a ring on the centre circle through the whole stadium card.
  if (stagingHoldsOpeningFrame) {
    for (auto& cast : entranceCast)
      if (cast.player->CastHumanoid()) cast.player->CastHumanoid()->Hide();
    HideUnstagedPlayers();
    choreoBoundsValid = false;
    return;
  }

  const EntranceChoreo& choreo = activeStaging ? activeStaging->choreo : entranceChoreo;
  float minX = 0, maxX = 0, minY = 0, maxY = 0;
  bool anyPosed = false;
  const float elapsedFrame =
      stagingHoldsOpeningFrame ? 0.0f
                               : (GetEntranceElapsedSeconds() - stagingStartSeconds) * 100.0f;
  for (auto& cast : entranceCast) {
    Vector3 position;
    radian yaw = 0;
    int animFrame = 0;
    choreo.Sample(*cast.slot, elapsedFrame, position, yaw, animFrame);
    position += stagingOffset;
    // Sample wraps the frame against the SLOT's cycle - the length of that
    // actor's path through the staging - but the clip it plays is its own,
    // much shorter loop. Indexing a 750-frame walk cycle at frame 1500 read
    // off the end of the animation and dropped the cast flat onto the pitch.
    const int clipFrames = cast.clip ? cast.clip->GetFrameCount() : 0;
    if (clipFrames > 0) animFrame %= clipFrames;
    cast.player->CastHumanoid()->SetChoreoPose(cast.clip, animFrame, position, yaw);

    // Remembered for the camera: this is where the cast actually is.
    if (!anyPosed) {
      minX = maxX = position.coords[0];
      minY = maxY = position.coords[1];
      anyPosed = true;
    } else {
      minX = std::min(minX, position.coords[0]);
      maxX = std::max(maxX, position.coords[0]);
      minY = std::min(minY, position.coords[1]);
      maxY = std::max(maxY, position.coords[1]);
    }
  }

  // Everybody the staging does not stage is off camera, not standing about on the
  // pitch. PES walks its cast out of the tunnel to an empty field; a .chor stages
  // the slots its own pack authored - never all twenty-two - and the rest were
  // left at their kickoff marks, in shot, for the whole presentation.
  HideUnstagedPlayers();

  choreoBoundsValid = anyPosed;
  if (anyPosed) {
    choreoBoundsCentre = Vector3((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, 0.0f);
    choreoBoundsExtent = Vector3(maxX - minX, maxY - minY, 0.0f);
  }
}

void Match::HideUnstagedPlayers() {
  // Hide() parks the model far away, and a player's own Process puts it back, so
  // this has to run after the cast is posed and on every frame of the entrance.
  std::set<PlayerBase*> staged;
  for (const auto& cast : entranceCast) staged.insert(cast.player);
  for (int teamID = 0; teamID < 2; teamID++) {
    if (!teams[teamID]) continue;
    std::vector<Player*> squad;
    teams[teamID]->GetActivePlayers(squad);
    for (Player* player : squad) {
      if (!player || staged.count(player)) continue;
      if (player->CastHumanoid()) player->CastHumanoid()->Hide();
    }
  }
}

ModelViewerSettings Match::LoadModelViewerSettings() const {
  ModelViewerSettings settings;
  settings.seconds = GetConfiguration()->GetReal("debug_model_viewer_seconds", 0.0f);
  settings.playerFilter = GetConfiguration()->Get("debug_model_viewer_player", "");
  settings.animFilter = GetConfiguration()->Get("debug_model_viewer_anim", "");
  settings.clipSeconds =
      GetConfiguration()->GetReal("debug_model_viewer_clip_seconds", 4.0f);
  settings.radius = GetConfiguration()->GetReal("debug_model_viewer_radius", 3.4f);
  return settings;
}

Player* Match::PickModelViewerSubject(const std::string& filter) {
  Player* fallback = nullptr;
  for (int teamID = 0; teamID < 2; teamID++) {
    std::vector<Player*> squad;
    teams[teamID]->GetActivePlayers(squad);
    for (Player* candidate : squad) {
      if (!fallback) fallback = candidate;
      if (filter.empty()) return fallback;
      const std::string modelDir =
          GetPlayerModelDir(candidate->GetPlayerData()->GetDatabaseID());
      // "default" benches a player on the shared default body
      if (filter == "default" && modelDir.empty()) return candidate;
      if (!modelDir.empty() && modelDir.find(filter) != std::string::npos)
        return candidate;
    }
  }
  return fallback;
}

void Match::UpdateModelViewerPlayback() {
  const ModelViewerSettings settings = LoadModelViewerSettings();
  if (!ModelViewerIsRunning(settings, actualTime_ms)) return;
  // only while the match is held (see the constructor): posing a player who is
  // taking part fights his own animation machinery
  if (!IsInEntrance()) return;
  // picked here, on the game thread: the camera runs on its own and must not
  // hand a humanoid across to be posed
  Player* subject = PickModelViewerSubject(settings.playerFilter);
  if (!subject) return;
  // posing a player re-derives his geometry offsets, which read the ball and
  // the mental images - neither exists in the opening moments of a match
  if (actualTime_ms < 2000 || !ball || mentalImages.empty()) return;

  std::vector<Animation*> playlist;
  for (Animation* anim : anims->GetAnimations())
    if (ModelViewerAccepts(settings.animFilter, anim->GetName()))
      playlist.push_back(anim);

  const int slot = ModelViewerClipIndex(settings, actualTime_ms, (int)playlist.size());
  if (slot < 0) return;
  Animation* clip = playlist[slot];
  if (slot != modelViewerAnimIndex) {
    modelViewerAnimIndex = slot;
    Log(e_Notice, "Match", "ModelViewer",
        "clip " + int_to_str(slot + 1) + "/" + int_to_str((int)playlist.size()) + ": " +
            clip->GetName());
  }
  subject->CastHumanoid()->SetChoreoPose(
      clip, ModelViewerClipFrame(settings, actualTime_ms, clip->GetFrameCount()),
      subject->GetPosition(), 0.0f);
}

void Match::LoadCutsceneChoreo(const std::string& category, const std::string& dir) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
    if (entry.path().extension() != ".chor") continue;
    std::ifstream file(entry.path());
    EntranceChoreo choreo;
    if (!file.good() || !choreo.Load(file)) continue;
    // the clips sit in an anims/ directory next to the choreography
    const std::string base = entry.path().parent_path().string();
    bool complete = true;
    for (const auto& slot : choreo.GetSlots()) {
      if (cutsceneClips.count(slot.animFile)) continue;
      const std::string clipPath = base + "/" + slot.animFile;
      if (!std::filesystem::exists(clipPath)) {
        complete = false;
        continue;
      }
      auto clip = std::make_shared<Animation>();
      clip->Load(clipPath);
      if (clip->GetFrameCount() >= 2) cutsceneClips[slot.animFile] = clip;
    }
    if (!complete) continue;
    cutsceneChoreoPools[category].push_back(choreo);
    const std::string parent = entry.path().parent_path().filename().string();
    if (parent != category)
      cutsceneChoreoPools[category + "/" + parent].push_back(choreo);
  }
}

void Match::StartCutsceneChoreo(const std::string& category) {
  activeCutsceneChoreo = nullptr;
  cutsceneCast.clear();
  cutsceneOfficialCast.clear();
  auto pool = cutsceneChoreoPools.find(category);
  if (pool == cutsceneChoreoPools.end() || pool->second.empty()) {
    const size_t slash = category.find('/');
    if (slash == std::string::npos) return;
    pool = cutsceneChoreoPools.find(category.substr(0, slash));
    if (pool == cutsceneChoreoPools.end() || pool->second.empty()) return;
  }
  activeCutsceneChoreo =
      &pool->second[(actualTime_ms / 10) % pool->second.size()];
  // PES stages a foul about the incident: its actors' baked root tracks sit
  // within a few metres of the origin. Used as world positions they planted the
  // offender, the man he fouled and the referee on the centre spot, however far
  // away the challenge actually was.
  activeStagingAnchoring =
      CutsceneViewer::ClassifyAnchoring(CutsceneViewer::MeasureChoreography(*activeCutsceneChoreo));

  // Cast by role first - the incident's own people take their marks - then
  // fill the remaining marks with whoever stands nearest them.
  std::vector<Player*> available;
  for (int teamID = 0; teamID < 2; teamID++) {
    std::vector<Player*> squad;
    teams[teamID]->GetActivePlayers(squad);
    for (Player* player : squad) available.push_back(player);
  }
  auto take = [&available](Player* player) {
    if (!player) return false;
    auto at = std::find(available.begin(), available.end(), player);
    if (at == available.end()) return false;
    available.erase(at);
    return true;
  };

  for (const auto& slot : activeCutsceneChoreo->GetSlots()) {
    auto clip = cutsceneClips.find(slot.animFile);
    if (clip == cutsceneClips.end()) continue;

    Player* cast = nullptr;
    if (slot.role == e_ChoreoRole_Official) {
      // the referee plays himself; he is not one of the 22
      if (officials && officials->GetReferee()) {
        cutsceneOfficialCast.push_back({officials->GetReferee(), &slot, clip->second.get()});
        continue;
      }
    } else if (slot.role == e_ChoreoRole_Primary && take(cutscenePrimary)) {
      cast = cutscenePrimary;
    } else if (slot.role == e_ChoreoRole_Opponent && take(cutsceneOpponent)) {
      cast = cutsceneOpponent;
    }

    if (!cast) {
      if (available.empty()) continue;
      Vector3 mark;
      radian yaw = 0;
      int animFrame = 0;
      activeCutsceneChoreo->Sample(slot, 0.0f, mark, yaw, animFrame);
      auto nearest = available.begin();
      float bestDistance = (*nearest)->GetPosition().GetDistance(mark);
      for (auto iter = available.begin(); iter != available.end(); iter++) {
        const float distance = (*iter)->GetPosition().GetDistance(mark);
        if (distance < bestDistance) {
          bestDistance = distance;
          nearest = iter;
        }
      }
      cast = *nearest;
      available.erase(nearest);
    }
    cutsceneCast.push_back({cast, &slot, clip->second.get()});
  }
}

void Match::UpdateCutsceneChoreo() {
  if (!activeCutsceneChoreo || !activeCutscene) {
    if (!activeCutscene) {
      activeCutsceneChoreo = nullptr;
      cutsceneCast.clear();
      cutsceneOfficialCast.clear();
      cutscenePrimary = nullptr;
      cutsceneOpponent = nullptr;
    }
    return;
  }
  const unsigned long now = EnvironmentManager::GetInstance().GetTime_ms();
  const float elapsedFrame = (now - cutsceneStart_ms) * 0.1f;  // 10 ms frames
  // Staging authored about the incident is played out at the incident. Only the
  // positions move: the actors' facings are relative to each other within the
  // authored frame, and the camera is placed in that same frame, so rotating
  // one without the other would only break the shot.
  const Vector3 stagingOffset =
      activeStagingAnchoring == CutsceneViewer::Anchoring::IncidentLocal
          ? CutsceneAnchorPosition().Get2D()
          : Vector3(0, 0, 0);
  for (auto& cast : cutsceneCast) {
    Vector3 position;
    radian yaw = 0;
    int animFrame = 0;
    activeCutsceneChoreo->Sample(*cast.slot, elapsedFrame, position, yaw, animFrame);
    // SetChoreoPose wraps the clip frame; the path is already held at its end.
    cast.player->CastHumanoid()->SetChoreoPose(cast.clip, animFrame, position + stagingOffset, yaw);
  }
  for (auto& cast : cutsceneOfficialCast) {
    Vector3 position;
    radian yaw = 0;
    int animFrame = 0;
    activeCutsceneChoreo->Sample(*cast.slot, elapsedFrame, position, yaw, animFrame);
    cast.official->CastHumanoid()->SetChoreoPose(cast.clip, animFrame, position + stagingOffset,
                                                 yaw);
  }
}

void Match::InvalidateCachedMentalImages() {
  for (int teamID = 0; teamID < 2; teamID++) {
    if (!teams[teamID]) continue;
    std::vector<Player*> squad;
    teams[teamID]->GetAllPlayers(squad);
    for (Player* player : squad)
      if (player && player->CastHumanoid()) player->CastHumanoid()->InvalidateMentalImage();
  }
  // the referee and his assistants run humanoids of their own
  if (officials) {
    PlayerOfficial* crew[3] = {officials->GetReferee(), officials->GetLinesmanNorth(),
                               officials->GetLinesmanSouth()};
    for (PlayerOfficial* official : crew)
      if (official && official->CastHumanoid())
        official->CastHumanoid()->InvalidateMentalImage();
  }
}

void Match::ResetSituation(const Vector3& focusPos) {
  camPos.clear();
  SetBallRetainer(nullptr);
  SetGoalScored(false);
  // The situation is over, so any scripted replay belonging to it is spent.
  // Both sequences schedule their restart behind the replay, so by the time
  // this runs the replay has already been shown.
  replayStartOffset_ms = 0;
  foulReplayDue_ms = 0;
  replayCamera = kReplayCameraBehindGoal;
  // Players cache the mental image they last read; the vector below is about
  // to be emptied, so drop those pointers before the memory goes. Without
  // this the graphics thread's next put reads a freed image and dies inside
  // Match::GetBall().
  InvalidateCachedMentalImages();
  mentalImages.clear();
  goalScored = false;
  ballIsInGoal = false;
  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchTeamIDs[i] = -1;
  }
  lastTouchTeamID = -1;
  lastGoalScorer = 0;
  bestPossessionTeamID = -1;

  mentalImages.clear();
  possessionSideHistory->Clear();

  lastBodyBallCollisionTime_ms = 0;

  ball->ResetSituation(focusPos);
  teams[0]->ResetSituation(focusPos);
  teams[1]->ResetSituation(focusPos);

  // reset temporalsmoother vars
  // todo: not sure if we may access buf_ vars here
  /*
    buf_cameraOrientation.Clear();
    buf_cameraNodeOrientation.Clear();
    buf_cameraNodePosition.Clear();
    buf_cameraFOV.Clear();
  */
}

void Match::StartCutscene(const std::string& category, float capSeconds) {
  if (activeCutscene) return;
  auto pool = cutscenePools.find(category);
  // "foul/card_yellow" asks for the shots of a booking; if none were imported,
  // fall back to the category's whole pool rather than showing nothing.
  if (pool == cutscenePools.end() || pool->second.empty()) {
    const size_t slash = category.find('/');
    if (slash != std::string::npos) pool = cutscenePools.find(category.substr(0, slash));
  }
  const bool haveCamera = pool != cutscenePools.end() && !pool->second.empty();
  cutsceneStart_ms = EnvironmentManager::GetInstance().GetTime_ms();
  float seconds = capSeconds;
  activeCutsceneAnchoring = CutsceneViewer::Anchoring::StadiumWorld;
  cutsceneShotTaken = false;
  if (haveCamera) {
    const CamTrack& track =
        pool->second[(actualTime_ms / 10 + GetScore(0) + GetScore(1)) %
                     pool->second.size()];
    activeCutscene = &track;
    seconds = std::min(capSeconds, track.GetDurationSeconds());
    // PES's foul shots are a static camera five metres from the origin: they
    // are authored about the incident, not the stadium. Used as world positions
    // they filmed every foul from beside the centre spot, whatever corner the
    // challenge happened in. Measure the track and place it accordingly.
    activeCutsceneAnchoring =
        CutsceneViewer::ClassifyAnchoring(CutsceneViewer::MeasureTrack(track));
  }
  StartCutsceneChoreo(category);
  // Some incidents are staged but not filmed: PES ships no camera pack at all
  // for an offside, because the assistant's flag and the disallowed-goal
  // reactions are meant to play out on the live broadcast camera. So a
  // choreography with no camerawork is a cutscene too - it just does not take
  // the camera. With nothing at all to show, there is no cutscene.
  if (!haveCamera && !activeCutsceneChoreo) return;
  cutsceneEnd_ms = cutsceneStart_ms + (unsigned long)(seconds * 1000.0f);
  Log(e_Notice, "Match", "StartCutscene",
      "category " + category + (haveCamera ? "" : " (choreography only)") + ", clock " + int_to_str(matchTime_ms / 60000) + ":" +
          int_to_str((matchTime_ms / 1000) % 60) + ", " + int_to_str((int)(seconds * 10)) +
          " ds");
}

void Match::SetMatchPhase(e_MatchPhase newMatchPhase) {
  matchPhase = newMatchPhase;
  if (matchPhase == e_MatchPhase_1stHalf)
    matchTime_ms = 0;
  else if (matchPhase == e_MatchPhase_2ndHalf) {
    matchTime_ms = 2700000;
    StartCutscene("timeup", 6.0f);
  } else if (matchPhase == e_MatchPhase_1stExtraTime) {
    matchTime_ms = 5400000;
    StartCutscene("timeup", 6.0f);
  } else if (matchPhase == e_MatchPhase_2ndExtraTime)
    matchTime_ms = 6300000;
  else if (matchPhase == e_MatchPhase_Penalties) {
    matchTime_ms = 7200000;
    StartCutscene("pk", 7.0f);
  }

  matchTimeExact_ms = static_cast<double>(matchTime_ms);

  // A fresh period starts with a clean added-time slate.
  stoppage = MatchProgression::Stoppage();

  if (matchPhase == e_MatchPhase_2ndHalf) {
    teams[0]->RelaxFatigue(0.05f);
    teams[1]->RelaxFatigue(0.05f);
  }
}

void Match::SetScore(int teamID, int goals) {
  matchData->SetGoalCount(teamID, goals);
  scoreboard->SetGoalCount(teamID, goals);
}

signed int Match::GetBestPossessionTeamID() {
  return bestPossessionTeamID;
}

void Match::GameOver() {
  gameOver = true;
  // The pre-match screen can pin the post-match presentation to one family;
  // StartCutscene falls back to the whole pool when that family is not installed.
  {
    const std::string family = GetConfiguration()->Get("result_cutscene_id", "");
    StartCutscene(family.empty() ? "result" : "result/" + family, 8.0f);
  }
}

void Match::AddExcitementBoost(float amount, int duration_ms) {
  if (amount <= 0.0f || duration_ms <= 0)
    return;
  amount = clamp(amount, 0.0f, 1.0f);
  if (amount > excitementEventBoost)
    excitementEventBoost = amount;
  if (duration_ms > excitementEventTimer_ms)
    excitementEventTimer_ms = duration_ms;
}

bool Match::IsSubstitutionWindow() const {
  // Not merely "play is stopped": before kick-off play is stopped too, and the
  // pre-match presentation is no moment to be making substitutions in.
  return Substitutions::IsSubstitutionWindow(IsInPlay(), matchPhase != e_MatchPhase_PreMatch,
                                             IsInEntrance());
}

void Match::RequestFoulReplay(unsigned long foulTime_ms, int foulType) {
  // The referee has just whistled and started its cutscene. The replay waits
  // for that to finish - showing the challenge rather than the aftermath - and
  // FoulSequence gives the referee a restart delay that leaves room for it.
  foulReplayFoulTime_ms = foulTime_ms;
  foulReplayFoulType = foulType;
  foulReplayDue_ms = FoulSequence::ReplayFiresAt_ms(foulTime_ms, foulType);
}

void Match::ProcessFoulReplay() {
  if (foulReplayDue_ms == 0 || actualTime_ms < foulReplayDue_ms)
    return;
  const unsigned long elapsed = actualTime_ms - foulReplayFoulTime_ms;
  replayStartOffset_ms = FoulSequence::ReplayStartOffset_ms(elapsed);
  replayCamera = kReplayCameraCloseUp;
  Log(e_Notice, "Match", "ProcessFoulReplay",
      "foul replay (type " + int_to_str(foulReplayFoulType) + "): cutscene ran " +
          int_to_str((int)elapsed) + " ms, replay reaches " +
          int_to_str((int)replayStartOffset_ms) + " ms back");
  foulReplayDue_ms = 0;
  pause = true;
  sig_OnExtendedReplayMoment(this);
}

Substitutions::e_Result Match::RequestSubstitution(int teamID, Player* playerOut,
                                                   Player* playerIn) {
  Team* team = GetTeam(teamID);
  const Substitutions::SquadView squad = team->DescribeSwap(playerOut, playerIn);
  const Substitutions::e_Result result =
      Substitutions::Validate(substitutionState, teamID, squad, IsSubstitutionWindow());
  if (result != Substitutions::e_Result_Accepted)
    return result;

  // rebuilding a humanoid races the graphics put phase, so the swap is
  // queued and executed under the put-buffer mutex (ExecutePendingSubstitutions)
  std::lock_guard<std::mutex> lock(pendingSubstitutionsMutex);
  pendingSubstitutions.push_back({teamID, playerOut, playerIn});
  return Substitutions::e_Result_Accepted;
}

bool Match::HasPendingSubstitutions() {
  std::lock_guard<std::mutex> lock(pendingSubstitutionsMutex);
  return !pendingSubstitutions.empty();
}

void Match::ExecutePendingSubstitutions() {
  std::vector<PendingSubstitution> pending;
  {
    std::lock_guard<std::mutex> lock(pendingSubstitutionsMutex);
    pending.swap(pendingSubstitutions);
  }
  for (const auto& sub : pending) {
    Team* team = GetTeam(sub.teamID);
    // conditions may have changed since the request; re-validate quietly
    const Substitutions::SquadView squad =
        team->DescribeSwap(sub.playerOut, sub.playerIn);
    if (Substitutions::Validate(substitutionState, sub.teamID, squad,
                                IsSubstitutionWindow()) != Substitutions::e_Result_Accepted)
      continue;
    if (!team->Substitute(sub.playerOut, sub.playerIn))
      continue;
    Substitutions::Commit(substitutionState, sub.teamID);
    AddLostTime(MatchProgression::e_Stoppage_Substitution);
    // A substitution announces itself once, from Team::Substitute ("<TEAM>: X
    // on for Y") - there used to be a second, team-tagged banner here, and both
    // fired for every change. The tunnel cutscene is the exception rather than
    // the rule, as PES only cuts away now and then
    // ("substitution_cutscene_chance", 0..1).
    std::string out = sub.playerOut ? sub.playerOut->GetPlayerData()->GetLastName() : "";
    std::string in = sub.playerIn ? sub.playerIn->GetPlayerData()->GetLastName() : "";
    Log(e_Notice, "Match", "ExecutePendingSubstitutions",
        "substitution, team " + int_to_str(sub.teamID) + ": in " + in + ", out " + out);
    if (random(0.0f, 1.0f) <
        GetConfiguration()->GetReal("substitution_cutscene_chance", 0.35f))
      StartCutscene("change", 5.0f);
  }
}

void Match::ProcessAutoSubstitutions() {
  // Only at stoppages during normal play, and no more than one decision per
  // second. A shootout is not a stoppage to make substitutions in, and neither
  // is anything before kick-off.
  if (!IsSubstitutionWindow() || actualTime_ms % 1000 != 0)
    return;
  if (matchPhase == e_MatchPhase_Penalties || gameOver)
    return;

  for (int teamID = 0; teamID < 2; teamID++) {
    // A human manager makes his own calls - and in coach mode the CPU manager
    // makes none at all, for either team.
    if (!AIManagerRunsTeam(teamID))
      continue;
    if (Substitutions::GetRemaining(substitutionState, teamID) <= 0)
      continue;

    Team* team = GetTeam(teamID);

    std::vector<Player*> squadPlayers;
    team->GetAllPlayers(squadPlayers);

    std::vector<AIManager::SubstitutionCandidate> squad;
    std::vector<Player*> considered;
    squad.reserve(squadPlayers.size());
    Player* goalie = team->GetGoalie();
    for (Player* player : squadPlayers) {
      if (!player->IsActive() && team->HasBeenSubstituted(player->GetID()))
        continue;  // already used up
      // Keepers only come off if they are actually injured.
      if (player == goalie && player->GetInjuryLevel() < AIManager::substitutionInjuryLevel)
        continue;

      AIManager::SubstitutionCandidate candidate;
      candidate.fatigueFactorInv = player->GetFatigueFactorInv();
      candidate.injuryLevel = player->GetInjuryLevel();
      candidate.isOnPitch = player->IsActive();
      candidate.averageStat = player->GetAverageStat();
      squad.push_back(candidate);
      considered.push_back(player);
    }

    AIManager::MatchSituation situation;
    situation.goalDifference =
        matchData->GetGoalCount(teamID) - matchData->GetGoalCount(abs(teamID - 1));
    situation.matchTime_ms = matchTime_ms;
    situation.possessionShare =
        CrowdMood::GetHomeSupportFactor(matchData->GetPossessionFactor_60seconds(), teamID);

    const AIManager::SubstitutionPlan plan = AIManager::PlanSubstitution(
        squad, situation, Substitutions::GetRemaining(substitutionState, teamID));
    if (!plan.wanted)
      continue;

    RequestSubstitution(teamID, considered.at(plan.playerOutIndex),
                        considered.at(plan.playerInIndex));
  }
}

int Match::GetCoachedTeamID(bool preferSecondTeam) const {
  // The team the touchline hotkeys address: the human's own side, or in coach
  // mode the coached team (shift picks the other one in a manager duel).
  if (preferSecondTeam && CoachMode::CanEditTactics(coachSetup, 1))
    return 1;
  for (int teamID = 0; teamID < 2; teamID++) {
    if (CoachMode::CanEditTactics(coachSetup, teamID))
      return teamID;
  }
  return 0;
}

IHIDevice* Match::GetTouchlineDevice(int teamID) {
  // A human on the sticks uses his own pad for the touchline too.
  Team* team = GetTeam(teamID);
  for (unsigned int i = 0; i < team->GetHumanGamerCount(); i++) {
    IHIDevice* device = team->GetHumanGamerDevice(i);
    if (device && device->GetDeviceType() == e_HIDeviceType_Gamepad)
      return device;
  }

  // A coach has no player to control, so the pad matching his bench is used:
  // controller 0 runs team 0, controller 1 runs team 1.
  if (teamID < static_cast<int>(controllers.size())) {
    IHIDevice* device = controllers.at(teamID);
    if (device && device->GetDeviceType() == e_HIDeviceType_Gamepad)
      return device;
  }
  return nullptr;
}

void Match::ProcessTacticalHotkeysForPad(int teamID) {
  IHIDevice* device = GetTouchlineDevice(teamID);
  if (!device)
    return;
  // RT is the touchline modifier; without it the d-pad plays football as usual.
  if (!device->GetButton(e_ButtonFunction_Sprint))
    return;

  Team* team = GetTeam(teamID);
  TeamInstructions::State& instructions = team->GetController()->GetInstructions();
  bool changed = false;

  // RT + d-pad: four mentality presets.
  const e_ButtonFunction directionButtons[TeamInstructions::presetDirectionCount] = {
      e_ButtonFunction_Up, e_ButtonFunction_Right, e_ButtonFunction_Down, e_ButtonFunction_Left};
  for (int i = 0; i < TeamInstructions::presetDirectionCount; i++) {
    // Only on the press, so holding a direction does not spin through presets.
    if (!device->GetButton(directionButtons[i]) ||
        device->GetPreviousButtonState(directionButtons[i]))
      continue;
    TeamInstructions::SelectMentality(instructions,
                                      TeamInstructions::GetPresetForDirection(
                                          static_cast<TeamInstructions::e_PresetDirection>(i)));
    changed = true;
  }

  // RT + face buttons: the four instructions a manager reaches for most.
  const e_ButtonFunction quickButtons[TeamInstructions::quickInstructionCount] = {
      e_ButtonFunction_ShortPass, e_ButtonFunction_Shot, e_ButtonFunction_HighPass,
      e_ButtonFunction_LongPass};
  for (int i = 0; i < TeamInstructions::quickInstructionCount; i++) {
    if (!device->GetButton(quickButtons[i]) || device->GetPreviousButtonState(quickButtons[i]))
      continue;
    TeamInstructions::Toggle(instructions, TeamInstructions::GetQuickInstructionAt(i));
    changed = true;
  }

  if (changed)
    AnnounceInstructions(teamID);
}

void Match::AnnounceInstructions(int teamID) {
  Team* team = GetTeam(teamID);
  // Take effect at once, and show the manager what he just did.
  team->GetController()->UpdateTactics();
  // The banner names whoever the viewer is watching - the man on the ball, or
  // the selected player when nobody is carrying it - rather than a fixed name.
  std::string subject;
  Player* focus = team->GetDesignatedTeamPossessionPlayer();
  if (!focus || focus->GetTeam() != team) focus = team->GetLastTouchPlayer();
  if (focus && focus->GetPlayerData()) subject = focus->GetPlayerData()->GetLastName();
  ShowBanner(teamID, TeamInstructions::Describe(team->GetController()->GetInstructions()), subject,
            3500);
}

void Match::ProcessTacticalHotkeys() {
  // Either bench can be run from its own pad, so both teams are served.
  for (int teamID = 0; teamID < 2; teamID++)
    ProcessTacticalHotkeysForPad(teamID);

  UserEventManager& events = UserEventManager::GetInstance();

  // Shift addresses the other bench in a manager duel.
  const bool otherTeam =
      events.GetKeyboardState(SDLK_LSHIFT) || events.GetKeyboardState(SDLK_RSHIFT);
  Team* team = GetTeam(GetCoachedTeamID(otherTeam));
  TeamInstructions::State& instructions = team->GetController()->GetInstructions();

  bool changed = false;

  // Push the team up the pitch, or drop it back.
  if (events.GetKeyboardState(SDLK_PAGEUP)) {
    events.SetKeyboardState(SDLK_PAGEUP, false);
    TeamInstructions::PushUp(instructions);
    changed = true;
  }
  if (events.GetKeyboardState(SDLK_PAGEDOWN)) {
    events.SetKeyboardState(SDLK_PAGEDOWN, false);
    TeamInstructions::DropBack(instructions);
    changed = true;
  }

  // F5..F11 toggle the advanced instructions.
  const SDL_Keycode instructionKeys[TeamInstructions::instructionCount] = {
      SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11};
  for (int i = 0; i < TeamInstructions::instructionCount; i++) {
    if (!events.GetKeyboardState(instructionKeys[i]))
      continue;
    events.SetKeyboardState(instructionKeys[i], false);
    TeamInstructions::Toggle(instructions, TeamInstructions::GetInstructionAt(i));
    changed = true;
  }

  if (!changed)
    return;

  AnnounceInstructions(team->GetID());
}

void Match::UpdateBallHeatmap() {
  // One sample a second is plenty for a readable heatmap.
  if (!IsInPlay() || actualTime_ms % 1000 != 0)
    return;
  MatchAnalytics::AddSample(ballHeatmap, ball->Predict(0).Get2D());
}

void Match::UpdateCrowdAudio() {
  if (!pause) {
    float currentExcitement = 0.15f;
    if (IsGoalScored()) {
      currentExcitement = 1.0f;
    } else if (IsInPlay() && GetBestPossessionTeamID() != -1) {
      Team* possessionTeam = teams[GetBestPossessionTeamID()].get();
      Player* possessionPlayer = GetPlayer(possessionTeam->GetBestPossessionPlayerID());
      if (possessionPlayer) {
        float distance = (Vector3(pitchHalfW * -possessionTeam->GetSide(), 0, 0) -
                          possessionPlayer->GetPosition())
                             .GetLength();
        distance = clamp(distance / 80.0f, 0.3f, 0.7f);
        currentExcitement = pow(1.0f - distance, 1.2f);
      }
    }

    if (currentExcitement > excitement) {
      excitement = clamp(excitement * 0.97f + currentExcitement * 0.03f, 0.0f, 1.0f);
    } else {
      excitement = clamp(excitement * 0.997f + currentExcitement * 0.003f, 0.0f, 1.0f);
    }

    if (excitementEventTimer_ms > 0) {
      excitementEventTimer_ms = std::max(0, excitementEventTimer_ms - 10);
      excitementEventBoost *= excitementEventDecayRate;
      if (excitementEventTimer_ms == 0)
        excitementEventBoost = 0.0f;
    }
  }

  // Sustained possession by the home side lifts the crowd on top of the
  // goal/danger reactions above.
  const float possessionExcitement =
      CrowdMood::GetPossessionExcitement(matchData->GetPossessionFactor_60seconds(), 0);
  const float effectiveExcitement =
      clamp(CrowdMood::Blend(excitement + excitementEventBoost, possessionExcitement), 0.0f, 1.0f);
  const float masterVolume = clamp(GetConfiguration()->GetReal("audio_volume", 0.5f), 0.0f, 1.0f);
  const bool crowdMuted = masterVolume == 0.0f;
  const float pauseFactor = pause ? 0.22f : 1.0f;
  const float targetGain01 = (0.18f + effectiveExcitement * 0.42f) * masterVolume * pauseFactor;
  const float targetGain02 =
      clamp((effectiveExcitement - 0.22f) / 0.78f, 0.0f, 1.0f) * 0.62f * masterVolume * pauseFactor;

  if (crowdMuted) {
    crowdAmbientGain = 0.0f;
    crowdReactionGain = 0.0f;
  } else {
    const float ambientSmoothing = targetGain01 > crowdAmbientGain ? 0.12f : 0.025f;
    const float reactionSmoothing = targetGain02 > crowdReactionGain ? 0.16f : 0.035f;
    crowdAmbientGain += (targetGain01 - crowdAmbientGain) * ambientSmoothing;
    crowdReactionGain += (targetGain02 - crowdReactionGain) * reactionSmoothing;
  }

  if ((crowdMuted && crowd01->GetGain() != 0.0f) ||
      fabs(crowd01->GetGain() - crowdAmbientGain) >= crowdGainUpdateThreshold)
    crowd01->SetGain(crowdAmbientGain);
  if ((crowdMuted && crowd02->GetGain() != 0.0f) ||
      fabs(crowd02->GetGain() - crowdReactionGain) >= crowdGainUpdateThreshold)
    crowd02->SetGain(crowdReactionGain);
}

void Match::ToggleStatsOverlay() {
  if (statsOverlay->IsVisible()) {
    statsOverlay->Hide();
  } else {
    statsOverlay->UpdateStats();
    statsOverlay->Show();
  }
}

void Match::GetCameraParams(float& zoom, float& height, float& fov, float& angleFactor) {
  zoom = cameraUserZoom;
  height = cameraUserHeight;
  fov = cameraUserFOV;
  angleFactor = cameraUserAngleFactor;
}

void Match::SetCameraParams(float zoom, float height, float fov, float angleFactor) {
  cameraUserZoom = zoom;
  cameraUserHeight = height;
  cameraUserFOV = fov;
  cameraUserAngleFactor = angleFactor;
}

Vector3 Match::CutsceneAnchorPosition() const {
  // The offender if the referee named one, otherwise wherever the ball stopped:
  // at a stoppage that is the incident.
  if (cutscenePrimary)
    return cutscenePrimary->GetPosition();
  return ball ? ball->Predict(0).Get2D() : Vector3(0, 0, 0);
}

void Match::UpdateIngameCamera() {
  // stoppage cutscene: play until it ends or the ball is back in play
  if (activeCutscene) {
    unsigned long now = EnvironmentManager::GetInstance().GetTime_ms();
    if (now < cutsceneEnd_ms && !IsInPlay()) {
      CamTrackFrame frame =
          activeCutscene->Sample((now - cutsceneStart_ms) * 0.03f);
      if (activeCutsceneAnchoring == CutsceneViewer::Anchoring::IncidentLocal) {
        // Place the authored rig at the incident, then re-aim it there: the
        // authored rotation assumed its subject at the origin, so once the
        // camera has moved the aim has to follow.
        const Vector3 anchor = CutsceneAnchorPosition();
        frame.position[0] += anchor.coords[0];
        frame.position[1] += anchor.coords[1];
        frame = RetargetCamTrackFrame(
            frame, {anchor.coords[0], anchor.coords[1], anchor.coords[2] + 1.2f}, 1.5f, 1.1f);
      }
      cameraNodePosition = Vector3(frame.position[0], frame.position[1],
                                   frame.position[2]);
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraOrientation.Set(frame.rotation[0], frame.rotation[1],
                            frame.rotation[2], frame.rotation[3]);
      cameraFOV = frame.fov;
      cameraNearCap = std::max(0.1f, frame.near);
      cameraFarCap = frame.far;
      // Where the camera ended up relative to the incident it is filming. The
      // one number that says whether an incident-local shot was placed at the
      // challenge or left sitting by the centre spot ("debug_cutscene_report").
      if (!cutsceneShotTaken && now >= cutsceneStart_ms + 900 &&
          GetConfiguration()->GetBool("debug_cutscene_report", false)) {
        cutsceneShotTaken = true;
        const Vector3 anchor = CutsceneAnchorPosition();
        Log(e_Notice, "Match", "CutsceneReport",
            std::string("shot ") + CutsceneViewer::AnchoringName(activeCutsceneAnchoring) +
                ": incident at " + int_to_str((int)anchor.coords[0]) + "," +
                int_to_str((int)anchor.coords[1]) + ", camera " +
                int_to_str((int)(cameraNodePosition - anchor).GetLength()) + " m from it");
      }
      return;
    }
    activeCutscene = nullptr;
  }

  // pre-kickoff cutscene. With an imported PES camera track
  // ("intro_cutscene_track", see docs/PES21_CAMERA_TRACE.md) the original
  // hand-authored camerawork plays; otherwise a slow authored orbit around
  // the centre spot frames the stands and crowd.
  // the bench holds the match with the same mechanism, but frames the model
  // itself rather than the stadium
  const bool modelViewerHolding =
      GetConfiguration()->GetReal("debug_model_viewer_seconds", 0.0f) > 0.0f;
  if (introCutsceneEnd_ms > 0 && !modelViewerHolding) {
    // The presentation owns the camera for as long as it is running, which is
    // a real-time question now rather than an actualTime_ms one.
    if (entranceActive) {
      const float t = entranceSeconds > 0.0f
                          ? clamp(GetEntranceElapsedSeconds() / entranceSeconds, 0.0f, 1.0f)
                          : 0.0f;
      const PrematchTimeline::State beat = GetPrematchState();

      // Each beat of the presentation asks for its own kind of shot; a
      // timeline with no beats at all falls back to running the imported
      // camerawork straight through, which is what this used to do.
      PrematchTimeline::Camera want =
          prematchTimeline.beats.empty() ? PrematchTimeline::Camera::Entrance : beat.camera;
      if (want == PrematchTimeline::Camera::Hold && !heldCameraValid)
        want = PrematchTimeline::Camera::Aerial;
      if (want == PrematchTimeline::Camera::Entrance && introShots.empty() &&
          introCamTrack.GetFrameCount() == 0)
        want = PrematchTimeline::Camera::Orbit;  // nothing imported to play

      if (want == PrematchTimeline::Camera::Hold) {
        cameraNodePosition = heldCameraPosition;
        cameraNodeOrientation = heldCameraNodeOrientation;
        cameraOrientation = heldCameraOrientation;
        cameraFOV = heldCameraFOV;
        cameraNearCap = heldCameraNear;
        cameraFarCap = heldCameraFar;
        return;
      }

      // A beat that names an authored shot plays exactly that, across its own
      // duration - this is what puts the tunnel walkout, the anthems and the
      // team picture on screen rather than one family's tracks end to end.
      //
      // Only when the beat asked for that camera, though. "shot" names the
      // staging as well (see AcquirePrematchStaging), and a beat wants its cast
      // choreographed by PES without being filmed by camerawork authored for
      // another ground: taken unconditionally, this pushed the lens through the
      // players at the walk-on and flew outside the stadium at the end, whatever
      // the timeline asked for.
      // The camera that was paired with the staging now on the pitch, so the two
      // stay the variant PES authored together (prematchshotpair.hpp).
      const CamTrack* namedShot =
          (want == PrematchTimeline::Camera::Entrance && beat.beatIndex >= 0 &&
           beat.beatIndex < (int)prematchTimeline.beats.size())
              ? FindPrematchShot(!stagedCameraKey.empty()
                                     ? stagedCameraKey
                                     : prematchTimeline.beats[beat.beatIndex].shot)
              : nullptr;
      if (namedShot && namedShot->GetFrameCount() > 0) {
        // At the rate it was authored at, from the start of its beat. PES's
        // entrance camerawork is a montage - the cut table in its .fdc changes
        // shot every 100 frames, and the exported track carries those cuts as
        // jumps at 3.33 second intervals - so stretching it across a beat played
        // a hundred seconds of cutting in twenty-eight and left every shot
        // drifting. Sampled in real time, it cuts when PES cuts.
        const float beatSeconds =
            (beat.beatIndex >= 0 && beat.beatIndex < (int)prematchTimeline.beats.size())
                ? prematchTimeline.beats[beat.beatIndex].seconds
                : 0.0f;
        const float shotFrame = clamp(beat.beatT * beatSeconds * kPrematchShotFrameRate, 0.0f,
                                      (float)namedShot->GetTimelineFrameCount());
        const CamTrackFrame frame = namedShot->SampleTimeline(shotFrame);
        // The camerawork is authored in the same coordinates as the choreography
        // it films, so wherever the staging had to be moved to happen on our
        // pitch, the camera goes with it - otherwise PES's own shot points at the
        // empty ground the cast used to walk across.
        cameraNodePosition = Vector3(frame.position[0], frame.position[1], frame.position[2]) +
                             stagingOffset;
        // PES composed these cuts around its own players; over a 4cc cast, whose
        // characters are broader and carry props, the tight ones end up inside
        // somebody. Dolly straight back until the nearest body clears the lens -
        // the framing, the lens and the move stay PES's (camerastandoff.hpp).
        {
          std::vector<Vector3> castPositions;
          castPositions.reserve(entranceCast.size());
          const EntranceChoreo& posed = activeStaging ? activeStaging->choreo : entranceChoreo;
          const float castFrame =
              stagingHoldsOpeningFrame
                  ? 0.0f
                  : (GetEntranceElapsedSeconds() - stagingStartSeconds) * 100.0f;
          for (const auto& member : entranceCast) {
            Vector3 position;
            radian yaw = 0;
            int animFrame = 0;
            posed.Sample(*member.slot, castFrame, position, yaw, animFrame);
            castPositions.push_back(position + stagingOffset);
          }
          Quaternion aim = QUATERNION_IDENTITY;
          aim.Set(frame.rotation[0], frame.rotation[1], frame.rotation[2], frame.rotation[3]);
          const Vector3 forward = aim * Vector3(0, 0, -1);
          const float push = CameraStandoff::PushBack(castPositions, cameraNodePosition, forward,
                                                      kPrematchLensClearance);
          if (push > 0.0f) cameraNodePosition -= forward * push;
        }
        cameraNodeOrientation = QUATERNION_IDENTITY;
        cameraOrientation.Set(frame.rotation[0], frame.rotation[1], frame.rotation[2],
                              frame.rotation[3]);
        cameraFOV = frame.fov;
        cameraNearCap = std::max(0.1f, frame.near);
        cameraFarCap = frame.far;
        RememberPrematchCamera();
        return;
      }

      if (want == PrematchTimeline::Camera::Entrance) {
        // The imported shots are spread across the entrance beats only, so a
        // lineup graphic holding the picture does not eat into the walkout's
        // camerawork.
        // Played at the rate it was authored at, one track after another. These
        // tracks are montages: the cut table in a .fdc changes shot every 100
        // frames and the export carries those cuts as jumps in the track, three
        // and a third seconds apart. Sampling them by how far through the
        // presentation we are - which is what this did - stretched a hundred
        // seconds of cutting over the whole sequence, so no cut ever landed and
        // every shot drifted instead.
        float elapsed = GetEntranceElapsedSeconds();
        const CamTrack* shot = introShots.empty() ? &introCamTrack : &introShots.front();
        for (const CamTrack& candidate : introShots) {
          const float duration = candidate.GetDurationSeconds();
          if (elapsed <= duration || &candidate == &introShots.back()) {
            shot = &candidate;
            break;
          }
          elapsed -= duration;
        }
        if (shot->GetFrameCount() > 0) {
          const float shotFrame = clamp(elapsed * kPrematchShotFrameRate, 0.0f,
                                        (float)shot->GetTimelineFrameCount());
          CamTrackFrame frame = shot->SampleTimeline(shotFrame);
          cameraNodePosition = Vector3(frame.position[0], frame.position[1], frame.position[2]);
          cameraNodeOrientation = QUATERNION_IDENTITY;
          cameraOrientation.Set(frame.rotation[0], frame.rotation[1], frame.rotation[2],
                                frame.rotation[3]);
          cameraFOV = frame.fov;
          cameraNearCap = std::max(0.1f, frame.near);
          cameraFarCap = frame.far;
          RememberPrematchCamera();
          return;
        }
      }

      if (want == PrematchTimeline::Camera::Walkout ||
          want == PrematchTimeline::Camera::Lineup) {
        Vector3 castCentre, castExtent;
        if (GetEntranceCastBounds(castCentre, castExtent)) {
          // Stand off on the side the players face - they walk out towards
          // the middle of the pitch - and look back at the line.
          const float side = castCentre.coords[1] <= 0.0f ? 1.0f : -1.0f;
          // Clamped so the shot stays inside the bowl. Before kickoff the two
          // squads can be spread the width of the pitch, and a standoff scaled
          // straight off that put the camera through the back of a stand.
          const float lineHalf = clamp(castExtent.coords[0] * 0.5f, 6.0f, 18.0f);

          Vector3 target = castCentre;
          float distance, camHeight, fov;
          if (want == PrematchTimeline::Camera::Walkout) {
            // Pan slowly along the row, holding on the players as they pass -
            // the shot the reference broadcast uses for a walkout.
            target.coords[0] = castCentre.coords[0] + (beat.beatT * 2.0f - 1.0f) * lineHalf;
            // The reference holds the camera right on the line - the players
            // fill the frame - rather than watching from across the pitch.
            // Measured from the near edge of the group rather than its centre:
            // a standoff from the centre puts whoever is on the near side of it
            // against the lens.
            distance = 6.5f + castExtent.coords[1] * 0.5f;
            camHeight = 2.0f;
            fov = 46.0f;
          } else {
            // Head-on and wide enough to hold the whole line: the anthem and
            // team-picture shot.
            distance = clamp(lineHalf * 1.7f, 15.0f, 24.0f);
            camHeight = 4.5f;
            fov = 38.0f;
          }
          target.coords[2] = 1.1f;

          // Kept inside the playing area, so the camera never ends up inside
          // the stadium's own geometry. The walk-on staging works behind the
          // goal - PES plays it out of the tunnel - so this has to reach past
          // the halves of the pitch the live cameras ever use.
          Vector3 eye(clamp(target.coords[0], -44.0f, 44.0f),
                      clamp(castCentre.coords[1] + side * distance, -48.0f, 48.0f),
                      camHeight);

          // Ease towards the framing rather than snapping to it. Both the eye
          // and the target are computed from the squads' bounding box, which
          // shifts every tick as they walk - taken raw, the shot judders.
          if (smoothedCamValid) {
            const float ease = 0.06f;
            eye = smoothedCamEye + (eye - smoothedCamEye) * ease;
            target = smoothedCamTarget + (target - smoothedCamTarget) * ease;
          }
          smoothedCamEye = eye;
          smoothedCamTarget = target;
          smoothedCamValid = true;

          const Vector3 toTarget = target - eye;
          const float flat = std::sqrt(toTarget.coords[0] * toTarget.coords[0] +
                                       toTarget.coords[1] * toTarget.coords[1]);

          cameraNodePosition = eye;
          cameraNodeOrientation.SetAngleAxis(std::atan2(-toTarget.coords[0], toTarget.coords[1]),
                                             Vector3(0, 0, 1));
          cameraOrientation.SetAngleAxis(0.5f * pi + std::atan2(toTarget.coords[2], flat),
                                         Vector3(1, 0, 0));
          cameraFOV = fov;
          cameraNearCap = 0.5f;
          cameraFarCap = 400.0f;
          RememberPrematchCamera();
          return;
        }
        // Nothing staged (no choreography installed): fall through to the
        // wide, which at least shows the pitch.
        want = PrematchTimeline::Camera::Aerial;
      }

      if (want == PrematchTimeline::Camera::Aerial) {
        // The high, steeply-tilted wide the live game is played on (spec
        // section 5), held still - what the lineup graphics sit over.
        const float camHeight = 62.0f;
        const float back = 34.0f;
        cameraNodePosition = Vector3(0, -back, camHeight);
        cameraNodeOrientation = QUATERNION_IDENTITY;
        cameraOrientation.SetAngleAxis(0.5f * pi - std::atan2(camHeight, back), Vector3(1, 0, 0));
        cameraFOV = 32.0f;
        cameraNearCap = 2.0f;
        cameraFarCap = 400.0f;
        RememberPrematchCamera();
        return;
      }

      // Orbit: a slow authored sweep of the stands, one full circle across
      // however many beats ask for it. Set back and high enough to hold the
      // whole venue in frame - closer in it looked past the stands at open
      // sky, which is a poor way to open a broadcast.
      const float a = t * 2.0f * pi;
      const float radius = 78.0f;
      const float camHeight = 30.0f;
      cameraNodePosition = Vector3(std::sin(a) * radius, -std::cos(a) * radius, camHeight);
      cameraNodeOrientation.SetAngleAxis(a, Vector3(0, 0, 1));
      cameraOrientation.SetAngleAxis(0.5f * pi - std::atan2(camHeight, radius), Vector3(1, 0, 0));
      cameraFOV = 35.0f;
      cameraNearCap = 2.0f;
      cameraFarCap = 400.0f;
      RememberPrematchCamera();
      return;
    }
    introCutsceneEnd_ms = 0;
  }

  // camera

  float fov;
  float zoom;
  float height;

  fov = 0.5f + cameraUserFOV * 0.5f;
  zoom = cameraUserZoom;
  height = cameraUserHeight * 1.5f;

  float playerBias = 0.6f;  // 0.7f;
  Vector3 ballPos = ball->Predict(0) * (1.0f - playerBias) +
                    GetDesignatedPossessionPlayer()->GetPosition() * playerBias;
  // look in possession player's direction
  ballPos += GetDesignatedPossessionPlayer()->GetDirectionVec() * 1.0f;
  // look in possession team's attacking direction
  ballPos += Vector3(((teams[0]->GetFadingTeamPossessionAmount() - 1.0f) * -teams[0]->GetSide() +
                      (teams[1]->GetFadingTeamPossessionAmount() - 1.0f) * -teams[1]->GetSide()) *
                         4.0f,
                     0, 0);

  ballPos.coords[2] *= 0.1f;

  float maxW = pitchHalfW * 0.84f * (1.0 / (zoom + 0.01f));  // * (height * 0.75f + 0.25f);
  float maxH = pitchHalfH * 0.60f * (1.0 / (zoom + 0.01f)) * (height * 0.75f + 0.25f);  // 0.52f
  if (fabs(ballPos.coords[0]) > maxW)
    ballPos.coords[0] = maxW * signSide(ballPos.coords[0]);
  if (fabs(ballPos.coords[1]) > maxH)
    ballPos.coords[1] = maxH * signSide(ballPos.coords[1]);

  Vector3 shudder = Vector3(random(-0.1f, 0.1f), random(-0.1f, 0.1f), 0) *
                    (ball->GetMovement().GetLength() * 0.8f + 6.0f);
  shudder *= 0.2f;
  camPos.push_back(ballPos + shudder * ((float)camPos.size() / (float)camPosSize));
  if (camPos.size() > camPosSize)
    camPos.pop_front();

  Vector3 average;
  std::deque<Vector3>::iterator camIter = camPos.begin();
  float count = 0;
  float indexSize = camPos.size();
  int index = 0;
  while (camIter != camPos.end()) {
    float weight = sin((index / indexSize - 0.3f) * 1.4f * pi) * 0.5f +
                   0.5f;  // healthy mix of latest & middle | wa: sin((x / 100 - 0.3) * 1.4 * pi) *
                          // 0.5 + 0.5 | from x = 0 to 100
    weight *= pow(
        1.0f - index / indexSize,
        0.3f);  // sharp cutoff @ latest (because cameraperson can't 'foresee' the current moment
                // that fast) | wa: (1.0 - x / 100) ^ 0.3 * (<prev formula>) | from x = 0 to 100
    average += (*camIter) * weight;
    count += weight;
    camIter++;
    index++;
  }

  average /= count;

  radian angleFac =
      1.0f - cameraUserAngleFactor * 0.4f;  // 0.0 == 90 degrees max, 1.0 == sideline view

  // normal cam

  int camMethod = 1;  // 1 == wide, 2 == birds-eye, 3 == tele

  if (!IsGoalScored() || (IsGoalScored() && goalScoredTimer < 1000)) {
    if (camMethod == 1) {
      // wide cam

      zoom = (0.6f + zoom * 1.0f) * (1.0f / fov);
      height = 4.0f + height * 10;

      float distRot = average.coords[1] / 800.0f;

      cameraOrientation.SetAngleAxis(distRot + (0.42f - height * 0.01f) * pi, Vector3(1, 0, 0));
      cameraNodeOrientation.SetAngleAxis(
          (-average.coords[0] / pitchHalfW) * (1.0f - angleFac) * 0.25f * pi * 1.24f,
          Vector3(0, 0, 1));
      cameraNodePosition =
          average *
              Vector3(1.0f * (1.0f - cameraUserAngleFactor * 0.2f) * (1.0f - cameraUserZoom * 0.3f),
                      0.9f - cameraUserZoom * 0.3f, 0.2f) +
          Vector3(0, -41.4f - (cameraUserFOV * 3.7f) + pow(height, 1.2f) * 0.46f, 10.0f + height) *
              zoom;
      cameraFOV = (fov * 28.0f) - (cameraNodePosition.coords[1] / 30.0f);
      cameraNearCap = cameraNodePosition.coords[2];
      cameraFarCap = 200;

    } else if (camMethod == 2) {
      // birds-eye cam

      cameraOrientation = QUATERNION_IDENTITY;
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraNodePosition = average * Vector3(1, 1, 0) + Vector3(0, 0, 50 + zoom * 20.0);
      cameraFOV = 28;
      cameraNearCap = 40 + height - 5;
      cameraFarCap = 250;  // 65 + height * 1.2; doesn't work wtf?

    } else if (camMethod == 3) {
      // tele cam

      zoom = (0.6f + zoom * 1.0f) * (1.0f / fov);

      cameraOrientation.SetAngleAxis(0.3f * pi * height + 0.4f * pi * (1.0 - height),
                                     Vector3(1, 0, 0));
      cameraNodeOrientation = QUATERNION_IDENTITY;
      Vector3 offset =
          Vector3(0, -175.0f, 125.0f) * height + Vector3(0, -230.0f, 65.0f) * (1.0 - height);
      cameraNodePosition = average * Vector3(0.9f, 0.7f, 0.2f) + offset * zoom * 0.4f;
      cameraFOV = 15.0f;
      cameraNearCap = 50 + zoom * 10.0f;
      cameraFarCap = 300;
    }

  } else {
    // scorer cam: PES's own goal camerawork when tracks are available,
    // mirrored to whichever goal was actually scored in
    bool trackApplied = false;
    // canned tracks don't follow the real ball: during a shootout that
    // masks the actual outcome, so the ball-tracking scorer cam rules there
    if (!goalCamTracks.empty() && lastGoalTeamID >= 0 &&
        matchPhase != e_MatchPhase_Penalties) {
      int pick = (lastGoalTeamID * 7 + GetScore(0) + GetScore(1) * 3) %
                 (int)goalCamTracks.size();
      const CamTrack& track = goalCamTracks[pick];
      CamTrackFrame frame = track.Sample(goalScoredTimer * 0.03f);
      int scoredSide = -teams[lastGoalTeamID]->GetSide();
      if (scoredSide != goalCamAuthoredSides[pick]) {
        frame.position[0] = -frame.position[0];
        frame.rotation[1] = -frame.rotation[1];
        frame.rotation[2] = -frame.rotation[2];
      }
      // the tracks are authored against PES's celebration staging; re-aim
      // them at the actual celebrating player (or the ball) so the shot
      // frames whoever scored instead of PES's staged runner, never clips
      // inside him, and the PES super-telephoto lens curve adapts to the
      // real subject distance
      Vector3 subject = lastGoalScorer
                            ? lastGoalScorer->GetPosition()
                            : ball->Predict(0).Get2D();
      frame = RetargetCamTrackFrame(
          frame,
          {subject.coords[0], subject.coords[1], subject.coords[2] + 1.5f},
          1.5f, 0.75f);
      cameraNodePosition = Vector3(frame.position[0], frame.position[1],
                                   frame.position[2]);
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraOrientation.Set(frame.rotation[0], frame.rotation[1],
                            frame.rotation[2], frame.rotation[3]);
      cameraFOV = frame.fov;
      cameraNearCap = std::max(0.1f, frame.near);
      cameraFarCap = frame.far;
      trackApplied = true;
    }

    Vector3 targetPos = ball->Predict(0).Get2D();
    if (lastGoalScorer) {
      targetPos = lastGoalScorer->GetPosition();
    }

    if (!trackApplied) {
      radian rot = (float)goalScoredTimer * 0.0005f;
      cameraOrientation.SetAngleAxis(0.45f * pi, Vector3(1, 0, 0));
      cameraNodeOrientation.SetAngleAxis(rot, Vector3(0, 0, 1));
      cameraNodePosition =
          targetPos + Vector3(0, -1, 0).GetRotated2D(rot) * 15.0f + Vector3(0, 0, 3);
      cameraFOV = 35.0f;

      cameraNearCap = 1;
      cameraFarCap = 220;
    }

    // the scoring team's chant swells while the goal is being celebrated
    if (lastGoalTeamID >= 0 && teamChant[lastGoalTeamID])
      teamChant[lastGoalTeamID]->SetGain(
          0.9f * (1.0f - NormalizedClamp(goalScoredTimer, 6000, 9000)));

    // Goal, celebration, replay, kickoff - in that order. The celebration plays
    // out first and the replay then reaches back past it to the build-up, which
    // is what the long buffer is for. A goal cutscene running past the plain
    // celebration wins: "when the celebration is done and no sooner".
    //
    // The referee schedules the kickoff off the same timings, because preparing
    // it calls ResetSituation, which clears the goal state and with it
    // goalScoredTimer - do that first and the trigger below is never reached.
    const unsigned long goalTime_ms = actualTime_ms - goalScoredTimer;
    if (replayStartOffset_ms == 0 &&
        actualTime_ms >= GoalSequence::ReplayFiresAt_ms(goalTime_ms, cutsceneEnd_ms)) {
      replayStartOffset_ms = GoalSequence::ReplayStartOffset_ms(goalScoredTimer);
      replayCamera = kReplayCameraBehindGoal;
      Log(e_Notice, "Match", "UpdateIngameCamera",
          "goal replay: celebration ran " + int_to_str((int)goalScoredTimer) +
              " ms, replay reaches " + int_to_str((int)replayStartOffset_ms) + " ms back");
      pause = true;
      sig_OnExtendedReplayMoment(this);
    }
  }

  // Debug close-up ("debug_face_closeup_seconds" config key, off by
  // default): parks the camera just in front of a face-rigged player for
  // the first N seconds of the match, so FaceRig expressions can be
  // verified visually in a headless capture run.
  float faceCloseupSeconds =
      GetConfiguration()->GetReal("debug_face_closeup_seconds", 0.0f);
  if (faceCloseupSeconds > 0.0f && !IsGoalScored() &&
      actualTime_ms < (unsigned long)(faceCloseupSeconds * 1000.0f)) {
    Player* subject = nullptr;
    Player* fallbackSubject = nullptr;
    for (int t = 0; t < 2 && !subject; t++) {
      std::vector<Player*> activePlayers;
      teams[t]->GetActivePlayers(activePlayers);
      for (unsigned int i = 0; i < activePlayers.size(); i++) {
        if (!fallbackSubject) fallbackSubject = activePlayers[i];
        if (activePlayers[i]->HasActiveFaceRig()) {
          subject = activePlayers[i];
          break;
        }
      }
    }
    if (!subject) subject = fallbackSubject;
    if (subject) {
      Vector3 head = subject->GetPosition() + Vector3(0, 0, 1.62f);
      Vector3 facing =
          subject->GetDirectionVec().GetNormalized(Vector3(0, -1, 0));
      Vector3 camPos = head + facing * 1.4f + Vector3(0, 0, 0.05f);
      CamTrackFrame frame;
      frame.position = {camPos.coords[0], camPos.coords[1], camPos.coords[2]};
      frame.fov = 24.0f;
      frame.near = 0.1f;
      frame.far = 300.0f;
      frame = RetargetCamTrackFrame(
          frame, {head.coords[0], head.coords[1], head.coords[2]}, 1.0f,
          0.28f);
      cameraNodePosition = Vector3(frame.position[0], frame.position[1],
                                   frame.position[2]);
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraOrientation.Set(frame.rotation[0], frame.rotation[1],
                            frame.rotation[2], frame.rotation[3]);
      cameraFOV = frame.fov;
      cameraNearCap = frame.near;
      cameraFarCap = frame.far;
    }
  }

  // Model viewer: debug tooling, so the bench itself lives in modelviewer.cpp
  const ModelViewerSettings viewer = LoadModelViewerSettings();
  // Not before the match is actually running: the opening frames are still
  // wiring humanoids up, and reaching into them from here took the put
  // thread down inside CalculateGeomOffsets.
  if (ModelViewerIsRunning(viewer, actualTime_ms) && actualTime_ms >= 2000 &&
      (IsInEntrance() || IsInPlay()) && !mentalImages.empty()) {
    Player* subject = PickModelViewerSubject(viewer.playerFilter);
    if (subject) {
      const Vector3 centre = subject->GetPosition() + Vector3(0, 0, 0.95f);
      const Vector3 camPos = ModelViewerCameraPosition(viewer, centre, actualTime_ms);
      CamTrackFrame frame;
      frame.position = {camPos.coords[0], camPos.coords[1], camPos.coords[2]};
      frame.fov = 32.0f;
      frame.near = 0.1f;
      frame.far = 300.0f;
      frame = RetargetCamTrackFrame(
          frame, {centre.coords[0], centre.coords[1], centre.coords[2]}, 1.0f, 0.5f);
      cameraNodePosition =
          Vector3(frame.position[0], frame.position[1], frame.position[2]);
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraOrientation.Set(frame.rotation[0], frame.rotation[1], frame.rotation[2],
                            frame.rotation[3]);
      cameraFOV = frame.fov;
      cameraNearCap = frame.near;
      cameraFarCap = frame.far;
    }
  }
}

// THE SPICE

void Match::Get() {}

void Match::Process() {
  // The player indicators follow whoever is being watched and whatever the
  // manager has set, so they are refreshed with the rest of the match state.
  for (int side = 0; side < 2; side++)
    if (playerHUD[side]) playerHUD[side]->Refresh();

  unsigned long time_ms =
      EnvironmentManager::GetInstance().GetTime_ms() - gameSequenceInfo.startTime_ms;
  timeSincePreviousProcess_ms = time_ms - GetPreviousProcessTime_ms();
  previousProcessTime_ms = time_ms;

  if (UserEventManager::GetInstance().GetKeyboardState(SDLK_F1)) {
    SetRandomSunParams();
    UserEventManager::GetInstance().SetKeyboardState(SDLK_F1, false);
  }

  // The presentation runs on real seconds (see Match::IsInEntrance). Until
  // its budget is spent, keep the kickoff one tick out of reach.
  if (entranceActive) {
    // The presentation's clock starts the first time the match is processed,
    // which is the first moment anything of it can actually be on screen.
    if (entranceRealStart_ms == 0) {
      entranceRealStart_ms = EnvironmentManager::GetInstance().GetTime_ms();
      Log(e_Notice, "Match", "Process", "pre-match presentation starts");
      // Nothing of the in-match HUD belongs over a broadcast opening: the
      // scoreboard bug and the radar only appear at kickoff (spec section 1,
      // shot 15 - the scoreboard arrives *with* the first whistle).
      ShowMatchHud(false);
    }
    if (GetEntranceElapsedSeconds() >= entranceSeconds) {
      entranceActive = false;
      ShowMatchHud(true);
      // The walkout set goes back inside: banners, flag bearers, the arch, the
      // pennant display on the centre circle.
      if (entrancePropsNode) {
        GetScene3D()->DeleteNode(entrancePropsNode);
        entrancePropsNode.reset();
        Log(e_Notice, "Match", "Process", "walkout set cleared for kickoff");
      }
      Log(e_Notice, "Match", "Process",
          "pre-match presentation over after " + int_to_str((int)GetEntranceElapsedSeconds()) +
              "s real / " + int_to_str((int)(actualTime_ms / 1000)) + "s match clock");
    } else {
      // A full second ahead, not one tick. The referee defers the kickoff to
      // this value every tick and re-prepares the set piece the moment
      // actualTime_ms reaches it (referee.cpp) - kept only one tick out, that
      // condition came true on every single tick and teleported all
      // twenty-two players back onto their kickoff marks each frame, which is
      // what had them flickering.
      introCutsceneEnd_ms = actualTime_ms + 1000;
    }
  }

  ProcessTacticalHotkeys();

  // F12 grabs a screenshot; "screenshot_interval_ms" grabs them on a timer,
  // which is how an offscreen run can be checked visually.
  if (UserEventManager::GetInstance().GetKeyboardState(SDLK_F12)) {
    UserEventManager::GetInstance().SetKeyboardState(SDLK_F12, false);
    RequestScreenshot(GetConfiguration()->Get("screenshot_path", "screenshot") + "_" +
                      int_to_str(actualTime_ms) + ".bmp");
  }
  const int screenshotInterval_ms = GetConfiguration()->GetInt("screenshot_interval_ms", 0);
  if (screenshotInterval_ms > 0 && actualTime_ms % screenshotInterval_ms == 0) {
    RequestScreenshot(GetConfiguration()->Get("screenshot_path", "screenshot") + "_" +
                      int_to_str(actualTime_ms) + ".bmp");
  }

  if (gameOver) {
    // todonow: just once ^
    sig_OnGameOver(this);
  }

  if (!pause) {
    if (IsInPlay()) {
      CheckBallCollisions();  // todo: should not read geoms during process
    }

    // HIJ IS EEN HONDELUUUL

    // The shootout owns the pitch during the penalties phase: normal play and
    // the referee's set-piece machinery stay out of the way, otherwise play
    // would simply restart underneath the shootout.
    if (matchPhase == e_MatchPhase_Penalties) {
      penaltyShootout->Process();
    } else {
      referee->Process();
    }

    // ball

    previousBallPos = ball->Predict(0);
    ball->Process();

    // create mental images for the AI to use

    auto mentalImage = std::make_shared<MentalImage>(this);
    mentalImage->TakeSnapshot();
    mentalImages.insert(mentalImages.begin(), mentalImage);
    if (mentalImages.size() > 30) {
      mentalImages.pop_back();
    }

    // obvious

    teams[0]->UpdateSwitch();
    teams[1]->UpdateSwitch();
    UpdateEntranceChoreo();
    UpdateCutsceneChoreo();
    // Posing a player who is still taking part in the match fights his own
// animation machinery and corrupts it (isolated by bisection: camera-only
// runs clean, this path segfaults in the put). Off until the bench can
// pose a player outside the live squad.
    if (GetConfiguration()->GetBool("debug_model_viewer_playback", true))
      UpdateModelViewerPlayback();
    teams[0]->Process();
    teams[1]->Process();
    officials->Process();

    teams[0]->UpdatePossessionStats();
    teams[1]->UpdatePossessionStats();
    CalculateBestPossessionTeamID();

    if (GetBallRetainer() == 0) {
      signed int bestTeamID = GetBestPossessionTeamID();
      if (bestTeamID != -1) {
        Player* candidate = teams[GetBestPossessionTeamID()]->GetDesignatedTeamPossessionPlayer();
        if (candidate != GetDesignatedPossessionPlayer()) {
          unsigned int designatedTime =
              GetDesignatedPossessionPlayer()->GetTimeNeededToGetToBall_ms();
          unsigned int candidateTime = candidate->GetTimeNeededToGetToBall_ms();
          float timeRating = (float)(candidateTime + 10) / (float)(designatedTime + 10);
          if (timeRating < 0.85f)
            designatedPossessionPlayer = candidate;
        }
      } else {
        // just stick with current team
        designatedPossessionPlayer = teams[GetDesignatedPossessionPlayer()->GetTeamID()]
                                         ->GetDesignatedTeamPossessionPlayer();
      }
    } else {
      designatedPossessionPlayer = GetBallRetainer();
    }

    // if (GetDebugMode() == e_DebugMode_Tactical)
    // GetLargeDebugCircle()->SetPosition(designatedPossessionPlayer->GetPosition());

    CheckHumanoidCollisions();  // todo: should not read geoms during process

    // time

    if (IsInPlay() && !IsInSetPiece()) {
      matchTimeExact_ms +=
          MatchDurationGameTimeFromRealMilliseconds(10.0, matchDurationMinutes, matchTimeScale);
      matchTime_ms = static_cast<unsigned long>(matchTimeExact_ms);
    }
    actualTime_ms += 10;
    if (IsGoalScored())
      goalScoredTimer += 10;
    else {
      if (goalScoredTimer != 0) {
        // celebration over: fade the chants back out
        for (int t = 0; t < 2; t++)
          if (teamChant[t]) teamChant[t]->SetGain(0.0f);
      }
      goalScoredTimer = 0;
      replayStartOffset_ms = 0;  // armed again for the next goal
    }

    if (IsInPlay() && !IsInSetPiece())
      GetMatchData()->AddPossessionTime_10ms(designatedPossessionPlayer->GetTeamID());

    // check for goals

    // The goal line is checked in every phase — the shootout needs to know
    // whether the ball actually crossed it, since that is what its tally is
    // built on. What the penalties phase suppresses is only the *scoring*
    // below: the shootout keeps its own count and the match score is frozen.
    const bool shootoutOwnsScoring = matchPhase == e_MatchPhase_Penalties;
    bool t1goal = CheckForGoal(teams[0]->GetSide());
    bool t2goal = CheckForGoal(teams[1]->GetSide());
    if (t1goal)
      ballIsInGoal = true;
    if (t2goal)
      ballIsInGoal = true;

    if (IsInPlay() && !shootoutOwnsScoring) {
      if (t1goal) {
        matchData->SetGoalCount(teams[1]->GetID(), matchData->GetGoalCount(1) + 1);
        scoreboard->SetGoalCount(1, matchData->GetGoalCount(1));
        goalScored = true;
        lastGoalTeamID = teams[1]->GetID();
        teams[1]->GetController()->UpdateTactics();
      }
      if (t2goal) {
        matchData->SetGoalCount(teams[0]->GetID(), matchData->GetGoalCount(0) + 1);
        scoreboard->SetGoalCount(0, matchData->GetGoalCount(0));
        goalScored = true;
        lastGoalTeamID = teams[0]->GetID();
        teams[0]->GetController()->UpdateTactics();
      }
      if (t1goal || t2goal) {
        AddExcitementBoost(1.0f, 5000);

        // find out who scored
        bool ownGoal = true;
        if (GetLastTouchTeamID(e_TouchType_Intentional_Kicked) == GetLastGoalTeamID() ||
            GetLastTouchTeamID(e_TouchType_Intentional_Nonkicked) == GetLastGoalTeamID())
          ownGoal = false;

        if (!ownGoal) {
          lastGoalScorer = teams[GetLastGoalTeamID()]->GetLastTouchPlayer();
          if (lastGoalScorer) {
            SpamMessage("GOAL for " + matchData->GetTeamData(GetLastGoalTeamID())->GetName() +
                            "! " + lastGoalScorer->GetPlayerData()->GetLastName() + " scores!",
                        4000);
          } else {
            SpamMessage("GOAL!!!", 4000);
          }
        }

        else {  // own goal
          lastGoalScorer = teams[abs(GetLastGoalTeamID() - 1)]->GetLastTouchPlayer();
          if (lastGoalScorer) {
            SpamMessage(
                "OWN GOAL! " + lastGoalScorer->GetPlayerData()->GetLastName() + " is so unlucky!",
                4000);
          } else {
            SpamMessage("It's an OWN GOAL! oh noes!", 4000);
          }
        }
      }
    }

    // average possession side

    if (IsInPlay()) {
      if (GetBestPossessionTeamID() >= 0) {
        float sideValue = 0;
        sideValue += (GetTeam(0)->GetFadingTeamPossessionAmount() - 0.5f) * GetTeam(0)->GetSide();
        sideValue += (GetTeam(1)->GetFadingTeamPossessionAmount() - 0.5f) * GetTeam(1)->GetSide();
        possessionSideHistory->Insert(sideValue);
      }
    }

    if (GetReferee()->GetBuffer().active == true &&
        (GetReferee()->GetCurrentFoulType() == 2 || GetReferee()->GetCurrentFoulType() == 3) &&
        GetReferee()->GetBuffer().stopTime < GetActualTime_ms() - 1000) {
      if (GetReferee()->GetBuffer().prepareTime > GetActualTime_ms()) {  // FOUL, film referee
        SetAutoUpdateIngameCamera(false);
        FollowCamera(cameraOrientation, cameraNodeOrientation, cameraNodePosition, cameraFOV,
                     officials->GetReferee()->GetPosition() + Vector3(0, 0, 0.8f), 1.5f);
        cameraNearCap = 1;
        cameraFarCap = 220;
        if (officials->GetReferee()->GetCurrentFunctionType() == e_FunctionType_Special)
          referee->AlterSetPiecePrepareTime(GetActualTime_ms() + 1000);
      } else {  // back to normal
        SetAutoUpdateIngameCamera(true);
      }
    }

  }  // end if !pause

  ProcessAutoSubstitutions();
  ProcessFoulReplay();
  UpdateBallHeatmap();
  UpdateCrowdAudio();

  if (autoUpdateIngameCamera)
    UpdateIngameCamera();

  if (!pause) {
    unsigned int zoomTime = 2000;
    unsigned int startTime = 0;
    if (actualTime_ms < zoomTime + startTime) {  // nice effect at the start

      Quaternion initialOrientation = QUATERNION_IDENTITY;
      initialOrientation.SetAngleAxis(0.0f * pi, Vector3(1, 0, 0));
      Quaternion zOrientation = QUATERNION_IDENTITY;
      initialOrientation = zOrientation * initialOrientation;

      Vector3 initialPosition = Vector3(0.0f, 0.0f, 60.0);

      int subTime = clamp(actualTime_ms - startTime, 0, zoomTime);
      float bias = subTime / (float)(zoomTime);
      bias *= pi;
      bias = sin(bias - 0.5f * pi) * -0.5f + 0.5f;

      cameraOrientation = cameraOrientation.GetSlerped(bias, QUATERNION_IDENTITY);
      cameraNodeOrientation = cameraNodeOrientation.GetSlerped(bias, initialOrientation);
      cameraNodePosition = cameraNodePosition * (1.0f - bias) + initialPosition * bias;
      cameraFOV = cameraFOV * (1.0f - bias) + 40 * bias;
      cameraNearCap = cameraNearCap * (1.0f - bias) + 2.0f * bias;
    }
  }  // end if !pause

  // tactics debug

  if (tacticsDebug && actualTime_ms % 1000 == 0) {
    for (unsigned int teamID = 0; teamID < 2; teamID++) {
      const TeamTactics& tactics = matchData->GetTeamData(teamID)->GetTactics();
      const map_Properties* userMods = tactics.userProperties.GetProperties();
      map_Properties::const_iterator tacIter = userMods->begin();
      int i = 0;
      while (tacIter != userMods->end()) {
        // printf("setting tactical debug item %s (%s)\n", (*tacIter).first.c_str(),
        // (*tacIter).second.c_str());
        float userValue = atof((*tacIter).second.c_str());
        float autoValue = 0.0f;  // autoTacticsModifiers.GetReal((*tacIter).first.c_str(), 0.5f);
        // float liveValue = liveTacticsModifiers.GetReal((*tacIter).first.c_str(), 0.5f);
        // printf("%s for team %i (entry %i): %f %f %f\n", (*tacIter).first.c_str(), teamID, i,
        // userValue, autoValue, liveValue);
        tacticsDebug->SetValue(i, 0, teamID, userValue);
        tacticsDebug->SetValue(i, 1, teamID, autoValue);
        // tacticsDebug->SetValue(i, 2, teamID, liveValue);
        tacIter++;
        i++;
      }
    }
  }

  // log

  if (!pause && _positionLogging) {
    std::string frame = "frame" + int_to_str(GetActualTime_ms() / 10) + ":\n";
    positionLogFile << frame.c_str();

    Vector3 pos = ball->Predict(0);
    std::string bla = "    ball: " + real_to_str(pos.coords[0]) + ", " +
                      real_to_str(pos.coords[1]) + ", " + real_to_str(pos.coords[2]) + "\n";
    positionLogFile << bla.c_str();

    std::vector<Player*> playas;
    int count = 1;
    for (int teamID = 0; teamID < 2; teamID++) {
      bla = "    team" + int_to_str(teamID + 1) + ":\n";
      positionLogFile << bla.c_str();
      GetActiveTeamPlayers(teamID, playas);
      for (unsigned int i = 0; i < playas.size(); i++) {
        Vector3 pos = playas.at(i)->GetPosition();
        bla = "        player" + int_to_str(count) + ": " + real_to_str(pos.coords[0]) + ", " +
              real_to_str(pos.coords[1]) + ", 0\n";
        positionLogFile << bla.c_str();
        count++;
      }
      playas.clear();
    }

    positionLogFile << bla.c_str();
  }

  iterations.Lock();
  iterations.data++;
  iterations.Unlock();
}

void Match::PreparePutBuffers() {
  gameSequenceInfo = GetScheduler()->GetTaskSequenceInfo("game");
  unsigned long time_ms =
      EnvironmentManager::GetInstance().GetTime_ms() - gameSequenceInfo.startTime_ms;
  timeSincePreviousPreparePut_ms = time_ms - GetPreviousPreparePutTime_ms();
  previousPreparePutTime_ms = time_ms;

  // snapshot time is the time that is 'represented' by the snapshot
  unsigned long snapshotTime_ms = gameSequenceInfo.timesRan * gameSequenceInfo.sequenceTime_ms;
  // printf("%lu, %lu\n", (GetIterations() - 1) * 10, gameSequenceInfo.timesRan *
  // gameSequenceInfo.sequenceTime_ms); printf("PREP time: %lu, snapshot time: %lu, actual time:
  // %lu\n", time_ms, snapshotTime_ms, actualTime_ms);

  if (!GetPause()) {
    ball->PreparePutBuffers(snapshotTime_ms);
    teams[0]->PreparePutBuffers(snapshotTime_ms);
    teams[1]->PreparePutBuffers(snapshotTime_ms);
    officials->PreparePutBuffers(snapshotTime_ms);
  }

  buf_cameraOrientation.SetValue(cameraOrientation, snapshotTime_ms);
  buf_cameraNodeOrientation.SetValue(cameraNodeOrientation, snapshotTime_ms);

  // test fun!
  // float xfun = sin((float)EnvironmentManager::GetInstance().GetTime_ms() * 0.001f) * 60;
  // float xfun = sin((float)(EnvironmentManager::GetInstance().GetTime_ms() +
  // PredictFrameTimeToGo_ms(7)) * 0.001f) * 60; float xfun = sin(snapshotTime_ms * 0.001f) * 60.0f;
  // buf_cameraNodePosition.SetValue(cameraNodePosition + Vector3(xfun, 0, 0), snapshotTime_ms);
  buf_cameraNodePosition.SetValue(cameraNodePosition, snapshotTime_ms);

  // printf("timetogo prediction: %i ms\n", PredictFrameTimeToGo_ms(7));

  buf_cameraFOV.SetValue(cameraFOV, snapshotTime_ms);
  buf_cameraNearCap = cameraNearCap;
  buf_cameraFarCap = cameraFarCap;

  buf_matchTime_ms = matchTime_ms;
  buf_actualTime_ms = actualTime_ms;
}

void Match::FetchPutBuffers() {
  if (GetIterations() < 1)
    return;  // no processes done yet

  unsigned long time_ms =
      EnvironmentManager::GetInstance().GetTime_ms() - gameSequenceInfo.startTime_ms;
  timeSincePreviousPut_ms = time_ms - GetPreviousPutTime_ms();
  previousPutTime_ms = time_ms;
  unsigned long putTime_ms =
      time_ms;  // - gameSequenceInfo.startTime_ms; // test: + PredictFrameTimeToGo_ms(7) - 15;
  // printf("FETCH time: %lu - seqstarttime: %lu = put time: %lu, times ran * 10: %i\n", time_ms,
  // gameSequenceInfo.startTime_ms, putTime_ms, (int)gameSequenceInfo.timesRan *
  // gameSequenceInfo.sequenceTime_ms); printf("FETCH buf - snapshot time delta: %i\n",
  // (int)putTime_ms - (int)gameSequenceInfo.timesRan * gameSequenceInfo.sequenceTime_ms);
  fetchedbuf_timeDelta =
      (int)putTime_ms - (int)gameSequenceInfo.timesRan * gameSequenceInfo.sequenceTime_ms;

  fetchedbuf_matchTime_ms = buf_matchTime_ms;
  fetchedbuf_actualTime_ms = buf_actualTime_ms;

  fetchedbuf_cameraOrientation = buf_cameraOrientation.GetValue(putTime_ms);
  fetchedbuf_cameraNodeOrientation = buf_cameraNodeOrientation.GetValue(putTime_ms);
  fetchedbuf_cameraNodePosition = buf_cameraNodePosition.GetValue(putTime_ms);
  fetchedbuf_cameraFOV = buf_cameraFOV.GetValue(putTime_ms);
  fetchedbuf_cameraNearCap = buf_cameraNearCap;
  fetchedbuf_cameraFarCap = buf_cameraFarCap;

  if (!GetPause()) {
    ball->FetchPutBuffers(putTime_ms);
    teams[0]->FetchPutBuffers(putTime_ms);
    teams[1]->FetchPutBuffers(putTime_ms);
    officials->FetchPutBuffers(putTime_ms);
  }
}

void Match::Put() {
  if (GetIterations() < 2)
    return;  // no processes done yet (todo: this is not the correct way to measure that :p)

  // fun!
  // sunNode->SetPosition(Vector3(sin(buf_actualTime_ms * 0.001) * 3000, cos(buf_actualTime_ms *
  // 0.001) * 3000, 1000.0));

  /*
    unsigned long time_ms = EnvironmentManager::GetInstance().GetTime_ms();
    timeSincePreviousPut_ms = time_ms - GetPreviousPutTime_ms();
    previousPutTime_ms = time_ms;
    unsigned long putTime_ms = time_ms - gameSequenceInfo.startTime_ms; // test: +
    PredictFrameTimeToGo_ms(7) - 15;
    //printf("PUT time: %lu - seqstarttime: %lu = put time: %lu, times ran * 10: %i\n", time_ms,
    gameSequenceInfo.startTime_ms, putTime_ms, (int)gameSequenceInfo.timesRan *
    gameSequenceInfo.sequenceTime_ms);
    //printf("PUT put - snapshot time delta: %i\n", (int)putTime_ms - (int)gameSequenceInfo.timesRan
    * gameSequenceInfo.sequenceTime_ms);
  */

  camera->SetPosition(Vector3(0, 0, 0), false);
  camera->SetRotation(fetchedbuf_cameraOrientation, false);
  cameraNode->SetPosition(fetchedbuf_cameraNodePosition, false);
  /*
    int targetTime = EnvironmentManager::GetInstance().GetTime_ms() - 10;
    float bias = NormalizedClamp(targetTime, buf_testTime[0], buf_testTime[1] + 1);
    //printf("%i (%i - %i)\n", targetTime, buf_testTime[0], buf_testTime[1]);

    unsigned long time_ms = EnvironmentManager::GetInstance().GetTime_ms();
    int diff = buf_testTime[1] - buf_testTime[0];
    printf("%i to %i (%i)\n", buf_testTime[0] - time_ms, buf_testTime[1] - time_ms, diff);

    //printf("%f\n", bias);
    Vector3 resultPos = buf_testPos[0] * (1.0f - bias) + buf_testPos[1] * bias;
    cameraNode->SetPosition(resultPos, false);
  */

  cameraNode->SetRotation(fetchedbuf_cameraNodeOrientation, false);
  camera->SetFOV(fetchedbuf_cameraFOV);
  // the sky dome sits ~320m out, beyond the 200-250m gameplay far caps that
  // predate it; floor the far plane so the sky is never clipped away
  float farCap = fetchedbuf_cameraFarCap;
  if (skydomeNode) farCap = std::max(farCap, 500.0f);
  // A stadium that carries its own sky needs the same room: Namek's dome reaches
  // 625 m and was falling outside the frustum entirely, so the engine's fallback
  // gradient showed instead of a green sky (see stadiumfar.hpp).
  farCap = StadiumFar::ChooseFarCap(farCap, stadiumFarNeeded);
  camera->SetCapping(fetchedbuf_cameraNearCap, farCap);

  if (!GetPause()) {
    if (GetDebugMode() == e_DebugMode_AI) {
      int contextW, contextH, bpp;  // context
      GetScene2D()->GetContextSize(contextW, contextH, bpp);
      // fade out effect
      GetDebugOverlay()->SetAlpha(0.5f);
    }

    /*
        // side view hack
        Quaternion rot;
        rot.SetAngleAxis(pi * 0.5, Vector3(1, 0, 0));
        camera->SetPosition(Vector3(0, 0, 0), false);
        camera->SetRotation(rot, false);
        cameraNode->SetRotation(QUATERNION_IDENTITY, false);
        cameraNode->SetPosition(Vector3(0, -60, 1), false);
        camera->SetFOV(2);
    */

    ball->Put();
    teams[0]->Put();
    teams[1]->Put();
    officials->Put();

  } else {  // pause
    ProcessReplayMessages();
  }

  GetDynamicNode()->RecursiveUpdateSpatialData(e_SpatialDataType_Both);

  if (!pause) {
    teams[0]->Put2D();
    teams[1]->Put2D();

    // if (buf_actualTime_ms % 100 == 0) { // a better way would be to count iterations (this modulo
    // is irregular since not all process runs are put)
    //  clock

    // Past the period's scheduled end the clock holds and shows the overtime
    // separately ("45:00 +0:12"), so added time is visible on the scoreboard.
    scoreboard->SetTimeStr(MatchProgression::FormatClock(
        fetchedbuf_matchTime_ms, MatchProgression::GetPeriodEndTime_ms(GetMatchPhase())));
    //}

    if (statsOverlay->IsVisible())
      statsOverlay->UpdateStats();

    // radar

    radar->Put();

    if (tacticsDebug) {
      tacticsDebug->Redraw();
    }

    UpdateGoalNetting(GetBall()->BallTouchesNet());

    // replay
    CaptureReplayFrame(fetchedbuf_actualTime_ms + fetchedbuf_timeDelta);

    if (GetDebugMode() == e_DebugMode_AI)
      GetDebugOverlay()->OnChange();

  } else {
    teams[0]->Hide2D();
    teams[1]->Hide2D();
  }
}

boost::intrusive_ptr<Node> Match::GetDynamicNode() {
  return dynamicNode;
}

void Match::ApplyReplayFrame(unsigned long replayTime_ms) {
  for (unsigned int i = 0; i < replay.size(); i++) {
    /* todo: smoothing
    std::map<unsigned long, ReplayFrame>::iterator iter1 = replay.at(i).frames.find(first_ms);
    if (iter1 == replay.at(i).frames.end()) printf("FRAME NOT FOUND, SHOULD NOT HAPPEN AAARGHH\n");
    std::map<unsigned long, ReplayFrame>::iterator iter2 = replay.at(i).frames.find(last_ms);
    if (iter2 == replay.at(i).frames.end()) printf("FRAME NOT FOUND, SHOULD NOT HAPPEN AAARGHH\n");
    */

    blunted::circular_buffer<ReplaySpatialFrame>::iterator iter = replay.at(i)->frames.begin();
    while (iter != replay.at(i)->frames.end()) {
      if (iter->frameTime_ms >= replayTime_ms) {
        blunted::circular_buffer<ReplaySpatialFrame>::iterator iterPrev = iter;
        if (iterPrev != replay.at(i)->frames.begin())
          iterPrev--;
        const ReplaySpatialFrame& frame1 = *iterPrev;  //->frames.at(frame - 1);
        const ReplaySpatialFrame& frame2 = *iter;      //->frames.at(frame);
        int count = frame2.frameTime_ms - frame1.frameTime_ms;
        int offset = replayTime_ms - frame1.frameTime_ms;
        if (count == 0)
          count = 1;  // never divide by zero, will implode universe
        float bias = (float)offset / (float)count;
        // printf("bias: %f\n", bias);

        // same stale-entry hazard as CaptureReplayFrame: skip null spatials
        if (!replay.at(i)->spatial) break;
        replay.at(i)->spatial->SetPosition(frame1.position * (1.0f - bias) + frame2.position * bias,
                                           false);
        // frame1.orientation.MakeSameNeighborhood(frame2.orientation); only needed for Lerp
        replay.at(i)->spatial->SetRotation(
            frame1.orientation.GetSlerped(bias, frame2.orientation).GetNormalized(), false);
        replay.at(i)->spatial->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
        break;
      }
      iter++;
    }
  }

  std::vector<Player*> players;
  GetActiveTeamPlayers(0, players);
  GetActiveTeamPlayers(1, players);
  for (unsigned int i = 0; i < players.size(); i++) {
    players.at(i)->UpdateFullbodyNodes();
  }
  std::vector<PlayerBase*> playerOfficials;
  GetOfficialPlayers(playerOfficials);
  for (unsigned int i = 0; i < playerOfficials.size(); i++) {
    playerOfficials.at(i)->UpdateFullbodyNodes();
  }

  blunted::circular_buffer<ReplayBallTouchesNetFrame>::iterator iter =
      replayBallTouchesNetFrames.begin();
  while (iter != replayBallTouchesNetFrames.end()) {
    if (iter->frameTime_ms >= replayTime_ms) {
      bool ballTouchesNet = (*iter).ballTouchesNet;
      UpdateGoalNetting(ballTouchesNet);
      break;
    }
    iter++;
  }
}

void Match::GetReplaySpatials(std::list<boost::intrusive_ptr<Spatial>>& spatials) {
  spatials.push_back(teams[0]->GetSceneNode());
  teams[0]->GetSceneNode()->GetSpatials(spatials);
  spatials.push_back(teams[1]->GetSceneNode());
  teams[1]->GetSceneNode()->GetSpatials(spatials);
  spatials.push_back(ball->GetBallGeom());
  spatials.push_back(GetGreenDebugPilon());
  spatials.push_back(GetBlueDebugPilon());
  spatials.push_back(GetYellowDebugPilon());
  spatials.push_back(GetRedDebugPilon());
  spatials.push_back(GetSmallDebugCircle1());
  spatials.push_back(GetSmallDebugCircle2());
  spatials.push_back(GetLargeDebugCircle());
  spatials.push_back(officials->GetYellowCardGeom());
  spatials.push_back(officials->GetRedCardGeom());

  std::vector<Player*> players;
  GetActiveTeamPlayers(0, players);
  GetActiveTeamPlayers(1, players);
  for (unsigned int i = 0; i < players.size(); i++) {
    spatials.push_back(players.at(i)->GetHumanoidNode());
    players.at(i)->GetHumanoidNode()->GetSpatials(spatials);
  }
  std::vector<PlayerBase*> playerOfficials;
  GetOfficialPlayers(playerOfficials);
  for (unsigned int i = 0; i < playerOfficials.size(); i++) {
    spatials.push_back(playerOfficials.at(i)->GetHumanoidNode());
    playerOfficials.at(i)->GetHumanoidNode()->GetSpatials(spatials);
  }
}

void Match::ReplacePlayerReferences(Player* playerOut, Player* playerIn) {
  if (designatedPossessionPlayer == playerOut)
    designatedPossessionPlayer = playerIn;
  if (ballRetainer == playerOut)
    ballRetainer = playerIn;
  if (lastGoalScorer == playerOut)
    lastGoalScorer = nullptr;
  // The mental images hold snapshots of a humanoid that no longer exists.
  InvalidateCachedMentalImages();
  mentalImages.clear();
  bestPossessionTeamID = -1;
}

void Match::RebuildReplaySpatials() {
  replay.clear();

  std::list<boost::intrusive_ptr<Spatial>> spatials;
  GetReplaySpatials(spatials);
  for (auto& spatial : spatials) {
    auto replaySpatial = std::make_unique<ReplaySpatial>(GetReplaySize_ms() / 10);
    replaySpatial->spatial = spatial;
    replay.push_back(std::move(replaySpatial));
  }
}

void Match::CaptureReplayFrame(unsigned long replayTime_ms) {
  for (unsigned int i = 0; i < replay.size(); i++) {
    // a substitution can momentarily leave a stale entry until
    // RebuildReplaySpatials runs; skip rather than dereference null
    if (!replay.at(i)->spatial) continue;
    Vector3 pos = replay.at(i)->spatial->GetPosition();
    Quaternion orient = replay.at(i)->spatial->GetRotation();

    ReplaySpatialFrame frame;
    frame.frameTime_ms = replayTime_ms;
    frame.position = pos;
    frame.orientation = orient;
    replay.at(i)->frames.push_back(frame);
  }

  ReplayBallTouchesNetFrame ballTouchesNetFrame;
  ballTouchesNetFrame.frameTime_ms = replayTime_ms;
  ballTouchesNetFrame.ballTouchesNet = GetBall()->BallTouchesNet();
  replayBallTouchesNetFrames.push_back(ballTouchesNetFrame);
}

bool Match::CheckForGoal(signed int side) {
  if (fabs(ball->Predict(10).coords[0]) < pitchHalfW - 1.0)
    return false;

  Line line;
  line.SetVertex(0, previousBallPos);
  line.SetVertex(1, ball->Predict(0));

  Triangle goal1;
  goal1.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 0));
  goal1.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal1.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal1.SetNormals(Vector3(-side, 0, 0));
  Triangle goal2;
  goal2.SetVertex(0, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 0));
  goal2.SetVertex(1, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, -3.7f, 2.5f));
  goal2.SetVertex(2, Vector3((pitchHalfW + lineHalfW + 0.11f) * side, 3.7f, 2.5f));
  goal2.SetNormals(Vector3(-side, 0, 0));

  // match->SetDebugPilon(Vector3(55 * side, 3.66, 2.44));
  // match->SetDebugPilon2(line.GetVertex(1));

  Vector3 intersectVec;
  bool intersect = goal1.IntersectsLine(line, intersectVec);
  if (!intersect) {
    intersect = goal2.IntersectsLine(line, intersectVec);
  }
  // extra check: ball could have gone 'in' via the side netting, if line begin == inside pitch, but
  // outside of post, and line end == in goal. disallow!
  if (fabs(previousBallPos.coords[1]) > 3.7 &&
      fabs(previousBallPos.coords[0]) > pitchHalfW - lineHalfW - 0.11)
    return false;

  if (intersect)
    return true;
  else
    return false;
}

void Match::CalculateBestPossessionTeamID() {
  if (GetBallRetainer() != 0) {
    int retainTeamID = GetBallRetainer()->GetTeamID();
    bestPossessionTeamID = retainTeamID;
  } else {
    int bestTime_ms[2] = {100000, 100000};
    for (int teamID = 0; teamID < 2; teamID++) {
      bestTime_ms[teamID] = teams[teamID]->GetTimeNeededToGetToBall_ms();
    }

    if (bestTime_ms[0] < bestTime_ms[1])
      bestPossessionTeamID = 0;
    else if (bestTime_ms[0] > bestTime_ms[1])
      bestPossessionTeamID = 1;
    else if (bestTime_ms[0] == bestTime_ms[1])
      bestPossessionTeamID = -1;
  }
}

void Match::CheckHumanoidCollisions() {
  std::vector<Player*> players;

  GetTeam(0)->GetActivePlayers(players);
  GetTeam(1)->GetActivePlayers(players);

  // outer vectors index == players[] index
  std::vector<std::vector<PlayerBounce>> playerBounces;

  // insert an empty entry for every player
  for (unsigned int i1 = 0; i1 < players.size(); i1++) {
    std::vector<PlayerBounce> bounce;
    playerBounces.push_back(bounce);
  }

  // check each combination of humanoids once
  for (unsigned int i1 = 0; i1 < players.size() - 1; i1++) {
    for (unsigned int i2 = i1 + 1; i2 < players.size(); i2++) {
      Vector3 tripVec1, tripVec2;
      CheckHumanoidCollision(players.at(i1), players.at(i2), playerBounces.at(i1),
                             playerBounces.at(i2));
    }
  }

  // do bouncy magic
  for (unsigned int i1 = 0; i1 < players.size(); i1++) {
    float totalForce = 0.0f;

    // if (playerBounces.at(i1).size() > 0) printf("  player %i.. ", players.at(i1)->GetID());

    for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
      const PlayerBounce& bounce = playerBounces.at(i1).at(i2);
      totalForce += bounce.force;
    }

    if (totalForce > 0.0f) {
      // if (playerBounces.at(i1).size() > 0) printf("%f, %f; ", totalBias, multiplier);

      Vector3 bounceVec;
      for (unsigned int i2 = 0; i2 < playerBounces.at(i1).size(); i2++) {
        const PlayerBounce& bounce = playerBounces.at(i1).at(i2);
        bounceVec += (bounce.opp->GetMovement() - players.at(i1)->GetMovement()) * bounce.force *
                     (bounce.force / totalForce);

        // //SetGreenDebugPilon(bounce.opp->GetPosition() + bounce.opp->GetMovement());
        // if (players.at(i1)->GetTeamID() == 0) {
        //   SetRedDebugPilon(players.at(i1)->GetPosition() + players.at(i1)->GetMovement());
        // } else {
        //   SetGreenDebugPilon(players.at(i1)->GetPosition() + players.at(i1)->GetMovement());
        // }
      }

      // if (playerBounces.at(i1).size() > 0) printf("\n");
      //  okay, accumulated all, now distribute them in normalized fashion
      players.at(i1)->OffsetPosition(bounceVec * 0.01f * 1.0f);
      // printf("moving player %i\n", i1);
    }
  }
}

void Match::CheckHumanoidCollision(Player* p1, Player* p2, std::vector<PlayerBounce>& p1Bounce,
                                   std::vector<PlayerBounce>& p2Bounce) {
  float distanceFactor = 0.72f;
  float bouncePlayerRadius = 0.5f * distanceFactor;
  float similarPlayerRadius = 0.8f * distanceFactor;
  float similarExp = 0.2f;           // 0.8f;
  float similarForceFactor = 0.25f;  // 0.5f would be the full effect

  Vector3 p1pos = p1->GetPosition();
  Vector3 p2pos = p2->GetPosition();

  float distance = (p1pos - p2pos).GetLength();

  Vector3 p1movement = p1->GetMovement();
  Vector3 p2movement = p2->GetMovement();
  assert(p1movement.coords[2] == 0.0f);
  assert(p2movement.coords[2] == 0.0f);

  float bounceBias = 0.0f;
  Vector3 bounceVec;
  float p1backFacing = 0.5f;
  float p2backFacing = 0.5f;

  if (distance < bouncePlayerRadius * 2.0f ||
      distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
    bounceVec = (p1pos - p2pos).GetNormalized(Vector3(0, -1, 0));

    /*
        // skew a bit so the bounce is mostly sideways from possession player - this way, players
       are more likely to walk side by side in ball battles instead of behind each other if (p1 ==
       GetDesignatedPossessionPlayer() || p2 == GetDesignatedPossessionPlayer()) {
          //radian angle = GetDesignatedPossessionPlayer()->GetDirectionVec().GetAngle2D();
          radian angle =
       GetBall()->GetMovement().Get2D().GetNormalized(GetDesignatedPossessionPlayer()->GetDirectionVec()).GetAngle2D();
          bounceVec.Rotate2D(-angle);
          bounceVec.coords[0] *= 0.5f;
          bounceVec.Rotate2D(angle);
          bounceVec.Normalize();
        }
    */

    // back facing
    Vector3 p1facing = p1->GetDirectionVec().GetRotated2D(p1->GetRelBodyAngle() * 0.7f);
    Vector3 p2facing = p2->GetDirectionVec().GetRotated2D(p2->GetRelBodyAngle() * 0.7f);
    p1backFacing = clamp(p1facing.GetDotProduct(bounceVec) * 0.5f + 0.5f, 0.0f,
                         1.0f);  // 0 .. 1 == worst .. best
    p2backFacing = clamp(p2facing.GetDotProduct(-bounceVec) * 0.5f + 0.5f, 0.0f, 1.0f);

    if (distance < bouncePlayerRadius * 2.0f) {
      bounceBias += p1backFacing * 0.8f;
      bounceBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      bounceBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if (p1->TouchPending() && p1->GetCurrentFunctionType() == e_FunctionType_Interfere)
        bounceBias += 0.1f + 0.4f * p1->GetStat("technical_standingtackle");
      if (p1->TouchPending() && p1->GetCurrentFunctionType() == e_FunctionType_Sliding)
        bounceBias += 0.1f + 0.4f * p1->GetStat("technical_slidingtackle");
      if (p2->TouchPending() && p2->GetCurrentFunctionType() == e_FunctionType_Interfere)
        bounceBias -= 0.1f + 0.4f * p2->GetStat("technical_standingtackle");
      if (p2->TouchPending() && p2->GetCurrentFunctionType() == e_FunctionType_Sliding)
        bounceBias -= 0.1f + 0.4f * p2->GetStat("technical_slidingtackle");

      // problem is, once possession is lost (usually directly after ball is touched), bias may turn
      // around the other way. (well, maybe that's not a problem. dunno.) if (p1->HasPossession() ==
      // true) bounceBias -= 0.3f; if (p2->HasPossession() == true) bounceBias += 0.3f;

      if (p1 == GetDesignatedPossessionPlayer())
        bounceBias += 0.4f;
      if (p2 == GetDesignatedPossessionPlayer())
        bounceBias -= 0.4f;

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor =
            clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) *
            1.0f;  // std::min is cap so difference won't matter if ball is far away (so only used
                   // in battles about the ball)
        bounceBias += ballDistanceDiffFactor;
      }

      bounceBias += p1->GetStat("physical_balance") * 1.0f;
      bounceBias -= p2->GetStat("physical_balance") * 1.0f;

      bounceBias = clamp(bounceBias, -1.0f, 1.0f);
      bounceBias *= 0.5f;

      // convert bounceBias to 0 .. 1 instead of -1 .. 1
      float bounceBias0to1 = bounceBias * 0.5f + 0.5f;
      // bounceBias0to1 = curve(bounceBias0to1, 0.5f); // more binary

      Vector3 offset1 = (p1pos - p2pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) *
                        (1.0f - bounceBias0to1) * 2.0f;
      Vector3 offset2 = (p2pos - p1pos).GetNormalized(0) * (bouncePlayerRadius - distance * 0.5f) *
                        bounceBias0to1 * 2.0f;

      // slow down on contact
      /*
      Vector3 averageMomentum = (p1movement + p2movement) * 0.5f;
      offset1 -= averageMomentum * 0.001f;
      offset2 -= averageMomentum * 0.001f;
      */

      // make players snap to the side of opponents (rather, just a bit in front of them too)
      // todo: make less binary, and more based on stats. maybe make this whole push/pull thing a
      // separate system?

      if (GetDesignatedPossessionPlayer() == p2 && p2->HasPossession()) {
        Vector3 p2_leftside =
            p2pos + p2->GetDirectionVec().GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p2_rightside =
            p2pos + p2->GetDirectionVec().GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p1_to_p2_left = (p1pos - p2_leftside).GetLength();
        float p1_to_p2_right = (p1pos - p2_rightside).GetLength();
        Vector3 p2side = p1_to_p2_left < p1_to_p2_right ? p2_leftside : p2_rightside;
        // SetYellowDebugPilon(p2side);
        offset1 +=
            (p2side - p1pos).GetNormalizedMax(0.01f) * p1->GetStat("physical_balance") * 0.3f;
      }

      else if (GetDesignatedPossessionPlayer() == p1 && p1->HasPossession()) {
        Vector3 p1_leftside =
            p1pos + p1->GetDirectionVec().GetRotated2D(0.3f * pi) * bouncePlayerRadius * 2;
        Vector3 p1_rightside =
            p1pos + p1->GetDirectionVec().GetRotated2D(-0.3f * pi) * bouncePlayerRadius * 2;
        float p2_to_p1_left = (p2pos - p1_leftside).GetLength();
        float p2_to_p1_right = (p2pos - p1_rightside).GetLength();
        Vector3 p1side = p2_to_p1_left < p2_to_p1_right ? p1_leftside : p1_rightside;
        // SetRedDebugPilon(p1side);
        offset2 +=
            (p1side - p2pos).GetNormalizedMax(0.01f) * p2->GetStat("physical_balance") * 0.3f;
      }

      // can not bump faster than sprint
      offset1.NormalizeMax(sprintVelocity * 0.01f);
      offset2.NormalizeMax(sprintVelocity * 0.01f);

      p1->OffsetPosition(offset1);
      p2->OffsetPosition(offset2);
    }

    // take over each others movement a bit (precalc phase)

    float similarBias = 0.0f;

    if (similarForceFactor > 0.0f && distance < (bouncePlayerRadius + similarPlayerRadius) * 2.0f) {
      float shellDistance = std::max(0.0f, distance - bouncePlayerRadius * 2.0f);

      bool verbose = false;
      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("similarbias: ");

      similarBias += p1backFacing * 0.8f;
      similarBias -= p2backFacing * 0.8f;

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      similarBias -= clamp(((p1velocity - p2velocity) / sprintVelocity) * 0.2f, -0.2f, 0.2f);

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("backfacing: %f; ", similarBias);

      if (p1 == GetDesignatedPossessionPlayer())
        similarBias += 0.6f;
      if (p2 == GetDesignatedPossessionPlayer())
        similarBias -= 0.6f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("designated: %f; ", similarBias);

      // closest to ball
      if (p1 == p1->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
          p2 == p2->GetTeam()->GetDesignatedTeamPossessionPlayer()) {
        float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
        float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
        float ballDistanceDiffFactor =
            clamp(std::min(p2BallDistance, 1.2f) - std::min(p1BallDistance, 1.2f), -0.6f, 0.6f) *
            1.0f;  // std::min is cap so difference won't matter if ball is far away (so only used
                   // in battles about the ball)
        similarBias += ballDistanceDiffFactor;
      }

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("ball closeness: %f; ", similarBias);

      similarBias += p1->GetStat("physical_balance") * 1.0f;
      similarBias -= p2->GetStat("physical_balance") * 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("balance stat: %f; ", similarBias);

      similarBias = clamp(similarBias, -1.0f, 1.0f);
      similarBias *= 0.9f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("result: %f\n", similarBias);

      float similarForce = clamp(1.0f - (shellDistance / (similarPlayerRadius * 2.0f)), 0.0f, 1.0f);
      similarForce = pow(similarForce, similarExp);
      similarForce *= similarForceFactor;

      assert(similarForce >= 0.0f && similarForce <= 1.0f);

      float similarBias0to1 = similarBias * 0.5f + 0.5f;

      PlayerBounce player1Bounce;
      player1Bounce.opp = p2;
      player1Bounce.force = similarForce * (1.0f - similarBias0to1);
      p1Bounce.push_back(player1Bounce);

      PlayerBounce player2Bounce;
      player2Bounce.opp = p1;
      player2Bounce.force = similarForce * similarBias0to1;
      p2Bounce.push_back(player2Bounce);
    }

    // u b trippin?

    if (distance < bouncePlayerRadius * 2.0f) {
      bool verbose = false;
      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("trip sensitivity: ");

      float p1sensitivity = 0.0f;
      float p2sensitivity = 0.0f;

      p1sensitivity += (1.0f - p1backFacing) * 1.0f;
      p2sensitivity += (1.0f - p2backFacing) * 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("backfacing: %f - %f; ", p1sensitivity, p2sensitivity);

      // velocity, faster is worse
      float p1velocity = p1->GetFloatVelocity();
      float p2velocity = p2->GetFloatVelocity();
      p1sensitivity += NormalizedClamp(p1velocity, idleVelocity, sprintVelocity) * 1.0f;
      p2sensitivity += NormalizedClamp(p2velocity, idleVelocity, sprintVelocity) * 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("velocity: %f - %f; ", p1sensitivity, p2sensitivity);

      if (p1->HasBestPossession() == true)
        p1sensitivity += 1.0f;
      if (p2->HasBestPossession() == true)
        p2sensitivity += 1.0f;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("haspossession: %f - %f; ", p1sensitivity, p2sensitivity);

      float balanceWeight = 3.0f;
      p1sensitivity += (1.0f - p1->GetStat("physical_balance") * 1.0f) * balanceWeight;
      p2sensitivity += (1.0f - p2->GetStat("physical_balance") * 1.0f) * balanceWeight;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("balance: %f - %f; ", p1sensitivity, p2sensitivity);

      p1sensitivity += clamp(p1->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);
      p2sensitivity += clamp(p2->GetDecayingPositionOffsetLength() * 10.0f, 0.0f, 1.0f);

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("decposoffset: %f - %f", p1sensitivity, p2sensitivity);

      // penetration
      float penetrationWeight = 6.0f;
      float penetration = ((p1->GetPosition() + p1->GetMovement() * 0.03f) -
                           (p2->GetPosition() + p2->GetMovement() * 0.03f))
                              .GetLength();
      // if (p1->GetDebug() || p2->GetDebug()) printf("penetration: %f\n", pow(1.0f -
      // NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f));
      p1sensitivity +=
          pow(1.0f - NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f) *
          penetrationWeight;
      p2sensitivity +=
          pow(1.0f - NormalizedClamp(penetration, 0.0f, bouncePlayerRadius * 2.0f), 0.4f) *
          penetrationWeight;

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("penetration: %f - %f\n", p1sensitivity, p2sensitivity);

      // ball proximity (usually means: stability is less because we sacrifice balance to control
      // the ball)
      float p1BallDistance = (GetBall()->Predict(10).Get2D() - p1->GetPosition()).GetLength();
      float p2BallDistance = (GetBall()->Predict(10).Get2D() - p2->GetPosition()).GetLength();
      p1sensitivity += 1.0f - NormalizedClamp(p1BallDistance, 0.0f, 0.7f);
      p2sensitivity += 1.0f - NormalizedClamp(p2BallDistance, 0.0f, 0.7f);

      if ((p1->GetDebug() || p2->GetDebug()) && verbose)
        printf("ball proximity: %f - %f\n", p1sensitivity, p2sensitivity);

      // divided by elements active
      p1sensitivity /= 5.0f + balanceWeight + penetrationWeight;
      p2sensitivity /= 5.0f + balanceWeight + penetrationWeight;

      float trip0threshold = 0.38f;
      float trip1threshold = 0.48f;
      float trip2threshold = 0.58f;

      if (p1sensitivity > trip0threshold) {
        int tripType = 0;
        if (p1sensitivity > trip1threshold)
          tripType = 1;
        if (p1sensitivity > trip2threshold)
          tripType = 2;
        if (tripType > 0) {
          p1->TripMe((p1->GetMovement() * 0.1f + p2->GetMovement() * 0.06f + bounceVec * 1.0f)
                         .GetNormalized(bounceVec),
                     tripType);
          referee->TripNotice(p1, p2, tripType);
          // foul statistics are counted by the referee at the whistle
          const bool p1WasFit = p1->GetInjuryLevel() < AIManager::substitutionInjuryLevel;
          p1->Injure(tripType * 0.04f);
          if (p1WasFit && p1->GetInjuryLevel() >= AIManager::substitutionInjuryLevel)
            AddLostTime(MatchProgression::e_Stoppage_Injury);
          AddExcitementBoost(0.5f + tripType * 0.1f, 3000);
        }
      }
      if (p2sensitivity > trip0threshold) {
        int tripType = 0;
        if (p2sensitivity > trip1threshold)
          tripType = 1;
        if (p2sensitivity > trip2threshold)
          tripType = 2;
        if (tripType > 0) {
          p2->TripMe((p2->GetMovement() * 0.1f + p1->GetMovement() * 0.06f - bounceVec * 1.0f)
                         .GetNormalized(-bounceVec),
                     tripType);
          referee->TripNotice(p2, p1, tripType);
          // foul statistics are counted by the referee at the whistle
          const bool p2WasFit = p2->GetInjuryLevel() < AIManager::substitutionInjuryLevel;
          p2->Injure(tripType * 0.04f);
          if (p2WasFit && p2->GetInjuryLevel() >= AIManager::substitutionInjuryLevel)
            AddLostTime(MatchProgression::e_Stoppage_Injury);
          AddExcitementBoost(0.5f + tripType * 0.1f, 3000);
        }
      }

    }  // within either bump, similar or trip range
  }

  // check for tackling collisions

  int tackle = 0;
  if ((p1->GetCurrentFunctionType() == e_FunctionType_Sliding ||
       p1->GetCurrentFunctionType() == e_FunctionType_Interfere) &&
      p1->GetFrameNum() > 5 && p1->GetFrameNum() < 28)
    tackle += 1;
  if ((p2->GetCurrentFunctionType() == e_FunctionType_Sliding ||
       p2->GetCurrentFunctionType() == e_FunctionType_Interfere) &&
      p2->GetFrameNum() > 5 && p2->GetFrameNum() < 28)
    tackle += 2;
  if (distance < 2.0f && tackle > 0 && tackle < 3) {  // if tackle is 3, ignore both
    std::list<boost::intrusive_ptr<Geometry>> tacklerObjectList;
    std::list<boost::intrusive_ptr<Geometry>> victimObjectList;
    /*
    if (tackle == 0) { // todo: this way, p1 would have an advantage
      if (p1->GetCurrentFunctionType() == e_FunctionType_Trap ||
          p1->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_LongPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_HighPass ||
          p1->GetCurrentFunctionType() == e_FunctionType_Shot ||
          p1->GetCurrentFunctionType() == e_FunctionType_Interfere) {
        p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
        p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
        p1action = true;
      }
      else if (p2->GetCurrentFunctionType() == e_FunctionType_Trap ||
               p2->GetCurrentFunctionType() == e_FunctionType_ShortPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_LongPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_HighPass ||
               p2->GetCurrentFunctionType() == e_FunctionType_Shot ||
               p2->GetCurrentFunctionType() == e_FunctionType_Interfere) {
        p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
        p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
        p2action = true;
      }
    }
    */
    if (tackle == 1) {
      p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
      p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
    }
    if (tackle == 2) {
      p2->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, tacklerObjectList);
      p1->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, victimObjectList);
    }

    // iterate through all body parts of tackler
    std::list<boost::intrusive_ptr<Geometry>>::iterator objIter = tacklerObjectList.begin();
    while (objIter != tacklerObjectList.end()) {
      AABB objAABB = (*objIter)->GetAABB();

      // make a tad smaller: AABBs are usually too large.
      objAABB.minxyz += 0.1f;
      objAABB.maxxyz -= 0.1f;

      std::list<boost::intrusive_ptr<Geometry>>::iterator victimIter = victimObjectList.begin();
      while (victimIter != victimObjectList.end()) {
        std::string bodyPartName = (*victimIter)->GetName();
        if (bodyPartName == "left_foot" || bodyPartName == "right_foot" ||
            bodyPartName == "left_lowerleg" || bodyPartName == "right_lowerleg"
            /*bodyPartName == "left_upperleg" || bodyPartName == "right_upperleg"*/) {
          if (objAABB.Intersects((*victimIter)->GetAABB())) {
            // printf("HIT: %s hits %s\n", (*objIter)->GetName().c_str(),
            // (*victimIter)->GetName().c_str());

            if (tackle == 1) {
              if (p1->GetFrameNum() > 10 && p1->GetFrameNum() < p1->GetFrameCount() - 6) {
                Vector3 tripVec = p2->GetDirectionVec();
                int tripType = 3;  // sliding
                if (p1->GetCurrentFunctionType() == e_FunctionType_Interfere)
                  tripType = 1;  // was 2
                p2->TripMe(tripVec, tripType);
                referee->TripNotice(p2, p1, tripType);
                AddExcitementBoost(tripType == 3 ? 0.55f : 0.35f, tripType == 3 ? 2200 : 1500);
              }
            }
            if (tackle == 2) {
              if (p2->GetFrameNum() > 10 && p2->GetFrameNum() < p2->GetFrameCount() - 6) {
                Vector3 tripVec = p1->GetDirectionVec();
                int tripType = 3;  // sliding
                if (p2->GetCurrentFunctionType() == e_FunctionType_Interfere)
                  tripType = 1;  // was 2
                p1->TripMe(tripVec, tripType);
                referee->TripNotice(p1, p2, tripType);
                AddExcitementBoost(tripType == 3 ? 0.55f : 0.35f, tripType == 3 ? 2200 : 1500);
              }
            }
            break;
          }
        }

        victimIter++;
      }

      objIter++;
    }
  }
}

void Match::CheckBallCollisions() {
  // todo: rewrite this function, this SHIT is UNREADABLE!!!111 olololololo

  // printf("%i - %i hihi\n", actualTime_ms, lastBodyBallCollisionTime_ms + 150);
  if (actualTime_ms <= lastBodyBallCollisionTime_ms + 150)
    return;

  std::vector<Player*> players;
  GetTeam(0)->GetActivePlayers(players);
  GetTeam(1)->GetActivePlayers(players);

  std::list<boost::intrusive_ptr<Geometry>> objectList;
  Vector3 bounceVec;
  float bias = 0.0;
  int bounceCount =
      0;  // this shit is shit, average properly in combination with bias or something like that
  float tackleBallExcitement = 0.0f;

  // printf("lasttouchbias: %f, isnul?: %s\n", GetLastTouchBias(200), GetLastTouchBias(200) == 0.0f
  // ? "true" : "false");
  for (int i = 0; i < (signed int)players.size(); i++) {
    // A keeper who has been beaten does not block the ball with his body.
    if (players[i]->IsBeatenKeeper())
      continue;

    bool biggestRatio = false;
    int teamID = players[i]->GetTeam()->GetID();

    int touchTimeThreshold_ms = 200;  // 700;
    float oppLastTouchBias = GetTeam(abs(teamID - 1))->GetLastTouchBias(touchTimeThreshold_ms);
    float lastTouchBias = players[i]->GetLastTouchBias(touchTimeThreshold_ms);
    float oppLastTouchBiasLong = GetTeam(abs(teamID - 1))->GetLastTouchBias(1600);

    if (lastTouchBias <= 0.01f &&
        oppLastTouchBias >
            0.01f /* && ballTowardsPlayer*/) {  // cannot collide if opp didn't recently touch ball
                                                // (we would be able to predict ball by then), or if
                                                // player itself already did (to overcome the
                                                // 'perpetuum collision' problem, and to allow for
                                                // 'controlled ball collisions' in humanoid class)

      bool collisionAnim = false;
      if (players[i]->GetCurrentFunctionType() == e_FunctionType_Movement ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Trip ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Sliding ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect)
        collisionAnim = true;
      bool onlyWhenDirectionChangedUnexpectedly = false;
      if (players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere ||
          players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect)
        onlyWhenDirectionChangedUnexpectedly = true;

      bool directionChangedUnexpectedly = false;
      if (onlyWhenDirectionChangedUnexpectedly) {
        float unexpectedDistance =
            (GetMentalImage(players[i]->GetController()->GetReactionTime_ms() +
                            players[i]->GetFrameNum() * 10)
                 ->GetBallPrediction(1000) -
             GetBall()->Predict(1000))
                .GetLength();  // mental image from when the anim began
        if (unexpectedDistance > 0.5f)
          directionChangedUnexpectedly = true;
      }
      // todo: this is the temp workaround version
      // if (onlyWhenDirectionChangedUnexpectedly) directionChangedUnexpectedly = true;

      if (Verbose())
        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect) {
          printf("onlyWhenDirectionChangedUnexpectedly: %i, directionChangedUnexpectedly: %i\n",
                 onlyWhenDirectionChangedUnexpectedly, directionChangedUnexpectedly);
        }

      if (collisionAnim && !players[i]->HasUniquePossession() &&
          (onlyWhenDirectionChangedUnexpectedly == directionChangedUnexpectedly)) {
        float boundingBoxSizeOffset =
            -0.1f;  // fake a big AABB for more blocking fun, or a small one for less bouncy bounce
        if (!players[i]->HasPossession())
          boundingBoxSizeOffset += 0.03f;
        else
          boundingBoxSizeOffset -= 0.03f;

        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Sliding ||
            players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere) {
          boundingBoxSizeOffset += 0.1f;
        }
        if (players[i]->GetCurrentFunctionType() == e_FunctionType_Deflect) {
          boundingBoxSizeOffset += 0.2f;
        }
        // A target man shields the ball with his body while holding position
        // (proposal 3A).
        boundingBoxSizeOffset += PlayerTraits::GetShieldingRadiusBonus(
            players[i]->GetPlayerData()->GetTraits(), players[i]->GetMovement().GetLength() < 1.0f);

        if (((players[i]->GetPosition() + Vector3(0, 0, 0.8f)) - ball->Predict(0)).GetLength() <
            2.5f) {  // premature optimization is the root of all evil :D
          players[i]->GetHumanoidNode()->GetObjects(e_ObjectType_Geometry, objectList);

          std::list<boost::intrusive_ptr<Geometry>>::iterator objIter = objectList.begin();
          while (objIter != objectList.end()) {
            AABB objAABB = (*objIter)->GetAABB();
            float ballRadius = 0.11f + boundingBoxSizeOffset;
            if (objAABB.Intersects(ball->Predict(0), ballRadius)) {
              if (Verbose())
                printf("HIT: %s\n", (*objIter)->GetName().c_str());

              if (players[i] == players[i]->GetTeam()->GetDesignatedTeamPossessionPlayer() &&
                  GetLastTouchBias(200) < 0.01f) {  // todo: use reaction time stat

                players[i]->TriggerControlledBallCollision();

              } else {
                // todonow: average bouncevec and bias together per hit
                float movementBias = oppLastTouchBias * 0.8f + 0.2f;
                bounceVec += (ball->Predict(0) - (*objIter)->GetDerivedPosition())
                                     .GetNormalized(Vector3(0)) *
                                 movementBias +
                             players[i]->GetMovement() * (1.0f - movementBias);
                bounceCount++;
                players[i]->GetTeam()->SetLastTouchPlayer(players[i], e_TouchType_Accidental);
                if (players[i]->GetCurrentFunctionType() == e_FunctionType_Sliding)
                  tackleBallExcitement = std::max(tackleBallExcitement, 0.35f);
                else if (players[i]->GetCurrentFunctionType() == e_FunctionType_Interfere)
                  tackleBallExcitement = std::max(tackleBallExcitement, 0.22f);
                Vector3 aabbCenter;
                objAABB.GetCenter(aabbCenter);
                bias += (1.0f - clamp(((ball->Predict(0) - aabbCenter).GetLength() - ballRadius) /
                                          objAABB.GetRadius(),
                                      0.0f, 1.0f)) *
                            0.9f +
                        0.1f;
              }
            }

            objIter++;
          }
        }
      }
    }
  }

  if (bias > 0.0f) {
    bounceVec /= (bounceCount * 1.0f);
    bounceVec.coords[2] *= 0.6f;
    bounceVec.Normalize();
    Vector3 currentMovement = ball->GetMovement();
    Vector3 fullCollisionVec = (bounceVec * 6.0f) +
                               (bounceVec * currentMovement.GetLength() * 0.6f) +
                               (currentMovement * -0.2f);
    bias = clamp(bias, 0.0f, 1.0f);
    bias = bias * 0.5f + 0.5f;
    Vector3 resultVector = fullCollisionVec * bias + currentMovement * (1.0f - bias);
    if (resultVector.GetLength() > currentMovement.GetLength())
      resultVector = resultVector.GetNormalized(0) * currentMovement.GetLength();
    // resultVector = resultVector.GetNormalized(0) * (currentMovement.GetLength() * 0.7f +
    // resultVector.GetLength() * 0.3f); // EXPERIMENT!
    resultVector *= 0.7f;

    ball->Touch(resultVector);
    ball->SetRotation(random(-30, 30), random(-30, 30), random(-30, 30), 0.5f * bias);
    ball->TriggerBallTouchSound(pow(NormalizedClamp(resultVector.GetLength(), 4.0f, 40.0f), 0.7f));

    if (tackleBallExcitement > 0.0f)
      AddExcitementBoost(tackleBallExcitement, 1400);

    lastBodyBallCollisionTime_ms = actualTime_ms;
  }
}

void Match::FollowCamera(Quaternion& orientation, Quaternion& nodeOrientation, Vector3& position,
                         float& FOV, const Vector3& targetPosition, float zoom) {
  orientation.SetAngleAxis(0.4f * pi, Vector3(1, 0, 0));
  nodeOrientation.SetAngleAxis(targetPosition.GetAngle2D() + 1.5 * pi, Vector3(0, 0, 1));
  position = targetPosition -
             targetPosition.Get2D().GetNormalized(Vector3(0, -1, 0)) * 10 * (1.0f / zoom) +
             Vector3(0, 0, 3);
  FOV = 60.0f;
}

void Match::SetReplayCamera(int camType, const Vector3& target, float modifierValue) {
  switch (camType) {
    // default wide view
    case 0: {
      float zoom = 1.0f + modifierValue * 0.5f;
      cameraFOV = 30 * zoom;
      cameraNodePosition = Vector3(target.coords[0], target.coords[1] - 50.0f, 20.0f);
      cameraOrientation.SetAngleAxis(0.37f * pi, Vector3(1, 0, 0));
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraNearCap = 20.0f;
      cameraFarCap = 250.0f;
    } break;

    // behind goal
    case 1: {
      signed int side = -1;
      if (target.coords[0] > 0)
        side = 1;
      float zoom = 1.0f + modifierValue * 0.5f;
      cameraNodePosition = Vector3(70 * side, -30, 20);
      float targetDist = clamp((cameraNodePosition - target).GetLength() / 100.0f, 0.2f, 1.0f);
      cameraFOV = (56 - targetDist * 50) * zoom;
      cameraOrientation.SetAngleAxis((0.3f + targetDist * 0.15f) * pi, Vector3(1, 0, 0));
      cameraNodeOrientation.SetAngleAxis((target - cameraNodePosition).GetAngle2D() + 1.5f * pi,
                                         Vector3(0, 0, 1));
      cameraNearCap = 20.0;
      cameraFarCap = 250.0;
    } break;

    // close, rotateable
    case 2: {
      radian rot = modifierValue * pi;
      cameraNodePosition = target + Vector3(sin(rot), cos(rot), 0.18f) * 10;
      cameraFOV = 30;
      cameraOrientation.SetAngleAxis(0.45f * pi, Vector3(1, 0, 0));
      cameraNodeOrientation.SetAngleAxis((target - cameraNodePosition).GetAngle2D() + 1.5f * pi,
                                         Vector3(0, 0, 1));
      cameraNearCap = 1.0;
      cameraFarCap = 250.0;
    } break;

    // birds-eye
    case 3: {
      cameraNodePosition = target + Vector3(0, 0, 40 + modifierValue * 20);
      cameraFOV = 30;
      cameraOrientation = QUATERNION_IDENTITY;
      cameraNodeOrientation = QUATERNION_IDENTITY;
      cameraNearCap = 10.0;
      cameraFarCap = 100.0;
    } break;

    default:
      break;
  }
}

int Match::GetReplaySize_ms() {
  return replaySize_ms;
}

int Match::GetReplayCamCount() {
  return 4;
}

void Match::ProcessReplayMessages() {
  replayState.Lock();
  if (replayState->dirty) {
    // printf("dirty replay state\n");
    ApplyReplayFrame(replayState->viewTime_ms);
    Vector3 replayTarget = GetBall()->GetBallGeom()->GetDerivedPosition();
    SetReplayCamera(replayState->cam, replayTarget, replayState->modifierValue);
    replayState->dirty = false;
  }
  replayState.Unlock();
}

void Match::PrepareGoalNetting() {
  // collect vertices into nettingMeshes[0..1]
  std::vector<MaterializedTriangleMesh>& triangleMesh =
      boost::static_pointer_cast<Geometry>(goalsNode->GetObject("goals"))
          ->GetGeometryData()
          ->GetResource()
          ->GetTriangleMeshesRef();

  // PES's own net pattern, if it has been imported. It is swapped in here rather
  // than written over the engine's goalnetting.png, because that file is part of
  // the repository and nothing PES-derived belongs in it
  // (tools/pes21_import/import_common_stadium_assets.py --net).
  {
    const std::string netting = GetConfiguration()->Get(
        "goal_netting_texture", "media/textures/stadium/goalnetting_pes.png");
    if (!netting.empty() && std::filesystem::exists(netting)) {
      bool alreadyThere = false;
      boost::intrusive_ptr<Resource<Surface>> surface =
          ResourceManagerPool::GetInstance()
              .GetManager<Surface>(e_ResourceType_Surface)
              ->Fetch(netting, true, alreadyThere, true);
      if (surface) {
        int swapped = 0;
        for (unsigned int m = 0; m < triangleMesh.size(); m++) {
          if (!triangleMesh.at(m).material.diffuseTexture) continue;
          const std::string ident = triangleMesh.at(m).material.diffuseTexture->GetIdentString();
          if (ident.find("goalnetting") == std::string::npos) continue;
          triangleMesh.at(m).material.diffuseTexture = surface;
          swapped++;
        }
        if (swapped)
          Log(e_Notice, "Match", "PrepareGoalNetting",
              "using " + netting + " for " + int_to_str(swapped) + " netting mesh(es)");
      }
    }
  }

  for (unsigned int m = 0; m < triangleMesh.size(); m++) {
    for (int i = 0; i < triangleMesh.at(m).verticesDataSize / GetTriangleMeshElementCount();
         i += 3) {
      int goalID = -1;
      if (triangleMesh.at(m).vertices[i + 0] < -pitchHalfW - 0.06f)
        goalID = 0;  // don't catch woodwork, only netting.. DIRTY HAXX
      if (triangleMesh.at(m).vertices[i + 0] > pitchHalfW + 0.06f)
        goalID = 1;
      if (goalID >= 0) {
        nettingMeshesSrc[goalID].push_back(Vector3(triangleMesh.at(m).vertices[i + 0],
                                                   triangleMesh.at(m).vertices[i + 1],
                                                   triangleMesh.at(m).vertices[i + 2]));
        nettingMeshes[goalID].push_back(&(triangleMesh.at(m).vertices[i]));
      }
    }
  }
}

void Match::UpdateGoalNetting(bool ballTouchesNet) {
  nettingHasChanged = false;
  int sideID = (ball->GetBallGeom()->GetPosition().coords[0] < 0) ? 0 : 1;
  if (ballTouchesNet) {
    // find vertex closest to ball
    float shortestDistance = 100000.0f;
    // int shortestDistanceID = 0;
    for (unsigned int i = 0; i < nettingMeshes[sideID].size(); i++) {
      Vector3 vertex = nettingMeshesSrc[sideID][i];
      float distance = vertex.GetDistance(ball->GetBallGeom()->GetPosition());
      if (distance < shortestDistance) {
        shortestDistance = distance;
        // shortestDistanceID = i;
      }
    }

    // pull vertices towards ball - the closer, the more intense
    for (unsigned int i = 0; i < nettingMeshes[sideID].size(); i++) {
      const Vector3& vertex = nettingMeshesSrc[sideID][i];
      float falloffDistance = 4.0f;
      // float influenceBias = clamp(1.0f - (vertex.GetDistance(ball->GetBallGeom()->GetPosition())
      // - shortestDistance) / falloffDistance, 0.0f, 1.0f);
      float influenceBias =
          pow(clamp((shortestDistance + 0.0001f) /
                        (vertex.GetDistance(ball->GetBallGeom()->GetPosition()) + 0.0001f),
                    0.0f, 1.0f),
              1.5f);
      // net is stuck to woodwork so lay off there
      float woodworkTensionBiasInv = clamp(
          (fabs(ball->GetBallGeom()->GetPosition().coords[0]) - pitchHalfW) * 2.0f, 0.0f, 1.0f);
      influenceBias *= woodworkTensionBiasInv;
      // http://www.wolframalpha.com/input/?i=sin%28x+*+pi+-+0.5+*+pi%29+*+0.5+%2B+0.5+from+x+%3D+0+to+1
      influenceBias = sin(influenceBias * pi - 0.5f * pi) * 0.5f + 0.5f;
      if (influenceBias > 0.0f) {
        Vector3 result =
            vertex * (1.0f - influenceBias) + ball->GetBallGeom()->GetPosition() * influenceBias;
        static_cast<float*>(nettingMeshes[sideID][i])[0] = result.coords[0];
        static_cast<float*>(nettingMeshes[sideID][i])[1] = result.coords[1];
        static_cast<float*>(nettingMeshes[sideID][i])[2] = result.coords[2];
      }
    }
    resetNetting = true;  // make sure to reset next time
    nettingHasChanged = true;

  } else if (resetNetting) {  // ball doesn't touch net (anymore), reset
    for (int sideID = 0; sideID < 2; sideID++) {
      for (unsigned int i = 0; i < nettingMeshes[sideID].size(); i++) {
        static_cast<float*>(nettingMeshes[sideID][i])[0] = nettingMeshesSrc[sideID][i].coords[0];
        static_cast<float*>(nettingMeshes[sideID][i])[1] = nettingMeshesSrc[sideID][i].coords[1];
        static_cast<float*>(nettingMeshes[sideID][i])[2] = nettingMeshesSrc[sideID][i].coords[2];
      }
    }
    resetNetting = false;
    nettingHasChanged = true;
  }
}

void Match::UploadGoalNetting() {
  if (nettingHasChanged) {
    boost::static_pointer_cast<Geometry>(goalsNode->GetObject("goals"))
        ->OnUpdateGeometryData(false);
  }
}

/*
void Match::AddMissingAnim(const MissingAnim &someAnim) {
  bool found = false;
  for (unsigned int i = 0; i < missingAnims.size(); i++) {
    if (someAnim == missingAnims.at(i)) {
      missingAnims.at(i).angleDifference = (missingAnims.at(i).angleDifference *
missingAnims.at(i).timesMissed + someAnim.angleDifference) / (missingAnims.at(i).timesMissed
+ 1.0f); missingAnims.at(i).timesMissed++; found = true; break;
    }
  }
  if (!found) {
    missingAnims.push_back(someAnim);
  }
}
*/
