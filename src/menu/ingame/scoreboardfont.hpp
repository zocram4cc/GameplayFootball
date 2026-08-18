// Where the broadcast numerals come from.
//
// The scoreboard, the added-time chip and the formation panel all draw their
// digits from a bitmap font - a PNG atlas plus a text metric file. The only one
// that existed was exported from PES 2021's own UI, which is Konami's artwork and
// does not belong in this repository, so the shipped default is built instead
// from Fira Sans Condensed ExtraBold (SIL OFL) by
// tools/art/make_scoreboard_font.py: tall, condensed and heavy, which is what a
// broadcast numeral is.
//
// "scoreboard_font_dir" points somewhere else for anyone who has exported PES's
// own and wants the exact glyphs ("media/ui/pes").

#ifndef _HPP_MENU_INGAME_SCOREBOARDFONT
#define _HPP_MENU_INGAME_SCOREBOARDFONT

#include <string>

namespace ScoreboardFont {

// The directory holding num_mid.fnt and num_match.fnt.
std::string Directory();

// <Directory()>/<name>.fnt
std::string Path(const std::string& name);

}  // namespace ScoreboardFont

#endif
