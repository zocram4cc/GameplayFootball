// Pictograms for the icon bars PES puts under its cards - the Game Plan tabs,
// the half-time / pause menu, the result page (docs/VGL26_DAY12_REFERENCE.md
// §2, §3). Drawn into the button's own surface with lines and rectangles, so
// no art asset is needed and nothing is committed that PES owns.

#ifndef _HPP_MENU_INGAME_MENUICONS
#define _HPP_MENU_INGAME_MENUICONS

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

// PES's icon button: a light plate with the pictogram over the caption, the
// focused one tinted (the pause / half-time bar, the result page, the Game
// Plan tabs). The plate is a Gui2Button so focus and clicks work as they do
// everywhere; the icon and caption are laid over it as siblings on `page`,
// because a button repaints its own face on every focus change.
struct IconButton {
  blunted::Gui2Button* button = nullptr;
  blunted::Gui2Image* icon = nullptr;
  blunted::Gui2Caption* caption = nullptr;
};
IconButton MakeIconButton(blunted::Gui2WindowManager* windowManager, blunted::Gui2View* page,
                          const std::string& name, float x, float y, float w, float h, Icon icon,
                          const std::string& caption);

// A dark translucent slab with rounded corners behind a crest or a mark, so a
// white badge reads over the aerial and the pitch (PES puts every crest on
// one). Fills the whole image.
void PaintPlate(blunted::Gui2Image* image, const blunted::Vector3& colour, int alpha);

// A row of icon buttons centred on the screen at `y`, each `w` wide with a gap
// between: PES's action bar under a card. Arrow keys move along it, and
// wrapping is the caller's (they are plain buttons on the page).
struct BarItem {
  Icon icon;
  std::string caption;
};
std::vector<IconButton> MakeIconBar(blunted::Gui2WindowManager* windowManager,
                                    blunted::Gui2View* page, const std::string& name, float y,
                                    float w, float h, const std::vector<BarItem>& items);

}  // namespace MenuIcons

#endif
