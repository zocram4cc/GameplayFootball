// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MATCH
#define _HPP_MATCH

#include <fstream>
#include <mutex>
#include "utils/cloth.hpp"
#include "utils/camtrack.hpp"
#include "prematchtimeline.hpp"
#include "prematchshotpair.hpp"
#include "goalcelebration.hpp"
#include "goalsequence.hpp"
#include "replaywipe.hpp"
#include "scenelighting.hpp"
#include "utils/entrancechoreo.hpp"
#include <iostream>
#include <memory>

#include "../data/matchanalytics.hpp"
#include "../data/matchdata.hpp"
#include "../menu/ingame/banner.hpp"
#include "../menu/ingame/formationgraphic.hpp"
#include "../menu/ingame/radar.hpp"
#include "../menu/ingame/playerhud.hpp"
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
#include "camerastandoff.hpp"
#include "cutsceneplayback.hpp"
#include "cutscenesequence.hpp"
#include "cutsceneviewer.hpp"
#include "substitutions.hpp"
#include "team.hpp"
#include "types/command.hpp"
#include "types/lockable.hpp"
#include "types/messagequeue.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

namespace RemoteControl {
class Server;
struct Command;
}  // namespace RemoteControl

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
class RigdioDirector;


class Match {
public:
  Match(MatchData* matchData, const std::vector<IHIDevice*>& controllers);
  virtual ~Match();

  void Exit();

  // Something else takes the screen - a replay, the half-time card - and the
  // in-match chrome goes with it, leaving the caller's own overlay. A flag
  // rather than a Hide() from the page, so the presentation still wins - the
  // HUD does not come back mid-walkout.
  void SuppressHud(bool suppressed);

  void SetSunParams();
  // The crowd's stand flags, painted with the playing teams' badges (teamflag.hpp).
  void PaintTeamFlags(const std::list<boost::intrusive_ptr<Geometry>>& geoms);
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
  // As above, but keeps the image alive for as long as the caller holds it.
  // Anyone who CACHES an image across frames has to take this: the vector is
  // emptied on every reset (half time, a restart, teardown) and a raw pointer
  // into it dangles the moment that happens, on whichever thread got there
  // first.
  std::shared_ptr<const MentalImage> GetMentalImageOwned(int history_ms);
  void UpdateLatestMentalImageBallPredictions();

  void ResetSituation(const Vector3& focusPos);
  // drops every player's cached mental image before the match frees them
  void InvalidateCachedMentalImages();

  void Pause(bool doPause) { pause = doPause; }
  bool GetPause() { return pause; }

  // The body that actually loaded, which is not necessarily the configured one:
  // an incomplete body falls back to the legacy one, and what follows from the
  // choice - the hairstyle meshes - has to follow the body in use.
  const std::string& GetPlayerBodyName() const { return playerBodyName; }
  void SetMatchPhase(e_MatchPhase newMatchPhase);
  e_MatchPhase GetMatchPhase() const { return matchPhase; }

  void StartPlay() { inPlay = true; }
  void StopPlay() { inPlay = false; }
  bool IsInPlay() const { return inPlay; }
  // How long the celebration has been running, for whoever needs to know which
  // half of it is playing (onthepitch/goalcelebration.hpp).
  unsigned long GetGoalScoredTimer() const { return goalScoredTimer; }
  // The performance the camera is filming, as the specialvar2 that asks for it, or 0
  // when there is no chosen celebration (goalcelebration.hpp). Only the scorer gives
  // it; his teammates celebrate however they like.
  int GetGoalCelebrationVar() const {
    return (goalCelebrationIndex >= 0 && goalCelebrationIndex < (int)goalCelebrations.size())
               ? goalCelebrations[goalCelebrationIndex].var
               : 0;
  }
  Player* GetLastGoalScorer() const { return lastGoalScorer; }

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
  // Whether a camera other than the match camera has the picture: the walkout,
  // a stoppage cutscene, a goal celebration, a replay, the closing ceremony.
  // In-world chrome - the name over a player's head - has no business in any of
  // those shots.
  bool IsStaged() const {
    return entranceActive || activeCutscene != nullptr || goalScored || hudSuppressed ||
           gameOver;
  }
  // How far into the presentation we are, in real seconds.
  float GetEntranceElapsedSeconds() const;
  unsigned long GetEntranceEndTime_ms() const { return introCutsceneEnd_ms; }
  // The whole presentation's length in real seconds (0 = no entrance). The
  // rigdio music director schedules the away and home anthems off it.
  float GetEntranceTotalSeconds() const { return entranceSeconds; }
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

  // Whether the ball is in the net is where the ball is, not whether anyone is
  // still calling it a goal: the netting in Ball::Process is gated on it. Clearing
  // it here shut the net off the instant a scorer was flagged - which is what the
  // penalty shootout does the moment it sees the ball go in, so a 25 m/s penalty
  // sailed straight through the back of the goal and off into the stands. Only a
  // reset clears it, by which time the ball is dead.
  void SetGoalScored(bool onOff) { goalScored = onOff; }
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
  // How far back the next scripted replay should start, in ms before now.
  // Zero means "whatever is in the buffer".
  unsigned long GetReplayStartOffset_ms() const { return replayStartOffset_ms; }
  // Which replay camera a scripted replay opens on: the goal replay uses the
  // behind-goal view, a foul the close one (see Match::SetReplayCamera).
  int GetReplayCamera() const { return replayCamera; }
  // How long the celebration on screen holds its intro, from the clip itself.
  unsigned long GetCelebrationIntroHold_ms() const { return goalCelebrationIntroHold_ms; }
  // How long the celebration on screen runs, measured from its own clip.
  unsigned long GetCelebrationLength_ms() const { return goalCelebrationLength_ms; }
  // Asks for a close-up replay of a foul, once its cutscene has finished.
  // Scheduled off FoulSequence so the referee's restart cannot pre-empt it.
  void RequestFoulReplay(unsigned long foulTime_ms, int foulType);
  // Fires the pending foul replay once its cutscene has run its course.
  void ProcessFoulReplay();
  // Asks for a replay, pausing only if a listener will actually play it.
  void RequestExtendedReplay();
  // The frame count of the celebration clip under this specialvar2, or 0 if absent.
  int CelebrationClipFrames(int specialVar2) const;
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
  // Whether the CPU manager may adapt this team's tactics and use its bench.
  // Off for both teams in coach mode - see CoachMode::AIManagerRuns.
  bool AIManagerRunsTeam(int teamID) const {
    return CoachMode::AIManagerRuns(coachSetup, teamID);
  }
  Substitutions::State& GetSubstitutionState() { return substitutionState; }
  // Play stopped, in a match that has actually started. Not the same thing as
  // !IsInPlay(), which is also true throughout the pre-match presentation.
  bool IsSubstitutionWindow() const;
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
  void WriteGoalNetting();
  void PrepareCornerFlags();
  void UpdateCornerFlags();
  void WriteCornerFlags();
  void UploadCornerFlags();
  // The pennant ring's flag, held all the way round its rim by the bearers.
  void PreparePennantCloth();
  void UpdatePennantCloth();
  void WritePennantCloth();
  void UploadPennantCloth();
  Vector3 FlagWind(unsigned long time_ms) const;

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

  // The remote-control channel (remotecontrolmode.hpp): commands from the
  // attached web panel are drained and applied on this thread, and a state
  // snapshot is published for it to read back. Null - and costing nothing -
  // unless the engine is in remote-control mode; the mode owns the server.
  void ProcessRemoteControl();
  void ApplyRemoteCommand(const RemoteControl::Command& command);
  void PublishRemoteState();
  RemoteControl::Server* remoteControl = nullptr;
  unsigned long lastRemoteStatePublish_ms = 0;

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
  // The touchline staff, imported from PES's common package and installed
  // beside the stadium (tools/pes21_import/stadium_staff.py).
  boost::intrusive_ptr<Node> staffNode;
  boost::intrusive_ptr<Node> propsNode;
  boost::intrusive_ptr<Node> crowdNode;
  boost::intrusive_ptr<Node> entrancePropsNode;
  boost::intrusive_ptr<Node> pennantNode;

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
  unsigned long replayStartOffset_ms = 0;
  // How long the celebration on screen actually is, measured from its clips when it
  // was chosen. A flat 1900 ms intro left short intros posing and a flat nine second
  // performance left finished loops running in place.
  unsigned long goalCelebrationIntroHold_ms = GoalCelebration::kIntroHold_ms;
  unsigned long goalCelebrationLength_ms = GoalSequence::kCelebration_ms;
  // The staged performance's own length: the last actor's clip ending, set by
  // StartGoalCast. Not the choreography's cycle, which runs on past the bodies.
  unsigned long goalCastLength_ms = 0;
  int replayCamera = 1;  // behind the goal, which is what a goal replay wants
  // A foul replay waits for its cutscene; 0 when none is pending.
  unsigned long foulReplayDue_ms = 0;
  int foulReplayFoulType = 0;
  unsigned long foulReplayFoulTime_ms = 0;
  unsigned long introCutsceneEnd_ms = 0;
  unsigned long introCutsceneDuration_ms = 0;
  bool entranceActive = false;
  unsigned long entranceRealStart_ms = 0;
  float entranceSeconds = 0.0f;
  // The cut from the pre-match presentation to kickoff, covered.
  //
  // PES never shows twenty-two players walking off their entrance marks and
  // onto their kickoff ones. Its wipe covers the screen, the pitch is set
  // underneath it, and it uncovers on a formation already standing. Ours used
  // to do the move in plain view, which reads as everyone drifting sideways.
  //
  // Same asset and the same timing the replay page uses (replaywipe.hpp): the
  // snap goes on the frame the matte has everything covered, so it is never
  // seen.
  Gui2Image* handoffWipe = nullptr;
  ReplayWipe::Timing handoffWipeTiming;
  std::string handoffWipeDir;
  unsigned long handoffWipeStarted_ms = 0;
  int handoffWipeFrameOnScreen = -1;
  bool handoffWipeRunning = false;
  void StartHandoffWipe();
  // -> true once the screen is covered, which is when the pitch may be set.
  bool RunHandoffWipe();

 public:
  // -> true once the screen is covered and the caller may rearrange the pitch.
  //
  // The entrance already did this for the kickoff: start the matte, and do the
  // reset on the frame it has everything covered. A stoppage needs it for the
  // same reason - the referee's restart calls ResetSituation, which teleports
  // twenty-two players onto their marks, and doing that in plain view is what
  // the owner sees as everyone jumping sideways after a foul or an offside.
  //
  // Repeatable: each request arms a fresh pass. With no wipe imported the
  // timing is invalid, this returns true on the first call, and the restart
  // happens exactly as it did before.
  bool CoverForRestart();

 protected:
  bool restartWipeArmed = false;
  // The match-music player: anthems, goal horns, victory anthems and chants
  // from the teams' rigdio exports (rigdiodirector.hpp, docs/RIGDIO.md).
  std::unique_ptr<RigdioDirector> rigdio;
  // imported PES camerawork ("intro_cutscene_track" .camtrack path, or the
  // track picked out of media/cutscenes/ent/<entrance_id>/ by stadium)
  // PES stages an entrance as several authored shots cut back to back; they
  // play in order, each one filling its own slice of the entrance
  CamTrack introCamTrack;
  std::vector<CamTrack> introShots;
  // The dolly-back that keeps an authored shot out of the bodies it films
  // (camerastandoff.hpp), for every camera that plays imported camerawork: the
  // walk-on, a stoppage's shot, the goal camera. `shot`/`cut` name the shot on
  // screen so a new one may start wherever it has to; within one the dolly moves
  // at its own speeds.
  std::vector<CameraStandoff::Body> StandoffBodies();
  void ApplyStandoff(const void* shot, int cut, const Vector3& forward, float clearance,
                     Vector3& eye);
  const void* standoffShot = nullptr;
  int standoffCut = -1;
  float standoffPush = 0.0f;
  float standoffSpeed = 0.0f;  // the retreat's current speed, run up gradually
  float standoffLastSeconds = 0.0f;
  // The beat list this entrance is staged against, picked per competition -
  // see prematchtimeline.hpp for the lookup and the file format.
  PrematchTimeline::Timeline prematchTimeline;
  PrematchTimeline::Timeline LoadPrematchTimeline() const;
  // Every piece of authored entrance camerawork installed, keyed by the shot
  // token in its file name (passage01, anth, circle_home, ...). A beat names
  // the shot it wants; see PrematchTimeline::Beat::shot.
  std::map<std::string, CamTrack> prematchShots;
  void LoadPrematchShots(const std::string& stadiumToken);
  const CamTrack* FindPrematchShot(const std::string& shot) const;
  // The key FindPrematchShot would resolve to, so the staging can be paired with
  // that camera's own (prematchshotpair.hpp).
  std::string FindPrematchShotName(const std::string& shot) const;

  // The player staging that goes with each shot. PES authors both together -
  // the tunnel pack walks the squads out, the anthem pack stands them on the
  // line, the circle pack puts them in the team photo - so a beat that names
  // a shot gets that shot's choreography as well as its camera. Indexed by
  // the same tokens as prematchShots; the packs are read lazily, since there
  // are seventy of them and a match plays a handful.
  struct PrematchStaging {
    std::string path;  // the .chor, empty once loaded
    std::string directory;
    EntranceChoreo choreo;
    std::map<std::string, std::shared_ptr<Animation>> clips;
    bool loaded = false;
  };
  std::map<std::string, PrematchStaging> prematchStagings;
  void LoadPrematchStagingIndex(const std::string& stadiumToken);
  PrematchStaging* AcquirePrematchStaging(const std::string& shot);
  PrematchStaging* activeStaging = nullptr;
  int stagedBeatIndex = -2;
  float stagingStartSeconds = 0.0f;
  bool stagingHoldsOpeningFrame = false;
  // Whether any pack has actually played. The establishing shots borrow the
  // first pack's opening frame before that; afterwards, a beat with no staging
  // leaves the cast released instead of hauling it back to the tunnel.
  bool stagingHasRun = false;
  bool hudSuppressed = false;
  // The camera track paired with the staging currently on the pitch.
  std::string stagedCameraKey;
  // Where this staging has to be moved to happen on our pitch rather than in
  // the stadium PES authored it for (staginganchor.hpp).
  Vector3 stagingOffset;
  // Where this ground's own lighting puts the sun, if it shipped any.
  SceneLighting::Sun stadiumSun;
  void RememberPrematchCamera();
  // Hides/shows the persistent in-match HUD (scoreboard, radar).
  void ShowMatchHud(bool visible);
  // Bounding box of the choreographed entrance cast, for the shots that
  // frame the players rather than the stadium. False when nothing is staged.
  bool GetEntranceCastBounds(Vector3& centre, Vector3& extent) const;
  Vector3 ComputeStagingOffset() const;
  // Camera::Hold keeps whatever the previous beat left on screen.
  Vector3 heldCameraPosition;
  Quaternion heldCameraOrientation;
  Quaternion heldCameraNodeOrientation;
  float heldCameraFOV = 35.0f;
  float heldCameraNear = 2.0f;
  float heldCameraFar = 400.0f;
  bool heldCameraValid = false;
  // The cast-framed shots are derived from where the players are, which moves
  // every tick; the eye and its target are eased towards that rather than
  // snapped to it, or the shot shakes with the squad's bounding box.
  Vector3 smoothedCamEye;
  Vector3 smoothedCamTarget;
  bool smoothedCamValid = false;
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
  // Where the choreography last put the cast. A posed player's own position is
  // still his simulation position - his kickoff mark - so a camera framed off
  // that films an empty pitch while the squads walk on somewhere else.
  Vector3 choreoBoundsCentre;
  Vector3 choreoBoundsExtent;
  bool choreoBoundsValid = false;
  bool entranceCastBuilt = false;

  // The same staging for stoppage cutscenes: a category's .chor files put the
  // referee, the scorer and whoever else PES staged on their marks while the
  // imported camerawork films them. Keyed by pool name, like the cameras.
  std::map<std::string, std::vector<EntranceChoreo>> cutsceneChoreoPools;
  // A clip is parsed the first time a cutscene casts it, not when the pools are
  // built: the stoppage choreographies reference some 600 MB of .anim text and
  // a match plays a dozen of them. cutsceneClipPaths holds where each one is;
  // cutsceneClips holds the ones that have been asked for. CutsceneClip() is
  // the only way in.
  std::map<std::string, std::string> cutsceneClipPaths;
  std::map<std::string, std::shared_ptr<Animation>> cutsceneClips;
  Animation* CutsceneClip(const std::string& animFile);
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
  // A goal celebration as PES stages it: the chosen performance's own
  // choreography, the scorer on its primary mark and his teammates on the rest,
  // played at goalCelebrationSubject turned by goalCelebrationYaw - the same
  // staging the goal camera gets (StageCamTrackFrame) - and timed off
  // goalScoredTimer so the cast and the camera montage share their zero.
  // False when no choreography was shot for that celebration, and the scorer
  // performs it on the spot as before.
  bool StartGoalCast(const std::string& celebration);
  const EntranceChoreo* FindGoalChoreo(const std::string& celebration) const;
  bool GoalCastActive() const {
    return activeCutsceneChoreo != nullptr && activeCutsceneCategory == "goal";
  }
  void EndGoalCast();

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
  // A substitution is staged at the touchline rather than where the man stood.
  bool cutsceneAtTouchline = false;
  // Which pool the running cutscene came from. Kept because a category PES never
  // filmed still has to be framed by something, and what it should be framed on
  // depends on which incident it is: an offside belongs on the assistant with the
  // flag up, not on the patch of grass the offence happened over.
  std::string activeCutsceneCategory;
  // Where the running cutscene's camerawork is authored: PES's foul shots are
  // authored about the incident and have to be placed at it, the rest are in
  // stadium coordinates and are used as they are. Measured when the cutscene
  // starts (onthepitch/cutsceneviewer.hpp) rather than assumed per category.
  CutsceneViewer::Anchoring activeCutsceneAnchoring = CutsceneViewer::Anchoring::StadiumWorld;
  // And where its staging is authored. Measured separately: a category can film
  // in stadium coordinates while staging its actors about the incident, and the
  // foul packs do exactly that - actors at 3.3 m from the origin, camera at 5.4.
  CutsceneViewer::Anchoring activeStagingAnchoring = CutsceneViewer::Anchoring::StadiumWorld;
  // The incident an incident-local cutscene is placed at.
  Vector3 CutsceneAnchorPosition() const;
  // Which official the running cutscene's Official slot belongs to. The role is
  // "referee or assistant" (entrancechoreo.hpp) and which one it is depends on
  // the incident, not on the slot.
  PlayerOfficial* OfficialForCutscene() const;
  bool cutsceneShotTaken = false;
  Player* cutsceneOpponent = nullptr;
  void BuildEntranceCast();
  // Everybody the staging does not stage, out of shot for the presentation.
  // Parks whoever should not be on the pitch during the entrance, and unparks
  // whoever should (onthepitch/entrancecast.hpp).
  void BenchUnstagedPlayers(bool holdingOpeningFrame);
  void UpdateEntranceChoreo();
  // goal-replay camerawork pool (media/cutscenes/goal/*.camtrack) with each
  // track's authored goal side (+1/-1 from its mean x) for mirroring
  std::vector<CamTrack> goalCamTracks;
  // Which track is which, so a celebration can be filmed by the camera PES shot it
  // with instead of by whatever the score happened to index (goalcelebration.hpp).
  std::vector<std::string> goalCamNames;
  std::vector<GoalCelebration::Celebration> goalCelebrations;
  // Chosen when the goal goes in and held for the celebration: which performance the
  // scorer is giving, and the camera that belongs to it.
  int goalCelebrationIndex = -1;
  int goalCelebrationCamera = -1;
  // the turn that lays PES's celebration staging on this scorer, held for the goal
  float goalCelebrationYaw = 0.0f;
  // stoppage cutscenes: PES's other fixdemo categories, played at their
  // match-flow trigger points (halftime, cards, subs, penalties, fulltime)
  std::map<std::string, std::vector<CamTrack>> cutscenePools;
  const CamTrack* activeCutscene = nullptr;
  std::vector<CutsceneSequence::Stage> cutsceneQueue;
  // Whether PES filmed the running category at all.
  bool cutsceneHasCamera = false;
  // Where the goal celebration is staged: the scorer's run target, decided when the
  // celebration is chosen and held while it plays.
  Vector3 goalCelebrationSubject;

  // The incident's own place, held for the whole cutscene (see CutsceneAnchorPosition).
  Vector3 cutsceneAnchor;
  bool cutsceneAnchorLatched = false;
  // The cutscene's clock: accumulated from the match's frame deltas, so it stops when
  // the match does and can be skipped (CutscenePlayback).
  CutscenePlayback::State cutscenePlayback;
  unsigned long cutsceneStart_ms = 0;
  // When a running cutscene ends, on the *match* clock, because the only reader
  // (the goal replay trigger) compares it against actualTime_ms. It used to
  // inherit cutsceneStart_ms, which is the wall clock since the process
  // started: once any cutscene had run, the stamp sat ahead of the match clock
  // by every menu, load and pause, so the trigger waited on a moment that never
  // arrived and the goal replay was lost. 0 when nothing is playing.
  unsigned long cutsceneEnd_ms = 0;

 public:
  // plays a random track from the category's pool during the current
  // stoppage (no-op when the pool is empty or a cutscene is running)
  void StartCutscene(const std::string& category, float capSeconds);
  // Plays the next shot of a queued sequence - the closing ceremony is several.
  void StartNextQueuedCutscene();

 protected:
  // per-team chant loops (config "team1_chant"/"team2_chant"), gained up on goals
  boost::intrusive_ptr<Sound> teamChant[2];

  std::shared_ptr<AnimCollection> anims;

  // A snapshot taken at construction - deliberately a copy, not a reference to the
  // global controller list. Sides are bound by index (SideSelection::controllerID),
  // so if a pad is hotplugged while the match runs, the live vector shifts and every
  // index after the change silently means a different physical device: a pad that
  // joined mid-match could take a team over. Freezing the list here means the
  // selection only ever changes by going back to the select-sides screen, which
  // builds the next Match. Devices are erased from the global list on unplug but not
  // deleted until shutdown, so these pointers stay valid for the match's lifetime.
  const std::vector<IHIDevice*> controllers;

  std::unique_ptr<Ball> ball;

  std::vector<std::shared_ptr<MentalImage>>
      mentalImages;  // [index] == index * 10 ms ago ([0] == now)

  std::unique_ptr<Gui2ScoreBoard> scoreboard;
  // The bottom-corner player indicators, one per side (see playerhud.hpp).
  std::unique_ptr<Gui2PlayerHUD> playerHUD[2];
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
  // the body Match actually loaded (see playerbody.hpp)
  std::string playerBodyName;
  // how far this stadium's geometry reaches, from farplane.txt (stadiumfar.hpp)
  float stadiumFarNeeded = 0.0f;
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
  SkinWeights skinWeights;

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
  // Which cloth point each of those corners is a copy of.
  std::vector<int> nettingWeld[2];
  Cloth nettingCloth[2];
  unsigned long nettingTime_ms = 0;

  // A football net is light and tied down all round, so it barely sags - but it does
  // give, and that is the whole difference. Settled once at load over two seconds of
  // steps, it holds that pose until the ball arrives.
  static constexpr float kNettingGravity = 9.81f;
  static constexpr float kNettingDamping = 0.86f;
  static constexpr int kNettingIterations = 3;
  static constexpr float kNettingStep_s = 0.02f;
  static constexpr int kNettingSettleSteps = 100;
  static constexpr float kNettingSettled_m = 0.0004f;
  // Cloth::Push's radius for the ball comes from ballphysics.hpp's
  // kNettingPushRadius_m instead of a value here, so the constant that must
  // clear the net's own point spacing lives next to the tests that pin it.
  // How close to the woodwork, the ground or the rear support counts as tied to it.
  static constexpr float kNettingAttachment_m = 0.02f;

  std::vector<float*> flagMeshes[4];
  std::vector<int> flagWeld[4];
  Cloth flagCloth[4];
  unsigned long flagTime_ms = 0;
  bool flagsHaveChanged = false;

  // The pennant ring: one flag rather than four, held all the way round its
  // border like the netting rather than along one edge like a corner flag,
  // because two dozen men have hold of its rim and only the middle can sag.
  std::vector<float*> pennantMeshes;
  std::vector<int> pennantWeld;
  Cloth pennantCloth;
  bool pennantHasChanged = false;
  // How much of the flag's own radius counts as the rim the bearers are
  // holding. PES's ring is 8.27 m across and the hands grip its outermost
  // few centimetres; 3% is a hand's width at that scale.
  static constexpr float kPennantRimFraction = 0.97f;
  // A flag this wide with no bending stiffness will hang like a hammock if it
  // is left to itself, so it settles under a gentler gravity than the netting.
  static constexpr float kPennantGravity = 3.0f;
  // A flag being carried is held between a man's knees and his shoulders, and
  // his hands are within reach of the rim he has hold of. Wide enough for a
  // shorter or taller set of bearers, narrow enough that their feet and heads -
  // the other two bands at that radius - cannot be mistaken for hands.
  static constexpr float kPennantHandLow_m = 0.70f;
  static constexpr float kPennantHandHigh_m = 1.50f;
  static constexpr float kPennantHandReach_m = 0.60f;
  // What separates the flag from the men holding it. This ring's flag spans
  // 0.26 m vertically over 2,426 vertices; its bearers span 1.43 and 1.88, and
  // the four scraps of trim beside it are 11 to 32 vertices apiece.
  static constexpr float kPennantFlagThickness_m = 0.60f;
  static constexpr int kPennantFlagVertices = 200;
  static constexpr int kPennantSettleSteps = 40;

  // The corner flag's pole is 2 cm across and stands on a disc 20 cm across. Holding
  // everything within 12 cm of the axis keeps both rigid and still leaves the panels -
  // which reach 0.6 m out - free to hang.
  static constexpr float kFlagPoleRadius_m = 0.12f;
  static constexpr float kFlagFootBand_m = 0.05f;
  static constexpr float kFlagDamping = 0.94f;
  static constexpr int kFlagSettleSteps = 60;
  // A light breeze, and how fast it comes and goes. Strong enough to lift a flag off
  // its pole and nowhere near enough to stand it out straight.
  static constexpr float kFlagWind_mps2 = 3.4f;
  static constexpr float kFlagGustRate = 0.55f;
  static constexpr float kFlagWindHeading = 1.05f;

  // boost::intrusive_ptr<Light> lightTest[100];

  // todo: this is temporary
  bool _positionLogging;
  std::ofstream positionLogFile;

  // std::vector<MissingAnim> missingAnims;

  float matchDifficulty;
};

#endif
