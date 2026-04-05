#ifndef CAREER_DATABASE_HPP
#define CAREER_DATABASE_HPP

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <cstdint>

namespace blunted {

enum class BidStatus { PENDING, ACCEPTED, REJECTED, WITHDRAWN };

// ---------------------------------------------------------------------------
// Owner mode data structures
// ---------------------------------------------------------------------------

struct StadiumUpgrade {
  std::string name;
  std::string description;
  long long cost;
  int buildTimeSeasons;         // how many seasons until complete
  int seasonsRemaining;         // 0 = complete
  int capacityIncrease;
  int revenueBonus;             // per-season revenue bonus once built
  bool isComplete() const { return seasonsRemaining <= 0; }
};

struct Stadium {
  std::string name;
  int capacity;
  int condition;                // 0-100, degrades over time
  int fanSatisfaction;          // 0-100
  long long maintenanceCost;    // per-season
  long long matchDayRevenue;    // per match
  std::vector<StadiumUpgrade> upgrades;
  std::vector<StadiumUpgrade> availableUpgrades;

  Stadium() : capacity(30000), condition(70), fanSatisfaction(60),
              maintenanceCost(500000), matchDayRevenue(800000) {}
};

struct StaffMember {
  std::string name;
  std::string role;             // "Manager", "AssistantManager", "HeadScout", "YouthCoach", "PhysioChief", "MarketingDirector"
  int skill;                    // 0-100
  long long salary;
  int contractYearsRemaining;
  int morale;                   // 0-100

  StaffMember() : skill(50), salary(100000), contractYearsRemaining(2), morale(70) {}
  StaffMember(const std::string& n, const std::string& r, int s, long long sal, int yrs)
    : name(n), role(r), skill(s), salary(sal), contractYearsRemaining(yrs), morale(70) {}
};

struct SponsorDeal {
  std::string sponsorName;
  std::string type;             // "Kit", "Stadium", "Training", "Sleeve"
  long long annualRevenue;
  int yearsRemaining;
  int reputationRequirement;    // min reputation to attract

  SponsorDeal() : annualRevenue(0), yearsRemaining(0), reputationRequirement(0) {}
  SponsorDeal(const std::string& name, const std::string& t, long long rev, int yrs, int repReq)
    : sponsorName(name), type(t), annualRevenue(rev), yearsRemaining(yrs), reputationRequirement(repReq) {}
};

struct ClubFinances {
  long long totalRevenue;
  long long totalExpenses;
  long long netWorth;
  long long matchDayIncome;     // accumulated this season
  long long sponsorIncome;      // accumulated this season
  long long merchandiseIncome;
  long long tvRevenue;
  long long playerWages;        // accumulated this season
  long long staffWages;
  long long stadiumCosts;
  long long transferSpending;
  long long transferIncome;
  int ticketPrice;              // 10-200
  int seasonTicketHolders;
  int debtLevel;                // 0 = no debt, higher = more debt

  ClubFinances() : totalRevenue(0), totalExpenses(0), netWorth(50000000),
                   matchDayIncome(0), sponsorIncome(0), merchandiseIncome(0),
                   tvRevenue(5000000), playerWages(0), staffWages(0),
                   stadiumCosts(0), transferSpending(0), transferIncome(0),
                   ticketPrice(40), seasonTicketHolders(15000), debtLevel(0) {}
};

enum class BoardObjectiveType { PROMOTION, AVOID_RELEGATION, WIN_TITLE, FINANCIAL_STABILITY, GROW_FANBASE };

struct BoardObjective {
  BoardObjectiveType type;
  std::string description;
  bool completed;
  int reputationReward;
  int confidencePenalty;        // if not met

  BoardObjective() : type(BoardObjectiveType::FINANCIAL_STABILITY), completed(false),
                     reputationReward(5), confidencePenalty(-10) {}
  BoardObjective(BoardObjectiveType t, const std::string& desc, int reward, int penalty)
    : type(t), description(desc), completed(false), reputationReward(reward), confidencePenalty(penalty) {}
};

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

  // Owner-mode specific fields
  Stadium stadium;
  ClubFinances finances;
  std::vector<StaffMember> staff;
  std::vector<SponsorDeal> activeSponsors;
  std::vector<SponsorDeal> availableSponsorOffers;
  std::vector<BoardObjective> boardObjectives;
  int fanBase;                  // thousands of fans
  int clubPrestige;             // 0-100, affects sponsor deals and transfers
  
  CareerSave() : reputation(50), seasonsPlayed(0), teamID(-1), 
                 transferBudget(15000000), wageBudget(250000), boardConfidence(75), 
                 trainingPoints(10), scoutingNetworkLevel(1), activeStrategy("Balanced"),
                 isSeasonActive(false), fanBase(50), clubPrestige(50) {}
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

  // Owner mode: Stadium
  void InitializeOwnerData();
  void UpgradeStadium(int upgradeIndex);
  void RenameStadium(const std::string& newName);
  void RepairStadium(int amount);
  void SetTicketPrice(int price);

  // Owner mode: Staff
  void HireStaff(const StaffMember& member);
  void FireStaff(const std::string& staffName);
  void GenerateStaffCandidates(std::vector<StaffMember>& candidates);

  // Owner mode: Sponsors
  void GenerateSponsorOffers();
  bool AcceptSponsorDeal(int dealIndex);
  void TerminateSponsorDeal(const std::string& sponsorName);

  // Owner mode: Finances
  void ProcessSeasonFinances();
  long long GetSeasonProfit() const;
  std::string GetFinancialHealthString() const;

  // Owner mode: Board
  void GenerateBoardObjectives();
  void EvaluateBoardObjectives();

  // Owner mode: Club
  void InvestInFanBase(long long amount);
  void InvestInPrestige(long long amount);

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