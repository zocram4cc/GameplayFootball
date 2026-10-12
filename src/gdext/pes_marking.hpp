#pragma once

#include <godot_cpp/classes/image.hpp>
#include <string>
#include <vector>

#include "pes/marking.hpp"
#include "pes/uniform.hpp"
#include "pes/vfs.hpp"

namespace godot {

// What the compositor needs from the install, loaded once per library.
struct MarkingFonts {
  std::vector<pes::NameGlyph> glyphs;
  pes::NameFontMap nameFont;
};

// The per-player 1600x256 marking texture (name, back number, chest number)
// and 128x128 leg-number texture (docs/re/pes-re-markings.md 2-3). Null when
// the kit has no such strips or the number/name draws nothing.
Ref<Image> ComposeMarking(const pes::PesVfs& vfs, const pes::KitConfig& kit, const MarkingFonts& fonts, int number,
                          const std::string& name, float* nameWidthSum);
Ref<Image> ComposeLegNumber(const pes::PesVfs& vfs, const pes::KitConfig& kit, int number);

}  // namespace godot
