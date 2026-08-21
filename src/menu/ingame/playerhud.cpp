// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "playerhud.hpp"

#include <SDL2/SDL.h>

#include <vector>

#include "../../onthepitch/match.hpp"
#include "../../onthepitch/team.hpp"
#include "../../onthepitch/teamAIcontroller.hpp"
#include "../../onthepitch/teaminstructions.hpp"
#include "../../onthepitch/teamphilosophy.hpp"
#include "menu/ingame/formationgraphiclayout.hpp"
#include "menu/ingame/hudindicators.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

namespace {

// Proportions of the indicator's own box, following the broadcast layout:
// badge at the outer edge over the plate, plate, level box, dial.
const float kBadgeX = 0.00f, kBadgeW = 0.13f;
const float kPlateX = 0.09f, kPlateW = 0.71f;
const float kBoxX = 0.81f, kBoxW = 0.07f;
const float kDialX = 0.89f, kDialW = 0.11f;

const float kPlateTextInsetX = 0.05f;
const float kStaminaInsetX = 0.03f;
const float kStaminaW = 0.60f;  // of the plate, leaving room for the text
const float kStaminaY = 0.10f, kStaminaH = 0.09f;
const float kBoxY = 0.12f, kBoxH = 0.76f;
const float kBandH = 0.17f;

// A side nobody is playing is drawn back, as the broadcast draws it.
const float kMirroredTransparency = 0.35f;

}  // namespace

Gui2PlayerHUD::Gui2PlayerHUD(Gui2WindowManager* windowManager, const std::string& name,
                             float x_percent, float y_percent, float width_percent,
                             float height_percent, Match* match, int teamID, bool mirrored)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent),
      match(match),
      teamID(teamID),
      mirrored(mirrored) {
  const float w = width_percent;
  const float h = height_percent;

  // Child offsets are relative to this view; a mirrored side counts from the
  // other end so the badge stays outermost.
  auto atX = [&](float fraction, float widthFraction) {
    const float x = fraction * w;
    return mirrored ? w - x - widthFraction * w : x;
  };

  plate = new Gui2Image(windowManager, name + "_plate", atX(kPlateX, kPlateW), 0, kPlateW * w, h);
  plate->LoadImage("media/menu/hud_plate.png");
  this->AddView(plate);
  plate->Show();

  staminaTrack = new Gui2Image(windowManager, name + "_stamina_track",
                               atX(kPlateX + kStaminaInsetX, kPlateW * kStaminaW),
                               kStaminaY * h, kPlateW * kStaminaW * w, kStaminaH * h);
  staminaTrack->LoadImage("media/menu/hud_stamina_track.png");
  this->AddView(staminaTrack);
  staminaTrack->Show();

  staminaFullWidth = kPlateW * kStaminaW * w;
  staminaX = atX(kPlateX + kStaminaInsetX, kPlateW * kStaminaW);
  staminaY = kStaminaY * h;
  staminaH = kStaminaH * h;
  stamina = new Gui2Image(windowManager, name + "_stamina", staminaX, staminaY,
                          staminaFullWidth, staminaH);
  stamina->LoadImage("media/menu/hud_stamina.png");
  this->AddView(stamina);
  stamina->Show();

  plateText = new Gui2Caption(windowManager, name + "_text",
                              atX(kPlateX + kPlateTextInsetX, kPlateW - kPlateTextInsetX * 2.0f),
                              kStaminaY * h + kStaminaH * h, (kPlateW - kPlateTextInsetX * 2.0f) * w,
                              h * 0.62f, "");
  this->AddView(plateText);
  plateText->Show();

  // The advanced instructions in force, under the plate. Without this the seven of
  // them changed nothing on screen once the banner had faded.
  instructionsText = new Gui2Caption(
      windowManager, name + "_instructions",
      atX(kPlateX + kPlateTextInsetX, kPlateW - kPlateTextInsetX * 2.0f),
      kStaminaY * h + kStaminaH * h + h * 0.62f, (kPlateW - kPlateTextInsetX * 2.0f) * w,
      h * 0.52f, "");
  this->AddView(instructionsText);
  instructionsText->Show();

  levelBox = new Gui2Image(windowManager, name + "_levelbox", atX(kBoxX, kBoxW), kBoxY * h,
                           kBoxW * w, kBoxH * h);
  levelBox->LoadImage("media/menu/hud_level_box.png");
  this->AddView(levelBox);
  levelBox->Show();

  // The band travels the box's height less its own, and is drawn bottom-up.
  bandTravelY = (kBoxH - kBandH) * h;
  bandTopY = kBoxY * h;
  bandX = atX(kBoxX, kBoxW);
  levelBand = new Gui2Image(windowManager, name + "_levelband", bandX,
                            bandTopY + bandTravelY * 0.5f, kBoxW * w, kBandH * h);
  levelBand->LoadImage("media/menu/hud_level_band.png");
  this->AddView(levelBand);
  levelBand->Show();

  dial = new Gui2Image(windowManager, name + "_dial", atX(kDialX, kDialW), kBoxY * h, kDialW * w,
                       kBoxH * h);
  dial->LoadImage("media/menu/hud_dial_0.png");
  this->AddView(dial);
  dial->Show();

  // The badge goes on last so it sits over the plate's end, as the broadcast has
  // it. A team without a badge simply shows none.
  if (match) {
    const std::string logo = match->GetTeam(teamID)->GetTeamData()->GetLogoUrl();
    if (!logo.empty()) {
      badge = new Gui2Image(windowManager, name + "_badge", atX(kBadgeX, kBadgeW), 0, kBadgeW * w,
                            h);
      badge->LoadImage(logo);
      // At its own shape inside that box, centred in what is left: a crest is
      // square and the box is not, so filling it stretched every badge sideways.
      const float aspect = badge->GetSourceAspectRatio();
      if (aspect > 0.0f) {
        const float boxW = kBadgeW * w;
        // The aspect is in pixels; the box is in screen percentages, which are
        // not square, so it is converted before it is fitted.
        const float screenAspect =
            windowManager->GetWidthPercentForHeight(h, aspect) / (h > 0.0f ? h : 1.0f);
        float fitW = boxW, fitH = h;
        HudIndicators::FitKeepingAspect(boxW, h, screenAspect, &fitW, &fitH);
        badge->SetSize(fitW, fitH);
        badge->SetPosition(atX(kBadgeX, kBadgeW) + (boxW - fitW) * 0.5f, (h - fitH) * 0.5f);
      }
      this->AddView(badge);
      badge->Show();
    }
  }

  Refresh();
}

Gui2PlayerHUD::~Gui2PlayerHUD() {}

void Gui2PlayerHUD::Redraw() {}

void Gui2PlayerHUD::Refresh() {
  if (!match) return;
  Team* team = match->GetTeam(teamID);
  if (!team) return;

  // Whoever the viewer is watching on this side: the man the user has, or the
  // one on the ball when the side is run by the computer.
  Player* subject = nullptr;
  activePlayers.clear();
  team->GetActivePlayers(activePlayers);
  for (unsigned int i = 0; i < activePlayers.size(); i++) {
    if (team->IsHumanControlled(activePlayers.at(i)->GetID())) {
      subject = activePlayers.at(i);
      break;
    }
  }
  if (!subject) subject = team->GetDesignatedTeamPossessionPlayer();

  // The broadcast draws the side the viewer is not playing further back. That is
  // about who holds the pad, not which corner it is drawn in - in a two-player
  // match both sides are the viewer's.
  const int humanControlled = team->GetHumanGamerCount() > 0 ? 1 : 0;
  if (humanControlled != lastHumanControlled) {
    lastHumanControlled = humanControlled;
    plateText->SetTransparency(humanControlled ? 0.0f : kMirroredTransparency);
  }

  if (subject && subject->GetPlayerData()) {
    // The engine has no shirt numbers; the squad slot is what the lineup panel
    // shows, so the two agree.
    int squadNumber = 0;
    for (unsigned int i = 0; i < activePlayers.size(); i++) {
      if (activePlayers.at(i) == subject) {
        squadNumber = FormationGraphicLayout::SquadNumberForSlot(static_cast<int>(i));
        break;
      }
    }
    const std::string text = HudIndicators::PlateText(
        squadNumber, subject->GetPlayerData()->GetLastName(), !mirrored);
    if (text != lastPlateText) {
      lastPlateText = text;
      plateText->SetCaption(text);
    }

    const float fraction = HudIndicators::StaminaFraction(subject->GetFatigueFactorInv());
    if (fraction != lastStamina) {
      lastStamina = fraction;
      // A bar with no width at all would be a stray pixel rather than nothing.
      stamina->SetSize(staminaFullWidth * fraction, staminaH);
      if (mirrored) {
        // Mirrored, the bar empties from the other end, so its left edge moves.
        stamina->SetPosition(staminaX + staminaFullWidth * (1.0f - fraction), staminaY);
      }
    }
  }

  const TeamInstructions::State& instructions = team->GetController()->GetInstructions();
  const int mentality = static_cast<int>(instructions.mentality);
  if (mentality != lastMentality) {
    lastMentality = mentality;
    const float position =
        HudIndicators::LevelBandPosition(mentality, TeamInstructions::e_Mentality_Count);
    // Drawn bottom-up: the most defensive rung sits at the bottom of the box.
    levelBand->SetPosition(bandX, bandTopY + bandTravelY * (1.0f - position));
  }

  const std::string instructionLine = HudIndicators::InstructionsText(instructions.instructions);
  if (instructionLine != lastInstructions) {
    lastInstructions = instructionLine;
    instructionsText->SetCaption(instructionLine);
  }

  const int philosophy = static_cast<int>(TeamPhilosophy::Parse(
      team->GetTeamData()->GetTactics().userProperties.Get("philosophy", "balanced")));
  if (philosophy != lastPhilosophy) {
    lastPhilosophy = philosophy;
    dial->LoadImage("media/menu/hud_dial_" + int_to_str(philosophy) + ".png");
  }
}
