// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "officials.hpp"

#include "../data/playerdata.hpp"
#include "match.hpp"
#include "player/humanoid/pesplayernode.hpp"
#include "player/playerofficial.hpp"
#include "simcontext.hpp"

Officials::Officials(Match* match, std::shared_ptr<AnimCollection> animCollection)
    : match(match) {
  boost::intrusive_ptr<Node> playerNode = BuildPlayerNode(match->GetRig());

  playerData = std::unique_ptr<PlayerData>(new PlayerData());
  referee = std::make_unique<PlayerOfficial>(e_OfficialType_Referee, match, playerData.get());
  linesmen[0] = std::make_unique<PlayerOfficial>(e_OfficialType_Linesman, match, playerData.get());
  linesmen[1] = std::make_unique<PlayerOfficial>(e_OfficialType_Linesman, match, playerData.get());

  referee->Activate(playerNode, animCollection);
  linesmen[0]->Activate(playerNode, animCollection);
  linesmen[1]->Activate(playerNode, animCollection);
  playerNode->Exit();
  playerNode.reset();

  referee->CastHumanoid()->ResetPosition(Vector3(10, -10, 0), Vector3(0));
  linesmen[0]->CastHumanoid()->ResetPosition(Vector3(25, -36.5, 0), Vector3(0));
  linesmen[1]->CastHumanoid()->ResetPosition(Vector3(-25, 36.5, 0), Vector3(0));
}

Officials::~Officials() {
  if (Verbose())
    printf("exiting officials.. ");
  referee.reset();
  linesmen[0].reset();
  linesmen[1].reset();
  playerData.reset();

  if (Verbose())
    printf("done\n");
}

void Officials::GetPlayers(std::vector<PlayerBase*>& players) {
  players.push_back(referee.get());
  players.push_back(linesmen[0].get());
  players.push_back(linesmen[1].get());
}

void Officials::Process() {
  referee->Process();
  linesmen[0]->Process();
  linesmen[1]->Process();
}

void Officials::PreparePutBuffers(unsigned long snapshotTime_ms) {
  referee->PreparePutBuffers(snapshotTime_ms);
  linesmen[0]->PreparePutBuffers(snapshotTime_ms);
  linesmen[1]->PreparePutBuffers(snapshotTime_ms);
}

void Officials::FetchPutBuffers(unsigned long putTime_ms) {
  referee->FetchPutBuffers(putTime_ms);
  linesmen[0]->FetchPutBuffers(putTime_ms);
  linesmen[1]->FetchPutBuffers(putTime_ms);
}

void Officials::Put() {
  referee->Put();
  linesmen[0]->Put();
  linesmen[1]->Put();
}
