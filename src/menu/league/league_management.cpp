#include "league_management.hpp"

#include "../../main.hpp"
#include "menu_smoke.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueManagementPage::LeagueManagementPage(Gui2WindowManager* windowManager,
                                           const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_management", 20, 5, 60, 3, "Management");
  this->AddView(title);
  title->Show();

  Gui2Button* btnContracts = new Gui2Button(windowManager, "btn_management_contracts", 0, 0, 60, 3, "Contracts");
  Gui2Button* btnTransfers = new Gui2Button(windowManager, "btn_management_transfers", 0, 0, 60, 3, "Transfers");
  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_management_back", 0, 0, 60, 3, "Back to Dashboard");

  btnContracts->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Management_Contracts); });
  btnTransfers->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Management_Transfers); });
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Forward); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_management", 20, 14, 60, 50);
  grid->AddView(btnContracts, 0, 0);
  grid->AddView(btnTransfers, 1, 0);
  grid->AddView(btnBack, 2, 0);
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  btnContracts->SetFocus();
  this->Show();
}

LeagueManagementPage::~LeagueManagementPage() {}

void LeagueManagementPage::Process() {
  Gui2Page::Process();

  if (autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kAdvanceDelay_ms) {
    return;
  }

  if (league_menu_smoke::RouteEnabled("management")) {
    autoAdvanceTriggered = true;
    printf("[menu-smoke] League management reached successfully\n");
    GetMenuTask()->QuitGame();
  } else if (league_menu_smoke::RouteEnabled("management_contracts")) {
    autoAdvanceTriggered = true;
    printf("[menu-smoke] Management page opening contracts\n");
    GoPage(e_PageID_League_Management_Contracts);
  }
}

void LeagueManagementPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueManagementContractsPage::LeagueManagementContractsPage(Gui2WindowManager* windowManager,
                                                             const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_management_contracts", 10, 3,
                                        80, 3, "Contracts");
  this->AddView(title);
  title->Show();

  Gui2Caption* header = new Gui2Caption(windowManager, "caption_contracts_header", 5, 7, 90, 2,
                                        "Name                  | Role                | Age");
  this->AddView(header);
  header->Show();

  auto result = GetDB()->Query(
      "SELECT p.firstname, p.lastname, p.role, p.age FROM players p "
      "JOIN teams t ON p.team_id = t.id JOIN settings s ON t.id = s.team_id "
      "ORDER BY p.formationorder");
  if (!result->data.empty()) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_contracts", 5, 10, 90, 72);
    int row = 0;
    for (const auto& r : result->data) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%-20s | %-19s | %2s",
               (r.at(0) + " " + r.at(1)).c_str(), r.at(2).c_str(), r.at(3).c_str());
      Gui2Button* btn = new Gui2Button(windowManager, "btn_contract_" + std::to_string(row), 0, 0, 86, 2.5, buf);
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    this->AddView(grid);
    grid->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_contracts_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Management), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  this->SetFocus();
  this->Show();
}

LeagueManagementContractsPage::~LeagueManagementContractsPage() {}

void LeagueManagementContractsPage::Process() {
  Gui2Page::Process();

  if (!league_menu_smoke::RouteEnabled("management_contracts") || autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kQuitDelay_ms) {
    return;
  }

  autoAdvanceTriggered = true;
  printf("[menu-smoke] Management contracts reached successfully\n");
  GetMenuTask()->QuitGame();
}

LeagueManagementTransfersPage::LeagueManagementTransfersPage(Gui2WindowManager* windowManager,
                                                             const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_management_transfers", 10, 5,
                                        80, 3, "Transfers");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_transfers_info", 10, 20, 80, 8,
                                       "Transfer market is not yet available in League mode. "
                                       "Try Career Mode for full transfer features.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_transfers_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Management), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueManagementTransfersPage::~LeagueManagementTransfersPage() {}
