#include "ownerpages.hpp"
#include "careerpages.hpp" // For PageIDs
#include "../pagefactory.hpp"
#include "../../data/careerdata.hpp"
#include "../../utils/gui2/widgets/frame.hpp" // For card layout

// ---------------------------------------------------------------------------
// OwnerHubPage - Modernized Executive Dashboard
// ---------------------------------------------------------------------------

OwnerHubPage::OwnerHubPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();

  // Premium Header
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_ownerhub", 5, 2, 90, 4, "[ EXECUTIVE DASHBOARD ]");
  this->AddView(title);
  title->Show();

  if (save) {
    // Top Bar - High-level Club Info
    std::string clubInfo = "Club: " + save->name + "  |  Season: " + std::to_string(save->seasonsPlayed + 1) + 
                           "  |  Reputation: " + CareerDatabase::GetInstance().GetReputationStatus();
    Gui2Caption* teamLabel =
        new Gui2Caption(windowManager, "caption_oh_team", 5, 6, 90, 2, clubInfo);
    this->AddView(teamLabel);
    teamLabel->Show();

    // -----------------------------------------------------------------------
    // Main Content Area - Widgets (Cards)
    // -----------------------------------------------------------------------
    int contentX = 35;
    int contentW = 60;

    // Financial Health Widget
    Gui2Frame* finFrame = new Gui2Frame(windowManager, "frame_oh_fin", contentX, 15, contentW, 11, true);
    
    Gui2Caption* finTitle = new Gui2Caption(windowManager, "cap_oh_fintitle", 2, 1, contentW - 4, 2, "Financial Snapshot");
    finFrame->AddView(finTitle);
    finTitle->Show();

    std::string finStr = "Net Worth: \xe2\x82\xac" + std::to_string(save->finances.netWorth) +
                         "\nBudget: \xe2\x82\xac" + std::to_string(save->transferBudget) +
                         "\nStatus: " + CareerDatabase::GetInstance().GetFinancialHealthString();
    Gui2Caption* finBody = new Gui2Caption(windowManager, "cap_oh_finbody", 2, 4, contentW - 4, 6, finStr);
    finFrame->AddView(finBody);
    finBody->Show();
    
    this->AddView(finFrame);
    finFrame->Show();

    // Board & Fan Confidence Widget
    Gui2Frame* boardFrame = new Gui2Frame(windowManager, "frame_oh_brd", contentX, 28, contentW, 11, true);
    
    Gui2Caption* boardTitle = new Gui2Caption(windowManager, "cap_oh_brdtitle", 2, 1, contentW - 4, 2, "Club Status");
    boardFrame->AddView(boardTitle);
    boardTitle->Show();

    std::string boardStr = "Board Confidence: " + std::to_string(save->boardConfidence) + "%" +
                           "\nFan Base: " + std::to_string(save->fanBase) + "k" +
                           "\nStadium Satisfaction: " + std::to_string(save->stadium.fanSatisfaction) + "%";
    Gui2Caption* boardBody = new Gui2Caption(windowManager, "cap_oh_brdbody", 2, 4, contentW - 4, 6, boardStr);
    boardFrame->AddView(boardBody);
    boardBody->Show();
    
    this->AddView(boardFrame);
    boardFrame->Show();

    // Infrastructure & Staff Widget
    Gui2Frame* infFrame = new Gui2Frame(windowManager, "frame_oh_inf", contentX, 41, contentW, 11, true);
    
    Gui2Caption* infTitle = new Gui2Caption(windowManager, "cap_oh_inftitle", 2, 1, contentW - 4, 2, "Infrastructure");
    infFrame->AddView(infTitle);
    infTitle->Show();

    std::string infStr = "Stadium: " + save->stadium.name + " (" + std::to_string(save->stadium.capacity) + " seats)" +
                         "\nSponsors: " + std::to_string(save->activeSponsors.size()) + " Active" + 
                         "\nStaff: " + std::to_string(save->staff.size()) + " Employed";
    Gui2Caption* infBody = new Gui2Caption(windowManager, "cap_oh_infbody", 2, 4, contentW - 4, 6, infStr);
    infFrame->AddView(infBody);
    infBody->Show();

    this->AddView(infFrame);
    infFrame->Show();
  }

  // -----------------------------------------------------------------------
  // Side Navigation Menu Card
  // -----------------------------------------------------------------------
  Gui2Frame* navFrame = new Gui2Frame(windowManager, "frame_oh_nav", 5, 15, 27, 45, true);

  Gui2Caption* navTitle = new Gui2Caption(windowManager, "cap_oh_navtitle", 1, 1, 25, 2, "Management Areas");
  navFrame->AddView(navTitle);
  navTitle->Show();

  Gui2Grid* navGrid = new Gui2Grid(windowManager, "oh_nav_grid", 1, 3, 25, 41);

  Gui2Button* btnStadium = new Gui2Button(windowManager, "btn_oh_stadium", 0, 0, 25, 3, "Stadium");
  Gui2Button* btnFinances = new Gui2Button(windowManager, "btn_oh_finances", 0, 0, 25, 3, "Finances");
  Gui2Button* btnSponsors = new Gui2Button(windowManager, "btn_oh_sponsors", 0, 0, 25, 3, "Sponsors");
  Gui2Button* btnStaff = new Gui2Button(windowManager, "btn_oh_staff", 0, 0, 25, 3, "Staff");
  Gui2Button* btnBoard = new Gui2Button(windowManager, "btn_oh_board", 0, 0, 25, 3, "Board Room");

  Gui2Button* btnTransfers = new Gui2Button(windowManager, "btn_oh_transfers", 0, 0, 25, 3, "Transfers");
  Gui2Button* btnSquad = new Gui2Button(windowManager, "btn_oh_squad", 0, 0, 25, 3, "Squad");
  Gui2Button* btnTraining = new Gui2Button(windowManager, "btn_oh_training", 0, 0, 25, 3, "Training");
  Gui2Button* btnFreeAgency = new Gui2Button(windowManager, "btn_oh_freeagency", 0, 0, 25, 3, "Free Agency");
  Gui2Button* btnYouth = new Gui2Button(windowManager, "btn_oh_youth", 0, 0, 25, 3, "Youth Academy");

  Gui2Button* btnSeason = new Gui2Button(windowManager, "btn_oh_season", 0, 0, 25, 4, "Advance Season");

  btnStadium->sig_OnClick.connect([this](...) { GoStadium(); });
  btnFinances->sig_OnClick.connect([this](...) { GoFinances(); });
  btnStaff->sig_OnClick.connect([this](...) { GoStaffManagement(); });
  btnSponsors->sig_OnClick.connect([this](...) { GoSponsors(); });
  btnBoard->sig_OnClick.connect([this](...) { GoBoardRoom(); });
  btnSeason->sig_OnClick.connect([this](...) { GoSeason(); });
  btnTransfers->sig_OnClick.connect([this](...) { GoTransferMarket(); });
  btnSquad->sig_OnClick.connect([this](...) { GoSquad(); });
  btnTraining->sig_OnClick.connect([this](...) { GoTraining(); });
  btnFreeAgency->sig_OnClick.connect([this](...) { GoFreeAgency(); });
  btnYouth->sig_OnClick.connect([this](...) { GoYouthAcademy(); });

  int row = 0;
  navGrid->AddView(btnStadium, row++, 0);
  navGrid->AddView(btnFinances, row++, 0);
  navGrid->AddView(btnSponsors, row++, 0);
  navGrid->AddView(btnStaff, row++, 0);
  navGrid->AddView(btnBoard, row++, 0);
  
  // Spacer
  row++;
  
  navGrid->AddView(btnTransfers, row++, 0);
  navGrid->AddView(btnSquad, row++, 0);
  navGrid->AddView(btnTraining, row++, 0);
  navGrid->AddView(btnFreeAgency, row++, 0);
  navGrid->AddView(btnYouth, row++, 0);

  row++; // Spacer
  navGrid->AddView(btnSeason, row++, 0);

  navGrid->UpdateLayout(0.5);
  navFrame->AddView(navGrid);
  navGrid->Show();

  this->AddView(navFrame);
  navFrame->Show();

  btnStadium->SetFocus();
  this->Show();
}

OwnerHubPage::~OwnerHubPage() {}

void OwnerHubPage::GoStadium() { CreatePage(e_PageID_OwnerStadium); }
void OwnerHubPage::GoFinances() { CreatePage(e_PageID_OwnerFinances); }
void OwnerHubPage::GoStaffManagement() { CreatePage(e_PageID_OwnerStaff); }
void OwnerHubPage::GoSponsors() { CreatePage(e_PageID_OwnerSponsors); }
void OwnerHubPage::GoBoardRoom() { CreatePage(e_PageID_OwnerBoardRoom); }
void OwnerHubPage::GoTransferMarket() { CreatePage(e_PageID_CareerTransferMarket); }
void OwnerHubPage::GoSquad() { CreatePage(e_PageID_CareerSquadRoster); }
void OwnerHubPage::GoTraining() { CreatePage(e_PageID_CareerTraining); }
void OwnerHubPage::GoFreeAgency() { CreatePage(e_PageID_CareerFreeAgency); }
void OwnerHubPage::GoYouthAcademy() { CreatePage(e_PageID_CareerYouthAcademy); }
void OwnerHubPage::GoSeason() { CreatePage(e_PageID_CareerSeason); }

// ---------------------------------------------------------------------------
// OwnerStadiumPage
// ---------------------------------------------------------------------------

OwnerStadiumPage::OwnerStadiumPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_stadium", 10, 3, 80, 3, "Stadium Management");
  this->AddView(title);
  title->Show();

  if (save) {
    auto& stad = save->stadium;
    
    Gui2Frame* infoFrame = new Gui2Frame(windowManager, "frame_stad_info", 5, 7, 90, 8, true);

    std::string info1 = stad.name + " | Capacity: " + std::to_string(stad.capacity) +
                        " | Condition: " + std::to_string(stad.condition) + "%" +
                        " | Fan Satisfaction: " + std::to_string(stad.fanSatisfaction) + "%";
    Gui2Caption* stadInfo = new Gui2Caption(windowManager, "caption_stad_info", 2, 2, 86, 2, info1);
    infoFrame->AddView(stadInfo);
    stadInfo->Show();

    std::string info2 = "Maintenance Cost: \xe2\x82\xac" + std::to_string(stad.maintenanceCost) + "/season" +
                        " | Match Revenue: \xe2\x82\xac" + std::to_string(stad.matchDayRevenue) + "/match";
    Gui2Caption* costInfo = new Gui2Caption(windowManager, "caption_stad_cost", 2, 5, 86, 2, info2);
    infoFrame->AddView(costInfo);
    costInfo->Show();
    
    this->AddView(infoFrame);
    infoFrame->Show();

    int nextY = 16;
    if (!stad.upgrades.empty()) {
      int activeHeight = 4 + stad.upgrades.size() * 3;
      Gui2Frame* activeFrame = new Gui2Frame(windowManager, "frame_stad_active", 5, nextY, 90, activeHeight, true);
      
      Gui2Caption* activeTitle = new Gui2Caption(windowManager, "caption_stad_active", 2, 1, 86, 2, "Active Upgrades:");
      activeFrame->AddView(activeTitle);
      activeTitle->Show();

      int yOff = 4;
      for (const auto& u : stad.upgrades) {
        std::string status = u.isComplete() ? "[COMPLETE]" : "[" + std::to_string(u.seasonsRemaining) + " seasons left]";
        Gui2Caption* entry = new Gui2Caption(windowManager, "caption_upg_" + u.name, 2, yOff, 86, 2,
          "  " + u.name + " " + status + " +" + std::to_string(u.capacityIncrease) + " seats, +\xe2\x82\xac" + std::to_string(u.revenueBonus) + "/season");
        activeFrame->AddView(entry);
        entry->Show();
        yOff += 3;
      }
      this->AddView(activeFrame);
      activeFrame->Show();
      nextY += activeHeight + 1;
    }

    Gui2Frame* availFrame = new Gui2Frame(windowManager, "frame_stad_avail", 5, nextY, 90, 88 - nextY, true);

    Gui2Caption* availTitle = new Gui2Caption(windowManager, "caption_stad_avail", 2, 1, 86, 2, "Available Upgrades & Actions:");
    availFrame->AddView(availTitle);
    availTitle->Show();

    Gui2Grid* ugGrid = new Gui2Grid(windowManager, "stad_ug_grid", 2, 4, 86, 80 - nextY);
    int row = 0;
    for (int i = 0; i < static_cast<int>(stad.availableUpgrades.size()); i++) {
      const auto& u = stad.availableUpgrades[i];
      std::string label = u.name + " | \xe2\x82\xac" + std::to_string(u.cost) +
                          " | +" + std::to_string(u.capacityIncrease) + " seats" +
                          " | " + std::to_string(u.buildTimeSeasons) + " season(s)";
      Gui2Button* btn = new Gui2Button(windowManager, "btn_upg_" + std::to_string(i), 0, 0, 86, 2.5, label);
      btn->sig_OnClick.connect([this, i](...) { UpgradeStadium(i); });
      ugGrid->AddView(btn, row++, 0);
    }

    long long repairCost = 50000 * std::max(1, (100 - stad.condition) / 10);
    Gui2Button* btnRepair = new Gui2Button(windowManager, "btn_stad_repair", 0, 0, 86, 2.5,
      "Repair Stadium (+10 condition, \xe2\x82\xac" + std::to_string(repairCost) + ")");
    btnRepair->sig_OnClick.connect([this](...) { RepairStadium(); });
    ugGrid->AddView(btnRepair, row++, 0);

    ugGrid->UpdateLayout(0.5);
    availFrame->AddView(ugGrid);
    ugGrid->Show();
    
    this->AddView(availFrame);
    availFrame->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_stad_back", 30, 90, 40, 3, "Back to Owner Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_OwnerHub); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

OwnerStadiumPage::~OwnerStadiumPage() {}

void OwnerStadiumPage::UpgradeStadium(int index) {
  CareerDatabase::GetInstance().UpgradeStadium(index);
  CreatePage(e_PageID_OwnerStadium);
}

void OwnerStadiumPage::RepairStadium() {
  CareerDatabase::GetInstance().RepairStadium(10);
  CreatePage(e_PageID_OwnerStadium);
}

void OwnerStadiumPage::RenameStadium() {
  CreatePage(e_PageID_OwnerStadium);
}

// ---------------------------------------------------------------------------
// OwnerFinancesPage
// ---------------------------------------------------------------------------

OwnerFinancesPage::OwnerFinancesPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_finances", 10, 3, 80, 3, "Club Finances");
  this->AddView(title);
  title->Show();

  if (save) {
    auto& fin = save->finances;

    // Overview Card
    Gui2Frame* overviewFrame = new Gui2Frame(windowManager, "frame_fin_overview", 5, 7, 90, 8, true);
    
    std::string healthStr = "Financial Health: " + CareerDatabase::GetInstance().GetFinancialHealthString() +
                            " | Debt Level: " + std::to_string(fin.debtLevel) + "%";
    Gui2Caption* healthCap = new Gui2Caption(windowManager, "fin_health", 2, 2, 86, 2, healthStr);
    overviewFrame->AddView(healthCap);
    healthCap->Show();
    
    std::string netWorthStr = "Net Worth: \xe2\x82\xac" + std::to_string(fin.netWorth);
    Gui2Caption* nwCap = new Gui2Caption(windowManager, "fin_nw", 2, 4, 86, 2, netWorthStr);
    overviewFrame->AddView(nwCap);
    nwCap->Show();
    
    this->AddView(overviewFrame);
    overviewFrame->Show();

    // Revenue Card
    Gui2Frame* revFrame = new Gui2Frame(windowManager, "frame_fin_rev", 5, 17, 43, 16, true);
    
    Gui2Caption* revTitle = new Gui2Caption(windowManager, "fin_revtitle", 2, 1, 39, 2, "REVENUE:");
    revFrame->AddView(revTitle);
    revTitle->Show();
    
    std::string revBody = "Match Day: \xe2\x82\xac" + std::to_string(fin.matchDayIncome) +
                          "\nSponsors: \xe2\x82\xac" + std::to_string(fin.sponsorIncome) +
                          "\nMerchandise: \xe2\x82\xac" + std::to_string(fin.merchandiseIncome) +
                          "\nTV Revenue: \xe2\x82\xac" + std::to_string(fin.tvRevenue) +
                          "\nTransfers: \xe2\x82\xac" + std::to_string(fin.transferIncome) +
                          "\n\nTOTAL: \xe2\x82\xac" + std::to_string(fin.totalRevenue);
    Gui2Caption* revText = new Gui2Caption(windowManager, "fin_revbody", 2, 4, 39, 10, revBody);
    revFrame->AddView(revText);
    revText->Show();
    
    this->AddView(revFrame);
    revFrame->Show();

    // Expenses Card
    Gui2Frame* expFrame = new Gui2Frame(windowManager, "frame_fin_exp", 52, 17, 43, 16, true);
    
    Gui2Caption* expTitle = new Gui2Caption(windowManager, "fin_exptitle", 2, 1, 39, 2, "EXPENSES:");
    expFrame->AddView(expTitle);
    expTitle->Show();
    
    std::string expBody = "Player Wages: \xe2\x82\xac" + std::to_string(fin.playerWages) +
                          "\nStaff Wages: \xe2\x82\xac" + std::to_string(fin.staffWages) +
                          "\nStadium Costs: \xe2\x82\xac" + std::to_string(fin.stadiumCosts) +
                          "\nTransfers: \xe2\x82\xac" + std::to_string(fin.transferSpending) +
                          "\n\nTOTAL: \xe2\x82\xac" + std::to_string(fin.totalExpenses);
    Gui2Caption* expText = new Gui2Caption(windowManager, "fin_expbody", 2, 4, 39, 10, expBody);
    expFrame->AddView(expText);
    expText->Show();
    
    this->AddView(expFrame);
    expFrame->Show();

    // Profit & Details Card
    Gui2Frame* profitFrame = new Gui2Frame(windowManager, "frame_fin_profit", 5, 35, 90, 8, true);
    
    std::string profitStr = "NET PROFIT: \xe2\x82\xac" + std::to_string(CareerDatabase::GetInstance().GetSeasonProfit());
    Gui2Caption* profitCap = new Gui2Caption(windowManager, "fin_profit", 2, 2, 86, 2, profitStr);
    profitFrame->AddView(profitCap);
    profitCap->Show();
    
    std::string tktStr = "Ticket Price: \xe2\x82\xac" + std::to_string(fin.ticketPrice) +
                         " | Season Ticket Holders: " + std::to_string(fin.seasonTicketHolders);
    Gui2Caption* tktCap = new Gui2Caption(windowManager, "fin_tp", 2, 4, 86, 2, tktStr);
    profitFrame->AddView(tktCap);
    tktCap->Show();
    
    this->AddView(profitFrame);
    profitFrame->Show();

    // Actions Card
    Gui2Frame* actFrame = new Gui2Frame(windowManager, "frame_fin_act", 5, 45, 90, 12, true);
    Gui2Grid* actGrid = new Gui2Grid(windowManager, "fin_act_grid", 2, 2, 86, 8);
    int row = 0;

    Gui2Button* btnTicketUp = new Gui2Button(windowManager, "btn_ticket_up", 0, 0, 42, 2.5,
      "Increase Ticket (\xe2\x82\xac10)");
    btnTicketUp->sig_OnClick.connect([this, &fin](...) { SetTicketPrice(fin.ticketPrice + 10); });
    actGrid->AddView(btnTicketUp, row, 0);

    Gui2Button* btnTicketDown = new Gui2Button(windowManager, "btn_ticket_down", 0, 0, 42, 2.5,
      "Decrease Ticket (\xe2\x82\xac10)");
    btnTicketDown->sig_OnClick.connect([this, &fin](...) { SetTicketPrice(fin.ticketPrice - 10); });
    actGrid->AddView(btnTicketDown, row++, 1);

    Gui2Button* btnFanInvest = new Gui2Button(windowManager, "btn_fan_invest", 0, 0, 42, 2.5,
      "Invest Fan Base (\xe2\x82\xac2M)");
    btnFanInvest->sig_OnClick.connect([this](...) { InvestFanBase(); });
    actGrid->AddView(btnFanInvest, row, 0);

    Gui2Button* btnPrestige = new Gui2Button(windowManager, "btn_prestige_invest", 0, 0, 42, 2.5,
      "Invest Prestige (\xe2\x82\xac3M)");
    btnPrestige->sig_OnClick.connect([this](...) { InvestPrestige(); });
    actGrid->AddView(btnPrestige, row++, 1);

    actGrid->UpdateLayout(0.5);
    actFrame->AddView(actGrid);
    actGrid->Show();

    this->AddView(actFrame);
    actFrame->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_fin_back", 30, 92, 40, 3, "Back to Owner Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_OwnerHub); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

OwnerFinancesPage::~OwnerFinancesPage() {}

void OwnerFinancesPage::SetTicketPrice(int price) {
  CareerDatabase::GetInstance().SetTicketPrice(price);
  CreatePage(e_PageID_OwnerFinances);
}

void OwnerFinancesPage::InvestFanBase() {
  CareerDatabase::GetInstance().InvestInFanBase(2000000);
  CreatePage(e_PageID_OwnerFinances);
}

void OwnerFinancesPage::InvestPrestige() {
  CareerDatabase::GetInstance().InvestInPrestige(3000000);
  CreatePage(e_PageID_OwnerFinances);
}

// ---------------------------------------------------------------------------
// OwnerStaffPage
// ---------------------------------------------------------------------------

OwnerStaffPage::OwnerStaffPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_staff", 10, 3, 80, 3, "Staff Management");
  this->AddView(title);
  title->Show();

  if (save) {
    Gui2Frame* staffFrame = new Gui2Frame(windowManager, "frame_staff", 5, 7, 90, 65, true);
    
    Gui2Caption* header = new Gui2Caption(windowManager, "caption_staff_hdr", 2, 2, 86, 2,
      "Name                   | Role              | Skill | Salary       | Contract | Morale");
    staffFrame->AddView(header);
    header->Show();

    Gui2Grid* grid = new Gui2Grid(windowManager, "staff_grid", 2, 5, 86, 56);
    int row = 0;
    for (const auto& s : save->staff) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%-22s | %-17s | %3d   | %10lld | %d yr    | %d%%",
        s.name.c_str(), s.role.c_str(), s.skill, s.salary, s.contractYearsRemaining, s.morale);
      Gui2Button* btn = new Gui2Button(windowManager, "btn_staff_" + std::to_string(row), 0, 0, 86, 2.5,
        std::string("[Fire] ") + buf);
      std::string staffName = s.name;
      btn->sig_OnClick.connect([this, staffName](...) { FireStaff(staffName); });
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    staffFrame->AddView(grid);
    grid->Show();

    this->AddView(staffFrame);
    staffFrame->Show();
  }

  Gui2Button* btnHire = new Gui2Button(windowManager, "btn_staff_hire", 10, 75, 40, 3, "Browse Staff Candidates");
  btnHire->sig_OnClick.connect([this](...) { GoHirePage(); });
  this->AddView(btnHire);
  btnHire->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_staff_back", 30, 90, 40, 3, "Back to Owner Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_OwnerHub); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

OwnerStaffPage::~OwnerStaffPage() {}

void OwnerStaffPage::FireStaff(const std::string& name) {
  CareerDatabase::GetInstance().FireStaff(name);
  CreatePage(e_PageID_OwnerStaff);
}

void OwnerStaffPage::GoHirePage() {
  CreatePage(e_PageID_OwnerStaffHire);
}

// ---------------------------------------------------------------------------
// OwnerStaffHirePage
// ---------------------------------------------------------------------------

OwnerStaffHirePage::OwnerStaffHirePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  CareerDatabase::GetInstance().GenerateStaffCandidates(m_candidates);

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_hiretitle", 10, 3, 80, 3, "Staff Candidates");
  this->AddView(title);
  title->Show();

  Gui2Frame* hireFrame = new Gui2Frame(windowManager, "frame_hire", 5, 7, 90, 80, true);
  
  Gui2Caption* header = new Gui2Caption(windowManager, "caption_hire_hdr", 2, 2, 86, 2,
    "Name                   | Role              | Skill | Salary       | Contract");
  hireFrame->AddView(header);
  header->Show();

  Gui2Grid* grid = new Gui2Grid(windowManager, "hire_grid", 2, 5, 86, 73);
  int row = 0;
  for (int i = 0; i < static_cast<int>(m_candidates.size()); i++) {
    const auto& c = m_candidates[i];
    char buf[256];
    snprintf(buf, sizeof(buf), "%-22s | %-17s | %3d   | %10lld | %d yr",
      c.name.c_str(), c.role.c_str(), c.skill, c.salary, c.contractYearsRemaining);
    Gui2Button* btn = new Gui2Button(windowManager, "btn_hire_" + std::to_string(i), 0, 0, 86, 2.5,
      std::string("[Hire] ") + buf);
    btn->sig_OnClick.connect([this, i](...) { HireCandidate(i); });
    grid->AddView(btn, row++, 0);
  }
  grid->UpdateLayout(0.5);
  hireFrame->AddView(grid);
  grid->Show();

  this->AddView(hireFrame);
  hireFrame->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_hire_back", 30, 90, 40, 3, "Back to Staff");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_OwnerStaff); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

OwnerStaffHirePage::~OwnerStaffHirePage() {}

void OwnerStaffHirePage::HireCandidate(int index) {
  if (index >= 0 && index < static_cast<int>(m_candidates.size())) {
    CareerDatabase::GetInstance().HireStaff(m_candidates[index]);
  }
  CreatePage(e_PageID_OwnerStaff);
}

// ---------------------------------------------------------------------------
// OwnerSponsorsPage
// ---------------------------------------------------------------------------

OwnerSponsorsPage::OwnerSponsorsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_sponsors", 10, 3, 80, 3, "Sponsorship Deals");
  this->AddView(title);
  title->Show();

  if (save) {
    int nextY = 7;

    if (!save->activeSponsors.empty()) {
      int sHeight = 4 + save->activeSponsors.size() * 3;
      Gui2Frame* activeFrame = new Gui2Frame(windowManager, "frame_sp_active", 5, nextY, 90, sHeight, true);

      Gui2Caption* activeTitle = new Gui2Caption(windowManager, "caption_sp_active", 2, 1, 86, 2,
        "Active Sponsors:");
      activeFrame->AddView(activeTitle);
      activeTitle->Show();

      Gui2Grid* activeGrid = new Gui2Grid(windowManager, "sp_active_grid", 2, 4, 86, sHeight - 4);
      int row = 0;
      for (const auto& sp : save->activeSponsors) {
        std::string label = sp.sponsorName + " (" + sp.type + ") | \xe2\x82\xac" +
          std::to_string(sp.annualRevenue) + "/yr | " + std::to_string(sp.yearsRemaining) + " yr left";
        Gui2Button* btn = new Gui2Button(windowManager, "btn_sp_term_" + std::to_string(row), 0, 0, 86, 2.5,
          "[Terminate] " + label);
        std::string spName = sp.sponsorName;
        btn->sig_OnClick.connect([this, spName](...) { TerminateDeal(spName); });
        activeGrid->AddView(btn, row++, 0);
      }
      activeGrid->UpdateLayout(0.5);
      activeFrame->AddView(activeGrid);
      activeGrid->Show();

      this->AddView(activeFrame);
      activeFrame->Show();

      nextY += sHeight + 1;
    }

    Gui2Frame* offersFrame = new Gui2Frame(windowManager, "frame_sp_offers", 5, nextY, 90, 88 - nextY, true);

    Gui2Caption* offersTitle = new Gui2Caption(windowManager, "caption_sp_offers", 2, 1, 86, 2,
      "Available Offers:");
    offersFrame->AddView(offersTitle);
    offersTitle->Show();

    if (save->availableSponsorOffers.empty()) {
      Gui2Caption* noOffers = new Gui2Caption(windowManager, "caption_sp_none", 2, 4, 86, 2,
        "No offers currently available. New offers appear each season.");
      offersFrame->AddView(noOffers);
      noOffers->Show();
    } else {
      Gui2Grid* offersGrid = new Gui2Grid(windowManager, "sp_offers_grid", 2, 4, 86, 88 - nextY - 4);
      int row = 0;
      for (int i = 0; i < static_cast<int>(save->availableSponsorOffers.size()); i++) {
        const auto& sp = save->availableSponsorOffers[i];
        std::string label = sp.sponsorName + " (" + sp.type + ") | \xe2\x82\xac" +
          std::to_string(sp.annualRevenue) + "/yr | " + std::to_string(sp.yearsRemaining) + " yr" +
          " | Req rep: " + std::to_string(sp.reputationRequirement);
        Gui2Button* btn = new Gui2Button(windowManager, "btn_sp_acc_" + std::to_string(i), 0, 0, 86, 2.5,
          "[Accept] " + label);
        btn->sig_OnClick.connect([this, i](...) { AcceptDeal(i); });
        offersGrid->AddView(btn, row++, 0);
      }
      offersGrid->UpdateLayout(0.5);
      offersFrame->AddView(offersGrid);
      offersGrid->Show();
    }
    
    this->AddView(offersFrame);
    offersFrame->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_sp_back", 30, 90, 40, 3, "Back to Owner Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_OwnerHub); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

OwnerSponsorsPage::~OwnerSponsorsPage() {}

void OwnerSponsorsPage::AcceptDeal(int index) {
  CareerDatabase::GetInstance().AcceptSponsorDeal(index);
  CreatePage(e_PageID_OwnerSponsors);
}

void OwnerSponsorsPage::TerminateDeal(const std::string& sponsorName) {
  CareerDatabase::GetInstance().TerminateSponsorDeal(sponsorName);
  CreatePage(e_PageID_OwnerSponsors);
}

// ---------------------------------------------------------------------------
// OwnerBoardRoomPage
// ---------------------------------------------------------------------------

OwnerBoardRoomPage::OwnerBoardRoomPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  CareerSave* save = CareerDatabase::GetInstance().GetActiveSave();

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_boardroom", 10, 3, 80, 3, "Board Room");
  this->AddView(title);
  title->Show();

  if (save) {
    Gui2Frame* brFrame = new Gui2Frame(windowManager, "frame_br_overview", 5, 7, 90, 8, true);
    
    std::string confStr = "Board Confidence: " + std::to_string(save->boardConfidence) + "%" +
                          " | Reputation: " + CareerDatabase::GetInstance().GetReputationStatus() +
                          " (" + std::to_string(save->reputation) + ")";
    Gui2Caption* confLabel = new Gui2Caption(windowManager, "caption_br_conf", 2, 2, 86, 4, confStr);
    brFrame->AddView(confLabel);
    confLabel->Show();
    
    this->AddView(brFrame);
    brFrame->Show();

    int objHeight = 4 + std::max(1, static_cast<int>(save->boardObjectives.size())) * 2;
    Gui2Frame* objFrame = new Gui2Frame(windowManager, "frame_br_obj", 5, 17, 90, objHeight, true);
    
    Gui2Caption* objTitle = new Gui2Caption(windowManager, "caption_br_objtitle", 2, 1, 86, 2,
      "Board Objectives:");
    objFrame->AddView(objTitle);
    objTitle->Show();

    int subY = 4;
    for (int i = 0; i < static_cast<int>(save->boardObjectives.size()); i++) {
      const auto& obj = save->boardObjectives[i];
      std::string status = obj.completed ? "[COMPLETED]" : "[IN PROGRESS]";
      std::string line = "  " + status + " " + obj.description +
        " (Reward: +" + std::to_string(obj.reputationReward) + " rep, Penalty: " +
        std::to_string(obj.confidencePenalty) + " confidence)";
      Gui2Caption* objLine = new Gui2Caption(windowManager, "caption_br_obj_" + std::to_string(i),
        2, subY, 86, 2, line);
      objFrame->AddView(objLine);
      objLine->Show();
      subY += 2;
    }
    this->AddView(objFrame);
    objFrame->Show();

    int yPos = 17 + objHeight + 2;
    Gui2Frame* evtFrame = new Gui2Frame(windowManager, "frame_br_evt", 5, yPos, 90, 88 - yPos, true);
    
    Gui2Caption* evtTitle = new Gui2Caption(windowManager, "caption_br_evttitle", 2, 1, 86, 2,
      "Recent Events:");
    evtFrame->AddView(evtTitle);
    evtTitle->Show();

    subY = 4;
    auto events = CareerDatabase::GetInstance().GetRecentEvents(8);
    for (int i = 0; i < static_cast<int>(events.size()); i++) {
      const auto& evt = events[i];
      std::string prefix = evt.isMajor ? "*** " : "  ";
      std::string repStr = evt.reputationImpact > 0 ? " (+" + std::to_string(evt.reputationImpact) + ")" :
                           evt.reputationImpact < 0 ? " (" + std::to_string(evt.reputationImpact) + ")" : "";
      Gui2Caption* evtLine = new Gui2Caption(windowManager, "caption_br_evt_" + std::to_string(i),
        2, subY, 86, 2, prefix + "[" + evt.type + "] " + evt.description + repStr);
      evtFrame->AddView(evtLine);
      evtLine->Show();
      subY += 2;
    }
    
    this->AddView(evtFrame);
    evtFrame->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_br_back", 30, 90, 40, 3, "Back to Owner Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_OwnerHub); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

OwnerBoardRoomPage::~OwnerBoardRoomPage() {}
