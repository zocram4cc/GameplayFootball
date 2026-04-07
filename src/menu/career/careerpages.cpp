#include "careerpages.hpp"

#include "../../data/teamdata.hpp"
#include "../../data/playerdata.hpp"
#include "../../gamedefines.hpp"
#include "../../main.hpp"
#include "../pagefactory.hpp"
#include "base/properties.hpp"
#include "base/utils.hpp"
#include "career_database.hpp"
#include <cstdio>

using namespace blunted;

namespace {

std::string GetCareerModeDisplay(const CareerSave* save) {
  if (!save) return "Career";
  if (save->mode == CareerMode::COACH) return "myCoach";
  if (save->mode == CareerMode::GM) return "myGM";
  if (save->mode == CareerMode::PLAYER) return "Player Career";
  if (save->mode == CareerMode::OWNER) return "Owner Career";
  return "Manager Career";
}

std::string FormatCareerMoney(long long amount) {
  return "EUR " + std::to_string(amount);
}

}

static bool IsOwnerMode() {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();
  return save && save->mode == CareerMode::OWNER;
}

static int GetHubPageID() {
  return IsOwnerMode() ? e_PageID_OwnerHub : e_PageID_CareerHub;
}

// ---------------------------------------------------------------------------
// CareerMenuPage
// ---------------------------------------------------------------------------

CareerMenuPage::CareerMenuPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_menu", 6, 6, 88, 88, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_career", 10, 8, 68, 4, "Career Modes");
  this->AddView(title);
  title->Show();

  Gui2Caption* subtitle = new Gui2Caption(
      windowManager, "caption_career_sub", 10, 13, 74, 4,
      "Choose whether you want to control the touchline, the boardroom, or a single player journey.");
  this->AddView(subtitle);
  subtitle->Show();

  Gui2Button* btnCoach = new Gui2Button(windowManager, "btn_mycoach", 0, 0, 34, 5, "myCoach\nMatchday leadership");
  Gui2Button* btnGM = new Gui2Button(windowManager, "btn_mygm", 0, 0, 34, 5, "myGM\nRoster building");
  Gui2Button* btnPlayer =
      new Gui2Button(windowManager, "btn_playercareer", 0, 0, 34, 5, "Player Career\nOne pro, full journey");
  Gui2Button* btnManager =
      new Gui2Button(windowManager, "btn_managercareer", 0, 0, 34, 5, "Manager Career\nSquad, tactics, results");
  Gui2Button* btnOwner =
      new Gui2Button(windowManager, "btn_ownercareer", 0, 0, 34, 5, "Owner Career\nFinances, board, stadium");

  btnCoach->sig_OnClick.connect([this](...) { GoMyCoach(); });
  btnGM->sig_OnClick.connect([this](...) { GoMyGM(); });
  btnPlayer->sig_OnClick.connect([this](...) { GoPlayerCareer(); });
  btnManager->sig_OnClick.connect([this](...) { GoManagerCareer(); });
  btnOwner->sig_OnClick.connect([this](...) { GoOwnerCareer(); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "career_grid", 10, 22, 72, 56);
  grid->AddView(btnCoach, 0, 0);
  grid->AddView(btnGM, 0, 1);
  grid->AddView(btnPlayer, 1, 0);
  grid->AddView(btnManager, 1, 1);
  grid->AddView(btnOwner, 2, 0);
  grid->UpdateLayout(0.5);

  this->AddView(grid);
  grid->Show();

  Gui2Caption* footer = new Gui2Caption(
      windowManager, "caption_career_footer", 10, 82, 72, 4,
      "Owner mode now surfaces club finances, staff, sponsors, and infrastructure in one executive flow.");
  this->AddView(footer);
  footer->Show();

  btnCoach->SetFocus();
  this->Show();
}

CareerMenuPage::~CareerMenuPage() {}

void CareerMenuPage::GoCareerMode(const std::string& mode) {
  Properties props;
  props.Set("careerMode", mode);
  CreatePage(e_PageID_CareerNewGame, props);
}

void CareerMenuPage::GoMyCoach() { GoCareerMode("mycoach"); }
void CareerMenuPage::GoMyGM() { GoCareerMode("mygm"); }
void CareerMenuPage::GoPlayerCareer() { GoCareerMode("player"); }
void CareerMenuPage::GoManagerCareer() { GoCareerMode("manager"); }
void CareerMenuPage::GoOwnerCareer() { GoCareerMode("owner"); }

// ---------------------------------------------------------------------------
// CareerNewGamePage
// ---------------------------------------------------------------------------

CareerNewGamePage::CareerNewGamePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_new", 5, 5, 90, 90, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  m_mode = pageData.properties ? pageData.properties->Get("careerMode", "manager") : "manager";

  std::string modeLabel = "Manager Career";
  if (m_mode == "mycoach")
    modeLabel = "myCoach";
  else if (m_mode == "mygm")
    modeLabel = "myGM";
  else if (m_mode == "player")
    modeLabel = "Player Career";
  else if (m_mode == "owner")
    modeLabel = "Owner Career";

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_newgame", 20, 10, 60, 3, "New " + modeLabel);
  this->AddView(title);
  title->Show();

  Gui2Caption* setupHint = new Gui2Caption(
      windowManager, "caption_newgame_hint", 12, 14, 76, 3,
      "Set your club and identity. Owner careers start with higher budgets and a business-focused dashboard.");
  this->AddView(setupHint);
  setupHint->Show();

  Gui2Caption* teamCaption =
      new Gui2Caption(windowManager, "caption_newgame_team", 10, 20, 30, 2.5, "Select your team:");
  this->AddView(teamCaption);
  teamCaption->Show();

  teamSelectPulldown =
      new Gui2Pulldown(windowManager, "pulldown_career_teamselect", 40, 20, 30, 3);
  RefreshTeamSelect();
  teamSelectPulldown->sig_OnChange.connect([this](Gui2Pulldown* pd) {
    m_selectedTeamID = pd->GetSelected();
  });
  this->AddView(teamSelectPulldown);
  teamSelectPulldown->Show();

  std::string nameFieldLabel = "Manager name:";
  std::string nameDefault = "Manager";
  if (m_mode == "player") {
    nameFieldLabel = "Player name:";
    nameDefault = "Player";
  } else if (m_mode == "mygm") {
    nameFieldLabel = "GM name:";
    nameDefault = "GM";
  } else if (m_mode == "mycoach") {
    nameFieldLabel = "Coach name:";
    nameDefault = "Coach";
  } else if (m_mode == "owner") {
    nameFieldLabel = "Owner name:";
    nameDefault = "Owner";
  }

  Gui2Caption* mgrCaption =
      new Gui2Caption(windowManager, "caption_newgame_mgr", 10, 28, 30, 2.5, nameFieldLabel);
  this->AddView(mgrCaption);
  mgrCaption->Show();

  managerNameInput =
      new Gui2EditLine(windowManager, "editline_career_mgrname", 40, 28, 30, 3, nameDefault);
  managerNameInput->SetMaxLength(32);
  this->AddView(managerNameInput);
  managerNameInput->Show();

  Gui2Button* btnStart =
      new Gui2Button(windowManager, "btn_start_career", 30, 50, 40, 3, "Start Career");
  btnStart->sig_OnClick.connect([this](...) { StartCareer(); });
  this->AddView(btnStart);
  btnStart->Show();
  btnStart->SetFocus();

  this->Show();
}

CareerNewGamePage::~CareerNewGamePage() {}

void CareerNewGamePage::RefreshTeamSelect() {
  teamSelectPulldown->ClearEntries();
  try {
    auto result = GetDB()->Query(
        "SELECT teams.id, teams.name, leagues.name FROM teams "
        "JOIN leagues ON teams.league_id = leagues.id ORDER BY leagues.name, teams.name");
    for (unsigned int r = 0; r < result->data.size(); r++) {
      std::string id = result->data.at(r).at(0);
      std::string teamName = result->data.at(r).at(1);
      std::string leagueName = result->data.at(r).at(2);
      teamSelectPulldown->AddEntry(teamName + " (" + leagueName + ")", id);
    }
  } catch (...) {
    teamSelectPulldown->AddEntry("No teams found", "0");
  }
  if (m_selectedTeamID.empty()) {
    m_selectedTeamID = "0";
  }
  teamSelectPulldown->SetSelected(0);
}

static std::string RoleToCareerPos(e_PlayerRole role) {
  return GetRoleName(role);
}

static int ComputePlayerOVR(PlayerData* pd) {
  const char* statNames[] = {
    "physical_balance", "physical_reaction", "physical_acceleration", "physical_velocity",
    "physical_stamina", "physical_agility", "physical_shotpower",
    "technical_standingtackle", "technical_slidingtackle", "technical_ballcontrol",
    "technical_dribble", "technical_shortpass", "technical_highpass", "technical_header",
    "technical_shot", "technical_volley",
    "mental_calmness", "mental_workrate", "mental_resilience",
    "mental_defensivepositioning", "mental_offensivepositioning", "mental_vision"
  };
  float total = 0.0f;
  int count = 0;
  for (const char* name : statNames) {
    total += pd->GetStat(name);
    count++;
  }
  return count > 0 ? static_cast<int>((total / count) * 100.0f) : 50;
}

void CareerNewGamePage::StartCareer() {
  int teamDBID = atoi(m_selectedTeamID.c_str());

  std::string teamName = "Unknown";
  std::string leagueName = "Unknown";
  try {
    auto result = GetDB()->Query(
        "SELECT teams.name, leagues.name FROM teams "
        "JOIN leagues ON teams.league_id = leagues.id WHERE teams.id = " +
        int_to_str(teamDBID));
    if (!result->data.empty()) {
      teamName = result->data.at(0).at(0);
      leagueName = result->data.at(0).at(1);
    }
  } catch (...) {}

  CareerDatabase::GetInstance().Initialize("user/career");
  CareerDatabase::GetInstance().CreateNewCareer(
      teamName, m_mode, managerNameInput->GetText());

  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();
  if (save) {
    save->club.clubID = teamDBID;
    save->club.leagueName = leagueName;

    save->roster.clear();
    TeamData teamData(teamDBID);
    const auto& players = teamData.GetPlayerData();
    for (const auto& pd : players) {
      int ovr = ComputePlayerOVR(pd.get());
      int pot = std::min(99, ovr + static_cast<int>(random(3, 20)));
      int age = 22;
      try {
        auto ageResult = GetDB()->Query(
            "SELECT age FROM players WHERE id = " + int_to_str(pd->GetDatabaseID()));
        if (!ageResult->data.empty()) {
          age = atoi(ageResult->data.at(0).at(0).c_str());
          pot = std::min(99, ovr + static_cast<int>((99 - age) * 0.5));
        }
      } catch (...) {}

      const auto& roles = pd->GetRoles();
      std::string pos = roles.empty() ? "CM" : RoleToCareerPos(roles[0]);

      long long value = static_cast<long long>(ovr) * static_cast<long long>(ovr) * 5000;
      long long wage = (value / 1000) + static_cast<int>(random(500, 2000));

      PlayerCareerState cp;
      cp.name = pd->GetFirstName() + " " + pd->GetLastName();
      cp.position = pos;
      cp.preferredPosition = pos;
      cp.ovr = ovr;
      cp.pot = pot;
      cp.age = age;
      cp.value = value;
      cp.wage = wage;
      cp.databaseID = pd->GetDatabaseID();
      cp.contract.yearsRemaining = static_cast<int>(random(2, 5));
      save->roster.push_back(cp);
    }

    long long totalWage = 0;
    for (const auto& p : save->roster) totalWage += p.wage;
    save->wageBudget = totalWage * 130 / 100;
    save->transferBudget = 15000000;

    if (m_mode == "owner") {
      save->mode = CareerMode::OWNER;
      save->transferBudget = 60000000;
      save->wageBudget = totalWage * 150 / 100;
    }
  }

  if (IsOwnerMode()) {
    CreatePage(e_PageID_OwnerHub);
  } else {
    CreatePage(e_PageID_CareerHub);
  }
}

// ---------------------------------------------------------------------------
// CareerHubPage
// ---------------------------------------------------------------------------

CareerHubPage::CareerHubPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_hub", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  if (IsOwnerMode()) {
    CreatePage(e_PageID_OwnerHub);
    return;
  }

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_careerhub", 20, 5, 60, 3, "Career Hub");
  this->AddView(title);
  title->Show();

  Gui2Button* btnTransfers =
      new Gui2Button(windowManager, "btn_transfers", 0, 0, 38, 3, "Transfer Market");
  Gui2Button* btnFreeAgency =
      new Gui2Button(windowManager, "btn_freeagency", 0, 0, 38, 3, "Free Agency (Recruiting)");
  Gui2Button* btnSquad = new Gui2Button(windowManager, "btn_squad", 0, 0, 38, 3, "My Squad");
  Gui2Button* btnTraining = new Gui2Button(windowManager, "btn_training", 0, 0, 38, 3, "Training");
  Gui2Button* btnStrategy = new Gui2Button(windowManager, "btn_strategy", 0, 0, 38, 3, "Strategy & Tactics");
  Gui2Button* btnYouth = new Gui2Button(windowManager, "btn_youth", 0, 0, 38, 3, "Youth Academy");
  Gui2Button* btnPressConf =
      new Gui2Button(windowManager, "btn_pressconf", 0, 0, 38, 3, "Press Conference");
  Gui2Button* btnLeagueExp =
      new Gui2Button(windowManager, "btn_leagueexp", 0, 0, 38, 3, "League Expansion / Relegation");
  Gui2Button* btnCustomLeague =
      new Gui2Button(windowManager, "btn_customleague", 0, 0, 38, 3, "Custom League");
  Gui2Button* btnSeason =
      new Gui2Button(windowManager, "btn_season_end", 0, 0, 38, 3, ">> End Season / Advance >>");

  btnTransfers->sig_OnClick.connect([this](...) { GoTransferMarket(); });
  btnFreeAgency->sig_OnClick.connect([this](...) { GoFreeAgency(); });
  btnSquad->sig_OnClick.connect([this](...) { GoSquad(); });
  btnTraining->sig_OnClick.connect([this](...) { GoTraining(); });
  btnStrategy->sig_OnClick.connect([this](...) { GoStrategy(); });
  btnYouth->sig_OnClick.connect([this](...) { GoYouthAcademy(); });
  btnPressConf->sig_OnClick.connect([this](...) { GoPressConference(); });
  btnLeagueExp->sig_OnClick.connect([this](...) { GoLeagueExpansion(); });
  btnCustomLeague->sig_OnClick.connect([this](...) { GoCustomLeague(); });
  btnSeason->sig_OnClick.connect([this](...) { GoSeason(); });

  CareerSave* activeSave = CareerDatabase::GetInstance().GetActiveSave();
  if (activeSave) {
    std::string modeDisplay = GetCareerModeDisplay(activeSave);

    Gui2Caption* teamLabel =
      new Gui2Caption(windowManager, "caption_hub_team", 10, 8, 80, 2,
        "Mode: " + modeDisplay + " | Team: " + activeSave->name +
        " | League: " + activeSave->club.leagueName);
    this->AddView(teamLabel);
    teamLabel->Show();

    std::string finInfo = "Transfer Budget: " + FormatCareerMoney(activeSave->transferBudget) +
                          " | Wage Budget: " + FormatCareerMoney(activeSave->wageBudget);
    Gui2Caption* finances = new Gui2Caption(windowManager, "caption_hub_fin", 10, 10, 80, 2, finInfo);
    this->AddView(finances);
    finances->Show();
    
    std::string repInfo = "Board Confidence: " + std::to_string(activeSave->boardConfidence) + "%" +
                          " | Rep: " + CareerDatabase::GetInstance().GetReputationStatus() +
                          " | Season: " + std::to_string(activeSave->season.currentSeason);
    Gui2Caption* reputation = new Gui2Caption(windowManager, "caption_hub_rep", 10, 12, 80, 2, repInfo);
    this->AddView(reputation);
    reputation->Show();

    std::string squadInfo = "Squad Size: " + std::to_string(activeSave->roster.size()) +
                            " | Training Points: " + std::to_string(activeSave->trainingPoints) +
                            " | Youth: " + std::to_string(activeSave->youthAcademy.size());
    Gui2Caption* squad = new Gui2Caption(windowManager, "caption_hub_squad", 10, 14, 80, 2, squadInfo);
    this->AddView(squad);
    squad->Show();

    std::string overview = "Strategy: " + activeSave->activeStrategy +
                           " | Free Agents: " + std::to_string(activeSave->freeAgents.size()) +
                           " | Inbox: " + std::to_string(activeSave->inbox.size());
    Gui2Caption* overviewLine = new Gui2Caption(windowManager, "caption_hub_overview", 10, 16, 80, 2, overview);
    this->AddView(overviewLine);
    overviewLine->Show();
  }

  Gui2Grid* grid = new Gui2Grid(windowManager, "hub_grid", 10, 22, 80, 62);
  grid->AddView(btnSquad, 0, 0);
  grid->AddView(btnStrategy, 1, 0);
  grid->AddView(btnTraining, 2, 0);
  grid->AddView(btnYouth, 3, 0);
  grid->AddView(btnSeason, 4, 0);
  grid->AddView(btnTransfers, 0, 1);
  grid->AddView(btnFreeAgency, 1, 1);
  grid->AddView(btnPressConf, 2, 1);
  grid->AddView(btnLeagueExp, 3, 1);
  grid->AddView(btnCustomLeague, 4, 1);
  grid->UpdateLayout(0.5);

  this->AddView(grid);
  grid->Show();

  btnTransfers->SetFocus();
  this->Show();
}

CareerHubPage::~CareerHubPage() {}

void CareerHubPage::GoTransferMarket() { CreatePage(e_PageID_CareerTransferMarket); }
void CareerHubPage::GoSquad() { CreatePage(e_PageID_CareerSquadRoster); }
void CareerHubPage::GoPressConference() { CreatePage(e_PageID_CareerPressConference); }
void CareerHubPage::GoLeagueExpansion() { CreatePage(e_PageID_CareerLeagueExpansion); }
void CareerHubPage::GoCustomLeague() { CreatePage(e_PageID_CareerCustomLeague); }
void CareerHubPage::GoFreeAgency() { CreatePage(e_PageID_CareerFreeAgency); }
void CareerHubPage::GoTraining() { CreatePage(e_PageID_CareerTraining); }
void CareerHubPage::GoStrategy() { CreatePage(e_PageID_CareerStrategy); }
void CareerHubPage::GoYouthAcademy() { CreatePage(e_PageID_CareerYouthAcademy); }
void CareerHubPage::GoSeason() { CreatePage(e_PageID_CareerSeason); }

// ---------------------------------------------------------------------------
// CareerTransferMarketPage
// ---------------------------------------------------------------------------

CareerTransferMarketPage::CareerTransferMarketPage(Gui2WindowManager* windowManager,
                                                   const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_tm", 0, 0, 100, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  CareerDatabase::GetInstance().PopulateTransferMarket();

  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();
  std::string budgetStr = save ? ("Transfer Budget: €" + std::to_string(save->transferBudget)) : "No active save";

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_tm_title", 10, 3, 80, 3, "Transfer Market");
  this->AddView(title);
  title->Show();

  Gui2Caption* budget =
      new Gui2Caption(windowManager, "caption_tm_budget", 10, 7, 80, 2, budgetStr);
  this->AddView(budget);
  budget->Show();

  Gui2Caption* marketHint = new Gui2Caption(
      windowManager, "caption_tm_hint", 10, 9, 80, 2,
      "Tap a listed player to open a lightweight negotiation screen with fee and budget warnings.");
  this->AddView(marketHint);
  marketHint->Show();

  Gui2Caption* header =
      new Gui2Caption(windowManager, "caption_tm_header", 3, 12, 94, 2,
                      "Name                  | POS | OVR | POT | Age | Value          | Asking Price");
  this->AddView(header);
  header->Show();

  auto targets = CareerDatabase::GetInstance().GetTransferTargets();
  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_tm", 3, 15, 94, 58);
  int row = 0;
  for (const auto& t : targets) {
    if (row >= 16) break;
    char buf[256];
    snprintf(buf, sizeof(buf), "%-20s | %-3s | %2d  | %2d  | %2d  | %12lld  | %12lld",
             t.name.c_str(), t.preferredPosition.c_str(),
             t.overallRating, t.potentialRating, t.age,
             t.value, t.askingPrice);
    Gui2Button* btn = new Gui2Button(windowManager, "btn_tm_" + std::to_string(row), 0, 0, 90, 2.5, buf);
    btn->sig_OnClick.connect([this, t](...) {
      Properties props;
      props.Set("playerName", t.name);
      props.Set("askingPrice", std::to_string(t.askingPrice));
      props.Set("playerWage", std::to_string(t.wage));
      CreatePage(e_PageID_CareerTransferBidDetail, props);
    });
    grid->AddView(btn, row++, 0);
  }
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  Gui2Button* btnBids = new Gui2Button(windowManager, "btn_tm_mybids", 5, 80, 40, 3, "My Bids");
  btnBids->sig_OnClick.connect([this](...) { CreatePage(e_PageID_CareerTransferBids); });
  this->AddView(btnBids);
  btnBids->Show();

  Gui2Button* btnProcess = new Gui2Button(windowManager, "btn_tm_process", 50, 80, 40, 3, "Process Pending Bids");
  btnProcess->sig_OnClick.connect([this](...) {
    CareerDatabase::GetInstance().ProcessPendingBids();
    CreatePage(e_PageID_CareerTransferBids);
  });
  this->AddView(btnProcess);
  btnProcess->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_tm_back", 30, 90, 40, 3, "Back to Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(GetHubPageID()); });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();

  this->Show();
}

CareerTransferMarketPage::~CareerTransferMarketPage() {}

// ---------------------------------------------------------------------------
// CareerTransferBidsPage
// ---------------------------------------------------------------------------

CareerTransferBidsPage::CareerTransferBidsPage(Gui2WindowManager* windowManager,
                                               const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_bids", 0, 0, 100, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_bids_title", 10, 5, 80, 3, "My Transfer Bids");
  this->AddView(title);
  title->Show();

  auto& bids = CareerDatabase::GetInstance().GetActiveBids();
  if (bids.empty()) {
    Gui2Caption* info =
        new Gui2Caption(windowManager, "caption_bids_empty", 10, 20, 80, 4, "No active bids.");
    this->AddView(info);
    info->Show();
  } else {
    Gui2Caption* header =
        new Gui2Caption(windowManager, "caption_bids_header", 5, 12, 90, 2,
                        "Player                 | Bid Amount      | Wage    | Yrs | Status");
    this->AddView(header);
    header->Show();

    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_bids", 5, 15, 90, 60);
    int row = 0;
    for (const auto& b : bids) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%-22s | €%-14lld | €%-6d | %d   | %s",
               b.playerName.c_str(), b.bidAmount, b.offeredWage, b.contractYears,
               CareerDatabase::GetInstance().GetBidStatusString(b.status).c_str());
      Gui2Button* btn = new Gui2Button(windowManager, "btn_bid_" + std::to_string(row), 0, 0, 86, 2.5, buf);
      if (b.status == BidStatus::ACCEPTED) {
        std::string pName = b.playerName;
        btn->sig_OnClick.connect([this, pName](...) {
          CareerDatabase::GetInstance().CompleteTransfer(pName);
          CreatePage(e_PageID_CareerTransferBids);
        });
      } else if (b.status == BidStatus::PENDING) {
        std::string pName = b.playerName;
        btn->sig_OnClick.connect([this, pName](...) { NegotiateBid(pName); });
      }
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_bids_back", 5, 85, 40, 3, "Back to Market");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_CareerTransferMarket); });
  this->AddView(btnBack);
  btnBack->Show();

  Gui2Button* btnHub = new Gui2Button(windowManager, "btn_bids_hub", 50, 85, 40, 3, "Back to Hub");
  btnHub->sig_OnClick.connect([this](...) { CreatePage(GetHubPageID()); });
  this->AddView(btnHub);
  btnHub->Show();
  btnBack->SetFocus();

  this->Show();
}

CareerTransferBidsPage::~CareerTransferBidsPage() {}

void CareerTransferBidsPage::NegotiateBid(const std::string& playerName) {
  auto& bids = CareerDatabase::GetInstance().GetActiveBids();
  for (auto& b : bids) {
    if (b.playerName == playerName && b.status == BidStatus::PENDING) {
      long long increase = b.bidAmount / 10;
      if (increase < 50000) increase = 50000;
      b.bidAmount += increase;
      b.agentFee = b.bidAmount / 20;
      b.negotiationRounds++;
      break;
    }
  }
  CreatePage(e_PageID_CareerTransferBids);
}

// ---------------------------------------------------------------------------
// CareerTransferBidDetailPage
// ---------------------------------------------------------------------------

CareerTransferBidDetailPage::CareerTransferBidDetailPage(Gui2WindowManager* windowManager,
                                                         const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_biddtl", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  m_playerName = pageData.properties ? pageData.properties->Get("playerName", "") : "";
  m_askingPrice = pageData.properties ? atoll(pageData.properties->Get("askingPrice", "0").c_str()) : 0;
  m_playerWage = pageData.properties ? atoll(pageData.properties->Get("playerWage", "0").c_str()) : 0;

  auto targets = CareerDatabase::GetInstance().GetTransferTargets();
  TransferTarget target;
  bool found = false;
  for (const auto& t : targets) {
    if (t.name == m_playerName) { target = t; found = true; break; }
  }

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_detail_title", 10, 5, 80, 3, "Transfer: " + m_playerName);
  this->AddView(title);
  title->Show();

  if (found) {
    std::string info1 = "Position: " + target.preferredPosition + " | OVR: " + std::to_string(target.overallRating) +
                       " | POT: " + std::to_string(target.potentialRating) + " | Age: " + std::to_string(target.age);
    Gui2Caption* line1 = new Gui2Caption(windowManager, "caption_detail_info1", 10, 12, 80, 3, info1);
    this->AddView(line1);
    line1->Show();

    std::string info2 = "Value: €" + std::to_string(target.value) +
                       " | Asking Price: €" + std::to_string(target.askingPrice) +
                       " | Wage: €" + std::to_string(target.wage);
    Gui2Caption* line2 = new Gui2Caption(windowManager, "caption_detail_info2", 10, 16, 80, 3, info2);
    this->AddView(line2);
    line2->Show();

    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_detail", 10, 25, 80, 40);

    long long askPrice = target.askingPrice;
    Gui2Button* bidFull = new Gui2Button(windowManager, "btn_bid_full", 0, 0, 76, 3,
      "Bid Asking Price: €" + std::to_string(askPrice));
    bidFull->sig_OnClick.connect([this, askPrice](...) { PlaceBidForPlayer(askPrice); });
    grid->AddView(bidFull, 0, 0);

    long long bid80 = target.askingPrice * 80 / 100;
    Gui2Button* bid80Button = new Gui2Button(windowManager, "btn_bid_80", 0, 0, 76, 3,
      "Bid 80%: €" + std::to_string(bid80) + " (may be rejected)");
    bid80Button->sig_OnClick.connect([this, bid80](...) { PlaceBidForPlayer(bid80); });
    grid->AddView(bid80Button, 1, 0);

    long long bid60 = target.askingPrice * 60 / 100;
    Gui2Button* bid60Button = new Gui2Button(windowManager, "btn_bid_60", 0, 0, 76, 3,
      "Bid 60%: €" + std::to_string(bid60) + " (likely rejected)");
    bid60Button->sig_OnClick.connect([this, bid60](...) { PlaceBidForPlayer(bid60); });
    grid->AddView(bid60Button, 2, 0);

    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();

    CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();
    if (save) {
      long long totalWithFee = target.askingPrice + (target.askingPrice / 20);
      std::string feeNote = "Agent fee (5%): €" + std::to_string(target.askingPrice / 20) +
                           " | Total cost: €" + std::to_string(totalWithFee);
      if (totalWithFee > save->transferBudget) {
        feeNote += " | WARNING: Exceeds budget!";
      }
      Gui2Caption* fee = new Gui2Caption(windowManager, "caption_detail_fee", 10, 68, 80, 4, feeNote);
      this->AddView(fee);
      fee->Show();
    }
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_detail_back", 30, 85, 40, 3, "Back to Market");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_CareerTransferMarket); });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();

  this->Show();
}

CareerTransferBidDetailPage::~CareerTransferBidDetailPage() {}

void CareerTransferBidDetailPage::PlaceBidForPlayer(long long amount) {
  TransferBid bid = CareerDatabase::GetInstance().PlaceBid(m_playerName, amount, static_cast<int>(m_playerWage), 3);
  if (bid.status == BidStatus::REJECTED) {
    Gui2Caption* warn =
        new Gui2Caption(windowManager, "caption_bid_warn", 10, 78, 80, 3, "Bid rejected - insufficient budget!");
    this->AddView(warn);
    warn->Show();
  } else {
    CreatePage(e_PageID_CareerTransferBids);
  }
}

// ---------------------------------------------------------------------------
// CareerPressConferencePage
// ---------------------------------------------------------------------------

CareerPressConferencePage::CareerPressConferencePage(Gui2WindowManager* windowManager,
                                                     const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_press", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_pressconf", 10, 5, 80, 3, "Press Conference");
  this->AddView(title);
  title->Show();

  Gui2Caption* question =
      new Gui2Caption(windowManager, "caption_pc_question", 10, 12, 80, 5,
                      "How do you feel about the team's performance this week?");
  this->AddView(question);
  question->Show();

  Gui2Button* btnPositive =
      new Gui2Button(windowManager, "btn_pc_positive", 0, 0, 76, 3,
                     "We gave everything – the fans should be proud. (+reputation)");
  Gui2Button* btnNeutral = new Gui2Button(windowManager, "btn_pc_neutral", 0, 0, 76, 3,
                                           "It was a decent performance; we move on.");
  Gui2Button* btnNegative = new Gui2Button(windowManager, "btn_pc_negative", 0, 0, 76, 3,
                                            "I'm disappointed. We must do better. (-reputation)");

  btnPositive->sig_OnClick.connect([this](...) { SelectAnswer(0); });
  btnNeutral->sig_OnClick.connect([this](...) { SelectAnswer(1); });
  btnNegative->sig_OnClick.connect([this](...) { SelectAnswer(2); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "pc_grid", 12, 22, 76, 50);
  grid->AddView(btnPositive, 0, 0);
  grid->AddView(btnNeutral, 1, 0);
  grid->AddView(btnNegative, 2, 0);
  grid->UpdateLayout(0.5);

  this->AddView(grid);
  grid->Show();

  btnPositive->SetFocus();
  this->Show();
}

CareerPressConferencePage::~CareerPressConferencePage() {}

void CareerPressConferencePage::SelectAnswer(int answerIndex) {
  int delta = m_reputationDeltas[answerIndex];
  CareerDatabase::GetInstance().AddEvent("press_conference",
    delta > 0 ? "Positive press conference (+reputation)" :
    delta < 0 ? "Negative press conference (-reputation)" :
    "Neutral press conference",
    delta, false);
  if (delta > 0) {
    CareerDatabase::GetInstance().ModifyBoardConfidence(1);
  } else if (delta < 0) {
    CareerDatabase::GetInstance().ModifyBoardConfidence(-2);
  }
  CreatePage(GetHubPageID());
}

// ---------------------------------------------------------------------------
// CareerLeagueExpansionPage
// ---------------------------------------------------------------------------

CareerLeagueExpansionPage::CareerLeagueExpansionPage(Gui2WindowManager* windowManager,
                                                     const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_exp", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_leagueexp", 10, 5, 80, 3,
                                       "League Expansion & Relegation");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_leagueexp_info", 10, 12, 80, 3,
                                      "Configure promotion and relegation for your league.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnEnable = new Gui2Button(windowManager, "btn_leagueexp_enable", 0, 0, 60, 3,
                                         "Enable Promotion / Relegation");
  Gui2Button* btnDisable = new Gui2Button(windowManager, "btn_leagueexp_disable", 0, 0, 60, 3,
                                          "Disable Promotion / Relegation");
  Gui2Button* btnAddDiv =
      new Gui2Button(windowManager, "btn_leagueexp_adddiv", 0, 0, 60, 3, "Add Division");

  btnEnable->sig_OnClick.connect([this](...) { EnableRelegation(); });
  btnDisable->sig_OnClick.connect([this](...) { DisableRelegation(); });
  btnAddDiv->sig_OnClick.connect([this](...) { AddDivision(); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "leagueexp_grid", 20, 20, 60, 60);
  grid->AddView(btnEnable, 0, 0);
  grid->AddView(btnDisable, 1, 0);
  grid->AddView(btnAddDiv, 2, 0);
  grid->UpdateLayout(0.5);

  this->AddView(grid);
  grid->Show();

  btnEnable->SetFocus();
  this->Show();
}

CareerLeagueExpansionPage::~CareerLeagueExpansionPage() {}
void CareerLeagueExpansionPage::EnableRelegation() { CreatePage(GetHubPageID()); }
void CareerLeagueExpansionPage::DisableRelegation() { CreatePage(GetHubPageID()); }
void CareerLeagueExpansionPage::AddDivision() { CreatePage(GetHubPageID()); }

// ---------------------------------------------------------------------------
// CareerCustomLeaguePage
// ---------------------------------------------------------------------------

CareerCustomLeaguePage::CareerCustomLeaguePage(Gui2WindowManager* windowManager,
                                               const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_cust", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_customleague", 10, 5, 80, 3,
                                       "Custom League Creation");
  this->AddView(title);
  title->Show();

  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_customleague_info", 10, 12, 80, 3,
                      "Design your own league: name, divisions, teams and cup competition.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnCreate = new Gui2Button(windowManager, "btn_customleague_create", 30, 50, 40, 3,
                                         "Create Custom League");
  btnCreate->sig_OnClick.connect([this](...) { CreateCustomLeague(); });
  this->AddView(btnCreate);
  btnCreate->Show();
  btnCreate->SetFocus();

  this->Show();
}

CareerCustomLeaguePage::~CareerCustomLeaguePage() {}
void CareerCustomLeaguePage::CreateCustomLeague() { CreatePage(GetHubPageID()); }

// ---------------------------------------------------------------------------
// CareerFreeAgencyPage
// ---------------------------------------------------------------------------

CareerFreeAgencyPage::CareerFreeAgencyPage(Gui2WindowManager* windowManager,
                                           const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_fa", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_freeagency", 10, 5, 80, 3, "Free Agency / Recruiting");
  this->AddView(title);
  title->Show();

  CareerSave* activeSave = CareerDatabase::GetInstance().GetActiveSave();
  if (activeSave) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "fa_grid", 10, 15, 80, 70);
    int row = 0;
    for (const PlayerCareerState& fa : activeSave->freeAgents) {
      std::string label = fa.name + " | OVR: " + std::to_string(fa.ovr) + 
                          " | Wage: €" + std::to_string(fa.wage);
      Gui2Button* btn = new Gui2Button(windowManager, "btn_recruit_" + fa.name, 0, 0, 76, 3, "Recruit " + label);
      btn->sig_OnClick.connect([this, fa](...) { RecruitPlayer(fa.name); });
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_fa_back", 30, 90, 40, 3, "Back to Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(GetHubPageID()); });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();

  this->Show();
}

CareerFreeAgencyPage::~CareerFreeAgencyPage() {}

void CareerFreeAgencyPage::RecruitPlayer(const std::string& playerName) {
  CareerDatabase::GetInstance().RecruitFreeAgent(playerName);
  CreatePage(e_PageID_CareerFreeAgency);
}

// ---------------------------------------------------------------------------
// CareerTrainingPage
// ---------------------------------------------------------------------------

CareerTrainingPage::CareerTrainingPage(Gui2WindowManager* windowManager,
                                       const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_train", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_training", 10, 5, 80, 3, "Squad Training");
  this->AddView(title);
  title->Show();

  CareerSave* activeSave = CareerDatabase::GetInstance().GetActiveSave();
  int tp = activeSave ? activeSave->trainingPoints : 0;
  
  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_tp", 10, 15, 80, 3, "Available Training Points: " + std::to_string(tp));
  this->AddView(info);
  info->Show();

  Gui2Caption* hint = new Gui2Caption(
      windowManager, "caption_train_hint", 10, 19, 80, 3,
      "General sessions raise form across the whole squad. Focus drills lean toward role-specific growth.");
  this->AddView(hint);
  hint->Show();

  Gui2Grid* grid = new Gui2Grid(windowManager, "train_grid", 15, 30, 70, 50);

  Gui2Button* btnGeneral = new Gui2Button(windowManager, "btn_train_gen", 0, 0, 66, 3, "General Training (+Form All, -1 TP)");
  btnGeneral->sig_OnClick.connect([this](...) { TrainSquad(); });
  grid->AddView(btnGeneral, 0, 0);

  Gui2Button* btnAttacking = new Gui2Button(windowManager, "btn_train_atk", 0, 0, 66, 3, "Attacking Focus (CF/LM/RM/AM +OVR, -1 TP)");
  btnAttacking->sig_OnClick.connect([this](...) { TrainFocus("Attacking"); });
  grid->AddView(btnAttacking, 1, 0);

  Gui2Button* btnDefending = new Gui2Button(windowManager, "btn_train_def", 0, 0, 66, 3, "Defending Focus (CB/LB/RB/DM/GK +OVR, -1 TP)");
  btnDefending->sig_OnClick.connect([this](...) { TrainFocus("Defending"); });
  grid->AddView(btnDefending, 2, 0);

  Gui2Button* btnPhysical = new Gui2Button(windowManager, "btn_train_phy", 0, 0, 66, 3, "Physical Focus (+Morale +Form All, -1 TP)");
  btnPhysical->sig_OnClick.connect([this](...) { TrainFocus("Physical"); });
  grid->AddView(btnPhysical, 3, 0);

  Gui2Button* btnTactical = new Gui2Button(windowManager, "btn_train_tac", 0, 0, 66, 3, "Tactical Focus (CM/DM/AM +OVR, -1 TP)");
  btnTactical->sig_OnClick.connect([this](...) { TrainFocus("Tactical"); });
  grid->AddView(btnTactical, 4, 0);

  Gui2Button* btnShooting = new Gui2Button(windowManager, "btn_train_shoot", 0, 0, 66, 3, "Shooting Focus (CF/LM/RM +OVR, -1 TP)");
  btnShooting->sig_OnClick.connect([this](...) { TrainFocus("Shooting"); });
  grid->AddView(btnShooting, 5, 0);

  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_tr_back", 30, 90, 40, 3, "Back to Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(GetHubPageID()); });
  this->AddView(btnBack);
  btnBack->Show();
  btnGeneral->SetFocus();

  this->Show();
}

CareerTrainingPage::~CareerTrainingPage() {}

void CareerTrainingPage::TrainSquad() {
  if (CareerDatabase::GetInstance().TrainSquad()) {
    CreatePage(e_PageID_CareerTraining);
  }
}

void CareerTrainingPage::TrainFocus(const std::string& focusArea) {
  if (CareerDatabase::GetInstance().TrainFocus(focusArea)) {
    CreatePage(e_PageID_CareerTraining);
  }
}

// ---------------------------------------------------------------------------
// CareerStrategyPage
// ---------------------------------------------------------------------------

CareerStrategyPage::CareerStrategyPage(Gui2WindowManager* windowManager,
                                       const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_strat", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_strategy", 10, 5, 80, 3, "Team Strategy");
  this->AddView(title);
  title->Show();

  CareerSave* activeSave = CareerDatabase::GetInstance().GetActiveSave();
  std::string curStrat = activeSave ? activeSave->activeStrategy : "None";
  
  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_curstrat", 10, 15, 80, 3, "Current Strategy: " + curStrat);
  this->AddView(info);
  info->Show();

  Gui2Caption* hint = new Gui2Caption(
      windowManager, "caption_curstrat_hint", 10, 19, 80, 3,
      "Use this page to set the broad tone for your squad between transfer, training, and season screens.");
  this->AddView(hint);
  hint->Show();

  Gui2Grid* grid = new Gui2Grid(windowManager, "strat_grid", 20, 32, 60, 40);
  
  Gui2Button* btnAttacking = new Gui2Button(windowManager, "btn_strat_atk", 0, 0, 60, 3, "Attacking");
  btnAttacking->sig_OnClick.connect([this](...) { SetStrategy("Attacking"); });
  grid->AddView(btnAttacking, 0, 0);

  Gui2Button* btnBalanced = new Gui2Button(windowManager, "btn_strat_bal", 0, 0, 60, 3, "Balanced");
  btnBalanced->sig_OnClick.connect([this](...) { SetStrategy("Balanced"); });
  grid->AddView(btnBalanced, 1, 0);

  Gui2Button* btnDefensive = new Gui2Button(windowManager, "btn_strat_def", 0, 0, 60, 3, "Defensive");
  btnDefensive->sig_OnClick.connect([this](...) { SetStrategy("Defensive"); });
  grid->AddView(btnDefensive, 2, 0);

  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_st_back", 30, 90, 40, 3, "Back to Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(GetHubPageID()); });
  this->AddView(btnBack);
  btnBack->Show();
  btnAttacking->SetFocus();

  this->Show();
}

CareerStrategyPage::~CareerStrategyPage() {}

void CareerStrategyPage::SetStrategy(const std::string& strategyName) {
  CareerDatabase::GetInstance().SetStrategy(strategyName);
  CreatePage(e_PageID_CareerStrategy);
}

// ---------------------------------------------------------------------------
// CareerYouthAcademyPage
// ---------------------------------------------------------------------------

CareerYouthAcademyPage::CareerYouthAcademyPage(Gui2WindowManager* windowManager,
                                               const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_ya", 5, 0, 90, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_youth", 10, 5, 80, 3, "Youth Academy");
  this->AddView(title);
  title->Show();

  CareerSave* activeSave = CareerDatabase::GetInstance().GetActiveSave();
  if (activeSave) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "ya_grid", 10, 15, 80, 60);
    int row = 0;
    
    int scoutCost = 50000 * activeSave->scoutingNetworkLevel;
    Gui2Button* btnScout = new Gui2Button(windowManager, "btn_scout_youth", 0, 0, 76, 3, 
                                          "Scout New Talent (-€" + std::to_string(scoutCost) + ")");
    btnScout->sig_OnClick.connect([this](...) { ScoutPlayer(); });
    grid->AddView(btnScout, row++, 0);

    for (const PlayerCareerState& ya : activeSave->youthAcademy) {
      std::string label = ya.name + " | Age: " + std::to_string(ya.age) + 
                          " | OVR: " + std::to_string(ya.ovr) + 
                          " | POT: " + std::to_string(ya.pot);
      Gui2Button* btn = new Gui2Button(windowManager, "btn_promote_" + ya.name, 0, 0, 76, 3, "Promote " + label);
      btn->sig_OnClick.connect([this, ya](...) { PromotePlayer(ya.name); });
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();
    btnScout->SetFocus();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_ya_back", 30, 90, 40, 3, "Back to Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(GetHubPageID()); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

CareerYouthAcademyPage::~CareerYouthAcademyPage() {}

void CareerYouthAcademyPage::ScoutPlayer() {
  CareerDatabase::GetInstance().ScoutYouthPlayer();
  CreatePage(e_PageID_CareerYouthAcademy);
}

void CareerYouthAcademyPage::PromotePlayer(const std::string& playerName) {
  CareerDatabase::GetInstance().PromoteYouthPlayer(playerName);
  CreatePage(e_PageID_CareerYouthAcademy);
}

// ---------------------------------------------------------------------------
// CareerSquadRosterPage
// ---------------------------------------------------------------------------

CareerSquadRosterPage::CareerSquadRosterPage(Gui2WindowManager* windowManager,
                                             const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_squad", 0, 0, 100, 100, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_squad", 10, 3, 80, 3, "My Squad");
  this->AddView(title);
  title->Show();

  CareerSave* activeSave = CareerDatabase::GetInstance().GetActiveSave();
  if (activeSave) {
    long long totalWage = 0;
    for (const auto& p : activeSave->roster) {
      totalWage += p.wage;
    }
    Gui2Caption* header =
      new Gui2Caption(windowManager, "caption_squad_header", 5, 6, 90, 2,
        "Name              | POS | OVR | POT | Age | Value          | Wage       | Morale | Form | Contract");
    this->AddView(header);
    header->Show();

    Gui2Caption* squadHint = new Gui2Caption(
      windowManager, "caption_squad_hint", 5, 8, 90, 2,
      "Selecting a player releases them immediately, so use this page as a lean roster-management screen.");
    this->AddView(squadHint);
    squadHint->Show();

    Gui2Grid* grid = new Gui2Grid(windowManager, "squad_grid", 5, 11, 90, 70);
    int row = 0;
    for (const auto& player : activeSave->roster) {
      std::string formStr = CareerDatabase::GetInstance().GetFormString(player.matchForm);
      std::string moraleStr = CareerDatabase::GetInstance().GetMoraleString(player.morale);

      char labelBuf[256];
      snprintf(labelBuf, sizeof(labelBuf),
        "%-17s | %-3s | %2d  | %2d  | %2d  | %10lld  | %9lld | %-7s | %-5s | %d yr",
        player.name.c_str(), player.preferredPosition.c_str(),
        player.ovr, player.pot, player.age,
        player.value, player.wage,
        moraleStr.c_str(), formStr.c_str(), player.contract.yearsRemaining);

      std::string label(labelBuf);
      Gui2Button* btn = new Gui2Button(windowManager, "btn_release_" + std::to_string(row), 0, 0, 84, 2.5, label);
      btn->sig_OnClick.connect([this, player](...) { ReleasePlayer(player.name); });
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();

    Gui2Caption* footer =
      new Gui2Caption(windowManager, "caption_squad_footer", 5, 83, 90, 2,
        "Total Squad: " + std::to_string(activeSave->roster.size()) + " players | Total Wage Bill: €" + std::to_string(totalWage) + "/week");
    this->AddView(footer);
    footer->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_squad_back", 30, 90, 40, 3, "Back to Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(GetHubPageID()); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

CareerSquadRosterPage::~CareerSquadRosterPage() {}

void CareerSquadRosterPage::ReleasePlayer(const std::string& playerName) {
  CareerDatabase::GetInstance().ReleasePlayer(playerName);
  CreatePage(e_PageID_CareerSquadRoster);
}

// ---------------------------------------------------------------------------
// CareerSeasonPage
// ---------------------------------------------------------------------------

CareerSeasonPage::CareerSeasonPage(Gui2WindowManager* windowManager,
                                   const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* bgPanel = new Gui2Frame(windowManager, "bg_career_season", 4, 2, 92, 96, true);
  this->AddView(bgPanel);
  bgPanel->Show();
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_season", 6, 4, 80, 3, IsOwnerMode() ? "Season Review" : "End of Season");
  bgPanel->AddView(title);
  title->Show();

  CareerSave* activeSave = CareerDatabase::GetInstance().GetActiveSave();
  if (activeSave) {
    Gui2Caption* info = new Gui2Caption(
      windowManager, "caption_season_info", 6, 8, 82, 2,
      "Current Season: " + std::to_string(activeSave->season.currentSeason) +
      " | Board Confidence: " + std::to_string(activeSave->boardConfidence) + "%" +
      " | Reputation: " + CareerDatabase::GetInstance().GetReputationStatus());
    bgPanel->AddView(info);
    info->Show();

    Gui2Frame* summaryFrame = new Gui2Frame(windowManager, "frame_season_summary", 4, 12, 84, 12, true);
    std::string summary = "Mode: " + GetCareerModeDisplay(activeSave) +
                          "\nTransfer Budget: " + FormatCareerMoney(activeSave->transferBudget) +
                          " | Wage Budget: " + FormatCareerMoney(activeSave->wageBudget) +
                          "\nSquad Size: " + std::to_string(activeSave->roster.size()) +
                          " | Youth Players: " + std::to_string(activeSave->youthAcademy.size());
    if (activeSave->mode == CareerMode::OWNER) {
      summary += "\nNet Worth: " + FormatCareerMoney(activeSave->finances.netWorth) +
                 " | Profit: " + FormatCareerMoney(CareerDatabase::GetInstance().GetSeasonProfit());
    }
    Gui2Caption* summaryCap = new Gui2Caption(windowManager, "caption_season_summary", 2, 2, 80, 8, summary);
    summaryFrame->AddView(summaryCap);
    summaryCap->Show();
    bgPanel->AddView(summaryFrame);
    summaryFrame->Show();

    Gui2Caption* warning =
      new Gui2Caption(windowManager, "caption_season_warn", 6, 27, 82, 4,
        activeSave->mode == CareerMode::OWNER ?
          "Owner review will settle seasonal finances, evaluate board objectives, rotate sponsor offers, and advance the football calendar." :
          "Advancing the season will process player growth, contract changes, budget updates, and age all players. This cannot be undone.");
    bgPanel->AddView(warning);
    warning->Show();

    if (activeSave->mode == CareerMode::OWNER) {
      Gui2Frame* ownerFrame = new Gui2Frame(windowManager, "frame_season_owner", 4, 34, 84, 18, true);
      Gui2Caption* ownerTitle = new Gui2Caption(windowManager, "caption_season_owner_title", 2, 1, 78, 2,
        "Owner Review Checklist");
      ownerFrame->AddView(ownerTitle);
      ownerTitle->Show();

      int ownerY = 4;
      std::string ownerLines[] = {
        "1. Financials are recalculated and net worth changes are applied.",
        "2. Board objectives are marked complete or failed.",
        "3. Sponsor inventory refreshes for the new season.",
        "4. Stadium projects tick down and completed upgrades unlock value."
      };
      for (int i = 0; i < 4; ++i) {
        Gui2Caption* line = new Gui2Caption(windowManager, "caption_season_owner_" + std::to_string(i), 2, ownerY, 78, 2, ownerLines[i]);
        ownerFrame->AddView(line);
        line->Show();
        ownerY += 3;
      }
      bgPanel->AddView(ownerFrame);
      ownerFrame->Show();
    }

    if (!activeSave->season.seasonSummaries.empty()) {
      Gui2Caption* histTitle =
        new Gui2Caption(windowManager, "caption_season_hist", 6, 55, 80, 2, "Past Seasons:");
      bgPanel->AddView(histTitle);
      histTitle->Show();

      Gui2Grid* histGrid = new Gui2Grid(windowManager, "season_hist_grid", 6, 58, 80, 18);
      int row = 0;
      int startIdx = std::max(0, static_cast<int>(activeSave->season.seasonSummaries.size()) - 5);
      for (int i = startIdx; i < static_cast<int>(activeSave->season.seasonSummaries.size()); i++) {
        Gui2Caption* entry =
          new Gui2Caption(windowManager, "caption_hist_" + std::to_string(row), 0, 0, 76, 2,
                          activeSave->season.seasonSummaries[i]);
        histGrid->AddView(entry, row++, 0);
      }
      histGrid->UpdateLayout(0.5);
      bgPanel->AddView(histGrid);
      histGrid->Show();
    }
  }

  Gui2Button* btnAdvance = new Gui2Button(windowManager, "btn_season_advance", 22, 80, 48, 4,
    IsOwnerMode() ? ">> CLOSE THE BOOKS AND ADVANCE >>" : ">> ADVANCE SEASON >>");
  btnAdvance->sig_OnClick.connect([this](...) { AdvanceSeason(); });
  bgPanel->AddView(btnAdvance);
  btnAdvance->Show();
  btnAdvance->SetFocus();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_season_back", 30, 87, 30, 3, "Back to Hub");
  btnBack->sig_OnClick.connect([this](...) { GoToHub(); });
  bgPanel->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

CareerSeasonPage::~CareerSeasonPage() {}

void CareerSeasonPage::AdvanceSeason() {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();
  if (save && save->mode == CareerMode::OWNER) {
    CareerDatabase::GetInstance().ProcessSeasonFinances();
    CareerDatabase::GetInstance().EvaluateBoardObjectives();
    CareerDatabase::GetInstance().GenerateSponsorOffers();
    CareerDatabase::GetInstance().GenerateBoardObjectives();
  }
  CareerDatabase::GetInstance().AdvanceSeason();

  if (IsOwnerMode()) {
    CreatePage(e_PageID_OwnerHub);
  } else {
    CreatePage(e_PageID_CareerHub);
  }
}

void CareerSeasonPage::GoToHub() {
  if (IsOwnerMode()) {
    CreatePage(e_PageID_OwnerHub);
  } else {
    CreatePage(e_PageID_CareerHub);
  }
}
