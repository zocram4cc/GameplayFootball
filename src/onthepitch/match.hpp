// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MATCH
#define _HPP_MATCH

#include <fstream>
#include <mutex>
#include "utils/camtrack.hpp"
#include "prematchtimeline.hpp"
#include "utils/entrancechoreo.hpp"
#include <iostream>
#include <memory>

#include "../data/matchanalytics.hpp"
#include "../data/matchdata.hpp"
#include "../menu/ingame/banner.hpp"
#include "../menu/ingame/formationgraphic.hpp"
#include "../menu/ingame/radar.hpp"
#include "../menu/ingame/scoreboard.hpp"
#include "../menu/ingame/statsoverlay.hpp"
#include "../menu/ingame/tacticsdebug.hpp"
#include "../menu/menutask.hpp"
#include "AIsupport/mentalimage.hpp"
#include "ball.hpp"
#include "base/circular_buffer.hpp"
#include "coachmode.hpp"
#include "matchprogression.hpp"
#include "modelviewer.hpp"
#include "framework/scheduler.hpp"
#include "officials.hpp"
#include "penaltyshootoutcontroller.hpp"
#include "player/humanoid/animcollection.hpp"
#include "referee.hpp"
#include "scene/objects/camera.hpp"
#include "scene/objects/light.hpp"
#include "scene/objects/sound.hpp"
#include "substitutions.hpp"
#include "team.hpp"
#include "types/command.hpp"
#include "types/lockable.hpp"
#include "types/messagequeue.hpp"
#include "utils/gui2/widgets/caption.hpp"

struct ReplaySpatialFrame {
  unsigned long frameTime_ms;
  Vector3 position;
  Quaternion orientation;
};

struct ReplayBallTouchesNetFrame {
  unsigned long frameTime_ms;
  bool ballTouchesNet;
};

struct ReplaySpatial {
  ReplaySpatial(int frameCount) {
    frames = blunted::circular_buffer<ReplaySpatialFrame>(frameCount);
  }
  boost::intrusive_ptr<Spatial> spatial;
  blunted::circular_buffer<ReplaySpatialFrame> frames;
};

struct PlayerBounce {
  Player* opp;
  float force;
};

struct ReplayState {
  ReplayState() { dirty = false; }
  bool dirty;
  unsigned long viewTime_ms;
  int cam;
  float modifierValue;
};

/*
struct MissingAnim {

  MissingAnim() {
    timesMissed = 1;
    angleDifference = 0.0f;
  }

  bool operator == (const MissingAnim &comp) const {
    if (this->outgoingVelocity == comp.outgoingVelocity &&
        this->outgoingDirection.GetDistance(comp.outgoingDirection) < 0.001f &&
        this->outgoingBodyDirection.GetDistance(comp.outgoingBodyDirection) <  0.001f) {
      return true;
    } else {
      return false;
    }
  }

  bool operator < (const MissingAnim &comp) const {
    if (this->timesMissed < comp.timesMissed) return true; else return false;
  }

  Vector3 outgoingDirection;
  e_Velocity outgoingVelocity;
  Vector3 outgoingBodyDirection;
  mutable int timesMissed;
  mutable radian angleDifference;
};
*/

class Match {
public:
  Match(MatchData* matchData, const std::vector<IHIDevice*>& controllers);
  virtual ~Match();

  void Exit();

  void SetRandomSunParams();
  void RandomizeAdboards(boost::intrusive_ptr<Node> stadiumNode);
  void UpdateControllerSetup();
  // Legacy plain-string call sites (goal commentary etc.) still work; routes
  // to the team-less (centre) banner slot. See ShowBanner for the richer,
  // team-tagged lower-third form (docs/PRESENTATION_SPEC.md section 4).
  void SpamMessage(const std::string& msg, int time_ms = 3000);
  // teamID -1 for a team-less message (referee/commentary cue), 0/1 for a
  // team-tagged one (tactics changes, subs, bookings/sending-off/offside).
  void ShowBanner(int teamID, const std::string& title, const std::string& subtitle,
                  int time_ms = 3500);
  int GetScore(int teamID) { return matchData->GetGoalCount(teamID); }
  Ball* GetBall() { return ball.get(); }
  Team* GetTeam(int teamID) { return teams[teamID].get(); }
  Player* GetPlayer(int playerID);
  void GetAllTeamPlayers(int teamID, std::vector<Player*>& players);
  void GetActiveTeamPlayers(int teamID, std::vector<Player*>& players);
  void GetOfficialPlayers(std::vector<PlayerBase*>& players);

  std::shared_ptr<AnimCollection> GetAnimCollection() { return anims; }

  const MentalImage* GetMentalImage(int history_ms);
  void UpdateLatestMentalImageBallPredictions();

  void ResetSituation(const Vector3& focusPos);
  // drops every player's cached mental image before the match frees them
  void InvalidateCachedMentalImages();

  void Pause(bool doPause) { pause = doPause; }
  bool GetPause() { return pause; }
  void SetMatchPhase(e_MatchPhase newMatchPhase);
  e_MatchPhase GetMatchPhase() const { return matchPhase; }

  void StartPlay() { inPlay = true; }
  void StopPlay() { inPlay = false; }
  bool IsInPlay() const { return inPlay; }

  // The match entrance: teams walking out and lining up before the kickoff.
  // While this is true the kickoff is held and no football is played.
  //
  // Timed on the wall clock, not on actualTime_ms. The match's own clock
  // advances a fixed 10 ms per simulation tick, and the tick rate is whatever
  // the machine can manage - on a fast or headless run that is many times
  // real time, which is what made a ninety-second presentation play out in
  // about five seconds. A presentation is measured in seconds the viewer
  // actually sits through.
  bool IsInEntrance() const { return entranceActive; }
  // How far into the presentation we are, in real seconds.
  float GetEntranceElapsedSeconds() const;
  unsigned long GetEntranceEndTime_ms() const { return introCutsceneEnd_ms; }
  // Which beat of the pre-match presentation is on air, and what it wants
  // drawn over it (docs/PRESENTATION_SPEC.md section 1). The formation
  // graphic reads its cue from here rather than working out its own
  // schedule, so a competition's timeline file governs both.
  PrematchTimeline::State GetPrematchState() const;
  // Where this player stands in the pre-kickoff line-up, and which way he
  // faces. Both teams line up along the halfway line facing the main stand.
  void GetEntranceSlot(const Player* player, Vector3& position, Vector3& lookAt) const;

  void StartSetPiece() { inSetPiece = true; }
  void StopSetPiece() { inSetPiece = false; }
  bool IsInSetPiece() const { return inSetPiece; }
  Referee* GetReferee() { return referee.get(); }
  Officials* GetOfficials() { return officials.get(); }
  const RefereeBuffer& GetRefereeBuffer() { return referee->GetBuffer(); };

  void SetGoalScored(bool onOff) {
    if (onOff == false)
      ballIsInGoal = false;
    goalScored = onOff;
  }
  bool IsGoalScored() const { return goalScored; }
  // Sets a team's goal count and keeps the scoreboard in step.
  void SetScore(int teamID, int goals);
  // Rebuilds the replay spatial list. Needed after a substitution, since the
  // incoming player gets a freshly built humanoid whose nodes are not in it.
  void RebuildReplaySpatials();
  // Drops every cached reference to a player who has just left the pitch.
  void ReplacePlayerReferences(Player* playerOut, Player* playerIn);
  int GetLastGoalTeamID() const { return lastGoalTeamID; }
  void SetLastTouchTeamID(int id, e_TouchType touchType = e_TouchType_Intentional_Kicked) {
    lastTouchTeamIDs[touchType] = id;
    lastTouchTeamID = id;
    referee->BallTouched();
  }
  int GetLastTouchTeamID(e_TouchType touchType) const { return lastTouchTeamIDs[touchType]; }
  int GetLastTouchTeamID() const { return lastTouchTeamID; }
  Team* GetLastTouchTeam(e_TouchType touchType) {
    if (lastTouchTeamIDs[touchType] != -1)
      return teams[lastTouchTeamIDs[touchType]].get();
    else
      return 0;
  }
  Team* GetLastTouchTeam() {
    if (lastTouchTeamID != -1)
      return teams[lastTouchTeamID].get();
    else
      return teams[0].get();
  }
  Player* GetLastTouchPlayer(e_TouchType touchType) {
    if (GetLastTouchTeam(touchType))
      return GetLastTouchTeam(touchType)->GetLastTouchPlayer(touchType);
    else
      return 0;
  }
  Player* GetLastTouchPlayer() {
    if (GetLastTouchTeam())
      return GetLastTouchTeam()->GetLastTouchPlayer();
    else
      return 0;
  }
  float GetLastTouchBias(int decay_ms, unsigned long time_ms = 0) {
    if (GetLastTouchTeam())
      return GetLastTouchTeam()->GetLastTouchBias(decay_ms, time_ms);
    else
      return 0;
  }
  bool IsBallInGoal() const { return ballIsInGoal; }

  signed int GetBestPossessionTeamID();
  Player* GetDesignatedPossessionPlayer() { return designatedPossessionPlayer; }
  Player* GetBallRetainer() { return ballRetainer; }
  void SetBallRetainer(Player* retainer) { ballRetainer = retainer; }

  float GetAveragePossessionSide(int time_ms) const {
    return possessionSideHistory->GetAverage(time_ms);
  }

  unsigned long GetIterations() const { return iterations.GetData(); }
  unsigned long GetMatchTime_ms() const { return matchTime_ms; }
  unsigned long GetActualTime_ms() const { return actualTime_ms; }

  void GameOver();

  void AddExcitementBoost(float amount, int duration_ms);
  void ToggleStatsOverlay();

  void GetCameraParams(float& zoom, float& height, float& fov, float& angleFactor);
  void SetCameraParams(float zoom, float height, float fov, float angleFactor);

  void UpdateIngameCamera();

  boost::intrusive_ptr<Camera> GetCamera() { return camera; }
  std::shared_ptr<AnimCollection> GetAnims() { return anims; }

  void Get();
  void Process();
  void PreparePutBuffers();
  void FetchPutBuffers();
  void Put();

  boost::intrusive_ptr<Node> GetDynamicNode();

  void ApplyReplayFrame(unsigned long replayTime_ms);

  void FollowCamera(Quaternion& orientation, Quaternion& nodeOrientation, Vector3& position,
                    float& FOV, const Vector3& targetPosition, float zoom);
  void SetReplayCamera(int camType, const Vector3& target, float modifierValue);

  void SetAutoUpdateIngameCamera(bool autoUpdate = true) {
    if (autoUpdate != autoUpdateIngameCamera) {
      camPos.clear();
      autoUpdateIngameCamera = autoUpdate;
    }
  }

  int GetReplaySize_ms();
  int GetReplayCamCount();

  void ProcessReplayMessages();
  Lockable<ReplayState> replayState;

  MatchData* GetMatchData() { return matchData; }

  // Law 7 allowance for time lost, accrued per event and reset at every phase
  // change. The referee extends the period by it; the scoreboard displays it.
  void AddLostTime(MatchProgression::e_StoppageReason reason) {
    MatchProgression::AddStoppage(stoppage, reason);
  }
  const MatchProgression::Stoppage& GetStoppage() const { return stoppage; }

  // Expected-goals tally and ball heatmap for the post-match analysis (5B).
  MatchAnalytics::ShotTally& GetShotTally() { return shotTally; }
  const MatchAnalytics::ShotTally& GetShotTally() const { return shotTally; }
  const MatchAnalytics::Heatmap& GetBallHeatmap() const { return ballHeatmap; }

  PenaltyShootoutController* GetPenaltyShootout() { return penaltyShootout.get(); }

  const CoachMode::Setup& GetCoachSetup() const { return coachSetup; }
  // Whether a human may open the tactics menu for this team.
  bool CanCoachTeam(int teamID) const { return CoachMode::CanEditTactics(coachSetup, teamID); }
  Substitutions::State& GetSubstitutionState() { return substitutionState; }
  // Requests a substitution for `teamID`; returns the rule check result and
  // performs the swap when it is accepted.
  Substitutions::e_Result RequestSubstitution(int teamID, Player* playerOut, Player* playerIn);
  // drains queued swaps; call only under GameTask's put-buffer mutex while
  // holding GameTask's substitution lock exclusively (the rebuild replaces
  // humanoid scene nodes the graphics put phase walks)
  void ExecutePendingSubstitutions();
  bool HasPendingSubstitutions();

  float GetMatchDurationFactor() const { return matchDurationFactor; }
  float GetMatchDifficulty() const { return matchDifficulty; }

  std::vector<Vector3>& GetAnimPositionCache(Animation* anim) {
    // The cache is built from the animation collection, so a clip that never
    // joined it - an imported cutscene clip, say - has no entry. Dereferencing
    // end() here used to take the process down.
    auto cached = animPositionCache.find(anim);
    if (cached != animPositionCache.end()) return cached->second;
    static std::vector<Vector3> empty;
    return empty;
  }

  void UploadGoalNetting();

  unsigned long GetPreviousProcessTime_ms() {
    return previousProcessTime_ms;
  }  // always around 10ms, not a very useful function, probably
  unsigned long GetPreviousPreparePutTime_ms() { return previousPreparePutTime_ms; }
  unsigned long GetPreviousPutTime_ms() { return previousPutTime_ms; }
  int GetTimeSincePreviousProcess_ms() { return timeSincePreviousProcess_ms; }
  int GetTimeSincePreviousPreparePut_ms() { return timeSincePreviousPreparePut_ms; }
  int GetTimeSincePreviousPut_ms() { return timeSincePreviousPut_ms; }

  // void AddMissingAnim(const MissingAnim &someAnim);

  // not sure about how signals work in this game at the moment. whole menu/game thing needs a
  // rethink, i guess
  boost::signals2::signal<void(Match*)> sig_OnMatchPhaseChange;
  boost::signals2::signal<void(Match*)> sig_OnShortReplayMoment;
  boost::signals2::signal<void(Match*)> sig_OnExtendedReplayMoment;
  boost::signals2::signal<void(Match*)> sig_OnGameOver;
  boost::signals2::signal<void(Match*)> sig_OnCreatedMatch;
  boost::signals2::signal<void(Match*)> sig_OnExitedMatch;

protected:
  void GetReplaySpatials(std::list<boost::intrusive_ptr<Spatial>>& spatials);
  void CaptureReplayFrame(unsigned long replayTime_ms);
  bool CheckForGoal(signed int side);

  void CalculateBestPossessionTeamID();
  // Touchline hotkeys: mentality and advanced instructions, with on-screen feedback.
  void ProcessTacticalHotkeys();
  void ProcessTacticalHotkeysForPad(int teamID);
  void AnnounceInstructions(int teamID);
  // The pad that runs a given bench, if there is one.
  IHIDevice* GetTouchlineDevice(int teamID);
  int GetCoachedTeamID(bool preferSecondTeam) const;
  void UpdateBallHeatmap();
  // Lets CPU-managed teams use their bench (AIManager); human-coached teams do
  // this from the menu instead.
  void ProcessAutoSubstitutions();
  void UpdateCrowdAudio();
  void CheckHumanoidCollisions();
  void CheckHumanoidCollision(Player* p1, Player* p2, std::vector<PlayerBounce>& p1Bounce,
                              std::vector<PlayerBounce>& p2Bounce);
  void CheckBallCollisions();

  void PrepareGoalNetting();
  void UpdateGoalNetting(bool ballTouchesNet = false);

  // for stuff like animation smoothing, we could need the time elapsed since last Put() and such
  unsigned long previousProcessTime_ms;
  unsigned long previousPreparePutTime_ms;
  unsigned long previousPutTime_ms;
  int timeSincePreviousProcess_ms;
  int timeSincePreviousPreparePut_ms;
  int timeSincePreviousPut_ms;

  MatchData* matchData;

  std::unique_ptr<PenaltyShootoutController> penaltyShootout;

  MatchAnalytics::ShotTally shotTally;
  MatchAnalytics::Heatmap ballHeatmap;

  CoachMode::Setup coachSetup;
  Substitutions::State substitutionState;
  MatchProgression::Stoppage stoppage;
  struct PendingSubstitution { int teamID; Player* playerOut; Player* playerIn; };
  std::vector<PendingSubstitution> pendingSubstitutions;
  std::mutex pendingSubstitutionsMutex;
  std::unique_ptr<Team> teams[2];

  std::unique_ptr<Officials> officials;

  boost::intrusive_ptr<Node> dynamicNode;

  boost::intrusive_ptr<Node> cameraNode;
  boost::intrusive_ptr<Camera> camera;
  boost::intrusive_ptr<Node> sunNode;

  boost::intrusive_ptr<Node> stadiumNode;
  boost::intrusive_ptr<Node> goalsNode;
  boost::intrusive_ptr<Node> skydomeNode;

  // camera user settings
  float cameraUserZoom;
  float cameraUserHeight;
  float cameraUserFOV;
  float cameraUserAngleFactor;

  // Match entrance. The kickoff is held while this runs: PES does not play
  // football underneath its entrance, it holds the restart until the teams have
  // walked out and lined up. Measured on the match's own 10 ms clock so the
  // referee's set-piece timing and this agree exactly.
  // ("intro_cutscene_seconds" / "entrance_id" config keys)
  // introCutsceneEnd_ms is kept one tick ahead of actualTime_ms for as long
  // as the entrance runs: the referee defers the kickoff to it every tick
  // (see referee.cpp), so when the entrance finishes the value it last
  // latched is immediately reachable and the restart arms at once.
  unsigned long introCutsceneEnd_ms = 0;
  unsigned long introCutsceneDuration_ms = 0;
  bool entranceActive = false;
  unsigned long entranceRealStart_ms = 0;
  float entranceSeconds = 0.0f;
  // imported PES camerawork ("intro_cutscene_track" .camtrack path, or the
  // track picked out of media/cutscenes/ent/<entrance_id>/ by stadium)
  // PES stages an entrance as several authored shots cut back to back; they
  // play in order, each one filling its own slice of the entrance
  CamTrack introCamTrack;
  std::vector<CamTrack> introShots;
  // The beat list this entrance is staged against, picked per competition -
  // see prematchtimeline.hpp for the lookup and the file format.
  PrematchTimeline::Timeline prematchTimeline;
  PrematchTimeline::Timeline LoadPrematchTimeline() const;
  void RememberPrematchCamera();
  // Camera::Hold keeps whatever the previous beat left on screen.
  Vector3 heldCameraPosition;
  Quaternion heldCameraOrientation;
  Quaternion heldCameraNodeOrientation;
  float heldCameraFOV = 35.0f;
  float heldCameraNear = 2.0f;
  float heldCameraFar = 400.0f;
  bool heldCameraValid = false;
  // imported PES player choreography for the entrance: a .chor exported from
  // the family's _pl packs (tools/pes21_import/entrance_pl.py), picked from
  // the same directory as the camerawork, plus its in-place .anim clips.
  // While it plays, the cast players are driven kinematically
  // (HumanoidBase::SetChoreoPose); players without a slot fall back to the
  // scripted walk (AddEntranceCommands).
  EntranceChoreo entranceChoreo;
  std::map<std::string, std::shared_ptr<Animation>> entranceClips;
  struct EntranceCastMember {
    Player* player;
    const ChoreoSlot* slot;
    Animation* clip;
  };
  std::vector<EntranceCastMember> entranceCast;
  bool entranceCastBuilt = false;

  // The same staging for stoppage cutscenes: a category's .chor files put the
  // referee, the scorer and whoever else PES staged on their marks while the
  // imported camerawork films them. Keyed by pool name, like the cameras.
  std::map<std::string, std::vector<EntranceChoreo>> cutsceneChoreoPools;
  std::map<std::string, std::shared_ptr<Animation>> cutsceneClips;
  const EntranceChoreo* activeCutsceneChoreo = nullptr;
  std::vector<EntranceCastMember> cutsceneCast;
  // the referee is not one of the 22, so his marks are held separately
  struct OfficialCastMember {
    PlayerOfficial* official;
    const ChoreoSlot* slot;
    Animation* clip;
  };
  std::vector<OfficialCastMember> cutsceneOfficialCast;
  // model viewer: drives the inspected player through the animation
  // collection so imported clips can be judged pose by pose
  // debug bench; the maths lives in modelviewer.{hpp,cpp}
  ModelViewerSettings LoadModelViewerSettings() const;
  Player* PickModelViewerSubject(const std::string& filter);
  void UpdateModelViewerPlayback();
  int modelViewerAnimIndex = -1;

  void LoadCutsceneChoreo(const std::string& category, const std::string& dir);
  void StartCutsceneChoreo(const std::string& category);
  void UpdateCutsceneChoreo();

public:
  // The people an incident involved, so its cutscene can be cast with them:
  // the booked player takes the primary mark, the man he fouled the opponent
  // mark, and the referee the official's. Cleared once the cutscene ends.
  void SetCutsceneParticipants(Player* primary, Player* opponent) {
    cutscenePrimary = primary;
    cutsceneOpponent = opponent;
  }

protected:
  Player* cutscenePrimary = nullptr;
  Player* cutsceneOpponent = nullptr;
  void BuildEntranceCast();
  void UpdateEntranceChoreo();
  // goal-replay camerawork pool (media/cutscenes/goal/*.camtrack) with each
  // track's authored goal side (+1/-1 from its mean x) for mirroring
  std::vector<CamTrack> goalCamTracks;
  std::vector<int> goalCamAuthoredSides;
  // stoppage cutscenes: PES's other fixdemo categories, played at their
  // match-flow trigger points (halftime, cards, subs, penalties, fulltime)
  std::map<std::string, std::vector<CamTrack>> cutscenePools;
  const CamTrack* activeCutscene = nullptr;
  unsigned long cutsceneStart_ms = 0;
  unsigned long cutsceneEnd_ms = 0;

 public:
  // plays a random track from the category's pool during the current
  // stoppage (no-op when the pool is empty or a cutscene is running)
  void StartCutscene(const std::string& category, float capSeconds);

 protected:
  // per-team chant loops (config "team1_chant"/"team2_chant"), gained up on goals
  boost::intrusive_ptr<Sound> teamChant[2];

  std::shared_ptr<AnimCollection> anims;

  const std::vector<IHIDevice*>& controllers;

  std::unique_ptr<Ball> ball;

  std::vector<std::shared_ptr<MentalImage>>
      mentalImages;  // [index] == index * 10 ms ago ([0] == now)

  std::unique_ptr<Gui2ScoreBoard> scoreboard;
  std::unique_ptr<Gui2Radar> radar;
  std::unique_ptr<Gui2TacticsDebug> tacticsDebug;
  std::unique_ptr<Gui2FormationGraphic> formationGraphic;
  std::unique_ptr<Gui2Banner> banner;
  std::unique_ptr<Gui2StatsOverlay> statsOverlay;

  mutable Lockable<unsigned long> iterations;
  TaskSequenceInfo gameSequenceInfo;
  unsigned long matchTime_ms;
  double matchTimeExact_ms;
  unsigned long actualTime_ms;
  unsigned long buf_matchTime_ms;
  unsigned long buf_actualTime_ms;
  unsigned long fetchedbuf_matchTime_ms;
  unsigned long fetchedbuf_actualTime_ms;
  unsigned long goalScoredTimer;

  bool pause;
  e_MatchPhase matchPhase;  // 0 - first half; 1 - second half; 2 - 1st extra time; 3 - 2nd extra
                            // time; 4 - penalties
  bool inPlay;
  bool inSetPiece;
  bool goalScored;  // true after goal scored, false again after next match state change
  bool ballIsInGoal;
  int lastGoalTeamID;
  Player* lastGoalScorer;
  int lastTouchTeamIDs[e_TouchType_SIZE];
  int lastTouchTeamID;
  signed int bestPossessionTeamID;
  Player* designatedPossessionPlayer;
  Player* ballRetainer;

  bool gameOver;

  boost::intrusive_ptr<Node> fullbodyNode;
  std::map<Vector3, Vector3> colorCoords;

  std::unique_ptr<ValueHistory<float>> possessionSideHistory;

  bool autoUpdateIngameCamera;

  // camera
  Quaternion cameraOrientation;
  Quaternion cameraNodeOrientation;
  Vector3 cameraNodePosition;
  float cameraFOV;
  float cameraNearCap;
  float cameraFarCap;

  TemporalSmoother<Quaternion> buf_cameraOrientation;
  TemporalSmoother<Quaternion> buf_cameraNodeOrientation;
  TemporalSmoother<Vector3> buf_cameraNodePosition;
  TemporalSmoother<float> buf_cameraFOV;
  float buf_cameraNearCap;
  float buf_cameraFarCap;
  Quaternion fetchedbuf_cameraOrientation;
  Quaternion fetchedbuf_cameraNodeOrientation;
  Vector3 fetchedbuf_cameraNodePosition;
  float fetchedbuf_cameraFOV;
  float fetchedbuf_cameraNearCap;
  float fetchedbuf_cameraFarCap;

  int fetchedbuf_timeDelta;

  unsigned int lastBodyBallCollisionTime_ms;

  std::deque<Vector3> camPos;  // todo: circular buffer?

  std::unique_ptr<Referee> referee;

  std::shared_ptr<MenuTask> menuTask;

  std::shared_ptr<Scene3D> scene3D;

  boost::intrusive_ptr<Sound> crowd01;
  boost::intrusive_ptr<Sound> crowd02;

  std::vector<std::unique_ptr<ReplaySpatial>> replay;
  blunted::circular_buffer<ReplayBallTouchesNetFrame> replayBallTouchesNetFrames;
  bool resetNetting;
  bool nettingHasChanged;

  float excitement;
  float excitementEventBoost;
  int excitementEventTimer_ms;
  float crowdAmbientGain;
  float crowdReactionGain;

  Vector3 previousBallPos;

  float matchDurationMinutes;
  float matchDurationFactor;
  float matchTimeScale;

  std::map<Animation*, std::vector<Vector3>> animPositionCache;

  std::vector<Vector3> nettingMeshesSrc[2];
  std::vector<float*> nettingMeshes[2];

  // boost::intrusive_ptr<Light> lightTest[100];

  // todo: this is temporary
  bool _positionLogging;
  std::ofstream positionLogFile;

  // std::vector<MissingAnim> missingAnims;

  float matchDifficulty;
};

#endif
