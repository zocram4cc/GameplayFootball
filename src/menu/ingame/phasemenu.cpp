// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "phasemenu.hpp"

#include <algorithm>

#include "../gameplan.hpp"
#include "../pagefactory.hpp"
#include "main.hpp"
#include "../../remotecontrolmode.hpp"
#include "utils/localization.hpp"

using namespace blunted;

namespace {

// How long a self-driving match holds on the half-time card. Half a second was
// not enough to draw it: building the card - two crest decodes, the stat rows,
// the heatmap - took longer than that, so the first Process() continued the
// match before a single frame of it had been presented, and every recorded
// showcase went from the walk-off straight to the second-half kickoff. Four
// seconds is long enough for a viewer to read the score and the possession.
constexpr unsigned long kMenuSmokeAdvanceDelay_ms = 4000;

bool MenuSmokeFullMatchEnabled() {
  return GetConfiguration()->GetBool("menu_smoke_test_full_match", false);
}

const char* PhaseName(e_MatchPhase phase) {
  switch (phase) {
    case e_MatchPhase_2ndHalf:
      return "second half";
    case e_MatchPhase_1stExtraTime:
      return "first extra time";
    case e_MatchPhase_2ndExtraTime:
      return "second extra time";
    case e_MatchPhase_Penalties:
      return "penalties";
    default:
      return "next phase";
  }
}

}  // namespace

MatchPhasePage::MatchPhasePage(Gui2WindowManager* windowManager, const Gui2PageData& pageData)
    : Gui2Page(windowManager, pageData),
      pageCreatedTime_ms(EnvironmentManager::GetInstance().GetTime_ms()),
      autoAdvanceTriggered(false) {
  Match* match = GetGameTask()->GetMatch();
  match->Pause(true);
  // This page takes the screen; the scoreboard, the radar and the player plates
  // have nothing to add over a half-time card, and the name over the man on the
  // ball stops floating through it.
  match->SuppressHud(true);

  nextPhase = (e_MatchPhase)pageData.properties->GetInt("nextphase");
  Localization& text = Localization::GetInstance();
  std::string phaseName;
  if (nextPhase == e_MatchPhase_2ndHalf)
    phaseName = text.Translate("phase_2nd_half");
  else if (nextPhase == e_MatchPhase_1stExtraTime)
    phaseName = text.Translate("phase_1st_extra_time");
  else if (nextPhase == e_MatchPhase_2ndExtraTime)
    phaseName = text.Translate("phase_2nd_extra_time");
  else if (nextPhase == e_MatchPhase_Penalties)
    phaseName = text.Translate("phase_penalties");

  // PES's half-time screen (docs/PRESENTATION_SPEC.md section 3.4): the two
  // crests and tags over a stat table, on a card over the dimmed stadium, and
  // the actions along the bottom. The card is the one TAB pulls up during play,
  // under this break's own title; what used to be here was an empty dark slab
  // with two menu items in its top-left corner.
  card = new Gui2StatsOverlay(windowManager, match, "phase_card");
  this->AddView(card);
  // The reference puts the score on this screen above everything else; the
  // header is where the eye lands, so it carries both the break and the score.
  const std::string breakName =
      nextPhase == e_MatchPhase_2ndHalf
          ? text.Translate("ingame_halftime")
          : (phaseName.empty() ? text.Translate("phase_match_phase") : phaseName);
  card->SetTitle(breakName + "   " + int_to_str(match->GetMatchData()->GetGoalCount(0)) + " - " +
                 int_to_str(match->GetMatchData()->GetGoalCount(1)));
  card->UpdateStats();
  card->Show();

  // The action bar sits in the band under the card. Two actions, the way the
  // reference's bottom bar reads ("Kick Off / >Game Plan"), on the same dark
  // rounded panel the tactical banner uses.
  float cardX, cardY, cardW, cardH;
  card->GetPosition(cardX, cardY);
  card->GetSize(cardW, cardH);
  const float barHeight = 5.5f;
  const float barY = std::min(cardY + cardH + 1.5f, 100.0f - barHeight - 1.5f);
  const float barWidth = cardW * 0.7f;
  const float barX = (100.0f - barWidth) * 0.5f;

  Gui2Image* bar = new Gui2Image(windowManager, "phase_actionbar", barX, barY, barWidth, barHeight);
  this->AddView(bar);
  bar->LoadImage("media/ui/pes/banner_panel.png");
  bar->Show();

  const std::string phaseLabel = text.Translate("phase_begin") + " " + phaseName;
  const float buttonWidth = barWidth * 0.46f;
  const float buttonHeight = barHeight * 0.64f;
  const float buttonY = barY + (barHeight - buttonHeight) * 0.5f;
  const float gap = barWidth * 0.04f;
  buttonNext = new Gui2Button(windowManager, "button_next", barX + gap, buttonY, buttonWidth,
                              buttonHeight, phaseLabel);
  Gui2Button* button1 =
      new Gui2Button(windowManager, "button1", barX + barWidth - gap - buttonWidth, buttonY,
                     buttonWidth, buttonHeight, text.Translate("phase_game_plan"));
  this->AddView(buttonNext);
  this->AddView(button1);
  buttonNext->Show();
  button1->Show();

  buttonNext->sig_OnClick.connect([this](...) { ContinueGame(); });
  button1->sig_OnClick.connect([this](...) { GoGamePlan(); });
  buttonNext->SetFocus();

  this->Show();
}

MatchPhasePage::~MatchPhasePage() {}

namespace {

// In remote-control mode the half-time break and the break before extra time
// hold for the panel's tactical changes; the streamer releases them from the
// web UI (resume) or right here through the normal continue button.
bool RemoteHoldsPhase(e_MatchPhase nextPhase) {
  return RemoteControlMode::IsActive() &&
         (nextPhase == e_MatchPhase_2ndHalf || nextPhase == e_MatchPhase_1stExtraTime);
}

}  // namespace

void MatchPhasePage::Process() {
  Gui2Page::Process();

  if (RemoteHoldsPhase(nextPhase)) {
    if (!RemoteControlMode::IsHolding()) {
      RemoteControlMode::SetHolding(true);
      printf("[remote-control] holding before %s until the streamer resumes\n",
             PhaseName(nextPhase));
    }
    if (RemoteControlMode::ConsumeResumeRequest()) {
      printf("[remote-control] hold released from the panel\n");
      ContinueGame();
    }
    return;
  }

  if (!autoAdvanceTriggered && MenuSmokeFullMatchEnabled() &&
      EnvironmentManager::GetInstance().GetTime_ms() >=
          pageCreatedTime_ms + kMenuSmokeAdvanceDelay_ms) {
    autoAdvanceTriggered = true;
    printf("[menu-smoke] Continuing %s automatically\n", PhaseName(nextPhase));
    ContinueGame();
  }
}

void MatchPhasePage::GoGamePlan() {
  Properties properties;
  // properties.SetInt("teamID", );
  CreatePage(e_PageID_GamePlan, properties);
}

void MatchPhasePage::ContinueGame() {
  // However the hold ends - panel resume or the streamer clicking through the
  // game's own menu - it is over.
  if (RemoteControlMode::IsHolding()) {
    RemoteControlMode::SetHolding(false);
    RemoteControlMode::ConsumeResumeRequest();
  }
  GetMenuTask()->ReleaseAllButtons();
  GetGameTask()->GetMatch()->SuppressHud(false);
  GetGameTask()->GetMatch()->Pause(false);
  GoBack();  // back to gamepage
}

void MatchPhasePage::ProcessWindowingEvent(WindowingEvent* event) {
  if (event->IsEscape()) {
    ContinueGame();
    event->Ignore();
  } else {
    Gui2Page::ProcessWindowingEvent(event);
  }
}
