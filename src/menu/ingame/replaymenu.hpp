// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MENU_INGAME_REPLAY
#define _HPP_MENU_INGAME_REPLAY

#include "../../onthepitch/match.hpp"
#include "../../onthepitch/replaywipe.hpp"
#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

class ReplayPage : public Gui2Page {
public:
  ReplayPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~ReplayPage();

  void OnClose();
  // `camera` is one of Match::SetReplayCamera's modes: a goal replay opens
  // behind the goal, a foul on the close view.
  void Autorun(int replayHistoryOffset_ms, bool stayInReplay, int camera = 1);

protected:
  Match* match;

  virtual void Process();
  virtual void ProcessKeyboardEvent(KeyboardEvent* event);
  virtual void ProcessJoystickEvent(JoystickEvent* event);
  void ProcessInput(const Vector3& direction, bool button1, bool button2, bool slowMotion);
  void UpdateTimeLabel();

  signed long actualTime_ms;
  unsigned long minTime_ms;
  unsigned long maxTime_ms;

  int cam;
  int replayCamCount;
  float modifierValue;

  bool autoRun;
  bool stayInReplay;
  bool slowMotion;
  bool closeWhenAutorunCompletes;

  Gui2Caption* timeLabel;

  // The 4cc wipe over the cut, in and out (replaywipe.hpp). One full-screen image
  // whose picture is swapped per frame, rather than a hundred views: the frames are
  // RGBA and the crest is drawn through PES's own matte.
  Gui2Image* wipe;
  ReplayWipe::Timing wipeTiming;
  std::string wipeDir;
  unsigned long wipeStarted_ms;
  int wipeFrameOnScreen;
  bool wipeRunning;
  // The outgoing wipe holds the page open until the cut is covered.
  bool wipeClosing;

  void StartWipe();
  // -> true once an outgoing wipe has covered the cut and the page may go.
  bool RunWipe();
};

#endif
