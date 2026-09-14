// Pictograms for the icon bars PES puts under its cards - the Game Plan tabs,
// the half-time / pause menu, the result page (docs/VGL26_DAY12_REFERENCE.md
// §2, §3). Drawn into the button's own surface with lines and rectangles, so
// no art asset is needed and nothing is committed that PES owns.

#ifndef _HPP_MENU_INGAME_MENUICONS
#define _HPP_MENU_INGAME_MENUICONS

#include "base/math/vector3.hpp"
#include "utils/gui2/widgets/image.hpp"

#include <string>
#include <vector>

namespace blunted {
class Gui2Button;
class Gui2Caption;
class Gui2View;
class Gui2WindowManager;
}

namespace MenuIcons {

enum class Icon {
  TeamSheet,    // a pitch with a marked spot: Team Sheet / Edit Position
  Tactics,      // paired up/down arrows: Tactics
  Boot,         // a boot: individual instructions
  Gear,         // a cog: Support Settings
  Folder,       // a folder: Data Management / Save
  Play,         // a play triangle: Highlights / Begin Second Half
  Records,      // a card with lines: Match Records
  Ball,         // a ball: Rematch / the match itself
  Shield,       // a shield: Select Team
  Back,         // a left arrow: Top Menu / Back
  GamePlan,     // a pitch outline: Game Plan
  Camera,       // camera settings
  Substitute,   // two arrows crossing: substitutions
};

// Paints `icon` into the image, centred, in `colour`, over a `plate` fill.
void Paint(blunted::Gui2Image* image, Icon icon, const blunted::Vector3& colour,
           const blunted::Vector3& plate, int plateAlpha);

// One geometry for every bar and card, so pause, half time, the result page
// and the Game Plan tabs are uniform by construction rather than by four
// hand-tuned copies (owner's review: "those black boxes are uneven").
constexpr float kBarItemW = 10.0f;     // plate width, % of screen
constexpr float kBarItemH = 10.0f;     // plate height
constexpr float kBarGap = 0.8f;        // gutter between plates
constexpr float kCardToBarGap = 2.5f;  // card bottom to bar top
constexpr float kHintH = 2.0f;         // the hint line under a bar
const blunted::Vector3 kAccent(120, 232, 224);      // PES's teal: focused icon+label, titles
const blunted::Vector3 kIconColour(235, 240, 250);  // resting icon
const blunted::Vector3 kLabelColour(186, 200, 224); // resting caption
const blunted::Vector3 kPlateColour(18, 22, 34);    // crest plates and card backing
const blunted::Vector3 kOutlineColour(6, 10, 24);

// PES's icon button: a plate with the pictogram over the caption. Focus keeps
// the plate and tints icon and caption to the accent, as PES does; the plate
// never changes colour. The plate is a Gui2Button so focus and clicks work as
// they do everywhere; the icon and caption are laid over it as siblings on
// `page`, because a button repaints its own face on every focus change.
struct IconButton {
  blunted::Gui2Button* button = nullptr;
  blunted::Gui2Image* icon = nullptr;
  blunted::Gui2Caption* caption = nullptr;
};
IconButton MakeIconButton(blunted::Gui2WindowManager* windowManager, blunted::Gui2View* page,
                          const std::string& name, float x, float y, float w, float h, Icon icon,
                          const std::string& caption);

// Paints a pictogram into an image the way the bar buttons do - the glyph in
// the same band and at the same stroke - so a tab face and a bar face match.
void PaintIconFace(blunted::Gui2WindowManager* windowManager, blunted::Gui2View* page,
                   const std::string& name, float x, float y, float w, float h, Icon icon,
                   blunted::Gui2Image** out);

// A dark translucent slab with rounded corners behind a crest or a mark, so a
// white badge reads over the aerial and the pitch (PES puts every crest on
// one). Fills the whole image.
void PaintPlate(blunted::Gui2Image* image, const blunted::Vector3& colour, int alpha);

// A crest on a plate with its label inside, under the crest: the opening
// graphic, the loading page. `centreX` is the plate's centre; `crestH` sizes
// the crest, the plate fits round it.
struct CrestPlate {
  blunted::Gui2Image* plate = nullptr;
  blunted::Gui2Image* crest = nullptr;
  blunted::Gui2Caption* label = nullptr;
  float height = 0.0f;  // plate height, so a neighbour can align to its middle
};
CrestPlate MakeCrestPlate(blunted::Gui2WindowManager* windowManager, blunted::Gui2View* page,
                          const std::string& name, float centreX, float top, float crestH,
                          const std::string& logo, const std::string& label);

// A row of icon buttons at kBarItemW x kBarItemH with kBarGap between, centred
// on `centreX` at `y`: PES's action bar under a card. Arrow keys move along
// it (they are plain buttons on the page).
struct BarItem {
  Icon icon;
  std::string caption;
};
std::vector<IconButton> MakeIconBar(blunted::Gui2WindowManager* windowManager,
                                    blunted::Gui2View* page, const std::string& name, float y,
                                    const std::vector<BarItem>& items, float centreX = 50.0f,
                                    size_t maxPerRow = 0);

// The hint line under a bar: one place, one size, one casing.
blunted::Gui2Caption* MakeHintLine(blunted::Gui2WindowManager* windowManager,
                                   blunted::Gui2View* page, const std::string& name, float y,
                                   const std::string& text);

// "PAUSE - 37:30 - 0-1 - MATCH STATS": the break titles, built one way.
std::string BreakTitle(const std::string& phase, const std::string& clock, int home, int away,
                       const std::string& page = "");
std::string Clock(unsigned long time_ms);

}  // namespace MenuIcons

#endif
