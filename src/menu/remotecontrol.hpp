// The Remote Control page: the way into remote-control mode from the main
// menu. It asks for the streamer key and nothing else; once the key is in, the
// engine goes limp behind a "waiting for control panel" caption and does only
// what the panel tells it - starting with a schedule command, which this page
// turns into the same self-driving launch the menu-smoke harnesses use.

#ifndef _HPP_MENU_REMOTECONTROL
#define _HPP_MENU_REMOTECONTROL

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/editline.hpp"

using namespace blunted;

class RemoteControlPage : public Gui2Page {
public:
  RemoteControlPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~RemoteControlPage();

  virtual void Process();
  virtual void ProcessWindowingEvent(WindowingEvent* event);

protected:
  void EnterMode(const std::string& streamerKey);
  void GoMainMenu();

  Gui2EditLine* keyInput = nullptr;
  Gui2Caption* status = nullptr;
};

#endif
