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

#include <vector>

using namespace blunted;

class ReplayPage : public Gui2Page {
public:
  ReplayPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~ReplayPage();

  void OnClose();
  // `camera` is one of Match::SetReplayCamera's modes: a goal replay opens
  // behind the goal, a foul on the close view.
  void Autorun(int replayHistoryOffset_ms, bool stayInReplay, int camera = 1);
  // PES's goal replay is two cuts, not one: a wide of the build-up and finish,
  // then a slow close-up of the finish alone - and both STOP at the goal. This
  // one played the tape to the present, so a goal replay replayed the
  // celebration that had just been on screen (owner, 06-09). `angles` are
  // Match::SetReplayCamera modes; `stopBefore_ms` is how far short of the
  // present each angle ends, i.e. where the goal is.
  void AutorunAngles(int replayHistoryOffset_ms, bool stayInReplay,
                     const std::vector<int>& angles, int stopBefore_ms);

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

  // The remaining angles of a multi-cut replay, and where each of them starts
  // and stops. Empty = the single-angle replay this page always had.
  std::vector<int> angleQueue;
  size_t angleIndex = 0;
  signed long angleStart_ms = 0;
  signed long angleStop_ms = 0;
  bool AdvanceAngle();

  // A replay the match started for itself, rather than one the user asked for.
  // PES plays the goal and foul replays as part of the cutscene - clean picture,
  // no scrub bar, no control hints - and offers "Replay Control" on a button if
  // you actually want to drive it. GF showed the whole tape-deck chrome over
  // every automatic replay, which is the one thing the reference never does.
  bool cinematic;
  // The only thing drawn over a cinematic replay: the skip/take-control offer.
  Gui2View* cinematicPrompt;

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
  // -> true once the wipe has the screen covered, which is when a cut may happen.
  bool RunWipe();
  // The cut into the replay, and the cut back out. Both happen under full cover, so
  // neither is ever seen: PES's own "fadestart" is the frame that marks it.
  void EnterReplay();
  void LeaveReplay();
  // The replay's own chrome, which has no business being on screen before the cut
  // into it or after the cut out of it.
  void ShowReplayChrome(bool shown);
  // Hands a cinematic replay over to the user: chrome in, prompt out, and the
  // page stops closing itself when playback reaches the end.
  void TakeReplayControl();

  // The replay is entered when the incoming wipe covers, and left when the outgoing
  // one does; the page itself lives until the wipe has played out.
  bool enteredReplay;
  bool leftReplay;
  std::vector<Gui2View*> replayChrome;
};

#endif
