// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "gametask.hpp"

#include "blunted.hpp"
#include "framework/scheduler.hpp"
#include "main.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "managers/taskmanager.hpp"
#include "onthepitch/player/humanoid/skinning.hpp"

void UploadFullbodyModel::Update() {
  for (unsigned int i = 0; i < geometryToUpload.size(); i++) {
    geometryToUpload.at(i)->OnUpdateGeometryData(false);
  }
}

GameTask::GameTask() {
  match = 0;
  menuScene = 0;

  // prohibits deletion of the scene before this object is dead
  scene3D = GetScene3D();
}

GameTask::~GameTask() {
  if (Verbose())
    printf("exiting gametask.. ");
  Exit();
  if (Verbose())
    printf("done\n");
}

void GameTask::Exit() {
  Action(e_GameTaskMessage_StopMatch);
  Action(e_GameTaskMessage_StopMenuScene);

  ResourceManagerPool::GetInstance().CleanUp();

  scene3D.reset();
}

void GameTask::Action(e_GameTaskMessage message) {
  switch (message) {
    case e_GameTaskMessage_StartMatch: {
      if (Verbose())
        printf("*gametaskmessage: starting match\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      MatchData* matchData = GetMenuTask()->GetMatchData();
      assert(matchData);
      Match* tmpMatch = new Match(matchData, GetControllers());

      matchLifetimeMutex.lock();
      matchPutBufferMutex.lock();
      assert(!match);
      match = tmpMatch;
      GetScheduler()->ResetTaskSequenceTime("game");
      matchPutBufferMutex.unlock();
      matchLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
    } break;

    case e_GameTaskMessage_StopMatch:
      if (Verbose())
        printf("*gametaskmessage: stopping match\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      // before the humanoids these uploads point into are deleted
      DrainPendingUploads();
      matchLifetimeMutex.lock();
      matchPutBufferMutex.lock();
      // assert(match);
      if (match) {
        match->Exit();
        delete match;
        match = 0;
      }
      matchPutBufferMutex.unlock();
      matchLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
      break;

    case e_GameTaskMessage_StartMenuScene:
      if (Verbose())
        printf("*gametaskmessage: starting menu scene\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      menuSceneLifetimeMutex.lock();
      assert(!menuScene);
      menuScene = new MenuScene();
      GetScheduler()->ResetTaskSequenceTime("game");
      menuSceneLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
      break;

    case e_GameTaskMessage_StopMenuScene:
      if (Verbose())
        printf("*gametaskmessage: stopping menu scene\n");

      GetGraphicsSystem()->getPhaseMutex.lock();
      DrainPendingUploads();
      menuSceneLifetimeMutex.lock();
      // assert(menuScene);
      if (menuScene) {
        delete menuScene;
        menuScene = 0;
      }
      menuSceneLifetimeMutex.unlock();
      GetGraphicsSystem()->getPhaseMutex.unlock();
      break;

    default:
      break;
  }
}

void GameTask::GetPhase() {
  // process messageQueue
  if (match)
    match->Get();
  if (menuScene)
    menuScene->Get();
}

void GameTask::ProcessPhase() {
  for (unsigned int i = 0; i < GetControllers().size(); i++) {
    GetControllers().at(i)->Process();
  }

  if (match) {
    match->Process();

    // Executing a substitution rebuilds humanoid scene nodes that the
    // graphics thread's PutPhase walks outside matchPutBufferMutex, so it
    // needs the substitution lock exclusively. Only pay for that on the rare
    // frame that has one queued; a request that slips in after this check
    // simply waits for the next Process frame.
    std::unique_lock<std::shared_mutex> substitutionLock(matchSubstitutionMutex, std::defer_lock);
    if (match->HasPendingSubstitutions())
      substitutionLock.lock();

    matchPutBufferMutex.lock();
    if (substitutionLock.owns_lock())
      match->ExecutePendingSubstitutions();
    match->PreparePutBuffers();
    matchPutBufferMutex.unlock();
  }

  if (menuScene) {
    menuScene->Process();
  }
}

void GameTask::DrainPendingUploads() {
  std::vector<boost::intrusive_ptr<UploadFullbodyModel>> collected;
  {
    std::lock_guard<std::mutex> lock(pendingUploadsMutex);
    collected.swap(pendingUploads);
  }
  // Waited on outside the lock: a worker finishing one of these does not need to
  // queue behind whoever is draining.
  for (unsigned int i = 0; i < collected.size(); i++) {
    collected.at(i)->Wait();
  }
}

void GameTask::PutPhase() {
  // Collect last frame's uploads before touching anything they hold.
  DrainPendingUploads();
  std::vector<boost::intrusive_ptr<UpdateFullbodyModel>> updateFullbodyModels;
  std::vector<boost::intrusive_ptr<UploadFullbodyModel>> uploadFullbodyModels;
  std::vector<PlayerBase*> playersToProcess;

  matchLifetimeMutex.lock();

  // Everything below reads humanoid scene nodes, the team player lists and
  // the replay spatials; hold the substitution lock (shared) so a substitution
  // executing on the game thread cannot rebuild them mid-put.
  matchSubstitutionMutex.lock_shared();

  if (match) {
    matchPutBufferMutex.lock();
    match->FetchPutBuffers();
    matchPutBufferMutex.unlock();

    match->Put();

    std::vector<Player*> players;
    match->GetActiveTeamPlayers(0, players);
    match->GetActiveTeamPlayers(1, players);
    std::vector<PlayerBase*> officials;
    match->GetOfficialPlayers(officials);

    for (unsigned int i = 0; i < players.size(); i++) {
      if (match->GetPause() || players.at(i)->NeedsModelUpdate())
        playersToProcess.push_back(players.at(i));
    }
    for (unsigned int i = 0; i < officials.size(); i++) {
      playersToProcess.push_back(officials.at(i));
    }

    // Spread the bodies over the whole worker pool. This was a hardcoded 7,
    // which on any machine with more than four cores left most of the pool idle
    // while a handful of workers skinned seven bodies each in turn.
    unsigned int playersPerThread = Skinning::BatchSize(
        playersToProcess.size(), TaskManager::GetInstance().GetWorkerThreadCount());
    unsigned int playerStartIndex = 0;
    while (playerStartIndex < playersToProcess.size()) {
      std::vector<PlayerBase*> playersToProcessInThread;
      for (unsigned int p = 0; p < playersPerThread; p++) {
        if (playerStartIndex + p >= playersToProcess.size())
          break;
        playersToProcessInThread.push_back(playersToProcess.at(playerStartIndex + p));
        // printf("adding player %i\n", playerStartIndex + p);
        //  unthreaded version: playersToProcess.at(playerStartIndex + p)->UpdateFullbodyModel();
      }
      playerStartIndex += playersPerThread;

      boost::intrusive_ptr<UpdateFullbodyModel> updateFullbodyModel(
          new UpdateFullbodyModel(playersToProcessInThread));
      updateFullbodyModels.push_back(updateFullbodyModel);
      TaskManager::GetInstance().EnqueueWork(updateFullbodyModel, true);
    }

    match->UploadGoalNetting();  // won't this block the whole process thing too? (opengl busy ==
                                 // wait, while mutex locked == no process)
  }

  for (unsigned int t = 0; t < updateFullbodyModels.size(); t++) {
    updateFullbodyModels.at(t)->Wait();
  }

  if (match) {
    unsigned int playersPerThread = Skinning::BatchSize(
        playersToProcess.size(), TaskManager::GetInstance().GetWorkerThreadCount());
    unsigned int playerStartIndex = 0;
    while (playerStartIndex < playersToProcess.size()) {
      std::vector<boost::intrusive_ptr<Geometry>> geometryToUploadInThread;
      for (unsigned int p = 0; p < playersPerThread; p++) {
        if (playerStartIndex + p >= playersToProcess.size())
          break;
        geometryToUploadInThread.push_back(boost::static_pointer_cast<Geometry>(
            playersToProcess.at(playerStartIndex + p)->GetFullbodyNode()->GetObject("fullbody")));
      }
      playerStartIndex += playersPerThread;

      boost::intrusive_ptr<UploadFullbodyModel> uploadFullbodyModel(
          new UploadFullbodyModel(geometryToUploadInThread));
      uploadFullbodyModels.push_back(uploadFullbodyModel);
      TaskManager::GetInstance().EnqueueWork(uploadFullbodyModel, true);

      // working on: maybe we need to use the gfx system get pointer somewhere here? too tired to
      // analyse this now :p
    }

  }  // !match

  // Held until the next put phase collects them; see pendingUploads.
  {
    std::lock_guard<std::mutex> lock(pendingUploadsMutex);
    pendingUploads = uploadFullbodyModels;
  }

  matchSubstitutionMutex.unlock_shared();
  matchLifetimeMutex.unlock();

  menuSceneLifetimeMutex.lock();
  if (menuScene)
    menuScene->Put();
  menuSceneLifetimeMutex.unlock();
}
