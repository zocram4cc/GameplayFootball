// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_MENU_MATCHOPTIONS
#define _HPP_MENU_MATCHOPTIONS

#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/frame.hpp"
#include "utils/gui2/widgets/grid.hpp"
#include "utils/gui2/widgets/iconselector.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/widgets/menu.hpp"
#include "utils/gui2/widgets/root.hpp"
#include "utils/gui2/widgets/slider.hpp"
#include "menu/prematchchoices.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

class MatchOptionsPage : public Gui2Page {
public:
  MatchOptionsPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData);
  virtual ~MatchOptionsPage();

  virtual void Process();
  void GoLoadingMatchPage();

  Gui2Button* buttonStart;

protected:
  void UpdateMatchDurationCaption();
  // Match conditions and pre-match tactics.
  void UpdateWeatherCaption();
  void UpdateTimeOfDayCaption();
  void UpdateKitCaptions();
  // Stadium, entrance and post-match presentation: all three exist in the engine
  // and were reachable only by editing a config file.
  void UpdateStadiumCaption();
  void UpdateEntranceCaption();
  void UpdateResultCutsceneCaption();
  void GoGamePlan(int teamID);

  bool gamePlanShotTriggered = false;
  bool gamePlanShotTaken = false;
  unsigned long gamePlanOpenedTime_ms = 0;
  Gui2Slider* weatherSlider;
  Gui2Slider* timeOfDaySlider;
  Gui2Slider* kitSlider[2];
  Gui2Slider* stadiumSlider;
  Gui2Slider* entranceSlider;
  Gui2Slider* resultCutsceneSlider;
  std::vector<PrematchChoices::Choice> stadiumChoices;
  std::vector<PrematchChoices::Choice> entranceChoices;
  std::vector<PrematchChoices::Choice> resultCutsceneChoices;
  Gui2Slider* difficultySlider;
  Gui2Slider* matchDurationSlider;
  unsigned long pageCreatedTime_ms;
  bool autoAdvanceTriggered;
};

#endif
