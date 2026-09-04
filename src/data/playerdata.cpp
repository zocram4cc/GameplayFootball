// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "playerdata.hpp"

#include <algorithm>
#include <cstring>

#include "../main.hpp"
#include "base/utils.hpp"
#include "utils/database.hpp"

PlayerData::PlayerData(int playerDatabaseID) : databaseID(playerDatabaseID) {
  // std::string test = "select * from players where id = " + int_to_str(databaseID) + " limit 1";
  // printf("test: %s\n", test.c_str());

  auto result = GetDB()->Query(
      "select firstname, lastname, role, base_stat, profile_xml, age, skincolor, hairstyle, "
      "haircolor, height from players where id = " +
      int_to_str(databaseID) + " limit 1");

  std::string roleString;
  std::string profileString;
  float baseStat = 0.0f;
  int age = 15;

  skinColor = int(round(random(1, 4)));
  hairStyle = "short01";
  hairColor = "darkblonde";
  height = 1.8f;

  for (unsigned int c = 0; c < result->data.at(0).size(); c++) {
    if (result->header.at(c).compare("firstname") == 0)
      firstName = result->data.at(0).at(c);
    if (result->header.at(c).compare("lastname") == 0)
      lastName = result->data.at(0).at(c);
    if (result->header.at(c).compare("role") == 0)
      roleString = result->data.at(0).at(c);
    if (result->header.at(c).compare("base_stat") == 0)
      baseStat = atof(result->data.at(0).at(c).c_str());
    if (result->header.at(c).compare("profile_xml") == 0)
      profileString = result->data.at(0).at(c);
    if (result->header.at(c).compare("age") == 0)
      age = atoi(result->data.at(0).at(c).c_str());
    if (result->header.at(c).compare("skincolor") == 0)
      skinColor = atoi(result->data.at(0).at(c).c_str());
    if (result->header.at(c).compare("hairstyle") == 0)
      hairStyle = result->data.at(0).at(c);
    if (result->header.at(c).compare("haircolor") == 0)
      hairColor = result->data.at(0).at(c);
    if (result->header.at(c).compare("height") == 0)
      height = atof(result->data.at(0).at(c).c_str());
  }

  std::vector<std::string> roleStrings;
  tokenize(roleString, roleStrings);

  for (int i = 0; i < (signed int)roleStrings.size(); i++) {
    roles.push_back(GetRoleFromString(roleStrings.at(i)));
  }

  // get average stat for current age

  XMLLoader loader;
  XMLTree tree = loader.Load(profileString);

  // printf("player: %s, %s (age %i)\n", lastName.c_str(), firstName.c_str(), age);
  traits = PlayerTraits::traitMaskNone;
  playerAge = age;
  bool traitsFromDatabase = false;

  map_XMLTree::const_iterator iter = tree.children.begin();
  while (iter != tree.children.end()) {
    // The profile may carry a comma-separated list of traits alongside the
    // numeric stats (see SIMULATION_IMPROVEMENT_PROPOSAL 3A).
    if ((*iter).first.compare("traits") == 0) {
      traits = PlayerTraits::Parse((*iter).second.value);
      traitsFromDatabase = traits != PlayerTraits::traitMaskNone;
      iter++;
      continue;
    }

    float profileStat = atof((*iter).second.value.c_str());  // profile value

    float value = CalculateStat(baseStat, profileStat, age, e_DevelopmentCurveType_Normal);
    // printf("base: %f; profile: %f; result: %f\n", baseStat, profileStat, value);

    stats.Set((*iter).first.c_str(), value);
    iter++;
  }

  if (!traitsFromDatabase)
    AssignPlayingStyles();
}

PlayerData::PlayerData() {
  // officials, for example, use this constructor
  skinColor = int(round(random(1, 4)));
  hairStyle = "short01";
  hairColor = "darkblonde";
  height = 1.8f;

  stats.Set("physical_balance", 0.6);
  stats.Set("physical_reaction", 0.6);
  stats.Set("physical_acceleration", 0.6);
  stats.Set("physical_velocity", 0.6);
  stats.Set("physical_stamina", 0.6);
  stats.Set("physical_agility", 0.6);
  stats.Set("physical_shotpower", 0.6);
  stats.Set("technical_standingtackle", 0.6);
  stats.Set("technical_slidingtackle", 0.6);
  stats.Set("technical_ballcontrol", 0.6);
  stats.Set("technical_dribble", 0.6);
  stats.Set("technical_shortpass", 0.6);
  stats.Set("technical_highpass", 0.6);
  stats.Set("technical_header", 0.6);
  stats.Set("technical_shot", 0.6);
  stats.Set("technical_volley", 0.6);
  stats.Set("mental_calmness", 0.6);
  stats.Set("mental_workrate", 0.6);
  stats.Set("mental_resilience", 0.6);
  stats.Set("mental_defensivepositioning", 0.6);
  stats.Set("mental_offensivepositioning", 0.6);
  stats.Set("mental_vision", 0.6);

  traits = PlayerTraits::traitMaskNone;
  playerAge = MatchPressure::unknownAge;
}

PlayerData::~PlayerData() {}

const std::vector<e_PlayerRole>& PlayerData::GetRoles() const {
  return roles;
}

bool PlayerData::ToggleRole(e_PlayerRole role) {
  for (size_t i = 0; i < roles.size(); i++) {
    if (roles.at(i) != role) continue;
    // His listed position stays: something has to be first.
    if (i == 0) return true;
    roles.erase(roles.begin() + i);
    return false;
  }
  roles.push_back(role);
  return true;
}

void PlayerData::AssignPlayingStyles() {
  // Nothing in the database, so give him a style of his own: deterministic from
  // his id, suited to his position and his finishing.
  const e_PlayerRole role = roles.empty() ? e_PlayerRole_CM : roles.at(0);
  traits = PlayerTraits::AssignForPlayer(databaseID, role, stats.GetReal("technical_shot", 0.5f));
}

float PlayerData::GetAverageStat() const {
  static const char* kStats[] = {
      "physical_balance",           "physical_reaction",
      "physical_acceleration",      "physical_velocity",
      "physical_stamina",           "physical_agility",
      "physical_shotpower",         "technical_standingtackle",
      "technical_slidingtackle",    "technical_ballcontrol",
      "technical_dribble",          "technical_shortpass",
      "technical_highpass",         "technical_header",
      "technical_shot",             "technical_volley",
      "mental_calmness",            "mental_workrate",
      "mental_resilience",          "mental_defensivepositioning",
      "mental_offensivepositioning", "mental_vision"};
  const int count = sizeof(kStats) / sizeof(kStats[0]);
  float total = 0.0f;
  for (int i = 0; i < count; i++) total += stats.GetReal(kStats[i], 0.0f);
  return total / static_cast<float>(count);
}

float PlayerData::GetStat(const char* name) {
  bool exists = stats.Exists(name);
  if (!exists)
    printf("Stat named '%s' does not exist!\n", name);
  assert(exists);
  float value = stats.GetReal(name, 1.0f);

  // Traits bend the raw stats: a speed merchant is quicker off the mark, a
  // target man is stronger in the air (see SIMULATION_IMPROVEMENT_PROPOSAL 3A).
  if (traits != PlayerTraits::traitMaskNone) {
    if (std::strcmp(name, "physical_acceleration") == 0)
      value *= PlayerTraits::GetAccelerationMultiplier(traits);
    else if (std::strcmp(name, "technical_header") == 0)
      value *= PlayerTraits::GetHeaderMultiplier(traits);
    value = std::min(value, 1.0f);
  }

  return value;
}
