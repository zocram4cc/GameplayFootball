#include "league_inbox.hpp"

#include "../../main.hpp"
#include "menu_smoke.hpp"
#include "../pagefactory.hpp"

LeagueInboxPage::LeagueInboxPage(Gui2WindowManager* windowManager,
                                 const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(league_menu_smoke::Now_ms()),
      autoAdvanceTriggered(false) {
  Gui2Frame* frame = new Gui2Frame(windowManager, "frame_league_inbox", 15, 5, 70, 90, true);
  this->AddView(frame);
  frame->Show();
 
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_league_inbox", 2, 2, 66, 3, "Inbox");
  frame->AddView(title);
  title->Show();

  Gui2Caption* info =
      new Gui2Caption(windowManager, "caption_inbox_info", 2, 10, 66, 6, "No new messages.");
  frame->AddView(info);
  info->Show();

  Gui2Button* btnBack = new Gui2Button(windowManager, "btn_inbox_back", 30, 92, 40, 3, "Back to Dashboard");
  btnBack->sig_OnClick.connect([this, windowManager](...) {
    this->Exit();
    Properties properties;
    windowManager->GetPageFactory()->CreatePage(static_cast<int>(e_PageID_League_Forward), properties, 0);
    delete this;
  });
  frame->AddView(btnBack);
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
