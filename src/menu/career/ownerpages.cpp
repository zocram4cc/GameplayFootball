#include "ownerpages.hpp"
#include "careerpages.hpp" // For PageIDs
#include "../pagefactory.hpp"
#include "../../data/careerdata.hpp"

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
    // Main Content Area - Widgets
    // -----------------------------------------------------------------------
    int contentX = 35;
    int contentW = 60;

    // Financial Health Widget
    Gui2Caption* finTitle = new Gui2Caption(windowManager, "cap_oh_fintitle", contentX, 16, contentW, 2, "--- Financial Snapshot ---");
    this->AddView(finTitle);
    finTitle->Show();

    std::string finStr = "Net Worth: \xe2\x82\xac" + std::to_string(save->finances.netWorth) +
                         "\nBudget: \xe2\x82\xac" + std::to_string(save->transferBudget) +
                         "\nStatus: " + CareerDatabase::GetInstance().GetFinancialHealthString();
    Gui2Caption* finBody = new Gui2Caption(windowManager, "cap_oh_finbody", contentX, 19, contentW, 6, finStr);
    this->AddView(finBody);
    finBody->Show();

    // Board & Fan Confidence Widget
    Gui2Caption* boardTitle = new Gui2Caption(windowManager, "cap_oh_brdtitle", contentX, 27, contentW, 2, "--- Club Status ---");
    this->AddView(boardTitle);
    boardTitle->Show();

    std::string boardStr = "Board Confidence: " + std::to_string(save->boardConfidence) + "%" +
                           "\nFan Base: " + std::to_string(save->fanBase) + "k" +
                           "\nStadium Satisfaction: " + std::to_string(save->stadium.fanSatisfaction) + "%";
    Gui2Caption* boardBody = new Gui2Caption(windowManager, "cap_oh_brdbody", contentX, 30, contentW, 6, boardStr);
    this->AddView(boardBody);
    boardBody->Show();

    // Infrastructure & Staff Widget
    Gui2Caption* infTitle = new Gui2Caption(windowManager, "cap_oh_inftitle", contentX, 38, contentW, 2, "--- Infrastructure ---");
    this->AddView(infTitle);
    infTitle->Show();

    std::string infStr = "Stadium: " + save->stadium.name + " (" + std::to_string(save->stadium.capacity) + " seats)" +
                         "\nSponsors: " + std::to_string(save->activeSponsors.size()) + " Active" + 
                         "\nStaff: " + std::to_string(save->staff.size()) + " Employed";
    Gui2Caption* infBody = new Gui2Caption(windowManager, "cap_oh_infbody", contentX, 41, contentW, 6, infStr);
    this->AddView(infBody);
    infBody->Show();
  }

  // -----------------------------------------------------------------------
  // Side Navigation Menu
  // -----------------------------------------------------------------------
  Gui2Grid* navGrid = new Gui2Grid(windowManager, "oh_nav_grid", 5, 16, 25, 70);

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
  this->AddView(navGrid);
  navGrid->Show();

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
    std::string info1 = stad.name + " | Capacity: " + std::to_string(stad.capacity) +
                        " | Condition: " + std::to_string(stad.condition) + "%" +
                        " | Fan Satisfaction: " + std::to_string(stad.fanSatisfaction) + "%";
    Gui2Caption* stadInfo = new Gui2Caption(windowManager, "caption_stad_info", 5, 7, 90, 2, info1);
    this->AddView(stadInfo);
    stadInfo->Show();

    std::string info2 = "Maintenance Cost: \xe2\x82\xac" + std::to_string(stad.maintenanceCost) + "/season" +
                        " | Match Revenue: \xe2\x82\xac" + std::to_string(stad.matchDayRevenue) + "/match";
    Gui2Caption* costInfo = new Gui2Caption(windowManager, "caption_stad_cost", 5, 9, 90, 2, info2);
    this->AddView(costInfo);
    costInfo->Show();

    if (!stad.upgrades.empty()) {
      Gui2Caption* activeTitle = new Gui2Caption(windowManager, "caption_stad_active", 5, 12, 90, 2, "Active Upgrades:");
      this->AddView(activeTitle);
      activeTitle->Show();

      int yOff = 14;
      for (const auto& u : stad.upgrades) {
        std::string status = u.isComplete() ? "[COMPLETE]" : "[" + std::to_string(u.seasonsRemaining) + " seasons left]";
        Gui2Caption* entry = new Gui2Caption(windowManager, "caption_upg_" + u.name, 5, yOff, 90, 2,
          "  " + u.name + " " + status + " +" + std::to_string(u.capacityIncrease) + " seats, +\xe2\x82\xac" + std::to_string(u.revenueBonus) + "/season");
        this->AddView(entry);
        entry->Show();
        yOff += 2;
      }
    }

    Gui2Caption* availTitle = new Gui2Caption(windowManager, "caption_stad_avail", 5, 28, 90, 2, "Available Upgrades:");
    this->AddView(availTitle);
    availTitle->Show();

    Gui2Grid* ugGrid = new Gui2Grid(windowManager, "stad_ug_grid", 5, 30, 90, 30);
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
    this->AddView(ugGrid);
    ugGrid->Show();
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

    int yPos = 7;
    auto addLine = [&](const std::string& id, const std::string& text) {
      Gui2Caption* c = new Gui2Caption(windowManager, id, 5, yPos, 90, 2, text);
      this->AddView(c);
      c->Show();
      yPos += 2;
    };

    addLine("fin_health", "Financial Health: " + CareerDatabase::GetInstance().GetFinancialHealthString() +
                          " | Debt Level: " + std::to_string(fin.debtLevel) + "%");
    addLine("fin_nw", "Net Worth: \xe2\x82\xac" + std::to_string(fin.netWorth));
    addLine("fin_sep1", "--- REVENUE ---");
    addLine("fin_md", "  Match Day Income: \xe2\x82\xac" + std::to_string(fin.matchDayIncome));
    addLine("fin_sp", "  Sponsor Income: \xe2\x82\xac" + std::to_string(fin.sponsorIncome));
    addLine("fin_mc", "  Merchandise: \xe2\x82\xac" + std::to_string(fin.merchandiseIncome));
    addLine("fin_tv", "  TV Revenue: \xe2\x82\xac" + std::to_string(fin.tvRevenue));
    addLine("fin_ti", "  Transfer Income: \xe2\x82\xac" + std::to_string(fin.transferIncome));
    addLine("fin_tr", "  TOTAL REVENUE: \xe2\x82\xac" + std::to_string(fin.totalRevenue));
    addLine("fin_sep2", "--- EXPENSES ---");
    addLine("fin_pw", "  Player Wages: \xe2\x82\xac" + std::to_string(fin.playerWages));
    addLine("fin_sw", "  Staff Wages: \xe2\x82\xac" + std::to_string(fin.staffWages));
    addLine("fin_sc", "  Stadium Costs: \xe2\x82\xac" + std::to_string(fin.stadiumCosts));
    addLine("fin_ts", "  Transfer Spending: \xe2\x82\xac" + std::to_string(fin.transferSpending));
    addLine("fin_te", "  TOTAL EXPENSES: \xe2\x82\xac" + std::to_string(fin.totalExpenses));
    addLine("fin_sep3", "---");
    addLine("fin_profit", "  NET PROFIT: \xe2\x82\xac" + std::to_string(CareerDatabase::GetInstance().GetSeasonProfit()));
    addLine("fin_tp", "Ticket Price: \xe2\x82\xac" + std::to_string(fin.ticketPrice) +
                      " | Season Ticket Holders: " + std::to_string(fin.seasonTicketHolders));

    Gui2Grid* actGrid = new Gui2Grid(windowManager, "fin_act_grid", 5, yPos + 1, 90, 20);
    int row = 0;

    Gui2Button* btnTicketUp = new Gui2Button(windowManager, "btn_ticket_up", 0, 0, 42, 2.5,
      "Increase Ticket Price (+\xe2\x82\xac10)");
    btnTicketUp->sig_OnClick.connect([this, &fin](...) { SetTicketPrice(fin.ticketPrice + 10); });
    actGrid->AddView(btnTicketUp, row, 0);

    Gui2Button* btnTicketDown = new Gui2Button(windowManager, "btn_ticket_down", 0, 0, 42, 2.5,
      "Decrease Ticket Price (-\xe2\x82\xac10)");
    btnTicketDown->sig_OnClick.connect([this, &fin](...) { SetTicketPrice(fin.ticketPrice - 10); });
    actGrid->AddView(btnTicketDown, row++, 1);

    Gui2Button* btnFanInvest = new Gui2Button(windowManager, "btn_fan_invest", 0, 0, 42, 2.5,
      "Invest in Fan Base (\xe2\x82\xac2M)");
    btnFanInvest->sig_OnClick.connect([this](...) { InvestFanBase(); });
    actGrid->AddView(btnFanInvest, row, 0);

    Gui2Button* btnPrestige = new Gui2Button(windowManager, "btn_prestige_invest", 0, 0, 42, 2.5,
      "Invest in Prestige (\xe2\x82\xac3M)");
    btnPrestige->sig_OnClick.connect([this](...) { InvestPrestige(); });
    actGrid->AddView(btnPrestige, row++, 1);

    actGrid->UpdateLayout(0.5);
    this->AddView(actGrid);
    actGrid->Show();
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
    Gui2Caption* header = new Gui2Caption(windowManager, "caption_staff_hdr", 5, 7, 90, 2,
      "Name                   | Role              | Skill | Salary       | Contract | Morale");
    this->AddView(header);
    header->Show();

    Gui2Grid* grid = new Gui2Grid(windowManager, "staff_grid", 5, 10, 90, 50);
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
    this->AddView(grid);
    grid->Show();
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

  Gui2Caption* header = new Gui2Caption(windowManager, "caption_hire_hdr", 5, 7, 90, 2,
    "Name                   | Role              | Skill | Salary       | Contract");
  this->AddView(header);
  header->Show();

  Gui2Grid* grid = new Gui2Grid(windowManager, "hire_grid", 5, 10, 90, 60);
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
  this->AddView(grid);
  grid->Show();

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
    int yPos = 7;

    if (!save->activeSponsors.empty()) {
      Gui2Caption* activeTitle = new Gui2Caption(windowManager, "caption_sp_active", 5, yPos, 90, 2,
        "Active Sponsors:");
      this->AddView(activeTitle);
      activeTitle->Show();
      yPos += 2;

      Gui2Grid* activeGrid = new Gui2Grid(windowManager, "sp_active_grid", 5, yPos, 90, 20);
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
      this->AddView(activeGrid);
      activeGrid->Show();
      yPos += row * 3 + 2;
    }

    Gui2Caption* offersTitle = new Gui2Caption(windowManager, "caption_sp_offers", 5, yPos, 90, 2,
      "Available Offers:");
    this->AddView(offersTitle);
    offersTitle->Show();
    yPos += 2;

    if (save->availableSponsorOffers.empty()) {
      Gui2Caption* noOffers = new Gui2Caption(windowManager, "caption_sp_none", 5, yPos, 90, 2,
        "No offers currently available. New offers appear each season.");
      this->AddView(noOffers);
      noOffers->Show();
    } else {
      Gui2Grid* offersGrid = new Gui2Grid(windowManager, "sp_offers_grid", 5, yPos, 90, 30);
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
      this->AddView(offersGrid);
      offersGrid->Show();
    }
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
    int yPos = 7;

    std::string confStr = "Board Confidence: " + std::to_string(save->boardConfidence) + "%" +
                          " | Reputation: " + CareerDatabase::GetInstance().GetReputationStatus() +
                          " (" + std::to_string(save->reputation) + ")";
    Gui2Caption* confLabel = new Gui2Caption(windowManager, "caption_br_conf", 5, yPos, 90, 2, confStr);
    this->AddView(confLabel);
    confLabel->Show();
    yPos += 3;

    Gui2Caption* objTitle = new Gui2Caption(windowManager, "caption_br_objtitle", 5, yPos, 90, 2,
      "Board Objectives:");
    this->AddView(objTitle);
    objTitle->Show();
    yPos += 2;

    for (int i = 0; i < static_cast<int>(save->boardObjectives.size()); i++) {
      const auto& obj = save->boardObjectives[i];
      std::string status = obj.completed ? "[COMPLETED]" : "[IN PROGRESS]";
      std::string line = "  " + status + " " + obj.description +
        " (Reward: +" + std::to_string(obj.reputationReward) + " rep, Penalty: " +
        std::to_string(obj.confidencePenalty) + " confidence)";
      Gui2Caption* objLine = new Gui2Caption(windowManager, "caption_br_obj_" + std::to_string(i),
        5, yPos, 90, 2, line);
      this->AddView(objLine);
      objLine->Show();
      yPos += 2;
    }

    yPos += 2;
    Gui2Caption* evtTitle = new Gui2Caption(windowManager, "caption_br_evttitle", 5, yPos, 90, 2,
      "Recent Events:");
    this->AddView(evtTitle);
    evtTitle->Show();
    yPos += 2;

    auto events = CareerDatabase::GetInstance().GetRecentEvents(8);
    for (int i = 0; i < static_cast<int>(events.size()); i++) {
      const auto& evt = events[i];
      std::string prefix = evt.isMajor ? "*** " : "  ";
      std::string repStr = evt.reputationImpact > 0 ? " (+" + std::to_string(evt.reputationImpact) + ")" :
                           evt.reputationImpact < 0 ? " (" + std::to_string(evt.reputationImpact) + ")" : "";
      Gui2Caption* evtLine = new Gui2Caption(windowManager, "caption_br_evt_" + std::to_string(i),
        5, yPos, 90, 2, prefix + "[" + evt.type + "] " + evt.description + repStr);
      this->AddView(evtLine);
      evtLine->Show();
      yPos += 2;
    }
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_br_back", 30, 90, 40, 3, "Back to Owner Hub");
  btnBack->sig_OnClick.connect([this](...) { CreatePage(e_PageID_OwnerHub); });
  this->AddView(btnBack);
  btnBack->Show();

  this->Show();
}

OwnerBoardRoomPage::~OwnerBoardRoomPage() {}
