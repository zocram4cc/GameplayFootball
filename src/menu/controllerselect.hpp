// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MENU_CONTROLLERSELECT
#define _HPP_MENU_CONTROLLERSELECT

#include "../onthepitch/match.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/widgets/frame.hpp"
#include "utils/gui2/widgets/menu.hpp"
#include "utils/gui2/widgets/root.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

class ControllerSelectPage : public Gui2Page {
public:
  ControllerSelectPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~ControllerSelectPage();

  void ConfirmSelection();
  void SetImagePositions();
  // The one-pad arrangement: both benches coached from a single controller. It is
  // exclusive of the per-side COACH marks, so turning it on clears them.
  void SetStreamerMode(bool on);
  void UpdateModeCaption();

  virtual void Process();
  virtual void ProcessKeyboardEvent(KeyboardEvent* event);
  virtual void ProcessJoystickEvent(JoystickEvent* event);
  virtual void ProcessWindowingEvent(WindowingEvent* event);

protected:
  std::vector<SideSelection> sides;
  std::vector<Gui2Caption*> coachCaptions;
  Gui2Caption* modeCaption = nullptr;
  // Who has the bench, the pad, the keyboard.
  static const int kTipLineCount = 3;
  std::vector<Gui2Caption*> tipCaptions;
  bool streamerMode = false;
  std::vector<unsigned long> delay;
  bool inGame;
  bool autoAssignedPlayerOne = false;
  unsigned long pageCreatedTime_ms;
  bool autoAdvanceTriggered;
};

#endif
