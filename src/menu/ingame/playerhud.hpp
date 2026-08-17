// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

// The in-play player indicator, one per side.
//
// PES's, measured off the VGL26 broadcast (docs/VGL26_REFERENCE.md), from the
// outer edge inwards: the team badge, a dark rounded plate carrying the shirt
// number and the name, a thin green stamina bar along the plate's top edge, a
// small vertical box whose white band gives the attack/defence level, and a
// two-tone circular dial for the tactical style. The away side mirrors it and is
// drawn dimmer.
//
// What each part says is decided in hudindicators.hpp; this puts the pieces on
// the screen and keeps them in step with the match.

#ifndef _HPP_GUI2_VIEW_PLAYERHUD
#define _HPP_GUI2_VIEW_PLAYERHUD

#include <string>

#include "scene/objects/image2d.hpp"
#include "utils/gui2/view.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"

using namespace blunted;

class Match;

class Gui2PlayerHUD : public Gui2View {
public:
  Gui2PlayerHUD(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                float y_percent, float width_percent, float height_percent, Match* match,
                int teamID, bool mirrored);
  virtual ~Gui2PlayerHUD();

  virtual void Redraw();

  // Reads the match and moves only what changed.
  void Refresh();

protected:
  // No image of its own: this is a container for the pieces below. The stub this
  // replaced pushed its (never assigned) image member into GetImages, which was
  // harmless only because nothing ever instantiated it - once it was on screen,
  // SetRecursiveZPriority walked into the null pointer.
  Match* match;
  int teamID;
  // The away side reads right-to-left, badge outermost, like the broadcast.
  bool mirrored;

  Gui2Image* plate = nullptr;
  Gui2Image* badge = nullptr;
  Gui2Image* staminaTrack = nullptr;
  Gui2Image* stamina = nullptr;
  Gui2Image* levelBox = nullptr;
  Gui2Image* levelBand = nullptr;
  Gui2Image* dial = nullptr;
  Gui2Caption* plateText = nullptr;

  // Geometry, in this view's percentages, worked out once in the constructor.
  float staminaFullWidth = 0.0f;
  float staminaX = 0.0f;
  float staminaY = 0.0f;
  float staminaH = 0.0f;
  float bandX = 0.0f;
  float bandTravelY = 0.0f;
  float bandTopY = 0.0f;

  // Only touch a widget when its value actually moves; these are HUD elements
  // redrawn every frame otherwise.
  std::string lastPlateText;
  int lastHumanControlled = -1;
  int lastMentality = -1;
  int lastPhilosophy = -1;
  float lastStamina = -1.0f;
};

#endif
