// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#ifndef _HPP_GUI2_MENUTASK
#define _HPP_GUI2_MENUTASK

#include "../gamedefines.hpp"
#include "ingame/gamepage.hpp"
#include "menuscript.hpp"
#include "scene/scene3d/scene3d.hpp"
#include "utils/gui2/guitask.hpp"
#include "utils/gui2/widgets/image.hpp"

class Match;
class MatchData;

using namespace blunted;

constexpr float kMenuAspectRatio = 16.0f / 9.0f;

enum e_MenuAction {
  e_MenuAction_Menu,  // start main menu
  e_MenuAction_Game,  // start game
  e_MenuAction_None
};

struct SideSelection {
  int controllerID = -1;
  Gui2Image* controllerImage = nullptr;
  int side = 0;  // -1, 0, 1
  // This pad runs the bench rather than the players, PES-style: marked on the
  // select-sides screen, per side, so one pad may coach one team and another play
  // the other. Both benches at once is just both sides marked.
  bool coach = false;
  Gui2Image* coachImage = nullptr;
};

// todo: just load match-, team-, and playerdata before starting match
// this requires some bigger changes, so stick with this imperfect system for the time being
struct QueuedFixture {
  QueuedFixture() {
    team1KitNum = 1;
    team2KitNum = 2;
    matchData = 0;
  }
  std::vector<SideSelection> sides;  // queued match fixture
  std::string teamID1, teamID2;      // queued match fixture
  int team1KitNum, team2KitNum;
  MatchData* matchData;
};

void SetActiveController(int side, bool keyboard);

class MenuTask : public Gui2Task {
public:
  MenuTask(float aspectRatio, float margin, TTF_Font* defaultFont, TTF_Font* defaultOutlineFont);
  virtual ~MenuTask();

  virtual void ProcessPhase();

  bool QuickStart();
  void QuitGame();

  void ReleaseAllButtons();

  void SetControllerSetup(const std::vector<SideSelection>& sides) {
    queuedFixture.Lock();
    queuedFixture->sides = sides;
    queuedFixture.Unlock();
  }
  const std::vector<SideSelection> GetControllerSetup() { return queuedFixture.GetData().sides; }
  void SetTeamIDs(const std::string& id1, const std::string& id2) {
    queuedFixture.Lock();
    queuedFixture->teamID1 = id1;
    queuedFixture->teamID2 = id2;
    queuedFixture.Unlock();
  }
  int GetTeamID(int whichOne) {
    if (whichOne == 0)
      return atoi(queuedFixture.GetData().teamID1.c_str());
    else
      return atoi(queuedFixture.GetData().teamID2.c_str());
  }
  int GetTeamKitNum(int teamID) {
    if (teamID == 0)
      return queuedFixture.GetData().team1KitNum;
    else
      return queuedFixture.GetData().team2KitNum;
  }
  void SetMatchData(MatchData* matchData) {
    queuedFixture.Lock();
    queuedFixture->matchData = matchData;
    queuedFixture.Unlock();
  }
  MatchData* GetMatchData() {
    return queuedFixture.GetData().matchData;
  }  // hint: this lock is useless, since we are returning the pointer and not a copy

  void SetMenuAction(e_MenuAction menuAction) { this->menuAction = menuAction; }

protected:
  e_MenuAction menuAction;

  Gui2Image* menuBackground;
  Lockable<QueuedFixture> queuedFixture;  // todo: we can probably unlock this stuff

  // Scripted keyboard input for headless verification runs (menuscript.hpp):
  // plays "menu_smoke_script" by writing straight into UserEventManager, the
  // same place a real keyboard lands, so the injected taps take the normal
  // guitask -> windowing event -> focused widget path.
  void TickMenuScript();
  bool menuScriptLoaded = false;
  std::vector<MenuScript::Step> menuScriptSteps;
  size_t menuScriptNextStep = 0;
  unsigned long menuScriptStartTime_ms = 0;
  // The key this driver is currently holding down, or 0 for none - released
  // on the following tick so a tap is exactly one frame, the way a person
  // pressing and releasing a real key would register to guitask.
  SDL_Keycode menuScriptHeldKey = 0;
};

#endif
