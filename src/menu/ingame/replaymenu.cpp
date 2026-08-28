// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "replaymenu.hpp"

#include "../../hid/gamepad.hpp"
#include "../../hid/keyboard.hpp"
#include "framework/scheduler.hpp"
#include "gametask.hpp"
#include "main.hpp"
#include "onthepitch/match.hpp"
#include "managers/environmentmanager.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/frame.hpp"
#include "utils/gui2/widgets/image.hpp"

#include <filesystem>

using namespace blunted;

namespace {

// The match's own HUD is not part of a replay: only the replay's overlay is.
void SuppressMatchHud(bool suppressed) {
  auto gameTask = GetGameTask();
  Match* match = gameTask ? gameTask->GetMatch() : nullptr;
  if (match) match->SuppressHudForReplay(suppressed);
}

}  // namespace

ReplayPage::ReplayPage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData) {
  match = GetGameTask()->GetMatch();

  this->SetFocus();
  this->Show();

  signed long tmp =
      match->GetActualTime_ms() - match->GetReplaySize_ms();  // must be signed for negative numbers
  minTime_ms = std::max((signed long)10, tmp);
  signed long tmp2 = static_cast<signed long>(match->GetActualTime_ms()) - 10;
  maxTime_ms = static_cast<unsigned long>(std::max(10L, tmp2));
  actualTime_ms = clamp(maxTime_ms - 3000, minTime_ms, maxTime_ms);
  replayCamCount = match->GetReplayCamCount();

  cam = 0;
  modifierValue = 0.0f;
  autoRun = false;
  slowMotion = false;
  stayInReplay = true;
  closeWhenAutorunCompletes = false;
  cinematic = false;
  cinematicPrompt = nullptr;

  // Under the scoreboard rather than across it: the PES bug runs to roughly
  // 40% of the width at the very top of frame (see scoreboard.cpp).
  Gui2Frame* header = new Gui2Frame(windowManager, "frame_replay_header", 36, 8.5f, 28, 5.5f, true);
  this->AddView(header);
  header->Show();
  replayChrome.push_back(header);
  Gui2Caption* title =
      new Gui2Caption(windowManager, "caption_replay_title", 2, 1.4f, 24, 2.6f, "INSTANT REPLAY");
  SuppressMatchHud(true);

  // The 4cc wipe over the cut into the replay. Full screen, on top of everything the
  // page draws, and hidden until it has a frame to show (replaywipe.hpp).
  wipe = new Gui2Image(windowManager, "image_replay_wipe", 0, 0, 100, 100);
  this->AddView(wipe);
  wipe->Hide();
  wipeDir = GetConfiguration()->Get("replay_wipe_dir", "media/textures/wipe/wepes");
  wipeTiming = ReplayWipe::Timing();
  wipeStarted_ms = 0;
  wipeFrameOnScreen = -1;
  wipeRunning = false;
  wipeClosing = false;
  const std::string sidecar = ReplayWipe::SidecarPath(wipeDir);
  if (!wipeDir.empty() && std::filesystem::exists(sidecar)) {
    std::ifstream file(sidecar);
    std::stringstream contents;
    contents << file.rdbuf();
    wipeTiming = ReplayWipe::Parse(contents.str());
  }
  StartWipe();
  header->AddView(title);
  title->Show();

  // Stops short of the radar in the bottom right corner (see match.cpp).
  Gui2Frame* footer = new Gui2Frame(windowManager, "frame_replay_footer", 21, 88.5f, 56, 9.5f, true);
  this->AddView(footer);
  footer->Show();
  replayChrome.push_back(footer);
  Gui2Caption* help = new Gui2Caption(
      windowManager, "caption_replay_help", 2, 1.6f, 52, 2.2f,
      "Left/Right: scrub | Up/Down: camera | Pass: change camera | Shoot: play/pause");
  footer->AddView(help);
  help->Show();

  timeLabel = new Gui2Caption(windowManager, "caption_replay_time", 2, 4.8f, 52, 2.8f, "");
  footer->AddView(timeLabel);
  timeLabel->Show();
  UpdateTimeLabel();
  // The cinematic offer, in the reference's own words and its own corner: a
  // clean picture with one small line low and left, nothing else over it.
  Gui2Frame* prompt =
      new Gui2Frame(windowManager, "frame_replay_prompt", 8, 91.0f, 34, 5.0f, true);
  this->AddView(prompt);
  Gui2Caption* promptText = new Gui2Caption(windowManager, "caption_replay_prompt", 1.5f, 1.2f,
                                            31, 2.4f, "Menu: skip   |   Pass: replay control");
  prompt->AddView(promptText);
  promptText->Show();
  prompt->Hide();
  cinematicPrompt = prompt;

  sig_OnClose.connect([this](...) { OnClose(); });

  // The cut into the replay waits for the wipe to cover the screen. It used to happen
  // right here, in the constructor, while the wipe's first frame had not been loaded
  // yet - so the first thing on screen was a frame of the replay and its chrome, and
  // the wipe arrived afterwards, covering nothing. Which is the opposite of a wipe.
  enteredReplay = false;
  leftReplay = false;
  ShowReplayChrome(false);
  if (!wipeTiming.valid) EnterReplay();   // no wipe imported: cut straight there
}

void ReplayPage::ShowReplayChrome(bool shown) {
  for (Gui2View* view : replayChrome) {
    if (!view) continue;
    if (shown)
      view->Show();
    else
      view->Hide();
  }
}

void ReplayPage::EnterReplay() {
  if (enteredReplay) return;
  enteredReplay = true;
  // A cinematic replay shows the picture and the offer, nothing else.
  ShowReplayChrome(!cinematic);
  if (cinematic && cinematicPrompt) cinematicPrompt->Show();
  match->SetAutoUpdateIngameCamera(false);

  match->replayState.Lock();
  match->replayState->viewTime_ms = actualTime_ms;
  match->replayState->cam = cam;
  match->replayState->modifierValue = 0.0f;
  match->replayState->dirty = true;
  match->replayState.Unlock();
}

void ReplayPage::TakeReplayControl() {
  if (!cinematic) return;
  cinematic = false;
  if (cinematicPrompt) cinematicPrompt->Hide();
  ShowReplayChrome(true);
  // It was going to close itself when playback ran out; now that it belongs to
  // the user it stays until he backs out of it.
  closeWhenAutorunCompletes = false;
}

void ReplayPage::LeaveReplay() {
  if (leftReplay) return;
  leftReplay = true;
  ShowReplayChrome(false);

  match->replayState.Lock();
  match->replayState->viewTime_ms = maxTime_ms;
  match->replayState->cam = cam;
  match->replayState->modifierValue = 0.0f;
  match->replayState->dirty = true;
  match->replayState.Unlock();

  GetScheduler()->ResetTaskSequenceTime("game");
  match->SetAutoUpdateIngameCamera(true);
  if (stayInReplay) match->Pause(false);
}

ReplayPage::~ReplayPage() { SuppressMatchHud(false); }

void ReplayPage::OnClose() {
  // Normally the outgoing wipe has already done this, under cover; a page closed any
  // other way (the user backing out) still has to hand the match back.
  LeaveReplay();
}

void ReplayPage::Autorun(int replayHistoryOffset_ms, bool stayInReplay, int camera) {
  autoRun = true;
  closeWhenAutorunCompletes = true;
  // The match asked for this one, so it is part of the cutscene: play it clean
  // and offer control rather than imposing the tape deck on it.
  cinematic = true;
  cam = clamp(camera, 0, replayCamCount - 1);
  modifierValue = 0.0;
  signed long tmp = maxTime_ms - replayHistoryOffset_ms;
  actualTime_ms = clamp(tmp, minTime_ms, maxTime_ms);
  this->stayInReplay = stayInReplay;
}

void ReplayPage::UpdateTimeLabel() {
  unsigned long replaySize_ms = maxTime_ms - minTime_ms;
  unsigned long elapsed_ms = actualTime_ms - minTime_ms;
  float positionPct = (replaySize_ms > 0) ? (elapsed_ms * 100.0f / replaySize_ms) : 0.0f;
  unsigned long secsAgo = (maxTime_ms - actualTime_ms) / 1000;
  std::string label = std::string(slowMotion ? "[0.5x] " : "") + int_to_str(elapsed_ms / 1000) +
                      "s / " + int_to_str(replaySize_ms / 1000) + "s  (" +
                      int_to_str(static_cast<int>(round(positionPct))) + "%)  -" +
                      int_to_str(secsAgo) + "s";
  timeLabel->SetCaption(label);
}

void ReplayPage::StartWipe() {
  if (!wipeTiming.valid) return;
  wipeStarted_ms = EnvironmentManager::GetInstance().GetTime_ms();
  wipeFrameOnScreen = -1;
  wipeRunning = true;
  // Put the first frame up now rather than on the next tick: a wipe that starts a
  // frame late is a frame of whatever it was meant to be hiding.
  RunWipe();
}

bool ReplayPage::RunWipe() {
  if (!wipeRunning) return true;
  const unsigned long now = EnvironmentManager::GetInstance().GetTime_ms();
  const unsigned long elapsed = now - wipeStarted_ms;
  const int frame = ReplayWipe::FrameAt(wipeTiming, elapsed);
  if (frame == ReplayWipe::kFinished) {
    wipeRunning = false;
    wipeFrameOnScreen = -1;
    if (wipe) wipe->Hide();
    return true;
  }
  if (frame != wipeFrameOnScreen && wipe) {
    wipeFrameOnScreen = frame;
    wipe->LoadImage(ReplayWipe::FramePath(wipeDir, frame));
    wipe->Show();
  }
  // The cut goes under full cover, which is what fadestart marks.
  return ReplayWipe::CutIsDue(wipeTiming, elapsed);
}

void ReplayPage::Process() {
  const bool covered = RunWipe();

  if (wipeClosing) {
    // On the way out the whole wipe plays, exactly as it does on the way in. The
    // match goes back to live play the moment the screen is covered, and the page
    // stays up - drawing nothing but the wipe - until the crest has swung away
    // again. Closing at the cut instead threw away five sixths of the animation and
    // snapped back to the pitch.
    if (covered) LeaveReplay();
    if (!wipeRunning) {
      wipeClosing = false;
      GoBack();
    }
    return;
  }

  // Going in, the cut waits for the cover in the same way.
  if (!enteredReplay) {
    if (covered) EnterReplay();
    return;
  }

  if (autoRun) {
    Vector3 direction;
    direction.coords[0] = slowMotion ? 0.25f : 0.5f;
    ProcessInput(direction, false, false, false);
  }
}

void ReplayPage::ProcessKeyboardEvent(KeyboardEvent* event) {
  const std::vector<IHIDevice*>& controllers = GetControllers();
  HIDKeyboard* keyboard = nullptr;
  for (IHIDevice* c : controllers) {
    if (c && c->GetDeviceType() == e_HIDeviceType_Keyboard) {
      keyboard = static_cast<HIDKeyboard*>(c);
      break;
    }
  }
  if (!keyboard) {
    return;
  }

  bool button1 = false;
  bool button2 = false;
  bool slowMo = false;
  if (event->GetKeyOnce(keyboard->GetFunctionMapping(e_ButtonFunction_ShortPass)))
    button1 = true;
  if (event->GetKeyOnce(keyboard->GetFunctionMapping(e_ButtonFunction_HighPass)))
    button2 = true;
  if (event->GetKeyContinuous(keyboard->GetFunctionMapping(e_ButtonFunction_Sprint)))
    slowMo = true;

  Vector3 direction;
  if (event->GetKeyContinuous(keyboard->GetFunctionMapping(e_ButtonFunction_Left)))
    direction.coords[0] += -0.5f;
  if (event->GetKeyContinuous(keyboard->GetFunctionMapping(e_ButtonFunction_Right)))
    direction.coords[0] += 0.5f;
  if (event->GetKeyContinuous(keyboard->GetFunctionMapping(e_ButtonFunction_Up)))
    direction.coords[1] += -0.5f;
  if (event->GetKeyContinuous(keyboard->GetFunctionMapping(e_ButtonFunction_Down)))
    direction.coords[1] += 0.5f;

  ProcessInput(direction, button1, button2, slowMo);
}

void ReplayPage::ProcessJoystickEvent(JoystickEvent* event) {
  int controllerID = 0;
  const std::vector<IHIDevice*>& controllers = GetControllers();

  // Find the gamepad driving Player 1. Do not assume the keyboard is at index 0
  // and a gamepad at index 1 - with no pad connected that cast would be OOB.
  HIDGamepad* gamepad = nullptr;
  for (IHIDevice* c : controllers) {
    if (c && c->GetDeviceType() == e_HIDeviceType_Gamepad) {
      gamepad = static_cast<HIDGamepad*>(c);
      break;
    }
  }
  if (!gamepad) {
    return;
  }

  bool button1 =
      event->GetButton(0, gamepad->GetControllerMapping(
                              gamepad->GetFunctionMapping(e_ButtonFunction_LongPass))) ||
      event->GetButton(0,
                       gamepad->GetControllerMapping(gamepad->GetFunctionMapping(
                           e_ButtonFunction_ShortPass)));  // need 2 options because maybe the first
                                                           // is set to gui's 'escape' function
  bool button2 =
      event->GetButton(0, gamepad->GetControllerMapping(
                              gamepad->GetFunctionMapping(e_ButtonFunction_HighPass))) ||
      event->GetButton(0, gamepad->GetControllerMapping(gamepad->GetFunctionMapping(
                              e_ButtonFunction_Shot)));  // need 2 options because maybe the first
                                                         // is set to gui's 'escape' function
  bool slowMo = event->GetButton(
      0, gamepad->GetControllerMapping(gamepad->GetFunctionMapping(e_ButtonFunction_Sprint)));

  Vector3 direction;
  direction.coords[0] = event->GetAxis(0, 0);
  direction.coords[1] = event->GetAxis(0, 1);

  float deadzone = 0.2f;
  if (fabs(direction.coords[0]) < deadzone) {
    direction.coords[0] = 0.0f;
  } else {
    direction.coords[0] =
        pow((fabs(direction.coords[0]) - deadzone) * (1.0f / (1.0f - deadzone)), 2.0f) *
        signSide(direction.coords[0]);
  }
  deadzone = 0.4f;
  if (fabs(direction.coords[1]) < deadzone) {
    direction.coords[1] = 0.0f;
  } else {
    direction.coords[1] =
        pow((fabs(direction.coords[1]) - deadzone) * (1.0f / (1.0f - deadzone)), 4.0f) *
        signSide(direction.coords[1]);
  }

  ProcessInput(direction, button1, button2, slowMo);
}

void ReplayPage::ProcessInput(const Vector3& direction, bool button1, bool button2,
                              bool slowMoInput) {
  // slow-motion: held sprint button halves playback speed
  slowMotion = slowMoInput;

  // While the replay is still cinematic the only thing the pass button does is
  // accept the offer. Returning here matters: the same press would otherwise
  // fall through and cycle the camera on the very frame control is handed over.
  if (cinematic) {
    if (button1) TakeReplayControl();
    return;
  }

  // autorun
  if (button2 && autoRun == false) {
    actualTime_ms = minTime_ms;
    autoRun = true;
    closeWhenAutorunCompletes = false;
  } else if (button2) {
    autoRun = false;
    closeWhenAutorunCompletes = false;
  }
  if (button1 && autoRun == true) {
    autoRun = false;
    closeWhenAutorunCompletes = false;
  } else if (button1) {
    cam++;
    if (cam == replayCamCount)
      cam = 0;
  }

  if (!autoRun) {
    modifierValue += direction.coords[1] * 0.05f;
  }

  if (cam == 2) {
    if (modifierValue < -1.0f)
      modifierValue += 2.0f;
    if (modifierValue > 1.0f)
      modifierValue -= 2.0f;
  } else {
    modifierValue = clamp(modifierValue, -1.0f, 1.0f);
  }

  float speedMultiplier = slowMotion ? 0.5f : 1.0f;
  float timeMovement = direction.coords[0] * 2.0f * speedMultiplier;
  actualTime_ms += int(round(timeMovement * 10.0f));

  if (autoRun && actualTime_ms >= (signed int)maxTime_ms) {
    autoRun = false;
    if (closeWhenAutorunCompletes) {
      closeWhenAutorunCompletes = false;
      // Wipe out of the replay the same way it wiped in: the whole animation, with
      // the cut back to live play hidden under its cover.
      if (wipeTiming.valid) {
        StartWipe();
        wipeClosing = true;
        return;
      }
      GoBack();
      return;
    }
  }

  actualTime_ms = clamp(actualTime_ms, minTime_ms, maxTime_ms);

  UpdateTimeLabel();

  unsigned long resultTime = actualTime_ms;

  // feed results to match - replays are effectively replayed there

  match->replayState.Lock();
  match->replayState->viewTime_ms = resultTime;
  match->replayState->cam = cam;
  match->replayState->modifierValue = modifierValue;
  match->replayState->dirty = true;
  match->replayState.Unlock();
}
