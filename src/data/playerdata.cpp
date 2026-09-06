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
  playingStyle = PlayingStyles::Player::None;
  comStyles = PlayingStyles::comMaskNone;
  playerAge = age;
  bool traitsFromDatabase = false;
  bool styleFromDatabase = false;
  bool comFromDatabase = false;

  map_XMLTree::const_iterator iter = tree.children.begin();
  while (iter != tree.children.end()) {
    const std::string& tag = (*iter).first;
    // The profile may carry a comma-separated list of traits alongside the
    // numeric stats (see SIMULATION_IMPROVEMENT_PROPOSAL 3A), and PES's own
    // Playing Style and COM cards. A tag that is present is PES's answer, even
    // "none": only an absent tag is inferred below.
    if (tag.compare("traits") == 0) {
      traits = PlayerTraits::Parse((*iter).second.value);
      traitsFromDatabase = traits != PlayerTraits::traitMaskNone;
      iter++;
      continue;
    }
    if (tag.compare("playing_style") == 0) {
      playingStyle = PlayingStyles::ParsePlayer((*iter).second.value);
      styleFromDatabase = true;
      iter++;
      continue;
    }
    if (tag.compare("com_styles") == 0) {
      comStyles = PlayingStyles::ParseCom((*iter).second.value);
      comFromDatabase = true;
      iter++;
      continue;
    }

    float profileStat = atof((*iter).second.value.c_str());  // profile value

    float value = CalculateStat(baseStat, profileStat, age, e_DevelopmentCurveType_Normal);
    // printf("base: %f; profile: %f; result: %f\n", baseStat, profileStat, value);

    stats.Set(tag.c_str(), value);
    iter++;
  }

  FillMissingStats();

  if (!traitsFromDatabase)
    AssignPlayingStyles();
  const e_PlayerRole role = roles.empty() ? e_PlayerRole_CM : roles.at(0);
  if (!styleFromDatabase)
    playingStyle = PlayingStyles::InferPlayer(databaseID, role, *this);
  if (!comFromDatabase)
    comStyles = PlayingStyles::InferCom(databaseID, playingStyle, *this);
}

void PlayerData::FillMissingStats() {
  // A partial profile (test fixtures, hand-written rows) gets the engine's
  // original keys at the middle, and profiles written before PES's remaining
  // attributes had keys get each of those from its nearest older neighbour -
  // so an incomplete database plays, not asserts, and the style inference
  // below can read any stat it likes.
  static const char* kCore[] = {
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
  for (const char* key : kCore)
    if (!stats.Exists(key)) stats.Set(key, 0.5f);
  struct Fallback {
    const char* key;
    const char* from;
  };
  static const Fallback kOutfield[] = {
      {"physical_jump", "technical_header"},
      {"physical_contact", "physical_balance"},
      {"technical_tightpossession", "technical_dribble"},
      {"technical_setpiece", "technical_shot"},
      {"technical_curl", "technical_highpass"},
      {"technical_interceptions", "mental_defensivepositioning"},
      {"technical_ballwinning", "technical_standingtackle"},
      {"mental_aggression", "mental_workrate"}};
  // A keeper's gk_* read off what the engine used for him until now; PES rates
  // an outfielder's goalkeeping at the floor.
  static const Fallback kKeeper[] = {{"gk_awareness", "mental_defensivepositioning"},
                                     {"gk_catching", "technical_ballcontrol"},
                                     {"gk_clearing", "technical_highpass"},
                                     {"gk_reflexes", "physical_reaction"},
                                     {"gk_coverage", "physical_agility"}};
  static const char* kMid[] = {"physical_form", "physical_injuryresistance",
                               "technical_weakfootusage", "technical_weakfootaccuracy"};
  const bool keeper = !roles.empty() && roles.at(0) == e_PlayerRole_GK;
  for (const Fallback& f : kOutfield)
    if (!stats.Exists(f.key)) stats.Set(f.key, stats.GetReal(f.from, 0.5f));
  for (const Fallback& f : kKeeper)
    if (!stats.Exists(f.key)) stats.Set(f.key, keeper ? stats.GetReal(f.from, 0.5f) : 0.1f);
  for (const char* key : kMid)
    if (!stats.Exists(key)) stats.Set(key, 0.5f);
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
  stats.Set("physical_jump", 0.6);
  stats.Set("physical_contact", 0.6);
  stats.Set("physical_form", 0.6);
  stats.Set("physical_injuryresistance", 0.6);
  stats.Set("technical_tightpossession", 0.6);
  stats.Set("technical_setpiece", 0.6);
  stats.Set("technical_curl", 0.6);
  stats.Set("technical_interceptions", 0.6);
  stats.Set("technical_ballwinning", 0.6);
  stats.Set("technical_weakfootusage", 0.6);
  stats.Set("technical_weakfootaccuracy", 0.6);
  stats.Set("mental_aggression", 0.6);
  stats.Set("gk_awareness", 0.6);
  stats.Set("gk_catching", 0.6);
  stats.Set("gk_clearing", 0.6);
  stats.Set("gk_reflexes", 0.6);
  stats.Set("gk_coverage", 0.6);

  traits = PlayerTraits::traitMaskNone;
  playingStyle = PlayingStyles::Player::None;
  comStyles = PlayingStyles::comMaskNone;
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
  // PES's overall rating is a mean of the skill ratings for the position the
  // player is listed at. Form, injury resistance and weak foot are traits of
  // his availability and habits, not of his level, and stay out; so do the
  // gk_* ratings of an outfielder (PES rates them at the floor for everyone
  // but keepers) and the outfield ratings of a keeper, who used to be scored
  // as a poor striker.
  static const char* kOutfield[] = {
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
      "mental_offensivepositioning", "mental_vision",
      "physical_jump",              "physical_contact",
      "technical_tightpossession",  "technical_setpiece",
      "technical_curl",             "technical_interceptions",
      "technical_ballwinning",      "mental_aggression"};
  static const char* kKeeper[] = {"gk_awareness", "gk_catching", "gk_clearing", "gk_reflexes",
                                  "gk_coverage"};
  const bool keeper = !roles.empty() && roles.at(0) == e_PlayerRole_GK;
  const char* const* keys = keeper ? kKeeper : kOutfield;
  const int count = keeper ? sizeof(kKeeper) / sizeof(kKeeper[0])
                           : sizeof(kOutfield) / sizeof(kOutfield[0]);
  float total = 0.0f;
  for (int i = 0; i < count; i++) total += stats.GetReal(keys[i], 0.0f);
  return total / static_cast<float>(count);
}

float PlayerData::GetStat(const char* name) const {
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
