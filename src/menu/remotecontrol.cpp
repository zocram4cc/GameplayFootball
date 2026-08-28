#include "remotecontrol.hpp"

#include "../onthepitch/remotecontrol.hpp"
#include "../onthepitch/remotecontrolserver.hpp"
#include "../remotecontrolmode.hpp"
#include "main.hpp"
#include "pagefactory.hpp"
#include "utils/gui2/widgets/frame.hpp"

using namespace blunted;

RemoteControlPage::RemoteControlPage(Gui2WindowManager* windowManager,
                                     const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  Gui2Frame* panel = new Gui2Frame(windowManager, "frame_remote", 25, 30, 50, 40, true);
  this->AddView(panel);
  panel->Show();

  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_remote_title", 2, 2, 46, 3, "Remote Control");
  panel->AddView(title);
  title->Show();

  status = new Gui2Caption(windowManager, "caption_remote_status", 2, 8, 46, 2.4f,
                           "Enter the streamer key and press Enter.");
  panel->AddView(status);
  status->Show();

  keyInput = new Gui2EditLine(windowManager, "edit_remote_key", 2, 13, 46, 3, "");
  keyInput->sig_OnEnter.connect(
      [this](Gui2EditLine* edit) { EnterMode(edit->GetText()); });
  panel->AddView(keyInput);

  if (RemoteControlMode::IsActive()) {
    // Returning here after a match: the mode (and its server) never left.
    status->SetCaption("Waiting for control panel...");
  } else if (GetConfiguration()->GetBool("remote_control_mode", false)) {
    // Headless entry: the key comes from the config instead of the keyboard.
    EnterMode(GetConfiguration()->Get("remote_control_key", ""));
  } else {
    keyInput->Show();
    keyInput->SetFocus();
  }

  this->Show();
}

RemoteControlPage::~RemoteControlPage() {}

void RemoteControlPage::EnterMode(const std::string& streamerKey) {
  if (!RemoteControlMode::Enter(streamerKey)) {
    status->SetCaption("Could not open the control port; see the log.");
    return;
  }
  keyInput->Hide();
  status->SetCaption("Waiting for control panel...");
}

void RemoteControlPage::Process() {
  Gui2Page::Process();

  RemoteControl::Server* server = RemoteControlMode::GetServer();
  if (!server) return;

  for (const RemoteControl::Command& command : server->GetQueue().Drain()) {
    if (command.type != RemoteControl::e_CommandType_Schedule) {
      Log(e_Warning, "RemoteControl", "Page", "refused command: no match is running");
      continue;
    }
    RemoteControl::ApplySchedule(command, *GetConfiguration());
    Log(e_Notice, "RemoteControl", "Page",
        "schedule accepted: teams " + int_to_str(command.schedule.team1Id) + " v " +
            int_to_str(command.schedule.team2Id) + " at " + command.schedule.stadiumObject);

    // The same self-driving road the menu-smoke harnesses take into a match.
    this->Exit();
    Properties properties;
    properties.SetBool("isInGame", false);
    windowManager->GetPageFactory()->CreatePage((int)e_PageID_ControllerSelect, properties, 0);
    delete this;
    return;
  }
}

void RemoteControlPage::GoMainMenu() {
  RemoteControlMode::Leave();
  this->Exit();
  Properties properties;
  windowManager->GetPageFactory()->CreatePage((int)e_PageID_MainMenu, properties, 0);
  delete this;
}

void RemoteControlPage::ProcessWindowingEvent(WindowingEvent* event) {
  if (event->IsEscape()) {
    event->Ignore();
    GoMainMenu();
    return;
  }
  Gui2Page::ProcessWindowingEvent(event);
}
