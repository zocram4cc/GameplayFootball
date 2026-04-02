#include "league_standings.hpp"

#include "../../main.hpp"
#include "../pagefactory.hpp"
#include "base/utils.hpp"

LeagueStandingsPage::LeagueStandingsPage(Gui2WindowManager* windowManager,
                                         const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_standings", 20, 5, 60, 3, "Standings");
  this->AddView(title);
  title->Show();

  Gui2Button* btnLeague = new Gui2Button(windowManager, "btn_standings_league", 0, 0, 60, 3, "League Table");
  Gui2Button* btnLeagueStats = new Gui2Button(windowManager, "btn_standings_league_stats", 0, 0, 60, 3, "League Stats");
  Gui2Button* btnNCup = new Gui2Button(windowManager, "btn_standings_ncup", 0, 0, 60, 3, "National Cup");
  Gui2Button* btnICup1 = new Gui2Button(windowManager, "btn_standings_icup1", 0, 0, 60, 3, "International Cup 1");
  Gui2Button* btnICup2 = new Gui2Button(windowManager, "btn_standings_icup2", 0, 0, 60, 3, "International Cup 2");
  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_standings_back", 0, 0, 60, 3, "Back to Dashboard");

  btnLeague->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings_League); });
  btnLeagueStats->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings_League_Stats); });
  btnNCup->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings_NCup); });
  btnICup1->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings_ICup1); });
  btnICup2->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings_ICup2); });
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Forward); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_standings", 20, 14, 60, 60);
  grid->AddView(btnLeague, 0, 0);
  grid->AddView(btnLeagueStats, 1, 0);
  grid->AddView(btnNCup, 2, 0);
  grid->AddView(btnICup1, 3, 0);
  grid->AddView(btnICup2, 4, 0);
  grid->AddView(btnBack, 5, 0);
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  btnLeague->SetFocus();
  this->Show();
}

LeagueStandingsPage::~LeagueStandingsPage() {}

void LeagueStandingsPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueStandingsLeaguePage::LeagueStandingsLeaguePage(Gui2WindowManager* windowManager,
                                                     const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_standings_league", 20, 5, 60, 3, "League");
  this->AddView(title);
  title->Show();

  Gui2Button* btnTable = new Gui2Button(windowManager, "btn_league_table", 0, 0, 60, 3, "Table");
  Gui2Button* btnStats = new Gui2Button(windowManager, "btn_league_stats", 0, 0, 60, 3, "Stats");
  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_league_back", 0, 0, 60, 3, "Back to Standings");

  btnTable->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings_League_Table); });
  btnStats->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings_League_Stats); });
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings); });

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_league_sub", 20, 14, 60, 50);
  grid->AddView(btnTable, 0, 0);
  grid->AddView(btnStats, 1, 0);
  grid->AddView(btnBack, 2, 0);
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  btnTable->SetFocus();
  this->Show();
}

LeagueStandingsLeaguePage::~LeagueStandingsLeaguePage() {}

void LeagueStandingsLeaguePage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueStandingsLeagueTablePage::LeagueStandingsLeagueTablePage(Gui2WindowManager* windowManager,
                                                               const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_league_table", 10, 2,
                                        80, 3, "League Table");
  this->AddView(title);
  title->Show();

  Gui2Caption* header = new Gui2Caption(windowManager, "caption_table_header", 3, 6, 94, 2,
                                        "Team                          | P  | W  | D  | L  | GF | GA | GD | Pts");
  this->AddView(header);
  header->Show();

  auto result = GetDB()->Query(
      "SELECT t.id, t.name, l.name FROM teams t JOIN leagues l ON t.league_id = l.id "
      "ORDER BY l.name, t.name");

  Gui2Grid* grid = new Gui2Grid(windowManager, "grid_league_table", 3, 9, 94, 78);
  int row = 0;
  std::string currentLeague;
  for (const auto& r : result->data) {
    std::string leagueName = r.at(2);
    if (leagueName != currentLeague) {
      currentLeague = leagueName;
      Gui2Caption* sep = new Gui2Caption(windowManager, "caption_league_sep_" + int_to_str(row),
                                         0, 0, 90, 2.5, "--- " + leagueName + " ---");
      grid->AddView(sep, row++, 0);
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%-29s | %2s | %2s | %2s | %2s | %2s | %2s | %2s | %3s",
             r.at(1).c_str(), "0", "0", "0", "0", "0", "0", "0", "0");
    Gui2Button* btn = new Gui2Button(windowManager, "btn_table_" + r.at(0), 0, 0, 90, 2.5, buf);
    grid->AddView(btn, row++, 0);
  }
  grid->UpdateLayout(0.5);
  this->AddView(grid);
  grid->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_table_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings_League), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  this->SetFocus();
  this->Show();
}

LeagueStandingsLeagueTablePage::~LeagueStandingsLeagueTablePage() {}

LeagueStandingsLeagueStatsPage::LeagueStandingsLeagueStatsPage(Gui2WindowManager* windowManager,
                                                               const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_league_stats", 10, 5,
                                        80, 3, "League Stats");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_league_stats_info", 10, 20, 80, 6,
                                       "No stats available yet. Play matches to see statistics.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_league_stats_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings_League), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsLeagueStatsPage::~LeagueStandingsLeagueStatsPage() {}

LeagueStandingsNCupPage::LeagueStandingsNCupPage(Gui2WindowManager* windowManager,
                                                 const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_ncup", 10, 5, 80, 3,
                                        "National Cup");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_ncup_info", 10, 15, 80, 8,
                                       "Coming Soon - National Cup tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_ncup_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings); });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsNCupPage::~LeagueStandingsNCupPage() {}

void LeagueStandingsNCupPage::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueStandingsNCupTreePage::LeagueStandingsNCupTreePage(Gui2WindowManager* windowManager,
                                                         const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_ncup_tree", 10, 5, 80, 3,
                                        "National Cup - Tournament Tree");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_ncup_tree_info", 10, 15, 80, 8,
                                       "Coming Soon - National Cup tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_ncup_tree_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsNCupTreePage::~LeagueStandingsNCupTreePage() {}

LeagueStandingsNCupStatsPage::LeagueStandingsNCupStatsPage(Gui2WindowManager* windowManager,
                                                           const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_ncup_stats", 10, 5, 80, 3,
                                        "National Cup - Stats");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_ncup_stats_info", 10, 15, 80, 8,
                                       "Coming Soon - National Cup tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_ncup_stats_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsNCupStatsPage::~LeagueStandingsNCupStatsPage() {}

LeagueStandingsICup1Page::LeagueStandingsICup1Page(Gui2WindowManager* windowManager,
                                                   const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup1", 10, 5, 80, 3,
                                        "International Cup 1");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup1_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 1 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup1_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings); });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup1Page::~LeagueStandingsICup1Page() {}

void LeagueStandingsICup1Page::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueStandingsICup1GroupTablePage::LeagueStandingsICup1GroupTablePage(
    Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup1_grouptable",
                                        10, 5, 80, 3, "International Cup 1 - Group Table");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup1_gt_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 1 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup1_gt_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup1GroupTablePage::~LeagueStandingsICup1GroupTablePage() {}

LeagueStandingsICup1TreePage::LeagueStandingsICup1TreePage(Gui2WindowManager* windowManager,
                                                           const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup1_tree", 10, 5, 80, 3,
                                        "International Cup 1 - Tournament Tree");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup1_tree_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 1 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup1_tree_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup1TreePage::~LeagueStandingsICup1TreePage() {}

LeagueStandingsICup1StatsPage::LeagueStandingsICup1StatsPage(Gui2WindowManager* windowManager,
                                                             const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup1_stats", 10, 5, 80, 3,
                                        "International Cup 1 - Stats");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup1_stats_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 1 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup1_stats_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup1StatsPage::~LeagueStandingsICup1StatsPage() {}

LeagueStandingsICup2Page::LeagueStandingsICup2Page(Gui2WindowManager* windowManager,
                                                   const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup2", 10, 5, 80, 3,
                                        "International Cup 2");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup2_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 2 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup2_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this](...) { GoPage(e_PageID_League_Standings); });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup2Page::~LeagueStandingsICup2Page() {}

void LeagueStandingsICup2Page::GoPage(e_PageID pageID) {
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage(static_cast<int>(pageID), properties, 0);
  delete this;
}

LeagueStandingsICup2GroupTablePage::LeagueStandingsICup2GroupTablePage(
    Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup2_grouptable",
                                        10, 5, 80, 3, "International Cup 2 - Group Table");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup2_gt_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 2 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup2_gt_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup2GroupTablePage::~LeagueStandingsICup2GroupTablePage() {}

LeagueStandingsICup2TreePage::LeagueStandingsICup2TreePage(Gui2WindowManager* windowManager,
                                                           const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup2_tree", 10, 5, 80, 3,
                                        "International Cup 2 - Tournament Tree");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup2_tree_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 2 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup2_tree_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup2TreePage::~LeagueStandingsICup2TreePage() {}

LeagueStandingsICup2StatsPage::LeagueStandingsICup2StatsPage(Gui2WindowManager* windowManager,
                                                             const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Caption* title = new Gui2Caption(windowManager, "caption_league_standings_icup2_stats", 10, 5, 80, 3,
                                        "International Cup 2 - Stats");
  this->AddView(title);
  title->Show();

  Gui2Caption* info = new Gui2Caption(windowManager, "caption_icup2_stats_info", 10, 15, 80, 8,
                                       "Coming Soon - International Cup 2 tournaments will be available in a future update.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_icup2_stats_back", 30, 90, 40, 3, "Back");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Standings), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();
  this->Show();
}

LeagueStandingsICup2StatsPage::~LeagueStandingsICup2StatsPage() {}
