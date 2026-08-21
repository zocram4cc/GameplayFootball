// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "team.hpp"

#include "../gamedefines.hpp"
#include "../main.hpp"
#include "../utils.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "managers/resourcemanagerpool.hpp"
#include "match.hpp"
#include "playercontrolsettings.hpp"
#include "utils/playermodelmap.hpp"

Team::Team(int id, Match* match, TeamData* teamData) : id(id), match(match), teamData(teamData) {
  assert(id == 0 || id == 1);
  assert(teamData->GetPlayerNum() >= playerNum);  // does team have enough players?

  teamNode = boost::intrusive_ptr<Node>(new Node("team node #" + int_to_str(id)));
  teamNode->SetLocalMode(e_LocalMode_Absolute);
  match->GetDynamicNode()->AddNode(teamNode);

  teamController = std::make_unique<TeamAIController>(this);

  timeNeededToGetToBall_ms = 100;
  hasPossession = false;

  teamPossessionAmount = 1.0;
  fadingTeamPossessionAmount = 1.0;

  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchPlayers[i] = 0;
  }
  lastTouchPlayer = 0;
  lastTouchType = e_TouchType_None;
}

Team::~Team() {}

void Team::Exit() {
  Hide2D();

  for (auto& humanGamer : humanGamers) {
    // std::unique_ptr will delete humanGamer
  }
  humanGamers.clear();
  for (auto& player : players) {
    // std::unique_ptr will delete player
  }
  players.clear();

  teamController.reset();

  playerNode->Exit();
  playerNode.reset();

  // Only kept so substitutes could be built; must not outlive the scene.
  fullbodyNode.reset();

  for (auto& customBody : customBodyNodes) {
    customBody->Exit();
  }
  customBodyNodes.clear();

  match->GetDynamicNode()->DeleteNode(teamNode);
}

// Activates a player on the model media/players/playermodels.cfg assigns him,
// falling back to the shared body. A substitute needs this as much as a starter:
// activating him on the shared fullbody left him wearing the stock body for the
// rest of the match, however carefully his own had been imported.
void Team::ActivateWithModel(Player* player, int formationIndex,
                             boost::intrusive_ptr<Node> fullbodyNode,
                             std::map<Vector3, Vector3>& colorCoords) {
  boost::intrusive_ptr<Resource<Surface>> kit = FetchKit(formationIndex);
  const std::string& modelDir = GetPlayerModelDir(player->GetPlayerData()->GetDatabaseID());
  if (modelDir.empty()) {
    player->Activate(playerNode, fullbodyNode, colorCoords, kit,
                     match->GetAnimCollection());
    return;
  }
  ObjectLoader loader;
  boost::intrusive_ptr<Node> customBody =
      loader.LoadObject(GetScene3D(), modelDir + "/fullbody.object");
  customBodyNodes.push_back(customBody);  // must not die before Exit()
  // the model's ase carries the directory name (unique resource key)
  const std::string& baseName = modelDir.substr(modelDir.find_last_of('/') + 1);
  std::map<Vector3, Vector3> customColors;
  GetVertexColors(customColors, modelDir + "/fullbody_" + baseName + ".ase");
  player->Activate(playerNode, customBody, customColors, kit,
                   match->GetAnimCollection());
}

void Team::InitPlayers(boost::intrusive_ptr<Node> fullbodyNode,
                       std::map<Vector3, Vector3>& colorCoords) {
  // first, load 1 instance of a player

  Log(e_Notice, "Team", "InitPlayers", "Loading player template instance");

  ObjectLoader loader;
  playerNode = loader.LoadObject(GetScene3D(), "media/objects/players/player.object");
  playerNode->SetName("player");
  playerNode->SetLocalMode(e_LocalMode_Absolute);

  activePlayerCount = playerNum;

  // Kept so substitutes can be activated mid-match.
  this->fullbodyNode = fullbodyNode;
  this->playerColorCoords = colorCoords;

  Log(e_Notice, "Team", "Team", "Creating players");

  // load all players in the team, even the players who sit on the bench. aww.
  for (int i = 0; i < static_cast<int>(teamData->GetPlayerNum()); i++) {
    PlayerData* playerData = teamData->GetPlayerData(i);
    auto playerPtr = std::make_unique<Player>(this, playerData);
    Player* player = playerPtr.get();
    // Add the player to 'players' before Activate(), since Activate() (via
    // Player::GetFormationEntry() -> Team::GetFormationEntry(playerID)) looks
    // the player up by scanning this same vector.
    players.push_back(std::move(playerPtr));

    if (i < activePlayerCount) {
      // activate playerCount players (the starting eleven, usually)
      ActivateWithModel(player, i, fullbodyNode, colorCoords);
    }
  }

  designatedTeamPossessionPlayer = players.at(0).get();
}

boost::intrusive_ptr<Resource<Surface>> Team::FetchKit(int formationIndex) {
  std::string kitFilename;
  if (teamData->GetFormationEntry(formationIndex).role != e_PlayerRole_GK) {
    // A kit picked on the match options screen wins over the fixture's default.
    const int configuredKit =
        GetConfiguration()->GetInt(("team" + int_to_str(GetID() + 1) + "_kit_num").c_str(), 0);
    const int kitNum = configuredKit > 0 ? configuredKit : GetMenuTask()->GetTeamKitNum(GetID());
    kitFilename = GetTeamData()->GetKitUrl() + "_kit_0" + int_to_str(kitNum) + ".png";
    if (!std::filesystem::exists(kitFilename))
      kitFilename =
          (GetID() == 0) ? "media/textures/almost_white.png" : "media/textures/almost_black.png";
  } else {
    kitFilename = "media/objects/players/textures/goalie_kit.png";
  }
  return ResourceManagerPool::GetInstance()
      .GetManager<Surface>(e_ResourceType_Surface)
      ->Fetch(kitFilename);
}

void Team::GetBenchPlayers(std::vector<Player*>& benchPlayers) {
  for (auto& player : players) {
    if (!player->IsActive() && !HasBeenSubstituted(player->GetID()))
      benchPlayers.push_back(player.get());
  }
}

bool Team::HasBeenSubstituted(int playerID) const {
  for (int id : substitutedPlayerIDs) {
    if (id == playerID)
      return true;
  }
  return false;
}

Substitutions::SquadView Team::DescribeSwap(Player* playerOut, Player* playerIn) const {
  Substitutions::SquadView squad;
  if (!playerOut || !playerIn)
    return squad;

  squad.playerOutIsOnPitch = playerOut->IsActive();
  squad.playerOutIsSentOff = playerOut->GetCards() > 1;
  squad.playerInIsOnBench = !playerIn->IsActive();
  squad.playerInHasPlayed = HasBeenSubstituted(playerIn->GetID());
  return squad;
}

bool Team::Substitute(Player* playerOut, Player* playerIn) {
  if (!playerOut || !playerIn || playerOut == playerIn)
    return false;
  // The shootout holds pointers to the taker and the keeper; taking either off
  // mid-shootout would pull the pitch out from under it.
  if (match->GetMatchPhase() == e_MatchPhase_Penalties)
    return false;

  // KNOWN BUG: building the substitute's humanoid segfaults (crash lands inside
  // Humanoid construction, right after "prepare full body model"). Until that is
  // found, swaps are opt-in through "substitutions_enabled" so a match cannot
  // crash under a player; the rules and the menu around it are complete.
  if (!GetConfiguration()->GetBool("substitutions_enabled", false)) {
    match->SpamMessage(teamData->GetShortName() + ": substitutions are disabled", 3000);
    return false;
  }
  if (!playerOut->IsActive() || playerIn->IsActive())
    return false;
  if (HasBeenSubstituted(playerIn->GetID()))
    return false;

  int indexOut = -1;
  int indexIn = -1;
  for (int i = 0; i < static_cast<int>(players.size()); i++) {
    if (players.at(i).get() == playerOut)
      indexOut = i;
    if (players.at(i).get() == playerIn)
      indexIn = i;
  }
  if (indexOut == -1 || indexIn == -1)
    return false;

  const Vector3 replacedPosition = playerOut->GetPosition();

  playerOut->Deactivate();
  substitutedPlayerIDs.push_back(playerOut->GetID());

  // The incoming player inherits the formation slot, which is addressed by
  // position in this vector, so the two players swap places in it.
  std::swap(players.at(indexOut), players.at(indexIn));

  ActivateWithModel(playerIn, indexOut, fullbodyNode, playerColorCoords);
  playerIn->ResetPosition(replacedPosition, Vector3(0));

  // Nobody may be left pointing at the player who just walked off.
  if (designatedTeamPossessionPlayer == playerOut)
    designatedTeamPossessionPlayer = playerIn;
  for (auto& humanGamer : humanGamers) {
    if (humanGamer->GetSelectedPlayerID() == playerOut->GetID())
      humanGamer->SetSelectedPlayerID(playerIn->GetID());
  }

  // Everything that cached the outgoing player has to let go of him: the match's
  // designated possession player, the mental images the AI reads, and the replay
  // spatials (the substitute's humanoid is a fresh set of scene nodes).
  match->ReplacePlayerReferences(playerOut, playerIn);
  match->RebuildReplaySpatials();

  match->SpamMessage(teamData->GetShortName() + ": " + playerIn->GetPlayerData()->GetLastName() +
                         " on for " + playerOut->GetPlayerData()->GetLastName(),
                     3000);
  return true;
}

signed int Team::GetSide() {
  signed int side;
  if (id == 0)
    side = -1;
  else
    side = 1;

  // -1 == left, 1 == right
  e_MatchPhase phase = match->GetMatchPhase();
  if (phase == e_MatchPhase_2ndHalf || phase == e_MatchPhase_2ndExtraTime)
    side *= -1;

  return side;
}

Player* Team::GetPlayer(int player_id) {
  for (auto& player : players) {
    if (player->GetID() == player_id) {
      return player.get();
    }
  }

  // id not found
  return nullptr;
}

PlayerData* Team::GetPlayerData(int playerID) {
  for (int i = 0; i < static_cast<int>(players.size()); i++) {
    if (players.at(i)->GetID() == playerID) {
      return teamData->GetPlayerData(i);
    }
  }

  assert(1 == 2);
  return nullptr;
}

FormationEntry Team::GetFormationEntry(int playerID) {
  for (int i = 0; i < static_cast<int>(players.size()); i++) {
    if (players.at(i)->GetID() == playerID) {
      return teamData->GetFormationEntry(i);
    }
  }

  assert(1 == 2);
  FormationEntry fail;
  return fail;
}

void Team::SetFormationEntry(int playerID, FormationEntry entry) {
  for (int i = 0; i < (signed int)players.size(); i++) {
    if (players.at(i)->GetID() == playerID) {
      teamData->SetFormationEntry(i, entry);
    }
  }
}

void Team::GetActivePlayers(std::vector<Player*>& activePlayers) {
  for (auto& player : players) {
    if (player->IsActive())
      activePlayers.push_back(player.get());
  }
}

void Team::AddHumanGamer(IHIDevice* hid, e_PlayerColor color) {
  auto humanGamer = std::make_unique<HumanGamer>(this, hid, color);

  humanGamer->SetSelectedPlayerID(
      AI_GetClosestPlayer(this, match->GetBall()->Predict(0).Get2D(), true)->GetID());

  humanGamers.push_back(std::move(humanGamer));

  switchPriority.push_back(humanGamers.size() - 1);
  designatedTeamPossessionPlayer =
      AI_GetClosestPlayer(this, match->GetBall()->Predict(0).Get2D(), false);
}

void Team::DeleteHumanGamers() {
  humanGamers.clear();
  switchPriority.clear();
}

e_PlayerColor Team::GetPlayerColor(int playerID) {
  for (unsigned int h = 0; h < humanGamers.size(); h++) {
    if (humanGamers.at(h)->GetSelectedPlayerID() == playerID)
      return humanGamers.at(h)->GetPlayerColor();
  }
  return e_PlayerColor_Default;
}

bool Team::IsHumanControlled(int playerID) {
  for (unsigned int h = 0; h < humanGamers.size(); h++) {
    if (humanGamers.at(h)->GetSelectedPlayerID() == playerID)
      return true;
  }
  return false;
}

bool Team::HasPossession() const {
  return hasPossession;
}

bool Team::HasUniquePossession() const {
  return HasPossession() && !match->GetTeam(abs(id - 1))->HasPossession();
}

int Team::GetTimeNeededToGetToBall_ms() const {
  return timeNeededToGetToBall_ms;
}

signed int Team::GetBestPossessionPlayerID() {
  return GetBestPossessionPlayer()->GetID();
}

Player* Team::GetBestPossessionPlayer() {
  int bestTime_ms = 10000000;
  Player* bestPlayer = nullptr;
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      int time_ms = players.at(i)->GetTimeNeededToGetToBall_ms();
      if (time_ms < bestTime_ms) {
        bestTime_ms = time_ms;
        bestPlayer = players.at(i).get();
      }
    }
  }

  assert(bestPlayer);

  return bestPlayer;
}

float Team::GetTeamPossessionAmount() const {
  return teamPossessionAmount;
}

float Team::GetFadingTeamPossessionAmount() const {
  return fadingTeamPossessionAmount;
}

void Team::SetFadingTeamPossessionAmount(float value) {
  fadingTeamPossessionAmount = clamp(value, 0.5, 1.5);
}

void Team::SetLastTouchPlayer(Player* player, e_TouchType touchType) {
  lastTouchPlayers[touchType] = player;
  lastTouchPlayer = player;
  lastTouchType = touchType;
  player->SetLastTouchTime_ms(match->GetActualTime_ms());
  player->SetLastTouchType(lastTouchType);
  match->SetLastTouchTeamID(GetID(), touchType);
}

void Team::ResetSituation(const Vector3& focusPos) {
  timeNeededToGetToBall_ms = 100;
  hasPossession = false;

  teamPossessionAmount = 1.0f;
  fadingTeamPossessionAmount = 1.0f;

  for (unsigned int i = 0; i < e_TouchType_SIZE; i++) {
    lastTouchPlayers[i] = 0;
  }
  lastTouchPlayer = 0;
  lastTouchType = e_TouchType_None;

  designatedTeamPossessionPlayer = players.at(0).get();

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->ResetSituation(focusPos);
    }
  }

  GetController()->Reset();
}

void Team::HumanGamersSelectAnyone() {
  // make sure all human gamers have a player selected

  if (match->IsInPlay()) {
    for (unsigned int i = 0; i < humanGamers.size(); i++) {
      if (humanGamers.at(i)->GetSelectedPlayerID() == -1) {
        int playerID =
            AI_GetClosestPlayer(this, match->GetBall()->Predict(0).Get2D(), true)->GetID();
        humanGamers.at(i)->SetSelectedPlayerID(playerID);
      }
    }
  }
}

void Team::SelectPlayer(Player* player) {
  // printf("trying to switch to %s\n", player->GetPlayerData()->GetLastName().c_str());
  if (AllowsAutomaticPlayerSelection(ReadConfiguredPlayerSwitchMode(*GetConfiguration())) &&
      !IsHumanControlled(player->GetID()) && humanGamers.size() != 0) {  // already selected
    humanGamers.at(*switchPriority.begin())->SetSelectedPlayerID(player->GetID());
    switchPriority.push_back(*switchPriority.begin());
    switchPriority.pop_front();
    if (Verbose())
      printf("switched player to %s\n", player->GetPlayerData()->GetLastName().c_str());
  }
  designatedTeamPossessionPlayer = player;
}

void Team::DeselectPlayer(Player* player) {
  for (unsigned int i = 0; i < humanGamers.size(); i++) {
    int selectedPlayerID = humanGamers.at(i)->GetSelectedPlayerID();
    if (selectedPlayerID == player->GetID()) {
      Player* somePlayer = AI_GetClosestPlayer(this, player->GetPosition(), true, player);
      if (somePlayer) {
        humanGamers.at(i)->SetSelectedPlayerID(somePlayer->GetID());
      } else {
        humanGamers.at(i)->SetSelectedPlayerID(-1);
      }
    }
  }
}

void Team::RelaxFatigue(float howMuch) {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->RelaxFatigue(howMuch);
    }
  }
}

void Team::Process() {
  if (!match->GetPause()) {
    const bool fullyManualSwitching = UsesFullyManualPlayerSwitching(*GetConfiguration());
    teamPossessionAmount =
        (float)(match->GetTeam(abs(GetID() - 1))->GetTimeNeededToGetToBall_ms() + 1500) /
        (float)(GetTimeNeededToGetToBall_ms() + 1500);
    float tmpFadingTeamPossessionAmount =
        fadingTeamPossessionAmount * 0.995f + clamp(teamPossessionAmount, 0.5f, 1.5f) * 0.005f;
    fadingTeamPossessionAmount += clamp(tmpFadingTeamPossessionAmount - fadingTeamPossessionAmount,
                                        -0.005f, 0.005f);  // maximum change per 10ms

    if (!match->IsInPlay() || match->IsInSetPiece() || match->GetBallRetainer() != 0) {
      if (match->GetBallRetainer() != 0) {
        fadingTeamPossessionAmount = teamPossessionAmount =
            (match->GetBallRetainer()->GetTeamID() == GetID()) ? 1.5f : 0.5f;
      } else {
        fadingTeamPossessionAmount = teamPossessionAmount =
            (match->GetBestPossessionTeamID() == GetID()) ? 1.5f : 0.5f;
      }
    }

    HumanGamersSelectAnyone();

    if (match->IsInPlay() && !match->IsInSetPiece()) {
      teamController->Process();

      if ((match->GetActualTime_ms() + 200 * id) % 400 == 0) {
        teamController->CalculateDynamicRoles();
        // printf("dynamic roles calc team %i\n", id);
      }

      if ((match->GetActualTime_ms() + 200 * id + 100) % 400 == 0) {
        teamController->CalculateManMarking();
        // printf("man marking calc team %i\n", id);
      }
    }

    for (unsigned int i = 0; i < players.size(); i++) {
      if (players.at(i)->IsActive()) {
        players.at(i)->Process();
      }
    }

    if (match->IsInPlay()) {
      for (unsigned int i = 0; i < humanGamers.size(); i++) {
        // switch button
        int selectedPlayerID = humanGamers.at(i)->GetSelectedPlayerID();
        Player* selectedPlayer = nullptr;
        selectedPlayer = GetPlayer(selectedPlayerID);
        assert(selectedPlayer);

        if (humanGamers.at(i)->GetHIDevice()->GetButton(e_ButtonFunction_Switch) &&
            !humanGamers.at(i)->GetHIDevice()->GetPreviousButtonState(e_ButtonFunction_Switch) &&
            (fullyManualSwitching ||
             ((!(selectedPlayerID == GetBestPossessionPlayerID() &&
                 selectedPlayerID == designatedTeamPossessionPlayer->GetID()) ||
               GetTeamPossessionAmount() < 1.0f) &&
              !selectedPlayer->HasUniquePossession()))) {
          int targetPlayerID = -1;
          Player* targetPlayer = nullptr;

          if (!IsHumanControlled(designatedTeamPossessionPlayer->GetID()) &&
              match->GetBestPossessionTeamID() == GetID()) {
            targetPlayer = designatedTeamPossessionPlayer;
          } else if (!IsHumanControlled(GetBestPossessionPlayer()->GetID()) &&
                     match->GetBestPossessionTeamID() == GetID()) {
            targetPlayer = GetBestPossessionPlayer();
          } else {
            targetPlayer = AI_GetBestSwitchTargetPlayer(
                match, this, humanGamers.at(i)->GetHIDevice()->GetDirection());
            if (targetPlayer)
              if (IsHumanControlled(targetPlayer->GetID()))
                targetPlayer = 0;
          }
          if (targetPlayer == GetGoalie())
            targetPlayer = 0;  // can not be goalie in current version, at least not during play,
                               // unless being directly passed to by teammate

          if (targetPlayer) {
            targetPlayerID = targetPlayer->GetID();
          }
          if (targetPlayerID != -1)
            humanGamers.at(i)->SetSelectedPlayerID(targetPlayerID);
        }
      }

    } else if (!fullyManualSwitching) {
      // make sure all human gamers don't have a player selected

      for (unsigned int i = 0; i < humanGamers.size(); i++) {
        if (humanGamers.at(i)->GetSelectedPlayerID() != -1) {
          humanGamers.at(i)->SetSelectedPlayerID(-1);
        }
      }
    }

    int designatedPlayerTime_ms = designatedTeamPossessionPlayer->GetTimeNeededToGetToBall_ms();
    Player* bestPlayer = GetBestPossessionPlayer();
    int oppTime_ms = match->GetTeam(abs(GetID() - 1))->GetTimeNeededToGetToBall_ms();
    if (designatedTeamPossessionPlayer != bestPlayer) {
      // switch only if other player is somewhat better, to overcome possession-chaos
      int bestPlayerTime_ms = bestPlayer->GetTimeNeededToGetToBall_ms();
      float timeRating = (float)(bestPlayerTime_ms + 500) / (float)(designatedPlayerTime_ms + 500);

      if (bestPlayer->HasPossession())
        timeRating *= 0.5f;
      if (designatedTeamPossessionPlayer->HasPossession())
        timeRating /= 0.5f;

      if (IsHumanControlled(bestPlayer->GetID()))
        timeRating *= 0.8f;
      if (IsHumanControlled(designatedTeamPossessionPlayer->GetID()))
        timeRating /= 0.8f;

      // current player can get to the ball before the closest opponent: less need to switch
      // if (GetID() == 0) printf("opptime: %i, designated time: %i\n", oppTime_ms,
      // designatedPlayerTime_ms);
      if (IsHumanControlled(bestPlayer->GetID()) == false &&
          designatedPlayerTime_ms < oppTime_ms - 100) {
        timeRating += 0.2f;
        timeRating *= 1.2f;
      }

      if (timeRating < 0.8f) {
        designatedTeamPossessionPlayer = bestPlayer;
      }
    }

    // printf("team id: %i, time: %i, other team id: %i, time: %i\n", GetID(),
    // GetTimeNeededToGetToBall_ms(), match->GetTeam(abs(GetID() - 1))->GetID(),
    // match->GetTeam(abs(GetID() - 1))->GetTimeNeededToGetToBall_ms());

    /*
      if (id == 0) {
        GetSmallDebugCircle1()->SetPosition(designatedTeamPossessionPlayer->GetPosition());
      } else {
        GetSmallDebugCircle2()->SetPosition(designatedTeamPossessionPlayer->GetPosition());
      }
    */
  }
}

void Team::PreparePutBuffers(unsigned long snapshotTime_ms) {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->PreparePutBuffers(snapshotTime_ms);
    }
  }
}

void Team::FetchPutBuffers(unsigned long putTime_ms) {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->FetchPutBuffers(putTime_ms);
    }
  }
}

void Team::Put() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->Put();
    }
  }
}

void Team::Put2D() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->Put2D();
    }
  }
}

void Team::Hide2D() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->Hide2D();
    }
  }
}

void Team::UpdatePossessionStats() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->UpdatePossessionStats();
    }
  }

  // possession?

  hasPossession = false;
  timeNeededToGetToBall_ms = 100000;
  for (int i = 0; i < (signed int)players.size(); i++) {
    if (players.at(i)->IsActive()) {
      if (players.at(i)->HasPossession())
        hasPossession = true;
      if (players.at(i)->GetTimeNeededToGetToBall_ms() < timeNeededToGetToBall_ms)
        timeNeededToGetToBall_ms = players.at(i)->GetTimeNeededToGetToBall_ms();
    }
  }
}

void Team::UpdateSwitch() {
  // lose turn on ball possession

  if (match->IsInPlay() && humanGamers.size() > 1) {
    int myTurn = *switchPriority.begin();
    if (humanGamers.at(myTurn)->GetSelectedPlayerID() ==
        match->GetDesignatedPossessionPlayer()->GetID()) {
      switchPriority.pop_front();
      switchPriority.push_back(myTurn);
    }
  }

  // autoswitch on proximity

  /* recently disabled
    if (match->IsInPlay() && humanGamers.size() > 0) {
      if (!IsHumanControlled(designatedTeamPossessionPlayer->GetID()) &&
          designatedTeamPossessionPlayer->GetTimeNeededToGetToBall_ms() < 2000 && // proximity
          designatedTeamPossessionPlayer->GetTimeNeededToGetToBall_ms() <=
    this->GetTimeNeededToGetToBall_ms() && // sometimes, the designated team possession player is
    not the player quickest to ball. don't autoswitch then GetTeamPossessionAmount() > 1.3f)
    SelectPlayer(designatedTeamPossessionPlayer);
    }
  */

  // if (GetID() == 0) printf("teamposs %f\n", GetTeamPossessionAmount());

  // team player in possession is not human selected

  if (match->IsInPlay() && humanGamers.size() > 0) {
    if (!IsHumanControlled(designatedTeamPossessionPlayer->GetID()) &&
        (designatedTeamPossessionPlayer->HasUniquePossession() || match->IsInSetPiece())) {
      if (designatedTeamPossessionPlayer != GetGoalie()) {
        SelectPlayer(designatedTeamPossessionPlayer);
      }
    }
  }
}

Player* Team::GetGoalie() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      if (players.at(i)->GetFormationEntry().role == e_PlayerRole_GK)
        return players.at(i).get();
    }
  }

  return nullptr;
}

void Team::SetKitNumber(int num) {
  std::string kitNumberString = int_to_str(num);
  if (kitNumberString.size() < 2)
    kitNumberString = "0" + kitNumberString;
  std::string kitFilename = GetTeamData()->GetKitUrl() + "_kit_" + kitNumberString + ".png";
  if (!std::filesystem::exists(kitFilename))
    kitFilename = GetID() == 0 ? "media/textures/white.png" : "media/textures/black.png";

  // new kits on the block!
  boost::intrusive_ptr<Resource<Surface>> newKit = ResourceManagerPool::GetInstance()
                                                       .GetManager<Surface>(e_ResourceType_Surface)
                                                       ->Fetch(kitFilename);

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      if (players.at(i)->GetFormationEntry().role != e_PlayerRole_GK)
        players.at(i)->SetKit(newKit);
    }
  }

  kit = newKit;
}
