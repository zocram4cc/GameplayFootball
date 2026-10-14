// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "team.hpp"

#include "../data/formstate.hpp"
#include "../gamedefines.hpp"
#include "simcontext.hpp"
#include "../utils.hpp"
#include "AIsupport/AIfunctions.hpp"
#include "match.hpp"
#include "offsiderule.hpp"
#include "pesstamina.hpp"
#include "player/humanoid/pesplayernode.hpp"
#include "referee.hpp"

Team::Team(int id, Match* match, TeamData* teamData) : id(id), match(match), teamData(teamData) {
  assert(id == 0 || id == 1);
  assert(teamData->GetPlayerNum() >= playerNum);  // does team have enough players?

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
  players.clear();

  teamController.reset();

  playerNode->Exit();
  playerNode.reset();
}

void Team::InitPlayers() {
  // first, build 1 instance of a player

  playerNode = BuildPlayerNode(match->GetRig());

  activePlayerCount = playerNum;

  Log(e_Notice, "Team", "Team", "Creating players");

  // The condition arrows for the coming match, one side at a time: the
  // pre-match "Condition: Home / Away" rows (match_condition_home/away) say
  // whether every player on the side is dealt the same arrow or draws his own
  // from his Form. Seeded off `match_condition_seed` and the player's own
  // index (FormState::ArrowForPlayer is a pure hash of the two plus the team),
  // so a side differs from match to match but not from player to player
  // within one, and a config that pins the seed deals the same arrows every
  // run. The wall clock only supplies the seed's DEFAULT, once, when the
  // pre-match sheet writes it (matchoptions.cpp) - it is not read here, and
  // saying it was sent a determinism hunt after the wrong suspect.
  const FormState::Policy conditionPolicy = FormState::ParsePolicy(GetConfiguration()->Get(
      id == 0 ? "match_condition_home" : "match_condition_away", "random"));
  const unsigned int conditionSeed =
      (unsigned int)GetConfiguration()->GetInt("match_condition_seed", 0);
  int arrowsDealt[(int)FormState::Arrow::Count] = {0, 0, 0, 0, 0};

  // load all players in the team, even the players who sit on the bench. aww.
  for (int i = 0; i < static_cast<int>(teamData->GetPlayerNum()); i++) {
    PlayerData* playerData = teamData->GetPlayerData(i);
    auto playerPtr = std::make_unique<Player>(this, playerData);
    Player* player = playerPtr.get();
    const FormState::Arrow arrow = FormState::ArrowForPlayer(
        conditionPolicy, playerData->GetStat("physical_form"), conditionSeed, id, i);
    player->SetConditionArrow(arrow);
    // PES's stamina tank at kick-off (docs/re/pes-re-stamina-drain.md 2): the
    // Stamina ability with PES's tiered scaling, the condition arrow the same
    // draw just dealt, the match length as F, and the full 100% freshness a
    // fresh player starts a match with. The tank, the starting value and the
    // breath reserve are all set here.
    {
      // PES's Stamina ability (40..99) is GF's physical_stamina on 0..1,
      // converted by the report's own range (ponytail: the range is the
      // roster's span, not a transcribed table).
      const int staminaAbility = static_cast<int>(
          std::lround(playerData->GetStat("physical_stamina") * (99.0f - 40.0f) + 40.0f));
      // F is the match length in minutes (report 2: F = the match-length
      // setting, live 10.0 under a 10-minute setting). This engine's
      // durationMinutes plays the same role.
      // The inline getters call the constants too, so the player holds a
      // copy of them alongside his state.
      player->SetStaminaConstants(match->GetPesStaminaConstants());
      pes::InitStamina(match->GetPesStaminaConstants(), staminaAbility, static_cast<int>(arrow),
                       0, static_cast<float>(match->GetMatchDurationMinutes()),
                       100, &player->GetStamina());
      player->GetStamina().breath =
          pes::BreathCap(match->GetPesStaminaConstants(), player->GetStamina());
    }
    arrowsDealt[(int)arrow]++;
    // Add the player to 'players' before Activate(), since Activate() (via
    // Player::GetFormationEntry() -> Team::GetFormationEntry(playerID)) looks
    // the player up by scanning this same vector.
    players.push_back(std::move(playerPtr));

    if (i < activePlayerCount) {
      // activate playerCount players (the starting eleven, usually)
      player->Activate(playerNode, match->GetAnimCollection());
    }
  }
  Log(e_Notice, "Team", "InitPlayers",
      std::string("condition arrows (") + FormState::PolicyName(conditionPolicy) +
          "): purple " + int_to_str(arrowsDealt[0]) + ", blue " + int_to_str(arrowsDealt[1]) +
          ", green " + int_to_str(arrowsDealt[2]) + ", orange " + int_to_str(arrowsDealt[3]) +
          ", red " + int_to_str(arrowsDealt[4]));

  designatedTeamPossessionPlayer = players.at(0).get();
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
  if (!GetConfiguration()->GetBool("substitutions_enabled", false))
    return false;
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

  playerIn->Activate(playerNode, match->GetAnimCollection());
  playerIn->ResetPosition(replacedPosition, Vector3(0));

  // Nobody may be left pointing at the player who just walked off.
  if (designatedTeamPossessionPlayer == playerOut)
    designatedTeamPossessionPlayer = playerIn;

  // Everything that cached the outgoing player has to let go of him: the match's
  // designated possession player and the mental images the AI reads.
  match->ReplacePlayerReferences(playerOut, playerIn);
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
  // PES 2021's ball-list reader FUN_14045e0d0 (docs/re/pes-re-offside.md 4):
  // a player offside-flagged in category 0 or 1 gets the impossible score, so
  // he is never the one who goes and gets the ball. This picker is that list -
  // the team's men sorted by how soon they reach the ball - and its consumers
  // (the designated possession player, the match's possession side) all read
  // from here.
  // ponytail: PES lists category 2 as a chaser too (a tight call the referee
  // holds), and only excludes the categories the ball list does; the key-player
  // reader FUN_14050f580 also excludes 2 and is not ported.
  Player* best = nullptr;
  Player* bestUnflagged = nullptr;
  int bestTime_ms = 10000000;
  int bestUnflaggedTime_ms = 10000000;
  for (unsigned int i = 0; i < players.size(); i++) {
    Player* active = players.at(i).get();
    if (!active->IsActive()) continue;
    const int time_ms = active->GetTimeNeededToGetToBall_ms();
    if (time_ms < bestTime_ms) {
      bestTime_ms = time_ms;
      best = active;
    }
    // A flag only keeps a man off the list, it never takes the list away from
    // everyone: with nobody left to send, the closest man goes anyway (PES's
    // list would read 0xff and the caller would take whatever it found).
    if (OffsideRule::OffsideBlocksChase(match->GetReferee()->GetOffsideCategory(active))) continue;
    if (time_ms < bestUnflaggedTime_ms) {
      bestUnflaggedTime_ms = time_ms;
      bestUnflagged = active;
    }
  }

  assert(best);
  return bestUnflagged ? bestUnflagged : best;
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

void Team::Process() {
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

  // PES's strategy decider runs every team tick and acts on dead balls, so it
  // sits outside the in-play gate below.
  teamController->UpdateStrategy();
  if (match->IsInPlay() && !match->IsInSetPiece()) {
    teamController->Process();

    // PES re-decides the whole man marking every frame (report 3, 14.2); GF
    // rebuilt it every 400 ms, which let a run go unmarked for half a second.
    teamController->CalculateManMarking();

    if ((match->GetActualTime_ms() + 200 * id) % 400 == 0) {
      teamController->CalculateDynamicRoles();
      // printf("dynamic roles calc team %i\n", id);
    }
  }

  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      players.at(i)->Process();
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

    // current player can get to the ball before the closest opponent: less need to switch
    // if (GetID() == 0) printf("opptime: %i, designated time: %i\n", oppTime_ms,
    // designatedPlayerTime_ms);
    if (designatedPlayerTime_ms < oppTime_ms - 100) {
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

Player* Team::GetGoalie() {
  for (unsigned int i = 0; i < players.size(); i++) {
    if (players.at(i)->IsActive()) {
      if (players.at(i)->GetFormationEntry().role == e_PlayerRole_GK)
        return players.at(i).get();
    }
  }

  return nullptr;
}

