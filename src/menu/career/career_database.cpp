#include "career_database.hpp"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <random>
#include <cmath>

namespace blunted {

CareerDatabase::CareerDatabase() {}

CareerDatabase::~CareerDatabase() {}

bool CareerDatabase::Initialize(const std::string& saveDir) {
  m_saveDirectory = saveDir;
  std::filesystem::create_directories(m_saveDirectory);
  
  // Create or load the career.sqlite database
  // This would normally use the game's database system
  // For now, it's a stub
  return true;
}

bool CareerDatabase::LoadCareerSave(const std::string& saveName) {
  // Stub: Load career data from database
  // like career_saves (name, mode, managerName, reputation, ...)
  m_activeSave = std::make_unique<CareerSave>();
  m_activeSave->name = saveName;
  m_activeSave->reputation = 65; // Example loaded reputation
  return true;
}

bool CareerDatabase::CreateNewCareer(const std::string& careerName, const std::string& mode, const std::string& managerName) {
  m_activeSave = std::make_unique<CareerSave>();
  m_activeSave->name = careerName;
  m_activeSave->mode = mode;
  m_activeSave->managerName = managerName;
  m_activeSave->reputation = 50;
  m_activeSave->boardConfidence = 75;
  m_activeSave->transferBudget = 15000000;
  m_activeSave->wageBudget = 250000;
  m_activeSave->teamID = 0;
  m_activeSave->leagueName = "Default League";

  // Roster is filled from the chosen team when the user confirms "Start Career"
  m_activeSave->roster.clear();

  m_activeSave->freeAgents.push_back(CareerPlayer("Chris Free", 71, 71, 29, 0, 15000, "CF"));
  m_activeSave->freeAgents.push_back(CareerPlayer("Mike Agent", 65, 70, 22, 0, 5000, "CM"));
  m_activeSave->freeAgents.push_back(CareerPlayer("Sam Unsigned", 78, 78, 31, 0, 45000, "CB"));
  m_activeSave->freeAgents.push_back(CareerPlayer("Lucas Fern", 69, 69, 26, 0, 12000, "LB"));
  m_activeSave->freeAgents.push_back(CareerPlayer("Omar Hassan", 62, 68, 23, 0, 4000, "AM"));
  
  m_activeSave->isSeasonActive = true;
  m_activeSave->seasonsPlayed = 0;
  
  // Save to database
  return SaveCareerData();
}

bool CareerDatabase::SaveCareerData() {
  // Stub: Save current m_activeSave to database
  return true;
}

void CareerDatabase::AddEvent(const std::string& eventType, const std::string& description, int reputationDelta, bool isMajor) {
  if (!m_activeSave) return;

  m_activeSave->reputation += reputationDelta;
  // clamp reputation
  if (m_activeSave->reputation > 100) m_activeSave->reputation = 100;
  if (m_activeSave->reputation < -100) m_activeSave->reputation = -100;

  int64_t currentTime = 0; // Stub timestamp
  m_activeSave->recentEvents.emplace_back(eventType, description, reputationDelta, currentTime, isMajor);

  if (m_activeSave->recentEvents.size() > 50) {
    m_activeSave->recentEvents.erase(m_activeSave->recentEvents.begin());
  }

  if (isMajor) {
    if (eventType == "scandal") {
      m_activeSave->legacyStats["scandals_involved"]++;
    } else if (eventType == "achievement") {
      m_activeSave->legacyStats["major_achievements"]++;
    }
  }

  SaveCareerData();
}

void CareerDatabase::RecruitFreeAgent(const std::string& playerName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->freeAgents.begin(), m_activeSave->freeAgents.end(), 
    [&playerName](const CareerPlayer& p) { return p.name == playerName; });

  if (it != m_activeSave->freeAgents.end()) {
    if (m_activeSave->wageBudget >= it->wage) {
      m_activeSave->wageBudget -= it->wage;
      CareerPlayer signedPlayer = *it;
      signedPlayer.contractYearsRemaining = 3;
      signedPlayer.morale = 80;
      signedPlayer.matchForm = 50;
      m_activeSave->roster.push_back(signedPlayer);
      m_activeSave->freeAgents.erase(it);
      AddEvent("recruitment", "Signed free agent " + playerName, 2, false);
      ModifyBoardConfidence(1);
    } else {
      AddEvent("financial", "Failed to sign " + playerName + " due to wage budget limits", -1, false);
    }
  }
}

void CareerDatabase::ScoutYouthPlayer() {
  if (!m_activeSave) return;
  int scoutCost = 50000 * m_activeSave->scoutingNetworkLevel;
  if (m_activeSave->transferBudget >= scoutCost) {
    m_activeSave->transferBudget -= scoutCost;

    static const std::vector<std::string> firstNames = {
      "Leo", "Kai", "Ravi", "Mateo", "Yuki", "Omar", "Felix", "Arjun",
      "Santiago", "Ibrahim", "Noah", "Emre", "Lucas", "Adrien", "Sven",
      "Tomoko", "Aiden", "Davi", "Hugo", "Elias", "Jesse", "Ryu", "Milan",
      "André", "Zane", "Kofi", "Dmitri", "Callum", "Alessio", "Idris"
    };
    static const std::vector<std::string> lastNames = {
      "Martinez", "Tanaka", "Okafor", "Silva", "Nakamura", "Al-Farsi",
      "Johansson", "Gupta", "Rossi", "Kim", "Dubois", "Chen", "Petrov",
      "Williams", "Fernandez", "Müller", "Yamamoto", "Adeyemi", "Larsson",
      "Kone", "Park", "Ivanov", "Torres", "Osei", "Berg", "Sato"
    };
    static const std::vector<std::string> positions = {
      "CF", "CM", "CB", "AM", "RM", "LB", "DM", "LM", "RB", "GK"
    };

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> firstDist(0, firstNames.size() - 1);
    std::uniform_int_distribution<int> lastDist(0, lastNames.size() - 1);
    std::uniform_int_distribution<int> posDist(0, positions.size() - 1);
    std::uniform_int_distribution<int> ageDist(15, 18);

    std::string name = firstNames[firstDist(rng)] + " " + lastNames[lastDist(rng)];
    std::string pos = positions[posDist(rng)];
    int age = ageDist(rng);
    int ovr = 48 + (m_activeSave->scoutingNetworkLevel * 3) + (rng() % 6);
    int pot = 72 + (m_activeSave->scoutingNetworkLevel * 4) + (rng() % 8);

    CareerPlayer youth(name, ovr, pot, age, 100000, 500, pos);
    youth.isYouth = true;
    m_activeSave->youthAcademy.push_back(youth);
    AddEvent("scouting", "Scout returned with prospect: " + name + " (" + pos + ")", 0, false);
  }
}

void CareerDatabase::PromoteYouthPlayer(const std::string& playerName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->youthAcademy.begin(), m_activeSave->youthAcademy.end(), 
    [&playerName](const CareerPlayer& p) { return p.name == playerName; });
    
  if (it != m_activeSave->youthAcademy.end()) {
    CareerPlayer promoted = *it;
    promoted.contractYearsRemaining = 4;
    promoted.isYouth = false;
    promoted.morale = 85;
    promoted.matchForm = 55;
    m_activeSave->roster.push_back(promoted);
    m_activeSave->youthAcademy.erase(it);
    AddEvent("academy", "Promoted academy player " + playerName + " to senior squad.", 1, false);
  }
}

void CareerDatabase::ModifyBudget(long long transferDelta, long long wageDelta) {
  if (!m_activeSave) return;
  m_activeSave->transferBudget += transferDelta;
  m_activeSave->wageBudget += wageDelta;
}

void CareerDatabase::ModifyBoardConfidence(int delta) {
  if (!m_activeSave) return;
  if (m_activeSave->boardConfidence <= 0 && delta < 0) return;
  m_activeSave->boardConfidence += delta;
  if (m_activeSave->boardConfidence > 100) m_activeSave->boardConfidence = 100;
  if (m_activeSave->boardConfidence < 0) {
    m_activeSave->boardConfidence = 0;
    AddEvent("sacked", "Fired by the board of directors.", -50, true);
  }
}

bool CareerDatabase::TrainSquad() {
  if (!m_activeSave) return false;
  if (m_activeSave->trainingPoints > 0) {
    m_activeSave->trainingPoints--;
    for (auto& player : m_activeSave->roster) {
      if (player.overallRating < player.potentialRating && player.matchForm < 90) {
        player.matchForm += 3;
      }
    }
    AddEvent("training", "Conducted squad training session", 1, false);
    return true;
  }
  return false;
}

bool CareerDatabase::TrainFocus(const std::string& focusArea) {
  if (!m_activeSave || m_activeSave->trainingPoints <= 0) return false;
  m_activeSave->trainingPoints--;

  std::vector<std::string> attackingPositions = {"CF", "LM", "RM", "AM"};
  std::vector<std::string> defendingPositions = {"CB", "LB", "RB", "DM", "GK"};
  std::vector<std::string> midfieldPositions = {"CM", "DM", "AM"};

  int playersImproved = 0;
  for (auto& player : m_activeSave->roster) {
    bool isAttacker = std::find(attackingPositions.begin(), attackingPositions.end(),
                                player.preferredPosition) != attackingPositions.end();
    bool isDefender = std::find(defendingPositions.begin(), defendingPositions.end(),
                                player.preferredPosition) != defendingPositions.end();
    bool isMidfielder = std::find(midfieldPositions.begin(), midfieldPositions.end(),
                                  player.preferredPosition) != midfieldPositions.end();

    if (focusArea == "Attacking" && isAttacker) {
      if (player.overallRating < player.potentialRating) {
        player.overallRating++;
        playersImproved++;
      }
      player.matchForm = std::min(100, player.matchForm + 5);
    } else if (focusArea == "Defending" && isDefender) {
      if (player.overallRating < player.potentialRating) {
        player.overallRating++;
        playersImproved++;
      }
      player.matchForm = std::min(100, player.matchForm + 5);
    } else if (focusArea == "Physical") {
      player.matchForm = std::min(100, player.matchForm + 4);
      player.morale = std::min(100, player.morale + 2);
    } else if (focusArea == "Tactical" && isMidfielder) {
      if (player.overallRating < player.potentialRating) {
        player.overallRating++;
        playersImproved++;
      }
      player.matchForm = std::min(100, player.matchForm + 4);
    } else if (focusArea == "Shooting" && isAttacker) {
      if (player.overallRating < player.potentialRating) {
        player.overallRating++;
        playersImproved++;
      }
      player.matchForm = std::min(100, player.matchForm + 3);
    }
  }

  AddEvent("training", "Focused training on " + focusArea + " (" + std::to_string(playersImproved) + " players improved)", 1, false);
  return true;
}

void CareerDatabase::SetStrategy(const std::string& strategy) {
  if (!m_activeSave) return;
  m_activeSave->activeStrategy = strategy;
  AddEvent("strategy", "Changed team strategy to " + strategy, 0, false);
}

int CareerDatabase::GetReputation() const {
  return m_activeSave ? m_activeSave->reputation : 0;
}

std::string CareerDatabase::GetReputationStatus() const {
  if (!m_activeSave) return "Unknown";
  
  int rep = m_activeSave->reputation;
  if (rep >= 80) return "Legendary";
  if (rep >= 50) return "Respected";
  if (rep >= 20) return "Known";
  if (rep >= -20) return "Unproven";
  if (rep >= -50) return "Controversial";
  return "Notorious";
}

std::string CareerDatabase::GetMoraleString(int morale) const {
  if (morale >= 80) return "Happy";
  if (morale >= 40) return "Content";
  return "Unhappy";
}

std::string CareerDatabase::GetFormString(int form) const {
  if (form >= 80) return "Excellent";
  if (form >= 40) return "Good";
  return "Poor";
}

int CareerDatabase::GetLegacyStat(const std::string& statName) const {
  if (!m_activeSave) return 0;
  
  auto it = m_activeSave->legacyStats.find(statName);
  return (it != m_activeSave->legacyStats.end()) ? it->second : 0;
}

std::vector<CareerEvent> CareerDatabase::GetRecentEvents(int limit) const {
  if (!m_activeSave) return {};

  std::vector<CareerEvent> res;
  int count = 0;
  for (auto it = m_activeSave->recentEvents.rbegin(); it != m_activeSave->recentEvents.rend() && count < limit; ++it) {
    res.push_back(*it);
    count++;
  }
  return res;
}

void CareerDatabase::ProcessPlayerGrowth(CareerPlayer& player) {
  static std::mt19937 rng(std::random_device{}());
  int potGap = player.potentialRating - player.overallRating;
  
  int growthChance = 0;
  int maxGrowth = 0;
  int declineMin = 0;
  int declineMax = 0;
  bool isInDecline = false;

  if (player.age <= 18) {
    growthChance = 80;
    maxGrowth = 5;
  } else if (player.age <= 21) {
    growthChance = 70;
    maxGrowth = 4;
  } else if (player.age <= 24) {
    growthChance = 55;
    maxGrowth = 3;
  } else if (player.age <= 27) {
    growthChance = 35;
    maxGrowth = 2;
  } else if (player.age <= 29) {
    growthChance = 15;
    maxGrowth = 1;
  } else if (player.age == 30) {
    declineMin = 0;
    declineMax = 1;
    isInDecline = true;
  } else if (player.age == 31) {
    declineMin = 1;
    declineMax = 2;
    isInDecline = true;
  } else if (player.age <= 33) {
    declineMin = 1;
    declineMax = 3;
    isInDecline = true;
  } else {
    declineMin = 2;
    declineMax = 5;
    isInDecline = true;
  }

  if (isInDecline) {
    std::uniform_int_distribution<int> dist(declineMin, declineMax);
    int decline = dist(rng);
    player.overallRating = std::max(40, player.overallRating - decline);
  } else if (growthChance > 0 && potGap > 0) {
    std::uniform_int_distribution<int> chanceDist(1, 100);
    if (chanceDist(rng) <= growthChance) {
      int growth = std::min(maxGrowth, potGap);
      if (growth > 1) {
        std::uniform_int_distribution<int> growthDist(1, growth);
        growth = growthDist(rng);
      }
      player.overallRating += growth;

      std::uniform_int_distribution<int> breakoutDist(1, 100);
      if (breakoutDist(rng) <= 8) {
        player.overallRating = std::min(player.potentialRating, player.overallRating + 2);
      }
    }
  }

  std::uniform_int_distribution<int> moraleDrift(-5, 5);
  player.morale = std::max(10, std::min(95, player.morale + moraleDrift(rng)));
  std::uniform_int_distribution<int> formDrift(-10, 10);
  player.matchForm = std::max(10, std::min(95, player.matchForm + formDrift(rng)));
}

void CareerDatabase::UpdatePlayerValue(CareerPlayer& player) {
  double ageFactor = 1.0;
  if (player.age < 21) ageFactor = 0.8;
  else if (player.age > 27) ageFactor = std::max(0.2, 1.0 - (player.age - 27) * 0.08);

  double formFactor = 0.7 + (player.matchForm / 100.0) * 0.6;

  double potFactor = 1.0;
  if (player.age < 26) {
    potFactor = 0.8 + (player.potentialRating / 100.0) * 0.5;
  }

  int effectiveOvr = std::max(40, player.overallRating);
  double baseValue = effectiveOvr * effectiveOvr * 5000.0;
  player.value = static_cast<long long>(baseValue * ageFactor * formFactor * potFactor);
  if (player.value < 100000) player.value = 100000;
  if (player.wage < 0) player.wage = 0;
}

void CareerDatabase::AdvanceSeason() {
  if (!m_activeSave) return;

  std::vector<std::string> releasedNames;

  for (auto& player : m_activeSave->roster) {
    ProcessPlayerGrowth(player);
    UpdatePlayerValue(player);
    player.age++;
    player.contractYearsRemaining--;

    if (player.contractYearsRemaining <= 0) {
      releasedNames.push_back(player.name);
    }
  }

  for (const auto& name : releasedNames) {
    auto it = std::find_if(m_activeSave->roster.begin(), m_activeSave->roster.end(),
      [&name](const CareerPlayer& p) { return p.name == name; });
    if (it != m_activeSave->roster.end()) {
      m_activeSave->wageBudget += it->wage;
      m_activeSave->freeAgents.push_back(*it);
      m_activeSave->roster.erase(it);
    }
  }

  for (auto& player : m_activeSave->youthAcademy) {
    ProcessPlayerGrowth(player);
    UpdatePlayerValue(player);
    player.age++;
  }

  for (auto& player : m_activeSave->freeAgents) {
    ProcessPlayerGrowth(player);
    UpdatePlayerValue(player);
    player.age++;
  }

  m_activeSave->freeAgents.erase(
    std::remove_if(m_activeSave->freeAgents.begin(), m_activeSave->freeAgents.end(),
      [](const CareerPlayer& p) { return p.age > 38 || p.overallRating < 50; }),
    m_activeSave->freeAgents.end());

  m_activeSave->youthAcademy.erase(
    std::remove_if(m_activeSave->youthAcademy.begin(), m_activeSave->youthAcademy.end(),
      [](const CareerPlayer& p) { return p.age > 21; }),
    m_activeSave->youthAcademy.end());

  static const std::vector<std::string> genFirstNames = {
    "Marcus", "Youssef", "Liam", "Jorge", "Tom", "Henrik", "Raj", "Pierre",
    "Diego", "Aaron", "Viktor", "Ali", "Brandon", "Erik", "Sergio"
  };
  static const std::vector<std::string> genLastNames = {
    "Thompson", "Al-Rashid", "Novak", "Cruz", "Mueller", "Patel", "Laurent",
    "Sanchez", "Chang", "Kowalski", "Bakker", "Moreau", "Lindberg", "Hayes"
  };
  static const std::vector<std::string> genPositions = {
    "CF", "CM", "CB", "AM", "RM", "LB", "DM", "GK"
  };
  static std::mt19937 rng(std::random_device{}());

  int newFA = 3 + (rng() % 4);
  for (int i = 0; i < newFA; i++) {
    std::uniform_int_distribution<int> fd(0, genFirstNames.size() - 1);
    std::uniform_int_distribution<int> ld(0, genLastNames.size() - 1);
    std::uniform_int_distribution<int> pd(0, genPositions.size() - 1);
    std::uniform_int_distribution<int> ad(19, 32);
    std::uniform_int_distribution<int> od(58, 82);
    std::uniform_int_distribution<int> wd(3000, 55000);
    std::uniform_int_distribution<int> potVar(0, 4);

    std::string name = genFirstNames[fd(rng)] + " " + genLastNames[ld(rng)];
    int age = ad(rng);
    int ovr = od(rng);
    int pot = std::min(99, ovr + potVar(rng));
    CareerPlayer fa(name, ovr, pot, age, 0, wd(rng) * 100, genPositions[pd(rng)]);
    UpdatePlayerValue(fa);
    m_activeSave->freeAgents.push_back(fa);
  }

  int maxFreeAgents = 20;
  if (static_cast<int>(m_activeSave->freeAgents.size()) > maxFreeAgents) {
    std::sort(m_activeSave->freeAgents.begin(), m_activeSave->freeAgents.end(),
      [](const CareerPlayer& a, const CareerPlayer& b) { return a.overallRating > b.overallRating; });
    m_activeSave->freeAgents.resize(maxFreeAgents);
  }

  long long budgetBonus = static_cast<long long>(m_activeSave->boardConfidence) * 100000;
  m_activeSave->transferBudget += budgetBonus;
  long long wageBonus = m_activeSave->boardConfidence * 500;
  m_activeSave->wageBudget += wageBonus;
  m_activeSave->trainingPoints = 8;

  m_activeSave->seasonsPlayed++;

  std::string summary = "Season " + std::to_string(m_activeSave->seasonsPlayed) +
    " | Budget +€" + std::to_string(budgetBonus) +
    " | Confidence: " + std::to_string(m_activeSave->boardConfidence) + "%" +
    " | Squad: " + std::to_string(m_activeSave->roster.size()) + " players" +
    " | Contracts expired: " + std::to_string(releasedNames.size());
  m_activeSave->seasonSummaries.push_back(summary);

  if (!releasedNames.empty()) {
    std::string releaseList;
    for (const auto& n : releasedNames) {
      releaseList += n + ", ";
    }
    releaseList.erase(releaseList.length() - 2);
    AddEvent("contracts", "Contracts expired: " + releaseList, -2, false);
  }

  if (m_activeSave->boardConfidence < 25) {
    AddEvent("warning", "The board is losing patience. Improve results or face consequences.", -3, false);
  }

  AddEvent("season", "Advanced to Season " + std::to_string(m_activeSave->seasonsPlayed), 2, false);

  SaveCareerData();
}

void CareerDatabase::ReleasePlayer(const std::string& playerName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->roster.begin(), m_activeSave->roster.end(),
    [&playerName](const CareerPlayer& p) { return p.name == playerName; });

  if (it != m_activeSave->roster.end()) {
    m_activeSave->wageBudget += it->wage;
    CareerPlayer released = *it;
    released.morale = 30;
    released.contractYearsRemaining = 0;
    m_activeSave->freeAgents.push_back(released);
    m_activeSave->roster.erase(it);
    AddEvent("roster", "Released " + playerName + " from the squad.", -1, false);
    ModifyBoardConfidence(-2);
  }
}

void CareerDatabase::RecordMatchStats(const std::string& playerName, int goals, int assists) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->roster.begin(), m_activeSave->roster.end(),
    [&playerName](const CareerPlayer& p) { return p.name == playerName; });

  if (it != m_activeSave->roster.end()) {
    it->careerGoals += goals;
    it->careerAssists += assists;
    it->matchesPlayed++;
    if (goals > 0 || assists > 0) {
      it->morale = std::min(100, it->morale + 5);
      it->matchForm = std::min(100, it->matchForm + 3);
    } else {
      it->matchForm = std::max(10, it->matchForm - 2);
    }
  }
}

void CareerDatabase::PopulateTransferMarket() {
  if (!m_activeSave) return;
  m_transferTargets.clear();

  static const std::vector<std::string> genFirstNames = {
    "Marcus", "Youssef", "Liam", "Jorge", "Tom", "Henrik", "Raj", "Pierre",
    "Diego", "Aaron", "Viktor", "Ali", "Brandon", "Erik", "Sergio", "Niko",
    "Julian", "Kwame", "Rafael", "Lukas", "Dante", "Callum", "Oscar", "Enzo"
  };
  static const std::vector<std::string> genLastNames = {
    "Thompson", "Al-Rashid", "Novak", "Cruz", "Mueller", "Patel", "Laurent",
    "Sanchez", "Chang", "Kowalski", "Bakker", "Moreau", "Lindberg", "Hayes",
    "Petrov", "Fernandez", "Kim", "Dubois", "Torres", "Osei"
  };
  static const std::vector<std::string> genPositions = {
    "CF", "CM", "CB", "AM", "RM", "LB", "DM", "GK", "RB", "LM"
  };

  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> fd(0, genFirstNames.size() - 1);
  std::uniform_int_distribution<int> ld(0, genLastNames.size() - 1);
  std::uniform_int_distribution<int> pd(0, genPositions.size() - 1);
  std::uniform_int_distribution<int> ad(18, 33);
  std::uniform_int_distribution<int> od(60, 88);
  std::uniform_int_distribution<int> potVar(2, 12);
  std::uniform_int_distribution<int> teamDist(1, 20);
  std::uniform_int_distribution<int> wageMult(1, 5);

  for (const auto& fa : m_activeSave->freeAgents) {
    TransferTarget t;
    t.name = fa.name;
    t.preferredPosition = fa.preferredPosition;
    t.overallRating = fa.overallRating;
    t.potentialRating = fa.potentialRating;
    t.age = fa.age;
    t.value = fa.value;
    t.wage = fa.wage;
    t.askingPrice = fa.value > 0 ? fa.value : 500000;
    t.teamID = 0;
    t.isListed = true;
    m_transferTargets.push_back(t);
  }

  for (int i = 0; i < 12; i++) {
    TransferTarget t;
    t.name = genFirstNames[fd(rng)] + " " + genLastNames[ld(rng)];
    t.preferredPosition = genPositions[pd(rng)];
    t.age = ad(rng);
    t.overallRating = od(rng);
    t.potentialRating = std::min(99, t.overallRating + potVar(rng));
    t.value = static_cast<long long>(t.overallRating) * t.overallRating * 5000;
    t.wage = (t.value / 1000) + 5000 + (rng() % 10000);
    t.askingPrice = static_cast<long long>(t.value * (0.8 + (rng() % 40) / 100.0));
    t.teamID = teamDist(rng);
    t.isListed = true;
    m_transferTargets.push_back(t);
  }

  std::sort(m_transferTargets.begin(), m_transferTargets.end(),
    [](const TransferTarget& a, const TransferTarget& b) { return a.overallRating > b.overallRating; });
}

std::vector<TransferTarget> CareerDatabase::GetTransferTargets() const {
  return m_transferTargets;
}

TransferBid CareerDatabase::PlaceBid(const std::string& playerName, long long bidAmount,
                                     int offeredWage, int contractYears) {
  TransferBid bid;
  bid.playerName = playerName;
  bid.bidAmount = bidAmount;
  bid.offeredWage = offeredWage;
  bid.contractYears = contractYears;
  bid.agentFee = bidAmount / 20;
  bid.status = BidStatus::PENDING;
  bid.negotiationRounds = 0;

  if (m_activeSave && bidAmount + bid.agentFee > m_activeSave->transferBudget) {
    bid.status = BidStatus::REJECTED;
    return bid;
  }

  auto it = std::find_if(m_activeBids.begin(), m_activeBids.end(),
    [&playerName](const TransferBid& b) { return b.playerName == playerName; });
  if (it != m_activeBids.end()) {
    *it = bid;
  } else {
    m_activeBids.push_back(bid);
  }

  return bid;
}

void CareerDatabase::WithdrawBid(const std::string& playerName) {
  m_activeBids.erase(
    std::remove_if(m_activeBids.begin(), m_activeBids.end(),
      [&playerName](const TransferBid& b) { return b.playerName == playerName; }),
    m_activeBids.end());
}

void CareerDatabase::ProcessPendingBids() {
  static std::mt19937 rng(std::random_device{}());

  for (auto& bid : m_activeBids) {
    if (bid.status != BidStatus::PENDING) continue;

    auto targetIt = std::find_if(m_transferTargets.begin(), m_transferTargets.end(),
      [&bid](const TransferTarget& t) { return t.name == bid.playerName; });
    if (targetIt == m_transferTargets.end()) continue;

    double ratio = static_cast<double>(bid.bidAmount) / targetIt->askingPrice;

    if (bid.negotiationRounds == 0) {
      if (ratio >= 1.0) {
        bid.status = BidStatus::ACCEPTED;
      } else if (ratio >= 0.7) {
        std::uniform_int_distribution<int> dist(1, 100);
        if (dist(rng) <= 50) {
          bid.status = BidStatus::ACCEPTED;
        } else {
          bid.status = BidStatus::REJECTED;
        }
      } else {
        bid.status = BidStatus::REJECTED;
      }
    } else {
      int counterChance = 20 + bid.negotiationRounds * 25;
      std::uniform_int_distribution<int> dist(1, 100);
      if (dist(rng) <= counterChance && ratio >= 0.6) {
        bid.status = BidStatus::ACCEPTED;
      } else {
        bid.status = BidStatus::REJECTED;
      }
    }

    bid.negotiationRounds++;
  }
}

std::string CareerDatabase::GetBidStatusString(BidStatus status) const {
  switch (status) {
    case BidStatus::PENDING: return "Pending";
    case BidStatus::ACCEPTED: return "ACCEPTED";
    case BidStatus::REJECTED: return "Rejected";
    case BidStatus::WITHDRAWN: return "Withdrawn";
    default: return "Unknown";
  }
}

bool CareerDatabase::CompleteTransfer(const std::string& playerName) {
  if (!m_activeSave) return false;

  auto bidIt = std::find_if(m_activeBids.begin(), m_activeBids.end(),
    [&playerName](const TransferBid& b) { return b.playerName == playerName && b.status == BidStatus::ACCEPTED; });
  if (bidIt == m_activeBids.end()) return false;

  long long totalCost = bidIt->bidAmount + bidIt->agentFee;
  if (totalCost > m_activeSave->transferBudget) return false;

  m_activeSave->transferBudget -= totalCost;
  m_activeSave->wageBudget -= bidIt->offeredWage;

  CareerPlayer signedPlayer(playerName, 0, 0, 0, 0, bidIt->offeredWage, "CM");

  auto targetIt = std::find_if(m_transferTargets.begin(), m_transferTargets.end(),
    [&playerName](const TransferTarget& t) { return t.name == playerName; });
  if (targetIt != m_transferTargets.end()) {
    signedPlayer.preferredPosition = targetIt->preferredPosition;
    signedPlayer.overallRating = targetIt->overallRating;
    signedPlayer.potentialRating = targetIt->potentialRating;
    signedPlayer.age = targetIt->age;
    signedPlayer.value = targetIt->value;
    signedPlayer.contractYearsRemaining = bidIt->contractYears;
    signedPlayer.databaseID = 0;
  }

  m_activeSave->roster.push_back(signedPlayer);

  auto faIt = std::find_if(m_activeSave->freeAgents.begin(), m_activeSave->freeAgents.end(),
    [&playerName](const CareerPlayer& p) { return p.name == playerName; });
  if (faIt != m_activeSave->freeAgents.end()) {
    m_activeSave->freeAgents.erase(faIt);
  }

  m_transferTargets.erase(
    std::remove_if(m_transferTargets.begin(), m_transferTargets.end(),
      [&playerName](const TransferTarget& t) { return t.name == playerName; }),
    m_transferTargets.end());

  AddEvent("transfer", "Signed " + playerName + " for €" + std::to_string(bidIt->bidAmount) +
    " (agent fee: €" + std::to_string(bidIt->agentFee) + ")", 3, true);

  m_activeBids.erase(bidIt);
  return true;
}

} // namespace blunted