// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MENU_GAME
#define _HPP_MENU_GAME

#include <boost/signals2.hpp>

#include "utils/gui2/page.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/windowmanager.hpp"

class Match;

using namespace blunted;

class GamePage : public Gui2Page {
public:
  GamePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~GamePage();

  virtual void Process();

  virtual void ProcessWindowingEvent(WindowingEvent* event);
  virtual void ProcessKeyboardEvent(KeyboardEvent* event);
  virtual void ProcessJoystickEvent(JoystickEvent* event);

  void GoShortReplayPage();
  void GoExtendedReplayPage();
  void GoMatchPhasePage();
  void GoGameOverPage();
  void OnCreatedMatch();

protected:
  // The opening versus banner: both crests, both names, over the beat that
  // asked for PrematchTimeline::Overlay::Versus. Built once, faded with the
  // beat's own cross-fade.
  void BuildVersusBanner();
  void UpdateVersusBanner();

  blunted::Gui2Caption* betaSign = nullptr;
  bool betaSignHidden = false;
  blunted::Gui2Image* versusCrest[2] = {nullptr, nullptr};
  blunted::Gui2Caption* versusName[2] = {nullptr, nullptr};
  blunted::Gui2Caption* versusVs = nullptr;
  float versusAlpha = -1.0f;
  Match* match;
  unsigned long matchReadyTime_ms;
  bool gamePlanShotTriggered = false;
  bool autoQuitTriggered;

  boost::signals2::connection conn_MatchPhaseChange;
  boost::signals2::connection conn_ShortReplayMoment;
  boost::signals2::connection conn_ExtendedReplayMoment;
  boost::signals2::connection conn_GameOver;
};

#endif
