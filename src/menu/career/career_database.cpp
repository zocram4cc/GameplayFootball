#include "career_database.hpp"

#include <algorithm>
#include <ctime>
#include <functional>
#include <random>
#include <fstream>
#include <sstream>

namespace {

std::mt19937& CareerRng() {
  static std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
  return rng;
}

void SeedCareerRng(unsigned int seed) {
  CareerRng().seed(seed);
}

int ClampInt(int value, int minValue, int maxValue) {
  return std::max(minValue, std::min(maxValue, value));
}

long long ClampLongLong(long long value, long long minValue, long long maxValue) {
  return std::max(minValue, std::min(maxValue, value));
}

int RandomInt(int minValue, int maxValue) {
  std::uniform_int_distribution<int> dist(minValue, maxValue);
  return dist(CareerRng());
}

bool IsGoalkeeper(const PlayerCareerState& player) {
  return player.preferredPosition == "GK" || player.position == "GK";
}

}

namespace blunted {

CareerDatabase::CareerDatabase() {}
CareerDatabase::~CareerDatabase() {}

bool CareerDatabase::Initialize(const std::string& saveDir) {
  m_saveDirectory = saveDir;
  return true;
}

bool CareerDatabase::LoadCareerSave(const std::string& saveName) {
  if (!m_saveDirectory.empty()) {
    std::string path = m_saveDirectory + "/career.save";
    if (LoadFromFile(path)) {
      printf("[career] Loaded save: %s\n", saveName.c_str());
      return true;
    }
  }
  m_activeSave = std::make_unique<CareerSave>();
  m_activeSave->name = saveName;
  m_activeSave->club.clubName = saveName;
  m_activeSave->reputation = 65;
  m_activeSave->club.reputation = 65;
  m_activeSave->transferBudget = 15000000;
  m_activeSave->wageBudget = 250000;
  m_activeSave->finance.transferBudget = m_activeSave->transferBudget;
  m_activeSave->finance.wageBudget = m_activeSave->wageBudget;
  return true;
}

bool CareerDatabase::CreateNewCareer(const std::string& careerName, const std::string& mode,
                                     const std::string& managerName) {
  m_activeSave = std::make_unique<CareerSave>();
  m_activeSave->name = careerName;
  m_activeSave->managerName = managerName;
  m_activeSave->club.clubName = careerName;
  if (mode == "player")
    m_activeSave->mode = CareerMode::PLAYER;
  else if (mode == "mygm")
    m_activeSave->mode = CareerMode::GM;
  else if (mode == "mycoach")
    m_activeSave->mode = CareerMode::COACH;
  else if (mode == "owner")
    m_activeSave->mode = CareerMode::OWNER;
  else
    m_activeSave->mode = CareerMode::MANAGER;
  m_activeSave->reputation = 50;
  m_activeSave->club.reputation = 50;
  m_activeSave->boardConfidence = 75;
  m_activeSave->board.confidence = 75;
  m_activeSave->transferBudget = 15000000;
  m_activeSave->wageBudget = 250000;
  m_activeSave->finance.transferBudget = m_activeSave->transferBudget;
  m_activeSave->finance.wageBudget = m_activeSave->wageBudget;
  m_activeSave->club.leagueName = "Default League";
  m_activeSave->season.currentSeason = 1;
  m_activeSave->season.currentWeek = 1;
  m_activeSave->season.inPreseason = true;
  m_activeSave->season.maxWeeks = 38;
  m_activeSave->season.transferWindowOpen = true;
  m_activeSave->stadium.name = careerName + " Stadium";
  InitializeOwnerData();
  GenerateBoardObjectives();
  GenerateSponsorOffers();
  return SaveCareerData();
}

bool CareerDatabase::SaveCareerData() {
  if (!m_activeSave || m_saveDirectory.empty()) return false;
  std::string path = m_saveDirectory + "/career.save";
  return SaveToFile(path);
}

void CareerDatabase::AddEvent(const std::string& eventType, const std::string& description,
                              int reputationDelta, bool isMajor) {
  if (!m_activeSave) return;
  m_activeSave->reputation = std::max(-100, std::min(100, m_activeSave->reputation + reputationDelta));
  m_activeSave->club.reputation = m_activeSave->reputation;
  m_activeSave->recentEvents.emplace_back(eventType, eventType + ": " + description, reputationDelta, 0, isMajor);
  if (m_activeSave->recentEvents.size() > 50) m_activeSave->recentEvents.erase(m_activeSave->recentEvents.begin());
  if (isMajor) {
    m_activeSave->legacyStats[eventType]++;
  }
  SaveCareerData();
}

void CareerDatabase::RecruitFreeAgent(const std::string& playerName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->freeAgents.begin(), m_activeSave->freeAgents.end(),
    [&playerName](const PlayerCareerState& p) { return p.name == playerName; });
  if (it == m_activeSave->freeAgents.end()) return;
  if (m_activeSave->wageBudget < it->wage) {
    AddEvent("financial", "Failed to sign " + playerName + " due to wage budget limits", -1, false);
    return;
  }
  m_activeSave->wageBudget -= it->wage;
  m_activeSave->finance.wageBudget = m_activeSave->wageBudget;
  m_activeSave->roster.push_back(*it);
  m_activeSave->freeAgents.erase(it);
  AddEvent("recruitment", "Signed free agent " + playerName, 2, false);
  ModifyBoardConfidence(1);
}

void CareerDatabase::ScoutYouthPlayer() {
  if (!m_activeSave) return;
  int scoutCost = 50000 * m_activeSave->scoutingNetworkLevel;
  if (m_activeSave->transferBudget < scoutCost) return;
  m_activeSave->transferBudget -= scoutCost;
  m_activeSave->finance.transferBudget = m_activeSave->transferBudget;

  static const std::vector<std::string> firstNames = {"Leo", "Kai", "Ravi", "Mateo", "Yuki"};
  static const std::vector<std::string> lastNames = {"Martinez", "Tanaka", "Okafor", "Silva", "Kim"};
  static const std::vector<std::string> positions = {"CF", "CM", "CB", "AM", "GK"};
  static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> fd(0, firstNames.size() - 1);
  std::uniform_int_distribution<int> ld(0, lastNames.size() - 1);
  std::uniform_int_distribution<int> pd(0, positions.size() - 1);
  std::uniform_int_distribution<int> ad(15, 18);

  PlayerCareerState youth;
  youth.name = firstNames[fd(rng)] + " " + lastNames[ld(rng)];
  youth.position = positions[pd(rng)];
  youth.preferredPosition = youth.position;
  youth.age = ad(rng);
  youth.ovr = 50 + (rng() % 10);
  youth.pot = 70 + (rng() % 15);
  youth.wage = 500;
  youth.value = 100000;
  youth.isYouth = true;
  m_activeSave->youthAcademy.push_back(youth);
  AddEvent("scouting", "Scout returned with prospect: " + youth.name + " (" + youth.position + ")", 0, false);
}

void CareerDatabase::PromoteYouthPlayer(const std::string& playerName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->youthAcademy.begin(), m_activeSave->youthAcademy.end(),
    [&playerName](const PlayerCareerState& p) { return p.name == playerName; });
  if (it == m_activeSave->youthAcademy.end()) return;
  PlayerCareerState promoted = *it;
  promoted.contract.yearsRemaining = 4;
  promoted.isYouth = false;
  promoted.morale = 85;
  promoted.matchForm = 55;
  m_activeSave->roster.push_back(promoted);
  m_activeSave->youthAcademy.erase(it);
  AddEvent("academy", "Promoted academy player " + playerName + " to senior squad.", 1, false);
}

void CareerDatabase::ModifyBudget(long long transferDelta, long long wageDelta) {
  if (!m_activeSave) return;
  m_activeSave->transferBudget += transferDelta;
  m_activeSave->wageBudget += wageDelta;
  m_activeSave->finance.transferBudget = m_activeSave->transferBudget;
  m_activeSave->finance.wageBudget = m_activeSave->wageBudget;
}

void CareerDatabase::ModifyBoardConfidence(int delta) {
  if (!m_activeSave) return;
  m_activeSave->boardConfidence = std::max(0, std::min(100, m_activeSave->boardConfidence + delta));
  m_activeSave->board.confidence = m_activeSave->boardConfidence;
}

bool CareerDatabase::TrainSquad() {
  if (!m_activeSave || m_activeSave->trainingPoints <= 0) return false;
  m_activeSave->trainingPoints--;
  for (auto& player : m_activeSave->roster) player.matchForm = std::min(100, player.matchForm + 3);
  AddEvent("training", "Conducted squad training session", 1, false);
  return true;
}

bool CareerDatabase::TrainFocus(const std::string& focusArea) {
  if (!m_activeSave || m_activeSave->trainingPoints <= 0) return false;
  m_activeSave->trainingPoints--;
  int playersImproved = 0;
  for (auto& player : m_activeSave->roster) {
    if ((focusArea == "Attacking" || focusArea == "Shooting") &&
        (player.preferredPosition == "CF" || player.preferredPosition == "AM")) {
      player.ovr++;
      playersImproved++;
    } else if (focusArea == "Defending" &&
               (player.preferredPosition == "CB" || player.preferredPosition == "LB" || player.preferredPosition == "RB")) {
      player.ovr++;
      playersImproved++;
    }
    player.matchForm = std::min(100, player.matchForm + 3);
  }
  AddEvent("training", "Focused training on " + focusArea + " (" + std::to_string(playersImproved) + " players improved)", 1, false);
  return true;
}

void CareerDatabase::SetStrategy(const std::string& strategy) {
  if (!m_activeSave) return;
  m_activeSave->activeStrategy = strategy;
  AddEvent("strategy", "Changed team strategy to " + strategy, 0, false);
}

int CareerDatabase::GetReputation() const { return m_activeSave ? m_activeSave->reputation : 0; }

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
  return it != m_activeSave->legacyStats.end() ? it->second : 0;
}

std::vector<CareerEvent> CareerDatabase::GetRecentEvents(int limit) const {
  if (!m_activeSave) return {};
  std::vector<CareerEvent> res;
  for (auto it = m_activeSave->recentEvents.rbegin(); it != m_activeSave->recentEvents.rend() && static_cast<int>(res.size()) < limit; ++it) {
    res.push_back(*it);
  }
  return res;
}

void CareerDatabase::ProcessPlayerGrowth(PlayerCareerState& player) {
  int growthChance = 15;
  if (player.age <= 21) growthChance = 55;
  else if (player.age <= 24) growthChance = 35;
  else if (player.age >= 31) growthChance = 8;

  if (player.matchForm >= 75) growthChance += 10;
  if (player.morale >= 75) growthChance += 5;

  if (RandomInt(1, 100) <= growthChance) {
    if (player.ovr < player.pot) {
      player.ovr = std::min(player.ovr + 1, player.pot);
    } else if (player.age >= 32 && RandomInt(1, 100) <= 35) {
      player.ovr = std::max(40, player.ovr - 1);
    }
  }

  player.matchForm = ClampInt(player.matchForm - RandomInt(2, 8), 0, 100);
  player.morale = ClampInt(player.morale + RandomInt(-3, 4), 0, 100);
  player.fitness = ClampInt(player.fitness - RandomInt(0, 5), 55, 100);
}

void CareerDatabase::UpdatePlayerValue(PlayerCareerState& player) {
  long long ageModifier = 120;
  if (player.age >= 30) ageModifier = 85;
  else if (player.age <= 21) ageModifier = 135;

  long long formModifier = 80 + ClampInt(player.matchForm, 0, 100) / 5;
  long long potentialModifier = 100 + std::max(0, player.pot - player.ovr);
  long long baseValue = static_cast<long long>(player.ovr) * player.ovr * 4000;
  player.value = std::max(50000LL, (baseValue * ageModifier * formModifier * potentialModifier) / 1200000LL);
  player.wage = std::max(500LL, player.value / 1200LL);
}

void CareerDatabase::AdvanceSeason() {
  if (!m_activeSave) return;

  SeasonRecord record;
  record.season = m_activeSave->season.currentSeason;
  record.teamID = m_activeSave->club.clubID;
  if (m_activeSave->seasonWins > 0 || m_activeSave->seasonDraws > 0 || m_activeSave->seasonLosses > 0) {
    record.wins = m_activeSave->seasonWins;
    record.draws = m_activeSave->seasonDraws;
    record.losses = m_activeSave->seasonLosses;
    record.goalsFor = m_activeSave->seasonGoalsFor;
    record.goalsAgainst = m_activeSave->seasonGoalsAgainst;
  } else {
    record.wins = RandomInt(8, 28);
    record.draws = RandomInt(4, 12);
    record.losses = std::max(0, 38 - record.wins - record.draws);
    record.goalsFor = RandomInt(30, 85);
    record.goalsAgainst = RandomInt(20, 70);
  }
  record.leaguePosition = RandomInt(1, 20);
  record.wonTitle = (record.leaguePosition == 1);
  m_activeSave->history.push_back(record);

  for (auto& player : m_activeSave->roster) {
    player.age++;
    ProcessPlayerGrowth(player);
    UpdatePlayerValue(player);
    player.matchesPlayed = 0;
    player.careerGoals = std::max(0, player.careerGoals);
    player.careerAssists = std::max(0, player.careerAssists);
  }

  for (auto& player : m_activeSave->staff) {
    if (player.contractYearsRemaining > 0) player.contractYearsRemaining--;
  }
  m_activeSave->staff.erase(
      std::remove_if(m_activeSave->staff.begin(), m_activeSave->staff.end(),
                     [](const StaffMember& member) { return member.contractYearsRemaining <= 0; }),
      m_activeSave->staff.end());

  for (auto& sponsor : m_activeSave->activeSponsors) {
    if (sponsor.yearsRemaining > 0) sponsor.yearsRemaining--;
  }
  m_activeSave->activeSponsors.erase(
      std::remove_if(m_activeSave->activeSponsors.begin(), m_activeSave->activeSponsors.end(),
                     [](const SponsorDeal& sponsor) { return sponsor.yearsRemaining <= 0; }),
      m_activeSave->activeSponsors.end());

  for (auto& upgrade : m_activeSave->stadium.upgrades) {
    if (upgrade.seasonsRemaining > 0) {
      upgrade.seasonsRemaining--;
      if (upgrade.seasonsRemaining == 0) {
        m_activeSave->stadium.capacity += upgrade.capacityIncrease;
        m_activeSave->stadium.matchDayRevenue += upgrade.revenueBonus;
        AddEvent("stadium", "Completed stadium upgrade: " + upgrade.name, 1, false);
      }
    }
  }

  m_activeSave->season.currentSeason++;
  m_activeSave->currentSeason = m_activeSave->season.currentSeason;
  m_activeSave->season.currentWeek = 1;
  m_activeSave->season.inPreseason = true;
  m_activeSave->season.transferWindowOpen = true;
  m_activeSave->trainingPoints = 10;
  m_activeSave->availableSponsorOffers.clear();
  m_activeBids.clear();
  m_transferTargets.clear();
  m_activeSave->seasonWins = 0;
  m_activeSave->seasonDraws = 0;
  m_activeSave->seasonLosses = 0;
  m_activeSave->seasonGoalsFor = 0;
  m_activeSave->seasonGoalsAgainst = 0;
  AddEvent("season", "Advanced to season " + std::to_string(m_activeSave->season.currentSeason), 1, true);
  SaveCareerData();
}

void CareerDatabase::ReleasePlayer(const std::string& playerName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->roster.begin(), m_activeSave->roster.end(),
                         [&playerName](const PlayerCareerState& player) { return player.name == playerName; });
  if (it == m_activeSave->roster.end()) return;

  m_activeSave->wageBudget += it->wage;
  m_activeSave->finance.wageBudget = m_activeSave->wageBudget;
  PlayerCareerState released = *it;
  released.transferStatus = TransferStatus::NONE;
  m_activeSave->freeAgents.push_back(released);
  m_activeSave->roster.erase(it);
  AddEvent("squad", "Released player " + playerName, -1, false);
  ModifyBoardConfidence(-1);
}

void CareerDatabase::RecordMatchStats(const std::string& playerName, int goals, int assists) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->roster.begin(), m_activeSave->roster.end(),
                         [&playerName](const PlayerCareerState& player) { return player.name == playerName; });
  if (it == m_activeSave->roster.end()) return;

  it->matchesPlayed++;
  it->careerGoals += std::max(0, goals);
  it->careerAssists += std::max(0, assists);
  it->matchForm = ClampInt(it->matchForm + goals * 6 + assists * 4 + 2, 0, 100);
  it->morale = ClampInt(it->morale + goals * 3 + assists * 2 + 1, 0, 100);
  UpdatePlayerValue(*it);
}

void CareerDatabase::PopulateTransferMarket() {
  if (!m_activeSave) return;
  if (!m_transferTargets.empty()) return;

  static const std::vector<std::string> firstNames = {"Alex", "Bruno", "Marco", "Noah", "Theo", "Rayan", "Luis", "Evan"};
  static const std::vector<std::string> lastNames = {"Silva", "Rossi", "Meyer", "Costa", "Santos", "Fischer", "Lopez", "Ibrahim"};
  static const std::vector<std::string> positions = {"GK", "CB", "LB", "RB", "DM", "CM", "AM", "CF", "ST"};

  m_transferTargets.clear();
  for (int i = 0; i < 18; ++i) {
    TransferTarget target;
    target.name = firstNames[RandomInt(0, static_cast<int>(firstNames.size()) - 1)] + " " +
                  lastNames[RandomInt(0, static_cast<int>(lastNames.size()) - 1)] + " " + std::to_string(i + 1);
    target.preferredPosition = positions[RandomInt(0, static_cast<int>(positions.size()) - 1)];
    target.age = RandomInt(18, 31);
    target.overallRating = RandomInt(62, 84);
    target.potentialRating = std::max(target.overallRating, target.overallRating + RandomInt(1, 10));
    target.value = static_cast<long long>(target.overallRating) * target.overallRating * 4500;
    target.askingPrice = target.value + target.value * RandomInt(10, 30) / 100;
    target.wage = std::max(1500LL, target.value / 1400LL);
    target.teamID = 1000 + i;
    target.isListed = true;
    m_transferTargets.push_back(target);
  }
}

std::vector<TransferTarget> CareerDatabase::GetTransferTargets() const { return m_transferTargets; }

TransferBid CareerDatabase::PlaceBid(const std::string& playerName, long long bidAmount, int offeredWage,
                                     int contractYears) {
  TransferBid bid;
  bid.playerName = playerName;
  bid.bidAmount = bidAmount;
  bid.offeredWage = offeredWage;
  bid.contractYears = contractYears;
  bid.agentFee = std::max(25000LL, bidAmount / 20);
  bid.status = BidStatus::REJECTED;

  if (!m_activeSave) return bid;

  auto targetIt = std::find_if(m_transferTargets.begin(), m_transferTargets.end(),
                               [&playerName](const TransferTarget& target) { return target.name == playerName; });
  if (targetIt == m_transferTargets.end()) return bid;

  long long totalCost = bidAmount + bid.agentFee;
  if (totalCost > m_activeSave->transferBudget || offeredWage > m_activeSave->wageBudget) {
    AddEvent("transfer", "Bid rejected for " + playerName + " due to budget limits", -1, false);
    return bid;
  }

  bid.status = BidStatus::PENDING;
  m_activeBids.push_back(bid);
  AddEvent("transfer", "Placed bid for " + playerName, 0, false);
  return bid;
}

void CareerDatabase::WithdrawBid(const std::string& playerName) {
  auto it = std::find_if(m_activeBids.begin(), m_activeBids.end(),
                         [&playerName](const TransferBid& bid) { return bid.playerName == playerName; });
  if (it == m_activeBids.end()) return;
  it->status = BidStatus::WITHDRAWN;
}

void CareerDatabase::ProcessPendingBids() {
  if (!m_activeSave) return;

  for (auto& bid : m_activeBids) {
    if (bid.status != BidStatus::PENDING) continue;

    auto targetIt = std::find_if(m_transferTargets.begin(), m_transferTargets.end(),
                                 [&bid](const TransferTarget& target) { return target.name == bid.playerName; });
    if (targetIt == m_transferTargets.end()) {
      bid.status = BidStatus::REJECTED;
      continue;
    }

    long long requiredPrice = targetIt->askingPrice;
    if (bid.negotiationRounds >= 2) requiredPrice = requiredPrice * 90 / 100;

    if (bid.bidAmount >= requiredPrice && bid.offeredWage >= targetIt->wage * 9 / 10) {
      bid.status = BidStatus::ACCEPTED;
      AddEvent("transfer", "Bid accepted for " + bid.playerName, 1, false);
    } else {
      bid.status = BidStatus::REJECTED;
      AddEvent("transfer", "Bid rejected for " + bid.playerName, -1, false);
    }
  }
}

std::string CareerDatabase::GetBidStatusString(BidStatus status) const {
  switch (status) {
    case BidStatus::PENDING: return "Pending";
    case BidStatus::ACCEPTED: return "Accepted";
    case BidStatus::REJECTED: return "Rejected";
    case BidStatus::WITHDRAWN: return "Withdrawn";
  }
  return "Pending";
}

bool CareerDatabase::CompleteTransfer(const std::string& playerName) {
  if (!m_activeSave) return false;

  auto bidIt = std::find_if(m_activeBids.begin(), m_activeBids.end(),
                            [&playerName](const TransferBid& bid) {
                              return bid.playerName == playerName && bid.status == BidStatus::ACCEPTED;
                            });
  if (bidIt == m_activeBids.end()) return false;

  auto targetIt = std::find_if(m_transferTargets.begin(), m_transferTargets.end(),
                               [&playerName](const TransferTarget& target) { return target.name == playerName; });
  if (targetIt == m_transferTargets.end()) return false;

  long long totalCost = bidIt->bidAmount + bidIt->agentFee;
  if (totalCost > m_activeSave->transferBudget || bidIt->offeredWage > m_activeSave->wageBudget) return false;

  PlayerCareerState player;
  player.name = targetIt->name;
  player.preferredPosition = targetIt->preferredPosition;
  player.position = targetIt->preferredPosition;
  player.ovr = targetIt->overallRating;
  player.pot = targetIt->potentialRating;
  player.age = targetIt->age;
  player.value = targetIt->value;
  player.wage = bidIt->offeredWage;
  player.contract.yearsRemaining = bidIt->contractYears;
  player.contract.transferListed = false;
  player.morale = 70;
  player.fitness = 95;
  player.matchForm = 60;

  m_activeSave->transferBudget -= totalCost;
  m_activeSave->wageBudget -= bidIt->offeredWage;
  m_activeSave->finance.transferBudget = m_activeSave->transferBudget;
  m_activeSave->finance.wageBudget = m_activeSave->wageBudget;
  m_activeSave->finances.transferSpending += bidIt->bidAmount;
  m_activeSave->roster.push_back(player);

  m_transferTargets.erase(targetIt);
  m_activeBids.erase(std::remove_if(m_activeBids.begin(), m_activeBids.end(),
                                    [&playerName](const TransferBid& bid) {
                                      return bid.playerName == playerName;
                                    }),
                     m_activeBids.end());

  AddEvent("transfer", "Completed transfer for " + playerName, 2, false);
  ModifyBoardConfidence(1);
  return true;
}

void CareerDatabase::InitializeOwnerData() {
  if (!m_activeSave) return;

  if (m_activeSave->stadium.name.empty()) m_activeSave->stadium.name = m_activeSave->name + " Stadium";
  if (m_activeSave->stadium.availableUpgrades.empty()) {
    m_activeSave->stadium.availableUpgrades.push_back({"Expand North Stand", "Adds a new upper tier.", 12000000, 2, 2, 8000, 1500000});
    m_activeSave->stadium.availableUpgrades.push_back({"Hospitality Suites", "Improves VIP match-day revenue.", 6500000, 1, 1, 0, 2200000});
    m_activeSave->stadium.availableUpgrades.push_back({"Training Complex", "Supports player development and prestige.", 9000000, 2, 2, 0, 1000000});
  }

  if (m_activeSave->staff.empty()) {
    m_activeSave->staff.push_back(StaffMember("Avery Cole", "Assistant Coach", 68, 850000, 3));
    m_activeSave->staff.push_back(StaffMember("Nina Petrov", "Head Scout", 72, 950000, 3));
    m_activeSave->staff.push_back(StaffMember("Marcus Reed", "Physio", 70, 780000, 2));
  }

  long long playerWages = 0;
  for (const auto& player : m_activeSave->roster) playerWages += player.wage;
  long long staffWages = 0;
  for (const auto& member : m_activeSave->staff) staffWages += member.salary;

  m_activeSave->finances.playerWages = playerWages;
  m_activeSave->finances.staffWages = staffWages;
  m_activeSave->finances.matchDayIncome = m_activeSave->stadium.matchDayRevenue;
  m_activeSave->finances.sponsorIncome = 0;
  m_activeSave->finances.stadiumCosts = m_activeSave->stadium.maintenanceCost;
  m_activeSave->finances.totalRevenue = m_activeSave->finances.matchDayIncome +
                                        m_activeSave->finances.sponsorIncome +
                                        m_activeSave->finances.merchandiseIncome +
                                        m_activeSave->finances.tvRevenue +
                                        m_activeSave->finances.transferIncome;
  m_activeSave->finances.totalExpenses = m_activeSave->finances.playerWages +
                                         m_activeSave->finances.staffWages +
                                         m_activeSave->finances.stadiumCosts +
                                         m_activeSave->finances.transferSpending;
}

void CareerDatabase::UpgradeStadium(int upgradeIndex) {
  if (!m_activeSave) return;
  if (upgradeIndex < 0 || upgradeIndex >= static_cast<int>(m_activeSave->stadium.availableUpgrades.size())) return;

  const StadiumUpgrade upgrade = m_activeSave->stadium.availableUpgrades[upgradeIndex];
  if (upgrade.cost > m_activeSave->finances.netWorth) return;

  m_activeSave->finances.netWorth -= upgrade.cost;
  m_activeSave->finances.stadiumCosts += upgrade.cost / std::max(1, upgrade.buildTimeSeasons);
  m_activeSave->stadium.upgrades.push_back(upgrade);
  m_activeSave->stadium.availableUpgrades.erase(m_activeSave->stadium.availableUpgrades.begin() + upgradeIndex);
  AddEvent("stadium", "Started stadium upgrade: " + upgrade.name, 1, false);
}

void CareerDatabase::RenameStadium(const std::string& newName) {
  if (!m_activeSave || newName.empty()) return;
  m_activeSave->stadium.name = newName;
}

void CareerDatabase::RepairStadium(int amount) {
  if (!m_activeSave) return;
  int repairAmount = std::max(1, amount);
  long long repairCost = 50000LL * std::max(1, repairAmount / 10);
  if (repairCost > m_activeSave->finances.netWorth) return;

  m_activeSave->finances.netWorth -= repairCost;
  m_activeSave->stadium.condition = ClampInt(m_activeSave->stadium.condition + repairAmount, 0, 100);
  m_activeSave->stadium.fanSatisfaction = ClampInt(m_activeSave->stadium.fanSatisfaction + repairAmount / 2, 0, 100);
}

void CareerDatabase::SetTicketPrice(int price) {
  if (!m_activeSave) return;
  m_activeSave->finances.ticketPrice = ClampInt(price, 10, 200);
  int delta = m_activeSave->finances.ticketPrice - 40;
  m_activeSave->fanBase = ClampInt(m_activeSave->fanBase - (delta / 8), 10, 100);
  m_activeSave->stadium.fanSatisfaction = ClampInt(m_activeSave->stadium.fanSatisfaction - (delta / 4), 0, 100);
}

void CareerDatabase::HireStaff(const StaffMember& member) {
  if (!m_activeSave) return;
  if (member.salary > m_activeSave->finances.netWorth) return;
  m_activeSave->staff.push_back(member);
  m_activeSave->finances.netWorth -= member.salary;
  m_activeSave->finances.staffWages += member.salary;
}

void CareerDatabase::FireStaff(const std::string& staffName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->staff.begin(), m_activeSave->staff.end(),
                         [&staffName](const StaffMember& member) { return member.name == staffName; });
  if (it == m_activeSave->staff.end()) return;
  m_activeSave->finances.staffWages = std::max(0LL, m_activeSave->finances.staffWages - it->salary);
  m_activeSave->staff.erase(it);
  ModifyBoardConfidence(-1);
}

void CareerDatabase::GenerateStaffCandidates(std::vector<StaffMember>& candidates) {
  candidates.clear();
  static const std::vector<std::string> firstNames = {
    "Jordan", "Sofia", "Callum", "Kei", "Marta", "Henrik", "Lena", "Omar",
    "Priya", "Diego", "Alina", "Samuel", "Yuki", "Fabio", "Rosa", "Thomas",
    "Aisha", "Liam", "Nadia", "Andre", "Clara"};
  static const std::vector<std::string> lastNames = {
    "Blake", "Marin", "Hart", "Tanaka", "Costa", "Lindqvist", "Petrov", "Ali",
    "Sharma", "Fernandez", "Novak", "Eriksson", "Rossi", "Muller", "Chen",
    "Dubois", "Park", "Santos", "Fischer", "Johansson", "Moreau", "Torres"};
  static const std::vector<std::string> roles = {
    "Assistant Coach", "Head Scout", "Fitness Coach", "Goalkeeping Coach",
    "Physio", "Tactical Analyst", "Youth Coach", "Set Piece Specialist"};

  std::vector<int> usedIndices;
  for (int i = 0; i < 5; ++i) {
    int nameIdx;
    do { nameIdx = RandomInt(0, static_cast<int>(firstNames.size()) - 1); }
    while (std::find(usedIndices.begin(), usedIndices.end(), nameIdx) != usedIndices.end());
    usedIndices.push_back(nameIdx);
    int roleIdx = RandomInt(0, static_cast<int>(roles.size()) - 1);
    candidates.push_back(StaffMember(
        firstNames[nameIdx] + " " + lastNames[nameIdx],
        roles[roleIdx], RandomInt(58, 88),
        RandomInt(500000, 1500000), RandomInt(2, 4)));
  }
}

void CareerDatabase::GenerateSponsorOffers() {
  if (!m_activeSave) return;
  m_activeSave->availableSponsorOffers.clear();
  m_activeSave->availableSponsorOffers.push_back(SponsorDeal("Vertex", "Shirt Sponsor", 4500000, 2, 40));
  m_activeSave->availableSponsorOffers.push_back(SponsorDeal("Apex Air", "Sleeve Sponsor", 2500000, 3, 55));
  m_activeSave->availableSponsorOffers.push_back(SponsorDeal("Northbank", "Training Kit", 1800000, 2, 30));
}

bool CareerDatabase::AcceptSponsorDeal(int dealIndex) {
  if (!m_activeSave) return false;
  if (dealIndex < 0 || dealIndex >= static_cast<int>(m_activeSave->availableSponsorOffers.size())) return false;

  const SponsorDeal deal = m_activeSave->availableSponsorOffers[dealIndex];
  if (m_activeSave->reputation < deal.reputationRequirement) return false;

  m_activeSave->activeSponsors.push_back(deal);
  m_activeSave->finances.sponsorIncome += deal.annualRevenue;
  m_activeSave->finances.netWorth += deal.annualRevenue;
  m_activeSave->availableSponsorOffers.erase(m_activeSave->availableSponsorOffers.begin() + dealIndex);
  AddEvent("commercial", "Signed sponsor deal with " + deal.sponsorName, 1, false);
  return true;
}

void CareerDatabase::TerminateSponsorDeal(const std::string& sponsorName) {
  if (!m_activeSave) return;
  auto it = std::find_if(m_activeSave->activeSponsors.begin(), m_activeSave->activeSponsors.end(),
                         [&sponsorName](const SponsorDeal& sponsor) { return sponsor.sponsorName == sponsorName; });
  if (it == m_activeSave->activeSponsors.end()) return;

  m_activeSave->finances.sponsorIncome = std::max(0LL, m_activeSave->finances.sponsorIncome - it->annualRevenue);
  m_activeSave->activeSponsors.erase(it);
  ModifyBoardConfidence(-2);
}

void CareerDatabase::ProcessSeasonFinances() {
  if (!m_activeSave) return;

  InitializeOwnerData();
  long long seasonMatchRevenue = m_activeSave->stadium.matchDayRevenue * 19LL;
  long long sponsorRevenue = 0;
  for (const auto& sponsor : m_activeSave->activeSponsors) sponsorRevenue += sponsor.annualRevenue;
  long long merchandiseRevenue = static_cast<long long>(m_activeSave->fanBase) * 90000LL;

  m_activeSave->finances.matchDayIncome = seasonMatchRevenue;
  m_activeSave->finances.sponsorIncome = sponsorRevenue;
  m_activeSave->finances.merchandiseIncome = merchandiseRevenue;
  m_activeSave->finances.stadiumCosts = m_activeSave->stadium.maintenanceCost;
  m_activeSave->finances.totalRevenue = seasonMatchRevenue + sponsorRevenue + merchandiseRevenue +
                                        m_activeSave->finances.tvRevenue + m_activeSave->finances.transferIncome;
  m_activeSave->finances.totalExpenses = m_activeSave->finances.playerWages +
                                         m_activeSave->finances.staffWages +
                                         m_activeSave->finances.stadiumCosts +
                                         m_activeSave->finances.transferSpending;
  long long profit = GetSeasonProfit();
  m_activeSave->finances.netWorth = std::max(0LL, m_activeSave->finances.netWorth + profit);
  m_activeSave->transferBudget = std::max(0LL, m_activeSave->transferBudget + profit / 2);
  m_activeSave->finance.transferBudget = m_activeSave->transferBudget;
}

long long CareerDatabase::GetSeasonProfit() const {
  if (!m_activeSave) return 0;
  return m_activeSave->finances.totalRevenue - m_activeSave->finances.totalExpenses;
}

std::string CareerDatabase::GetFinancialHealthString() const {
  if (!m_activeSave) return "Unknown";
  long long profit = GetSeasonProfit();
  if (m_activeSave->finances.netWorth >= 150000000 && profit >= 0) return "Elite";
  if (m_activeSave->finances.netWorth >= 75000000 && profit >= -5000000) return "Stable";
  if (m_activeSave->finances.netWorth >= 25000000) return "Tight";
  return "Critical";
}

void CareerDatabase::GenerateBoardObjectives() {
  if (!m_activeSave) return;
  m_activeSave->boardObjectives.clear();
  m_activeSave->boardObjectives.push_back({OwnerObjectiveType::FINANCIAL_STABILITY, "Finish the season with positive net profit", false, 4, -8});
  m_activeSave->boardObjectives.push_back({OwnerObjectiveType::GROW_FANBASE, "Grow the fan base to at least 60k", false, 3, -6});
  m_activeSave->boardObjectives.push_back({OwnerObjectiveType::PROMOTION, "Reach a top-half finish in the league", false, 5, -10});
}

void CareerDatabase::EvaluateBoardObjectives() {
  if (!m_activeSave) return;
  for (auto& objective : m_activeSave->boardObjectives) {
    bool completed = false;
    switch (objective.type) {
      case OwnerObjectiveType::FINANCIAL_STABILITY:
        completed = GetSeasonProfit() >= 0;
        break;
      case OwnerObjectiveType::GROW_FANBASE:
        completed = m_activeSave->fanBase >= 60;
        break;
      case OwnerObjectiveType::PROMOTION:
      case OwnerObjectiveType::AVOID_RELEGATION:
      case OwnerObjectiveType::WIN_TITLE:
        completed = !m_activeSave->history.empty() && m_activeSave->history.back().leaguePosition <= 10;
        break;
    }

    objective.completed = completed;
    if (completed) {
      m_activeSave->reputation = ClampInt(m_activeSave->reputation + objective.reputationReward, 0, 100);
      m_activeSave->club.reputation = m_activeSave->reputation;
      ModifyBoardConfidence(3);
    } else {
      ModifyBoardConfidence(objective.confidencePenalty);
    }
  }
}

void CareerDatabase::InvestInFanBase(long long amount) {
  if (!m_activeSave || amount <= 0 || amount > m_activeSave->finances.netWorth) return;
  m_activeSave->finances.netWorth -= amount;
  m_activeSave->fanBase = ClampInt(m_activeSave->fanBase + static_cast<int>(amount / 1000000LL) * 2, 0, 100);
  m_activeSave->stadium.fanSatisfaction = ClampInt(m_activeSave->stadium.fanSatisfaction + 5, 0, 100);
}

void CareerDatabase::InvestInPrestige(long long amount) {
  if (!m_activeSave || amount <= 0 || amount > m_activeSave->finances.netWorth) return;
  m_activeSave->finances.netWorth -= amount;
  m_activeSave->clubPrestige = ClampInt(m_activeSave->clubPrestige + static_cast<int>(amount / 1000000LL), 0, 100);
  m_activeSave->reputation = ClampInt(m_activeSave->reputation + static_cast<int>(amount / 1500000LL), 0, 100);
  m_activeSave->club.reputation = m_activeSave->reputation;
}

SimulatedMatch CareerDatabase::SimulateMatchResult(const std::string& opponentName, const std::string& opponentTeamDBID) {
  SimulatedMatch result;
  result.opponentName = opponentName;
  if (!m_activeSave) return result;

  int teamOVR = 65;
  int opponentOVR = 65;
  int teamMorale = 70;
  int teamForm = 50;
  std::string strategy = m_activeSave->activeStrategy;

  int ovrSum = 0;
  int moraleSum = 0;
  int formSum = 0;
  int count = 0;
  for (const auto& p : m_activeSave->roster) {
    ovrSum += p.ovr;
    moraleSum += p.morale;
    formSum += p.matchForm;
    count++;
  }
  if (count > 0) {
    teamOVR = ovrSum / count;
    teamMorale = moraleSum / count;
    teamForm = formSum / count;
  }

  // Derive a stable opponent rating from the opponent's identity so each
  // opponent plays to a different strength. Prefer the opponent name (the
  // caller always has it); fall back to a numeric team id, then to a random
  // rating. Parsing the id is guarded so a non-numeric id can never throw.
  if (!opponentName.empty()) {
    int seed = static_cast<int>(std::hash<std::string>{}(opponentName) % 1000);
    opponentOVR = 55 + (seed % 21);
  } else if (!opponentTeamDBID.empty()) {
    int idValue = 0;
    try {
      idValue = std::stoi(opponentTeamDBID);
    } catch (const std::exception&) {
      idValue = 0;
    }
    opponentOVR = 55 + ((idValue % 21) + 21) % 21;
  } else {
    opponentOVR = 60 + RandomInt(0, 20);
  }

  int baseAttack = teamOVR + teamForm / 4 + (teamMorale - 50) / 10;
  int baseDefense = teamOVR + teamForm / 5 + (teamMorale - 50) / 15;
  int oppAttack = opponentOVR + RandomInt(0, 10);
  int oppDefense = opponentOVR + RandomInt(0, 5);

  if (strategy == "Attacking") {
    baseAttack += 5;
    baseDefense -= 3;
  } else if (strategy == "Defensive") {
    baseAttack -= 3;
    baseDefense += 5;
  }

  float homeAdv = 1.1f;
  // Home goals scale with our attack relative to their defense; goals conceded
  // scale with their attack relative to our defense. Home advantage boosts our
  // attack and shores up our defense, so a stronger defense concedes fewer.
  float attackFactor = (float)baseAttack * homeAdv / std::max(1.0f, (float)oppDefense);
  float concedeFactor = (float)oppAttack / std::max(1.0f, (float)baseDefense * homeAdv);

  std::normal_distribution<float> goalDist(1.3f, 0.8f);
  int expectedHomeGoals = std::max(0, (int)(goalDist(CareerRng()) * attackFactor));
  int expectedAwayGoals = std::max(0, (int)(goalDist(CareerRng()) * concedeFactor));

  result.homeGoals = ClampInt(expectedHomeGoals, 0, 9);
  result.awayGoals = ClampInt(expectedAwayGoals, 0, 7);
  result.homeShots = result.homeGoals + RandomInt(2, 8);
  result.awayShots = result.awayGoals + RandomInt(2, 8);
  result.homePossession = ClampInt(50 + (teamOVR - opponentOVR) + RandomInt(-5, 5) + (strategy == "Attacking" ? 5 : strategy == "Defensive" ? -5 : 0), 30, 70);
  result.played = true;

  std::vector<int> scorerIndices;
  int rosterSize = static_cast<int>(m_activeSave->roster.size());
  if (rosterSize <= 0) return result;
  for (int g = 0; g < result.homeGoals; g++) {
    int attempts = 0;
    int pIdx;
    do {
      pIdx = RandomInt(0, rosterSize - 1);
      attempts++;
    } while (attempts < 20 &&
             std::find(scorerIndices.begin(), scorerIndices.end(), pIdx) != scorerIndices.end() &&
             rosterSize > static_cast<int>(scorerIndices.size()));
    scorerIndices.push_back(pIdx);
    result.scorers.push_back(m_activeSave->roster[pIdx].name);
  }

  return result;
}

void CareerDatabase::SeedRng(unsigned int seed) {
  SeedCareerRng(seed);
}

void CareerDatabase::Process3DMatchResult(int homeGoals, int awayGoals) {
  if (!m_activeSave) return;
  m_activeSave->seasonWins += (homeGoals > awayGoals) ? 1 : 0;
  m_activeSave->seasonDraws += (homeGoals == awayGoals) ? 1 : 0;
  m_activeSave->seasonLosses += (homeGoals < awayGoals) ? 1 : 0;
  m_activeSave->seasonGoalsFor += homeGoals;
  m_activeSave->seasonGoalsAgainst += awayGoals;

  std::string summary = m_activeSave->name + " " + std::to_string(homeGoals) + " - " +
                        std::to_string(awayGoals) + " (3D match)";
  AddEvent("matchday", summary,
           homeGoals > awayGoals ? 1 : (homeGoals == awayGoals ? 0 : -1),
           homeGoals != awayGoals);
}

bool CareerDatabase::SaveToFile(const std::string& path) const {
  if (!m_activeSave) return false;
  std::ofstream file(path);
  if (!file.is_open()) return false;
  file << "# Career Save: " << m_activeSave->name << "\n";
  file << "mode=" << static_cast<int>(m_activeSave->mode) << "\n";
  file << "name=" << m_activeSave->name << "\n";
  file << "managerName=" << m_activeSave->managerName << "\n";
  file << "clubName=" << m_activeSave->club.clubName << "\n";
  file << "clubID=" << m_activeSave->club.clubID << "\n";
  file << "clubLeague=" << m_activeSave->club.leagueName << "\n";
  file << "reputation=" << m_activeSave->reputation << "\n";
  file << "boardConfidence=" << m_activeSave->boardConfidence << "\n";
  file << "transferBudget=" << m_activeSave->transferBudget << "\n";
  file << "wageBudget=" << m_activeSave->wageBudget << "\n";
  file << "season=" << m_activeSave->season.currentSeason << "\n";
  file << "week=" << m_activeSave->season.currentWeek << "\n";
  file << "strategy=" << m_activeSave->activeStrategy << "\n";
  file << "fanBase=" << m_activeSave->fanBase << "\n";
  file << "clubPrestige=" << m_activeSave->clubPrestige << "\n";
  file << "seasonWins=" << m_activeSave->seasonWins << "\n";
  file << "seasonDraws=" << m_activeSave->seasonDraws << "\n";
  file << "seasonLosses=" << m_activeSave->seasonLosses << "\n";
  file << "seasonGoalsFor=" << m_activeSave->seasonGoalsFor << "\n";
  file << "seasonGoalsAgainst=" << m_activeSave->seasonGoalsAgainst << "\n";
  file << "netWorth=" << m_activeSave->finances.netWorth << "\n";
  file << "ticketPrice=" << m_activeSave->finances.ticketPrice << "\n";
  file << "stadiumCapacity=" << m_activeSave->stadium.capacity << "\n";
  file << "stadiumName=" << m_activeSave->stadium.name << "\n";
  file << "rosterSize=" << m_activeSave->roster.size() << "\n";
  for (size_t i = 0; i < m_activeSave->roster.size(); i++) {
    const auto& p = m_activeSave->roster[i];
    file << "player." << i << "=" << p.name << "|" << p.position << "|" << p.age << "|" << p.ovr << "|" << p.pot << "|" << p.value << "|" << p.wage << "\n";
  }
  file.close();
  printf("[career] Saved to %s\n", path.c_str());
  return true;
}

bool CareerDatabase::LoadFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) return false;
  m_activeSave = std::make_unique<CareerSave>();
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    if (key == "name") m_activeSave->name = val;
    else if (key == "mode") m_activeSave->mode = static_cast<CareerMode>(std::stoi(val));
    else if (key == "managerName") m_activeSave->managerName = val;
    else if (key == "clubName") m_activeSave->club.clubName = val;
    else if (key == "clubID") m_activeSave->club.clubID = std::stoi(val);
    else if (key == "clubLeague") m_activeSave->club.leagueName = val;
    else if (key == "reputation") m_activeSave->reputation = std::stoi(val);
    else if (key == "boardConfidence") m_activeSave->boardConfidence = std::stoi(val);
    else if (key == "transferBudget") m_activeSave->transferBudget = std::stoll(val);
    else if (key == "wageBudget") m_activeSave->wageBudget = std::stoll(val);
    else if (key == "season") m_activeSave->season.currentSeason = std::stoi(val);
    else if (key == "week") m_activeSave->season.currentWeek = std::stoi(val);
    else if (key == "strategy") m_activeSave->activeStrategy = val;
    else if (key == "fanBase") m_activeSave->fanBase = std::stoi(val);
    else if (key == "clubPrestige") m_activeSave->clubPrestige = std::stoi(val);
    else if (key == "seasonWins") m_activeSave->seasonWins = std::stoi(val);
    else if (key == "seasonDraws") m_activeSave->seasonDraws = std::stoi(val);
    else if (key == "seasonLosses") m_activeSave->seasonLosses = std::stoi(val);
    else if (key == "seasonGoalsFor") m_activeSave->seasonGoalsFor = std::stoi(val);
    else if (key == "seasonGoalsAgainst") m_activeSave->seasonGoalsAgainst = std::stoi(val);
    else if (key == "netWorth") m_activeSave->finances.netWorth = std::stoll(val);
    else if (key == "ticketPrice") m_activeSave->finances.ticketPrice = std::stoi(val);
    else if (key == "stadiumCapacity") m_activeSave->stadium.capacity = std::stoi(val);
    else if (key == "stadiumName") m_activeSave->stadium.name = val;
    else if (key == "rosterSize") { /* handled below */ }
    else if (key.rfind("player.", 0) == 0) {
      PlayerCareerState p;
      size_t p1 = 0, p2 = std::string::npos;
      p2 = val.find('|'); p.name = val.substr(0, p2);
      size_t p3 = val.find('|', p2 + 1); if (p3 != std::string::npos) { p.position = val.substr(p2 + 1, p3 - p2 - 1); }
      size_t p4 = val.find('|', p3 + 1); if (p4 != std::string::npos) { p.age = std::stoi(val.substr(p3 + 1, p4 - p3 - 1)); }
      size_t p5 = val.find('|', p4 + 1); if (p5 != std::string::npos) { p.ovr = std::stof(val.substr(p4 + 1, p5 - p4 - 1)); }
      size_t p6 = val.find('|', p5 + 1); if (p6 != std::string::npos) { p.pot = std::stof(val.substr(p5 + 1, p6 - p5 - 1)); }
      size_t p7 = val.find('|', p6 + 1); if (p7 != std::string::npos) { p.value = std::stoll(val.substr(p6 + 1, p7 - p6 - 1)); }
      if (p7 != std::string::npos && p7 + 1 < val.size()) { p.wage = std::stoll(val.substr(p7 + 1)); }
      m_activeSave->roster.push_back(p);
    }
  }
  file.close();
  printf("[career] Loaded from %s (%zu roster)\n", path.c_str(), m_activeSave->roster.size());
  return true;
}

} // namespace blunted
