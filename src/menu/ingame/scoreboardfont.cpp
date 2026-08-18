#include "scoreboardfont.hpp"

#include "main.hpp"

namespace ScoreboardFont {

std::string Directory() {
  return GetConfiguration()->Get("scoreboard_font_dir", "media/ui/scoreboard");
}

std::string Path(const std::string& name) { return Directory() + "/" + name + ".fnt"; }

}  // namespace ScoreboardFont
