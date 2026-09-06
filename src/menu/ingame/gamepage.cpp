// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "gamepage.hpp"

#include <filesystem>

#include "../../onthepitch/match.hpp"
#include "../../onthepitch/team.hpp"
#include "../pagefactory.hpp"
#include "gameover.hpp"
#include "main.hpp"
#include "phasemenu.hpp"
#include "replaymenu.hpp"
#include "systems/graphics/rendering/opengl_renderer3d.hpp"

using namespace blunted;

namespace {

constexpr unsigned long kMenuSmokeQuitDelay_ms = 2000;

bool MenuSmokeQuickMatchEnabled() {
  return GetConfiguration()->GetBool("menu_smoke_test_quick_match", false);
}

bool MenuSmokeFullMatchEnabled() {
  return GetConfiguration()->GetBool("menu_smoke_test_full_match", false);
}

}  // namespace

GamePage::GamePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData), match(0), matchReadyTime_ms(0), autoQuitTriggered(false) {
  betaSign = new Gui2Caption(windowManager, "caption_betasign", 0, 0, 0, 2, "League-Soccer v0.4.0");
  betaSign->SetColor(Vector3(180, 180, 180));
  betaSign->SetTransparency(0.3f);
  this->AddView(betaSign);
  // Bottom left corner, out of the lower third's middle where the banners and
  // the pre-match formation panel live.
  betaSign->SetPosition(1.5f, 97.0f);
  betaSign->Show();

  this->Show();

  this->SetFocus();
}

GamePage::~GamePage() {
  // todonow: only when connected in the first place?
  // problem is, this function may be called outside of gametask's or match's lifetime.

  if (match) {
    if (Verbose())
      printf("disconnecting signals\n");

    conn_MatchPhaseChange.disconnect();
    conn_ShortReplayMoment.disconnect();
    conn_ExtendedReplayMoment.disconnect();
    conn_GameOver.disconnect();
  }
}

void GamePage::Process() {
  Gui2Page::Process();

  // The version watermark is in-match chrome too: it has no business sitting
  // over a broadcast opening - or over a celebration, a card, a replay or the
  // closing ceremony, which is where the last showcase still had it (see
  // Match::ApplyHudVisibility).
  if (betaSign && match) {
    const bool inEntrance = match->IsStaged();
    if (inEntrance != betaSignHidden) {
      betaSignHidden = inEntrance;
      if (inEntrance)
        betaSign->Hide();
      else
        betaSign->Show();
    }
  }

  if (!match) {
    GetGameTask()->matchLifetimeMutex.lock();
    if (GetGameTask()->GetMatch() != 0) {
      match = GetGameTask()->GetMatch();

      if (Verbose())
        printf("connecting signals\n");

      conn_MatchPhaseChange =
          match->sig_OnMatchPhaseChange.connect([this](...) { GoMatchPhasePage(); });
      conn_ShortReplayMoment =
          match->sig_OnShortReplayMoment.connect([this](...) { GoShortReplayPage(); });
      conn_ExtendedReplayMoment =
          match->sig_OnExtendedReplayMoment.connect([this](...) { GoExtendedReplayPage(); });
      conn_GameOver = match->sig_OnGameOver.connect([this](...) { GoGameOverPage(); });

      if (MenuSmokeQuickMatchEnabled() || MenuSmokeFullMatchEnabled()) {
        matchReadyTime_ms = EnvironmentManager::GetInstance().GetTime_ms();
        printf("[menu-smoke] Gameplay page reached and live match is active\n");
      }
    }
    GetGameTask()->matchLifetimeMutex.unlock();
  }

  UpdateVersusBanner();

  if (match && !autoQuitTriggered && MenuSmokeQuickMatchEnabled() && !MenuSmokeFullMatchEnabled() &&
      matchReadyTime_ms != 0 &&
      EnvironmentManager::GetInstance().GetTime_ms() >=
          matchReadyTime_ms + kMenuSmokeQuitDelay_ms) {
    autoQuitTriggered = true;
    printf("[menu-smoke] Quick Match verification succeeded, quitting test run\n");
    EnvironmentManager::GetInstance().SignalQuit();
  }
}

void GamePage::GoShortReplayPage() {
  CreatePage((int)e_PageID_Replay);
}

void GamePage::GoExtendedReplayPage() {
  this->Exit();

  Properties properties;
  ReplayPage* replayPage = static_cast<ReplayPage*>(
      windowManager->GetPageFactory()->CreatePage((int)e_PageID_Replay, properties, 0));

  // todo: use properties instead?
  // A scripted replay says how far back it wants to start (a goal replay
  // reaches past its own celebration); anything else takes the whole buffer.
  int replayHistoryOffset_ms = match->GetReplayStartOffset_ms() > 0
                                   ? (int)match->GetReplayStartOffset_ms()
                                   : match->GetReplaySize_ms();
  bool stayInReplay = true;
  // A goal gets PES's two cuts, both ending at the goal itself: the wide of the
  // build-up, then the close-up of the finish at half speed. Anything else -
  // a foul, a near miss - keeps the single angle it always had.
  if (match->IsGoalScored() && match->GetReplayStartOffset_ms() > 0) {
    const int stopBefore_ms =
        (int)match->GetReplayStartOffset_ms() - (int)GoalSequence::kReplayLeadIn_ms;
    replayPage->AutorunAngles(replayHistoryOffset_ms, stayInReplay,
                              {match->GetReplayCamera(), 2 /* close-up */},
                              stopBefore_ms > 0 ? stopBefore_ms : 0);
  } else {
    replayPage->Autorun(replayHistoryOffset_ms, stayInReplay, match->GetReplayCamera());
  }

  delete this;
}

void GamePage::GoMatchPhasePage() {
  e_MatchPhase nextPhase = match->GetMatchPhase();

  Properties properties;
  properties.Set("nextphase", (int)nextPhase);
  CreatePage((int)e_PageID_MatchPhase, properties);
}

void GamePage::GoGameOverPage() {
  CreatePage((int)e_PageID_GameOver);
}

void GamePage::OnCreatedMatch() {}

void GamePage::BuildVersusBanner() {
  if (!match || versusCrest[0]) return;

  // PES's opening graphic: a crest either side of the fixture, low in the
  // frame so the aerial keeps the stadium. The crest artwork is the team's own
  // logo - the same file the loading page and the scoreboard read.
  constexpr float kCrestHeight = 14.0f;
  const float crestWidth = windowManager->GetWidthPercentForHeight(kCrestHeight, 1.0f);
  const float centre[2] = {29.0f, 71.0f};
  for (int side = 0; side < 2; side++) {
    Team* team = match->GetTeam(side);
    if (!team || !team->GetTeamData()) return;
    const std::string logo = team->GetTeamData()->GetLogoUrl();

    versusCrest[side] =
        new Gui2Image(windowManager, "image_versus_crest" + int_to_str(side),
                      centre[side] - crestWidth * 0.5f, 58.0f, crestWidth, kCrestHeight);
    this->AddView(versusCrest[side]);
    if (!logo.empty() && std::filesystem::exists(logo)) versusCrest[side]->LoadImage(logo);

    versusName[side] = new Gui2Caption(windowManager, "caption_versus_name" + int_to_str(side), 0,
                                       74.0f, 0, 3.4f, team->GetTeamData()->GetName());
    versusName[side]->SetPosition(centre[side] - versusName[side]->GetTextWidthPercent() * 0.5f,
                                  74.0f);
    this->AddView(versusName[side]);
  }

  versusVs = new Gui2Caption(windowManager, "caption_versus_vs", 0, 63.0f, 0, 4.0f, "VS");
  versusVs->SetPosition(50.0f - versusVs->GetTextWidthPercent() * 0.5f, 63.0f);
  this->AddView(versusVs);

  // Hidden until the beat that wants it: Show() here would put it over the
  // first frame of the walkout.
  versusAlpha = 0.0f;
  for (int side = 0; side < 2; side++) {
    versusCrest[side]->Hide();
    versusName[side]->Hide();
  }
  versusVs->Hide();
}

void GamePage::UpdateVersusBanner() {
  // The page outlives its match pointer's arrival: it picks the match up in
  // Process (OnCreatedMatch is never called), so the banner is built the first
  // frame there is a match to name.
  BuildVersusBanner();
  if (!match || !versusCrest[0]) return;

  const PrematchTimeline::State beat = match->GetPrematchState();
  const float alpha = (match->IsInEntrance() &&
                       beat.overlay == PrematchTimeline::Overlay::Versus)
                          ? beat.overlayAlpha
                          : 0.0f;
  if (alpha == versusAlpha) return;
  versusAlpha = alpha;

  // Images are shown or hidden, never faded - Surface::SetAlpha multiplies
  // into the alpha channel and would erase a crest's transparency for good
  // (the same trap Gui2FormationGraphic::ApplyAlpha documents). Captions
  // cross-fade properly.
  const bool visible = alpha > 0.02f;
  for (int side = 0; side < 2; side++) {
    if (visible)
      versusCrest[side]->Show();
    else
      versusCrest[side]->Hide();
    if (visible)
      versusName[side]->Show();
    else
      versusName[side]->Hide();
    versusName[side]->SetTransparency(1.0f - alpha);
  }
  if (visible)
    versusVs->Show();
  else
    versusVs->Hide();
  versusVs->SetTransparency(1.0f - alpha);
}

void GamePage::ProcessWindowingEvent(WindowingEvent* event) {
  if (Verbose())
    if (event->IsEscape())
      printf("escape!\n");
  event->Ignore();
}

void GamePage::ProcessKeyboardEvent(KeyboardEvent* event) {
  if (event->GetKeyOnce(SDLK_TAB)) {
    if (match)
      match->ToggleStatsOverlay();
    return;
  }

  if (event->GetKeyOnce(SDLK_ESCAPE)) {
    // check which team the keyboard belongs to
    int controllerID = 0;
    const std::vector<IHIDevice*>& controllers = GetControllers();
    for (unsigned int c = 0; c < controllers.size(); c++) {
      if (controllers.at(c)->GetDeviceType() == e_HIDeviceType_Keyboard) {
        controllerID = c;
        break;
      }
    }

    if (Verbose())
      printf("controller index %i (keyboard) pressed start\n", controllerID);

    int teamID = 0;
    const std::vector<SideSelection> sides = GetMenuTask()->GetControllerSetup();
    for (unsigned int s = 0; s < sides.size(); s++) {
      if (sides.at(s).controllerID == (signed int)controllerID) {
        teamID = int(round(sides.at(s).side * 0.5 + 0.5));
        break;
      }
    }

    if (Verbose())
      printf("team belonging to this controller seems to be %i\n", teamID);

    Properties properties;
    properties.Set("teamID", teamID);
    CreatePage((int)e_PageID_Ingame, properties);
    return;
  }

  event->Ignore();
}

void GamePage::ProcessJoystickEvent(JoystickEvent* event) {
  // oof, the problem is that we are using a GUI joystick event to find out what ingame HID
  // controller pressed <start>. I think we need a new system in which the game doesn't use its own
  // input system, but uses the GUI one. for now, this hax will have to do

  const std::vector<IHIDevice*>& controllers = GetControllers();
  for (unsigned int c = 0; c < controllers.size(); c++) {
    if (controllers.at(c)->GetDeviceType() == e_HIDeviceType_Gamepad) {
      HIDGamepad* gamepad = static_cast<HIDGamepad*>(controllers.at(c));
      int joyID =
          gamepad->GetGamepadID();  // these should be the same IDs the GUI system uses as joyID

      if (event->GetButton(joyID, gamepad->GetControllerMapping(
                                      gamepad->GetFunctionMapping(e_ButtonFunction_Start)))) {
        if (Verbose())
          printf("controller index %i, gamepad/joy ID %i pressed start\n", c, joyID);

        // check which team this controller belongs to
        int teamID = 0;
        const std::vector<SideSelection> sides = GetMenuTask()->GetControllerSetup();
        for (unsigned int s = 0; s < sides.size(); s++) {
          if (sides.at(s).controllerID == (signed int)c) {
            teamID = int(round(sides.at(s).side * 0.5 + 0.5));
            break;
          }
        }

        if (Verbose())
          printf("team belonging to this controller seems to be %i\n", teamID);

        Properties properties;
        properties.Set("teamID", teamID);
        CreatePage((int)e_PageID_Ingame, properties);
        return;
      }
    }
  }

  event->Ignore();
}
