// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "utils.hpp"

#include <algorithm>
#include <cctype>

#include "base/log.hpp"
#include "base/utils.hpp"
#include "simcontext.hpp"

float GetQuantizedDirectionBias() {
  return GetConfiguration()->GetReal("gameplay_quantizeddirectionbias",
                                     _default_QuantizedDirectionBias);
}

void QuantizeDirection(Vector3& inputDirection, float bias) {
  // digitize input

  Vector3 inputDirectionNorm = inputDirection.GetNormalized(0);

  int directions = GetConfiguration()->GetInt("gameplay_quantizeddirectioncount", 8);

  radian angle = inputDirectionNorm.GetAngle2D();
  angle /= pi * 2.0f;
  angle = round(angle * directions);
  angle /= directions;
  angle *= pi * 2.0f;

  inputDirection =
      (inputDirectionNorm * (1.0 - bias) + (Vector3(1, 0, 0).GetRotated2D(angle) * bias))
          .GetNormalized(inputDirectionNorm) *
      inputDirection.GetLength();
}

int GetVelocityID(e_Velocity velo, bool treatDribbleAsWalk) {
  int id = 0;
  switch (velo) {
    case e_Velocity_Idle:
      id = 0;
      break;
    case e_Velocity_Dribble:
      id = 1;
      break;
    case e_Velocity_Walk:
      id = 2;
      break;
    case e_Velocity_Sprint:
      id = 3;
      break;
    default:
      id = 0;
      break;
  }
  if (treatDribbleAsWalk && id > 1)
    id--;
  return id;
}

std::map<e_PositionName, std::vector<Stat>> defaultProfiles;

e_PositionName GetPositionName(const std::string& shortcut) {
  if (shortcut == "GK")
    return e_PositionName_GK;
  if (shortcut == "SW")
    return e_PositionName_SW;
  if (shortcut == "D")
    return e_PositionName_D;
  if (shortcut == "WB")
    return e_PositionName_WB;
  if (shortcut == "DM")
    return e_PositionName_DM;
  if (shortcut == "M")
    return e_PositionName_M;
  if (shortcut == "AM")
    return e_PositionName_AM;
  if (shortcut == "F")
    return e_PositionName_F;
  if (shortcut == "ST")
    return e_PositionName_ST;

  Log(e_FatalError, "utils.cpp", "GetPositionName", "Position '" + shortcut + "' not found!");
  return e_PositionName_M;
}

void GetWeightedPositions(const std::string& positionString,
                          std::vector<WeightedPosition>& weightedPositions) {
  // split by comma
  std::vector<std::string> positions;
  tokenize(positionString, positions, ",");
  for (unsigned int i = 0; i < positions.size(); i++) {
    // trim leading/trailing space
    {
      std::string& s = positions.at(i);
      s.erase(s.begin(),
              std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
      s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); })
                  .base(),
              s.end());
    }
    // remove L/C/R notice (after space)
    std::size_t firstspace = positions.at(i).find_first_of(" ");
    if (firstspace != std::string::npos)
      positions.at(i) = positions.at(i).substr(0, firstspace);

    if (positions.at(i).find_first_of("/") != std::string::npos) {
      // position mixture
      std::vector<std::string> subpositions;
      tokenize(positions.at(i), subpositions, "/");
      for (unsigned int j = 0; j < subpositions.size(); j++) {
        WeightedPosition weightedPosition;
        weightedPosition.positionName = GetPositionName(subpositions.at(j));
        weightedPosition.weight = 1.0 / subpositions.size();
        weightedPositions.push_back(weightedPosition);
      }
    } else {
      // just one position
      WeightedPosition weightedPosition;
      weightedPosition.positionName = GetPositionName(positions.at(i));
      weightedPosition.weight = 1.0;
      weightedPositions.push_back(weightedPosition);
    }
  }

  /*
  for (unsigned int i = 0; i < weightedPositions.size(); i++) {
    printf("[%i:%f]", weightedPositions.at(i).positionName, weightedPositions.at(i).weight);
  }
  printf("\n");
  */
}

void InitDefaultProfiles() {
  // std::map < e_PositionName, std::vector<Stat> > defaultProfiles;

  // todo: replace push_back's by emplace_back? (needs compiler flag though)

  std::vector<Stat> statsGK;
  statsGK.push_back(Stat{"physical_balance", 0.6});
  statsGK.push_back(Stat{"physical_reaction", 0.8});
  statsGK.push_back(Stat{"physical_acceleration", 0.5});
  statsGK.push_back(Stat{"physical_velocity", 0.4});
  statsGK.push_back(Stat{"physical_stamina", 0.5});
  statsGK.push_back(Stat{"physical_agility", 0.6});
  statsGK.push_back(Stat{"physical_shotpower", 0.6});
  statsGK.push_back(Stat{"technical_standingtackle", 0.2});
  statsGK.push_back(Stat{"technical_slidingtackle", 0.2});
  statsGK.push_back(Stat{"technical_ballcontrol", 0.3});
  statsGK.push_back(Stat{"technical_dribble", 0.2});
  statsGK.push_back(Stat{"technical_shortpass", 0.6});
  statsGK.push_back(Stat{"technical_highpass", 0.7});
  statsGK.push_back(Stat{"technical_header", 0.2});
  statsGK.push_back(Stat{"technical_shot", 0.3});
  statsGK.push_back(Stat{"technical_volley", 0.2});
  statsGK.push_back(Stat{"mental_calmness", 0.5});
  statsGK.push_back(Stat{"mental_workrate", 0.5});
  statsGK.push_back(Stat{"mental_resilience", 0.5});
  statsGK.push_back(Stat{"mental_defensivepositioning", 0.9});
  statsGK.push_back(Stat{"mental_offensivepositioning", 0.1});
  statsGK.push_back(Stat{"mental_vision", 0.5});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_GK, statsGK));

  std::vector<Stat> statsSW;
  statsSW.push_back(Stat{"physical_balance", 0.7});
  statsSW.push_back(Stat{"physical_reaction", 0.7});
  statsSW.push_back(Stat{"physical_acceleration", 0.5});
  statsSW.push_back(Stat{"physical_velocity", 0.5});
  statsSW.push_back(Stat{"physical_stamina", 0.5});
  statsSW.push_back(Stat{"physical_agility", 0.4});
  statsSW.push_back(Stat{"physical_shotpower", 0.7});
  statsSW.push_back(Stat{"technical_standingtackle", 0.8});
  statsSW.push_back(Stat{"technical_slidingtackle", 0.8});
  statsSW.push_back(Stat{"technical_ballcontrol", 0.3});
  statsSW.push_back(Stat{"technical_dribble", 0.2});
  statsSW.push_back(Stat{"technical_shortpass", 0.4});
  statsSW.push_back(Stat{"technical_highpass", 0.4});
  statsSW.push_back(Stat{"technical_header", 0.6});
  statsSW.push_back(Stat{"technical_shot", 0.2});
  statsSW.push_back(Stat{"technical_volley", 0.2});
  statsSW.push_back(Stat{"mental_calmness", 0.5});
  statsSW.push_back(Stat{"mental_workrate", 0.5});
  statsSW.push_back(Stat{"mental_resilience", 0.5});
  statsSW.push_back(Stat{"mental_defensivepositioning", 0.8});
  statsSW.push_back(Stat{"mental_offensivepositioning", 0.2});
  statsSW.push_back(Stat{"mental_vision", 0.4});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_SW, statsSW));

  std::vector<Stat> statsD;
  statsD.push_back(Stat{"physical_balance", 0.8});
  statsD.push_back(Stat{"physical_reaction", 0.7});
  statsD.push_back(Stat{"physical_acceleration", 0.5});
  statsD.push_back(Stat{"physical_velocity", 0.5});
  statsD.push_back(Stat{"physical_stamina", 0.5});
  statsD.push_back(Stat{"physical_agility", 0.5});
  statsD.push_back(Stat{"physical_shotpower", 0.6});
  statsD.push_back(Stat{"technical_standingtackle", 0.8});
  statsD.push_back(Stat{"technical_slidingtackle", 0.8});
  statsD.push_back(Stat{"technical_ballcontrol", 0.5});
  statsD.push_back(Stat{"technical_dribble", 0.3});
  statsD.push_back(Stat{"technical_shortpass", 0.5});
  statsD.push_back(Stat{"technical_highpass", 0.5});
  statsD.push_back(Stat{"technical_header", 0.6});
  statsD.push_back(Stat{"technical_shot", 0.3});
  statsD.push_back(Stat{"technical_volley", 0.2});
  statsD.push_back(Stat{"mental_calmness", 0.5});
  statsD.push_back(Stat{"mental_workrate", 0.5});
  statsD.push_back(Stat{"mental_resilience", 0.5});
  statsD.push_back(Stat{"mental_defensivepositioning", 0.8});
  statsD.push_back(Stat{"mental_offensivepositioning", 0.3});
  statsD.push_back(Stat{"mental_vision", 0.6});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_D, statsD));

  std::vector<Stat> statsWB;
  statsWB.push_back(Stat{"physical_balance", 0.6});
  statsWB.push_back(Stat{"physical_reaction", 0.5});
  statsWB.push_back(Stat{"physical_acceleration", 0.6});
  statsWB.push_back(Stat{"physical_velocity", 0.6});
  statsWB.push_back(Stat{"physical_stamina", 0.5});
  statsWB.push_back(Stat{"physical_agility", 0.5});
  statsWB.push_back(Stat{"physical_shotpower", 0.5});
  statsWB.push_back(Stat{"technical_standingtackle", 0.7});
  statsWB.push_back(Stat{"technical_slidingtackle", 0.7});
  statsWB.push_back(Stat{"technical_ballcontrol", 0.5});
  statsWB.push_back(Stat{"technical_dribble", 0.5});
  statsWB.push_back(Stat{"technical_shortpass", 0.7});
  statsWB.push_back(Stat{"technical_highpass", 0.7});
  statsWB.push_back(Stat{"technical_header", 0.5});
  statsWB.push_back(Stat{"technical_shot", 0.4});
  statsWB.push_back(Stat{"technical_volley", 0.3});
  statsWB.push_back(Stat{"mental_calmness", 0.5});
  statsWB.push_back(Stat{"mental_workrate", 0.5});
  statsWB.push_back(Stat{"mental_resilience", 0.5});
  statsWB.push_back(Stat{"mental_defensivepositioning", 0.6});
  statsWB.push_back(Stat{"mental_offensivepositioning", 0.4});
  statsWB.push_back(Stat{"mental_vision", 0.6});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_WB, statsWB));

  std::vector<Stat> statsDM;
  statsDM.push_back(Stat{"physical_balance", 0.7});
  statsDM.push_back(Stat{"physical_reaction", 0.6});
  statsDM.push_back(Stat{"physical_acceleration", 0.5});
  statsDM.push_back(Stat{"physical_velocity", 0.5});
  statsDM.push_back(Stat{"physical_stamina", 0.5});
  statsDM.push_back(Stat{"physical_agility", 0.5});
  statsDM.push_back(Stat{"physical_shotpower", 0.6});
  statsDM.push_back(Stat{"technical_standingtackle", 0.7});
  statsDM.push_back(Stat{"technical_slidingtackle", 0.6});
  statsDM.push_back(Stat{"technical_ballcontrol", 0.5});
  statsDM.push_back(Stat{"technical_dribble", 0.4});
  statsDM.push_back(Stat{"technical_shortpass", 0.7});
  statsDM.push_back(Stat{"technical_highpass", 0.7});
  statsDM.push_back(Stat{"technical_header", 0.5});
  statsDM.push_back(Stat{"technical_shot", 0.4});
  statsDM.push_back(Stat{"technical_volley", 0.3});
  statsDM.push_back(Stat{"mental_calmness", 0.5});
  statsDM.push_back(Stat{"mental_workrate", 0.5});
  statsDM.push_back(Stat{"mental_resilience", 0.5});
  statsDM.push_back(Stat{"mental_defensivepositioning", 0.6});
  statsDM.push_back(Stat{"mental_offensivepositioning", 0.4});
  statsDM.push_back(Stat{"mental_vision", 0.6});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_DM, statsDM));

  std::vector<Stat> statsM;
  statsM.push_back(Stat{"physical_balance", 0.4});
  statsM.push_back(Stat{"physical_reaction", 0.5});
  statsM.push_back(Stat{"physical_acceleration", 0.6});
  statsM.push_back(Stat{"physical_velocity", 0.6});
  statsM.push_back(Stat{"physical_stamina", 0.5});
  statsM.push_back(Stat{"physical_agility", 0.6});
  statsM.push_back(Stat{"physical_shotpower", 0.6});
  statsM.push_back(Stat{"technical_standingtackle", 0.4});
  statsM.push_back(Stat{"technical_slidingtackle", 0.3});
  statsM.push_back(Stat{"technical_ballcontrol", 0.7});
  statsM.push_back(Stat{"technical_dribble", 0.6});
  statsM.push_back(Stat{"technical_shortpass", 0.8});
  statsM.push_back(Stat{"technical_highpass", 0.7});
  statsM.push_back(Stat{"technical_header", 0.4});
  statsM.push_back(Stat{"technical_shot", 0.5});
  statsM.push_back(Stat{"technical_volley", 0.4});
  statsM.push_back(Stat{"mental_calmness", 0.5});
  statsM.push_back(Stat{"mental_workrate", 0.5});
  statsM.push_back(Stat{"mental_resilience", 0.5});
  statsM.push_back(Stat{"mental_defensivepositioning", 0.5});
  statsM.push_back(Stat{"mental_offensivepositioning", 0.5});
  statsM.push_back(Stat{"mental_vision", 0.7});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_M, statsM));

  std::vector<Stat> statsAM;
  statsAM.push_back(Stat{"physical_balance", 0.4f});
  statsAM.push_back(Stat{"physical_reaction", 0.5f});
  statsAM.push_back(Stat{"physical_acceleration", 0.6f});
  statsAM.push_back(Stat{"physical_velocity", 0.6f});
  statsAM.push_back(Stat{"physical_stamina", 0.5f});
  statsAM.push_back(Stat{"physical_agility", 0.7f});
  statsAM.push_back(Stat{"physical_shotpower", 0.7f});
  statsAM.push_back(Stat{"technical_standingtackle", 0.2f});
  statsAM.push_back(Stat{"technical_slidingtackle", 0.2f});
  statsAM.push_back(Stat{"technical_ballcontrol", 0.6f});
  statsAM.push_back(Stat{"technical_dribble", 0.7f});
  statsAM.push_back(Stat{"technical_shortpass", 0.6f});
  statsAM.push_back(Stat{"technical_highpass", 0.5f});
  statsAM.push_back(Stat{"technical_header", 0.4f});
  statsAM.push_back(Stat{"technical_shot", 0.7f});
  statsAM.push_back(Stat{"technical_volley", 0.5f});
  statsAM.push_back(Stat{"mental_calmness", 0.5f});
  statsAM.push_back(Stat{"mental_workrate", 0.5f});
  statsAM.push_back(Stat{"mental_resilience", 0.5f});
  statsAM.push_back(Stat{"mental_defensivepositioning", 0.3f});
  statsAM.push_back(Stat{"mental_offensivepositioning", 0.7f});
  statsAM.push_back(Stat{"mental_vision", 0.4f});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_AM, statsAM));

  std::vector<Stat> statsF;
  statsF.push_back(Stat{"physical_balance", 0.4f});
  statsF.push_back(Stat{"physical_reaction", 0.5f});
  statsF.push_back(Stat{"physical_acceleration", 0.7f});
  statsF.push_back(Stat{"physical_velocity", 0.7f});
  statsF.push_back(Stat{"physical_stamina", 0.5f});
  statsF.push_back(Stat{"physical_agility", 0.7f});
  statsF.push_back(Stat{"physical_shotpower", 0.7f});
  statsF.push_back(Stat{"technical_standingtackle", 0.2f});
  statsF.push_back(Stat{"technical_slidingtackle", 0.1f});
  statsF.push_back(Stat{"technical_ballcontrol", 0.5f});
  statsF.push_back(Stat{"technical_dribble", 0.8f});
  statsF.push_back(Stat{"technical_shortpass", 0.6f});
  statsF.push_back(Stat{"technical_highpass", 0.6f});
  statsF.push_back(Stat{"technical_header", 0.4f});
  statsF.push_back(Stat{"technical_shot", 0.7f});
  statsF.push_back(Stat{"technical_volley", 0.4f});
  statsF.push_back(Stat{"mental_calmness", 0.5f});
  statsF.push_back(Stat{"mental_workrate", 0.5f});
  statsF.push_back(Stat{"mental_resilience", 0.5f});
  statsF.push_back(Stat{"mental_defensivepositioning", 0.25f});
  statsF.push_back(Stat{"mental_offensivepositioning", 0.75f});
  statsF.push_back(Stat{"mental_vision", 0.5f});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_F, statsF));

  std::vector<Stat> statsST;
  statsST.push_back(Stat{"physical_balance", 0.3f});
  statsST.push_back(Stat{"physical_reaction", 0.6f});
  statsST.push_back(Stat{"physical_acceleration", 0.6f});
  statsST.push_back(Stat{"physical_velocity", 0.6f});
  statsST.push_back(Stat{"physical_stamina", 0.5f});
  statsST.push_back(Stat{"physical_agility", 0.7f});
  statsST.push_back(Stat{"physical_shotpower", 0.8f});
  statsST.push_back(Stat{"technical_standingtackle", 0.1f});
  statsST.push_back(Stat{"technical_slidingtackle", 0.1f});
  statsST.push_back(Stat{"technical_ballcontrol", 0.5f});
  statsST.push_back(Stat{"technical_dribble", 0.7f});
  statsST.push_back(Stat{"technical_shortpass", 0.5f});
  statsST.push_back(Stat{"technical_highpass", 0.4f});
  statsST.push_back(Stat{"technical_header", 0.6f});
  statsST.push_back(Stat{"technical_shot", 0.8f});
  statsST.push_back(Stat{"technical_volley", 0.7f});
  statsST.push_back(Stat{"mental_calmness", 0.5f});
  statsST.push_back(Stat{"mental_workrate", 0.5f});
  statsST.push_back(Stat{"mental_resilience", 0.5f});
  statsST.push_back(Stat{"mental_defensivepositioning", 0.2f});
  statsST.push_back(Stat{"mental_offensivepositioning", 0.8f});
  statsST.push_back(Stat{"mental_vision", 0.3f});
  defaultProfiles.insert(std::pair<e_PositionName, std::vector<Stat>>(e_PositionName_ST, statsST));

  // give all positions the same stats on average

  std::map<e_PositionName, std::vector<Stat>>::iterator iter = defaultProfiles.begin();
  while (iter != defaultProfiles.end()) {
    std::vector<Stat>& stats = iter->second;
    float totalStats = 0.0f;
    int statsAmount = stats.size();
    for (unsigned int j = 0; j < stats.size(); j++) {
      totalStats += stats.at(j).value;
    }
    float averageStat = totalStats / (float)statsAmount;
    // average should be 0.5...
    for (unsigned int j = 0; j < stats.size(); j++) {
      stats.at(j).value += (0.5f - averageStat);
      assert(stats.at(j).value > 0.0f && stats.at(j).value < 1.0f);
      // center around 1.0.. (0.5 .. 1.5)
      // stats.at(j).value += 0.5f;
    }
    iter++;
  }

  // defenders are underrated..
  /* edit: THIS SHOULD BE CALCULATED IN AVERAGESTARTSTAT NOT IN PROFILE YOU N00B!!
    iter = defaultProfiles.begin();
    float pos = 0.0;
    while (iter != defaultProfiles.end()) {
      std::vector<Stat> &stats = iter->second;
      for (unsigned int j = 0; j < stats.size(); j++) {
        stats.at(j).value *= 1.0f + pow(pos, 1.4f) * 0.2f; // defenders are low in the map, since
    it's sorted by enum '0 .. 8 value' assert(stats.at(j).value > 0.0f && stats.at(j).value < 1.0f);
      }
      iter++;
      pos += 0.1; // 9 entries total
    }
  */
}

void GetDefaultProfile(const std::vector<WeightedPosition>& weightedPositions,
                       std::vector<Stat>& averageProfile) {
  // total weight
  float totalWeight = 0.0f;
  for (unsigned int i = 0; i < weightedPositions.size(); i++) {
    // printf("** adding weight %f\n", weightedPositions.at(i).weight);
    totalWeight += weightedPositions.at(i).weight;
  }
  // printf("** done adding %i weights\n", weightedPositions.size());

  averageProfile.push_back(Stat{"physical_balance", 0.0});
  averageProfile.push_back(Stat{"physical_reaction", 0.0});
  averageProfile.push_back(Stat{"physical_acceleration", 0.0});
  averageProfile.push_back(Stat{"physical_velocity", 0.0});
  averageProfile.push_back(Stat{"physical_stamina", 0.0});
  averageProfile.push_back(Stat{"physical_agility", 0.0});
  averageProfile.push_back(Stat{"physical_shotpower", 0.0});
  averageProfile.push_back(Stat{"technical_standingtackle", 0.0});
  averageProfile.push_back(Stat{"technical_slidingtackle", 0.0});
  averageProfile.push_back(Stat{"technical_ballcontrol", 0.0});
  averageProfile.push_back(Stat{"technical_dribble", 0.0});
  averageProfile.push_back(Stat{"technical_shortpass", 0.0});
  averageProfile.push_back(Stat{"technical_highpass", 0.0});
  averageProfile.push_back(Stat{"technical_header", 0.0});
  averageProfile.push_back(Stat{"technical_shot", 0.0});
  averageProfile.push_back(Stat{"technical_volley", 0.0});
  averageProfile.push_back(Stat{"mental_calmness", 0.0});
  averageProfile.push_back(Stat{"mental_workrate", 0.0});
  averageProfile.push_back(Stat{"mental_resilience", 0.0});
  averageProfile.push_back(Stat{"mental_defensivepositioning", 0.0});
  averageProfile.push_back(Stat{"mental_offensivepositioning", 0.0});
  averageProfile.push_back(Stat{"mental_vision", 0.0});

  for (unsigned int i = 0; i < weightedPositions.size(); i++) {
    std::map<e_PositionName, std::vector<Stat>>::iterator iter =
        defaultProfiles.find(weightedPositions.at(i).positionName);
    assert(iter != defaultProfiles.end());
    const std::vector<Stat>& stats = iter->second;
    for (unsigned int j = 0; j < stats.size(); j++) {
      // printf("|| %f, %f, %f\n", stats.at(j).value, weightedPositions.at(i).weight, totalWeight);
      averageProfile.at(j).value +=
          stats.at(j).value * (weightedPositions.at(i).weight / totalWeight);
    }
  }

  /* verbose
  for (unsigned int i = 0; i < averageStats.size(); i++) {
    printf("%s: %f\n", averageStats.at(i).name.c_str(), averageStats.at(i).value);
  }
  */
}

std::string GetProfileString(const std::vector<Stat>& profileStats) {
  std::string result;
  for (unsigned int i = 0; i < profileStats.size(); i++) {
    result.append("<" + profileStats.at(i).name + ">" + real_to_str(profileStats.at(i).value) +
                  "</" + profileStats.at(i).name + ">\n");
  }
  return result;
}

/* deprecated
float GetAverageStatFromValue(int age, int value) {
  // right now: overall lowest/highest value: 10/45643480

  float currentStat = clamp((float)value / 46000000.0f, 0.000001f, 0.99f);

  // player value works exponentially.. rich clubs are willing to pay lots extra for that little bit
more skill currentStat = pow(currentStat, 0.2f);
  //printf("%i, %f\n", value, currentStat);

  // young players are 'overpriced', as in, their price is higher than the corresponding stat, since
'potential' is considered in the price
  // same for old players, the other way around. 'curve' this right
  int base = 15; // youngest (40 is oldest)
  float exaggerate = 1.5f;
  float valueToStatMultiplier[26];

  valueToStatMultiplier[15 - base] = 0.64;
  valueToStatMultiplier[16 - base] = 0.66;
  valueToStatMultiplier[17 - base] = 0.68;
  valueToStatMultiplier[18 - base] = 0.70;
  valueToStatMultiplier[19 - base] = 0.73;
  valueToStatMultiplier[20 - base] = 0.76;
  valueToStatMultiplier[21 - base] = 0.79;
  valueToStatMultiplier[22 - base] = 0.82;
  valueToStatMultiplier[23 - base] = 0.85;
  valueToStatMultiplier[24 - base] = 0.88;
  valueToStatMultiplier[25 - base] = 0.91;
  valueToStatMultiplier[26 - base] = 0.94;
  valueToStatMultiplier[27 - base] = 0.97;
  valueToStatMultiplier[28 - base] = 1.00; // toppling point - at this age the 'value' graph cuts
the 'stats' graph valueToStatMultiplier[29 - base] = 1.04; valueToStatMultiplier[30 - base] = 1.08;
  valueToStatMultiplier[31 - base] = 1.12;
  valueToStatMultiplier[32 - base] = 1.16;
  valueToStatMultiplier[33 - base] = 1.20;
  valueToStatMultiplier[34 - base] = 1.24;
  valueToStatMultiplier[35 - base] = 1.28;
  valueToStatMultiplier[36 - base] = 1.32;
  valueToStatMultiplier[37 - base] = 1.35;
  valueToStatMultiplier[38 - base] = 1.38;
  valueToStatMultiplier[39 - base] = 1.41;
  valueToStatMultiplier[40 - base] = 1.43;
  for (int i = 0; i <= 40 - base; i++) {
    valueToStatMultiplier[i] -= 1.0;
    valueToStatMultiplier[i] *= exaggerate;
    valueToStatMultiplier[i] += 1.0;

    //valueToStatMultiplier[i] -= 0.9;
  }
  currentStat *= valueToStatMultiplier[age - base];

  // even bad players have SOME skills.
  //XXcurrentStat = 0.4f + currentStat * 0.6f;
  //currentStat = 0.1f + currentStat * 0.9f;

  //currentStat = clamp(currentStat, 0.01, 0.99);

  return currentStat;
}
*/

float CalculateStat(float baseStat, float profileStat, float age,
                    e_DevelopmentCurveType developmentCurveType) {
  // todo: profile-profile on specific early/late age skill factors
  // todo: use development curves. this is just a quick and dirty version for the demo

  float idealAge = 27;
  float ageFactor = curve(1.0f - NormalizedClamp(fabs(age - idealAge), 0, 13) * 0.5f, 1.0f) * 2.0f -
                    1.0f;  // 0 .. 1
  assert(ageFactor >= 0.0f && ageFactor <= 1.0f);
  // ageFactor = clamp(ageFactor, 0.0f, 1.0f);

  // this factor should roughly be around 1.0f for good players at their top age.
  float agedBaseStat = baseStat * (ageFactor * 0.5f + 0.5f) * 1.2f;

  float agedProfileStat = clamp(profileStat * 2.0f * agedBaseStat, 0.01f,
                                1.0f);  // profile stat * 2 because average == 0.5

  return agedProfileStat;
}

/*
float GetIndividualStat(float averageStat, float profileStat, float age) {
  // todo: profile-profile on specific early/late age skill factors

  //float stat = averageStat * profileStat;

  //float ageSkill = 1.0 - fabs( 1.0 - ((age - 15) / (28 - 15)) ); // flip twice (1.0 - ..) to be
able to use fabs for changing direction @ peak age
  //float ageFactor = clamp(ageSkill, 0.0, 1.0);

  float ageFactor = clamp( float(age - 15) / float(28 - 15) , 0.0, 1.0);

  // some test thing, i guess
  // //float result = stat / (stat + 0.3f) * 1.3f;
  // float result = tanh(2.4f * stat) * 1.06f; // hyperbolic tangent
  // //float result = stat * 1.6f;

  //float agedProfileStat = tanh(2.4f * profileStat * ageFactor) * 1.06f; // hyperbolic tangent

  // even out specific skills with age (keep average around 0.5)
  ageFactor *= 0.6f;
  float agedProfileStat = (profileStat * (1.0 - ageFactor)) + (0.5 * ageFactor);

  float result = agedProfileStat;
  //float result = averageStat * agedProfileStat * 1.8f;// <-- why was I doing that? should have
commented it

  return clamp(result, 0.00000001, 0.99999999);
}

float GetAverageStatFromBaseStat(float baseStat, int age, e_DevelopmentCurveType
developmentCurveType) {
  // todo: use curves. this is just a quick and dirty version for the demo

  float ageFactor = pow(clamp( float(age - 15) / float(28 - 15) , 0.0, 1.0), 0.5);
  return pow(baseStat, 1.0 - ageFactor * 0.9f);

  float idealAge = 27;
  float ageFactor = curve( 1.0f - NormalizedClamp(fabs(age - idealAge), 0, 13) * 0.6f , 1.0f);

  return pow(baseStat, ageFactor);
}
*/
