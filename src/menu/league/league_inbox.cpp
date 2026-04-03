#include "league_inbox.hpp"

#include "../../main.hpp"
#include "menu_smoke.hpp"
#include "../pagefactory.hpp"

LeagueInboxPage::LeagueInboxPage(Gui2WindowManager* windowManager,
                                 const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_inbox", 20, 20, 60, 3, "Inbox");
  this->AddView(title);
  title->Show();

  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_inbox_info", 20, 40, 60, 6, "No new messages.");
  this->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_inbox_back", 30, 90, 40, 3, "Back to Dashboard");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Forward), properties, 0);
    delete this;
  });
  this->AddView(btnBack);
  btnBack->Show();
  btnBack->SetFocus();

  this->Show();
}

LeagueInboxPage::~LeagueInboxPage() {}

void LeagueInboxPage::Process() {
  Gui2Page::Process();

  if (!league_menu_smoke::RouteEnabled("inbox") || autoAdvanceTriggered ||
      league_menu_smoke::Now_ms() <
          pageCreatedTime_ms + league_menu_smoke::kQuitDelay_ms) {
    return;
  }

  autoAdvanceTriggered = true;
  printf("[menu-smoke] League inbox reached successfully\n");
  GetMenuTask()->QuitGame();
}
