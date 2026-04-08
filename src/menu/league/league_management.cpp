#include "league_management.hpp"

#include "../../main.hpp"
#include "menu_smoke.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"
#include "utils/gui2/widgets/dialog.hpp"
#include "utils/gui2/widgets/text.hpp"

LeagueManagementPage::LeagueManagementPage(Gui2WindowManager* windowManager,
                                           const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_management", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_management", 2, 2, 66, 3, "Management");
  frame->AddView(title);
  title->Show();

  Gui2Button* btnContracts = new Gui2Button(windowManager, "btn_management_contracts", 0, 0, 60, 3, "Contracts");
  Gui2Button* btnTransfers = new Gui2Button(windowManager, "btn_management_transfers", 0, 0, 60, 3, "Transfers");
  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_management_back", 0, 0, 60, 3, "Back to Dashboard");

  btnContracts->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Management_Contracts); });
  btnTransfers->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Management_Transfers); });
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Forward); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_management", 2, 10, 66, 50);
  grid->AddView(btnContracts, 0, 0);
  grid->AddView(btnTransfers, 1, 0);
  grid->AddView(btnBack, 2, 0);
  grid->UpdateLayout(0.5);
  frame->AddView(grid);
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
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_contracts", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_management_contracts", 2, 2,
                                        66, 3, "Contracts");
  frame->AddView(title);
  title->Show();

  Gui2Caption* header = new Gui2Caption(windowManager, "caption_contracts_header", 2, 6, 66, 2,
                                        "Name                  | Role                | Age");
  frame->AddView(header);
  header->Show();

  auto result = GetDB()->Query(
      "SELECT p.firstname, p.lastname, p.role, p.age FROM players p "
      "JOIN teams t ON p.team_id = t.id JOIN settings s ON t.id = s.team_id "
      "ORDER BY p.formationorder");
  if (!result->data.empty()) {
    Gui2Grid* grid = new Gui2Grid(windowManager, "grid_contracts", 2, 10, 66, 75);
    int row = 0;
    for (const auto& r : result->data) {
      std::string fullName = r.at(0) + " " + r.at(1);
      char buf[256];
      snprintf(buf, sizeof(buf), "%-20s | %-19s | %2s",
               fullName.c_str(), r.at(2).c_str(), r.at(3).c_str());
      std::string btnLabel(buf);
      Gui2Button* btn = new Gui2Button(windowManager, "btn_contract_" + std::to_string(row), 0, 0, 86, 2.5, btnLabel);
      btn->sig_OnClick.connect([this, windowManager, fullName](...) {
        auto detail = GetDB()->Query(
            "SELECT p.firstname, p.lastname, p.role, p.age, p.base_stat, t.name "
            "FROM players p JOIN teams t ON p.team_id = t.id "
            "JOIN settings s ON t.id = s.team_id "
            "WHERE p.firstname || ' ' || p.lastname = '" + fullName + "' LIMIT 1");
        Gui2Dialog* dlg = new Gui2Dialog(windowManager, "dialog_contract_detail", 25, 20, 50, 60, fullName);
        if (!detail->data.empty()) {
          Gui2Text* txt = new Gui2Text(windowManager, "text_contract_detail", 5, 5, 90, 80, 2.5, 40, "");
          const auto& d = detail->data.at(0);
          txt->AddText("Name: " + d.at(0) + " " + d.at(1));
          txt->AddText("Role: " + d.at(2));
          txt->AddText("Age: " + d.at(3));
          txt->AddText("Base Stat: " + d.at(4));
          txt->AddText("Team: " + d.at(5));
          txt->AddEmptyLine();
          txt->AddText("Contract management features coming soon.");
          dlg->AddContent(txt);
        }
        (dlg->AddSingleButton("Close"))->SetFocus();
        dlg->sig_OnPositive.connect([this, dlg](...) {
          dlg->Exit();
          delete dlg;
        });
        this->AddView(dlg);
        dlg->Show();
      });
      grid->AddView(btn, row++, 0);
    }
    grid->UpdateLayout(0.5);
    frame->AddView(grid);
    grid->Show();
  }

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_contracts_back", 30, 92, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Management), properties, 0);
    delete this;
  });
  frame->AddView(btnBack);
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
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_transfers", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_management_transfers", 2, 2,
                                        66, 3, "Transfer Market");
  frame->AddView(title);
  title->Show();

  Gui2Caption* header = new Gui2Caption(windowManager, "caption_transfers_header", 2, 6, 66, 2,
                                        "Name                  | Role                | Age | Stat | Team");
  frame->AddView(header);
  header->Show();

  auto result = GetDB()->Query(
      "SELECT p.id, p.firstname, p.lastname, p.role, p.age, p.base_stat, t.name "
      "FROM players p JOIN teams t ON p.team_id = t.id "
      "WHERE t.id != (SELECT team_id FROM settings LIMIT 1) "
      "ORDER BY p.base_stat DESC LIMIT 20");

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_transfers", 2, 9, 66, 75);
  int row = 0;

  if (!result->data.empty()) {
    for (const auto& r : result->data) {
      std::string playerID = r.at(0);
      std::string fullName = r.at(1) + " " + r.at(2);
      char buf[256];
      snprintf(buf, sizeof(buf), "%-20s | %-19s | %2s | %3s  | %s",
               fullName.c_str(), r.at(3).c_str(), r.at(4).c_str(), r.at(5).c_str(), r.at(6).c_str());
      std::string btnLabel(buf);
      Gui2Button* btn = new Gui2Button(windowManager, "btn_transfer_" + std::to_string(row),
                                       0, 0, 86, 2.5, btnLabel);
      btn->sig_OnClick.connect([this, windowManager, playerID, fullName](...) {
        auto detail = GetDB()->Query(
            "SELECT p.firstname, p.lastname, p.role, p.age, p.base_stat, t.name "
            "FROM players p JOIN teams t ON p.team_id = t.id "
            "WHERE p.id = " + playerID);
        Gui2Dialog* dlg = new Gui2Dialog(windowManager, "dialog_transfer_" + playerID,
                                         20, 15, 60, 70, fullName);
        if (!detail->data.empty()) {
          Gui2Text* txt = new Gui2Text(windowManager, "text_transfer_" + playerID,
                                        5, 5, 90, 60, 2.5, 40, "");
          const auto& d = detail->data.at(0);
          txt->AddText("Name: " + d.at(0) + " " + d.at(1));
          txt->AddText("Role: " + d.at(2));
          txt->AddText("Age: " + d.at(3));
          txt->AddText("Base Stat: " + d.at(4));
          txt->AddText("Current Team: " + d.at(5));
          txt->AddEmptyLine();
          txt->AddText("Transfer negotiations coming soon.");
          dlg->AddContent(txt);
        }
        (dlg->AddSingleButton("Close"))->SetFocus();
        dlg->sig_OnPositive.connect([this, dlg](...) {
          dlg->Exit();
          delete dlg;
        });
        this->AddView(dlg);
        dlg->Show();
      });
      grid->AddView(btn, row++, 0);
    }
  } else {
    Gui2Caption* emptyCap = new Gui2Caption(windowManager, "caption_transfers_empty", 0, 0, 86, 3,
                                            "No players available in the transfer market.");
    grid->AddView(emptyCap, 0, 0);
  }
  grid->UpdateLayout(0.5);
  frame->AddView(grid);
  grid->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_transfers_back", 25, 92, 50, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Management), properties, 0);
    delete this;
  });
  frame->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueManagementTransfersPage::~LeagueManagementTransfersPage() {}
