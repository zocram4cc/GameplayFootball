#ifndef CAREER_DATABASE_HPP
#define CAREER_DATABASE_HPP

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <cstdint>

namespace blunted {

enum class BidStatus { PENDING, ACCEPTED, REJECTED, WITHDRAWN };

struct CareerEvent {
  std::string type;
  std::string description;
  int reputationImpact;
  int64_t timestamp;
  bool isMajor;

  CareerEvent(const std::string& t, const std::string& desc, int rep, int64_t time, bool major = false)
    : type(t), description(desc), reputationImpact(rep), timestamp(time), isMajor(major) {}
};

struct TransferTarget {
  std::string name;
  std::string preferredPosition;
  int overallRating;
  int potentialRating;
  int age;
  long long value;
  long long wage;
  long long askingPrice;
  int teamID;
  bool isListed;

  TransferTarget()
    : overallRating(0), potentialRating(0), age(0), value(0), wage(0),
      askingPrice(0), teamID(-1), isListed(true) {}
};

struct TransferBid {
  std::string playerName;
  long long bidAmount;
  int offeredWage;
  int contractYears;
  long long agentFee;
  BidStatus status;
  int negotiationRounds;

  TransferBid()
    : bidAmount(0), offeredWage(0), contractYears(3), agentFee(0),
      status(BidStatus::PENDING), negotiationRounds(0) {}
};

struct CareerPlayer {
  std::string name;
  std::string preferredPosition; // Matches e_PlayerRole: "GK", "CB", "LB", "RB", "DM", "CM", "LM", "RM", "AM", "CF"
  int overallRating;
  int potentialRating;
  int age;
  long long value;
  long long wage;
  int morale;
  int matchForm;
  bool isYouth;
  int contractYearsRemaining;
  int careerGoals;
  int careerAssists;
  int matchesPlayed;
  int databaseID; // Links to players.id in SQLite (0 = generated, not from DB)

  CareerPlayer(std::string n, int ovr, int pot, int a, long long val, long long w,
                const std::string& pos = "CM")
    : name(n), preferredPosition(pos), overallRating(ovr), potentialRating(pot), age(a),
      value(val), wage(w), morale(75), matchForm(50), isYouth(false),
      contractYearsRemaining(3), careerGoals(0), careerAssists(0), matchesPlayed(0), databaseID(0) {}

  CareerPlayer()
    : preferredPosition("CM"), overallRating(0), potentialRating(0), age(0),
      value(0), wage(0), morale(50), matchForm(50), isYouth(false),
      contractYearsRemaining(0), careerGoals(0), careerAssists(0), matchesPlayed(0), databaseID(0) {}
};

struct CareerSave {
  std::string name;
  std::string mode;        // "manager", "player", "mygm", "mycoach", "owner"
  std::string managerName;
  int reputation;
  int seasonsPlayed;
  std::string leagueName;
  int teamID;

  // Board & Financials
  long long transferBudget;
  long long wageBudget;
  int boardConfidence; // 0-100

  // New additions for GM/Coach modes
  std::vector<CareerPlayer> roster;
  std::vector<CareerPlayer> freeAgents;
  std::vector<CareerPlayer> youthAcademy;
  
  int trainingPoints;
  int scoutingNetworkLevel; // 1-5
  std::string activeStrategy;

  std::map<std::string, int> legacyStats;
  
  std::vector<CareerEvent> recentEvents;

  std::vector<std::string> seasonSummaries;
  bool isSeasonActive;
  
  CareerSave() : reputation(50), seasonsPlayed(0), teamID(-1), 
                 transferBudget(15000000), wageBudget(250000), boardConfidence(75), 
                 trainingPoints(10), scoutingNetworkLevel(1), activeStrategy("Balanced"),
                 isSeasonActive(false) {}
};

class CareerDatabase {
public:
  CareerDatabase();
  ~CareerDatabase();

  static CareerDatabase& GetInstance() {
    static CareerDatabase instance;
    return instance;
  }

  // Initialize the career database for a new or loaded save
  bool Initialize(const std::string& saveDir);
  
  // Load an existing career save
  bool LoadCareerSave(const std::string& saveName);
  
  // Create and save a new career
  bool CreateNewCareer(const std::string& careerName, const std::string& mode, const std::string& managerName);

  // Save current career state
  bool SaveCareerData();

  // Get current active career save
  CareerSave* GetActiveSave() { return m_activeSave.get(); }

  // Add an event that affects reputation and narrative
  void AddEvent(const std::string& eventType, const std::string& description, int reputationDelta, bool isMajor);

  // Gameplay specific mutations
  void RecruitFreeAgent(const std::string& playerName);
  bool TrainSquad();
  bool TrainFocus(const std::string& focusArea);
  void SetStrategy(const std::string& strategy);
  
  // Youth Academy
  void ScoutYouthPlayer();
  void PromoteYouthPlayer(const std::string& playerName);

  // Board & Financials
  void ModifyBudget(long long transferDelta, long long wageDelta);
  void ModifyBoardConfidence(int delta);

  // Season & Growth
  void AdvanceSeason();
  void ProcessPlayerGrowth(CareerPlayer& player);
  void UpdatePlayerValue(CareerPlayer& player);
  void ReleasePlayer(const std::string& playerName);
  void RecordMatchStats(const std::string& playerName, int goals, int assists);

  // Transfer Market
  void PopulateTransferMarket();
  std::vector<TransferTarget> GetTransferTargets() const;
  TransferBid PlaceBid(const std::string& playerName, long long bidAmount, int offeredWage, int contractYears);
  std::vector<TransferBid>& GetActiveBids() { return m_activeBids; }
  void WithdrawBid(const std::string& playerName);
  void ProcessPendingBids();
  std::string GetBidStatusString(BidStatus status) const;
  int GetNextBidID() { return ++m_nextBidID; }
  bool CompleteTransfer(const std::string& playerName);

  // Helper getters
  int GetReputation() const;
  std::string GetReputationStatus() const;
  std::string GetMoraleString(int morale) const;
  std::string GetFormString(int form) const;
  int GetLegacyStat(const std::string& statName) const;
  std::vector<CareerEvent> GetRecentEvents(int limit = 5) const;

private:
  std::unique_ptr<CareerSave> m_activeSave;
  std::string m_saveDirectory;
  std::vector<TransferTarget> m_transferTargets;
  std::vector<TransferBid> m_activeBids;
  int m_nextBidID = 0;
};

} // namespace blunted

#endif // CAREER_DATABASE_HPP