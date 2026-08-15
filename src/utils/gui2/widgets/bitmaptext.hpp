// Bitmap-font text widget for the GUI2 framework.
//
// Renders a string from a glyph atlas (PNG) described by a plain-text .fnt
// file, as exported by tools/pes21_import/export_scoreboard_theme.py:
//   atlas <filename>            (relative to the .fnt file)
//   line_height <px>
//   ascent <px>
//   glyph <codepoint> <x> <y> <w> <h> <bearing_x> <bearing_y> <advance>
// Lines starting with '#' are comments.
//
// The widget is a fixed box; the text row is scaled to the box height
// (shrunk further if it would overflow the width) and aligned within it.

#ifndef _HPP_GUI2_VIEW_BITMAPTEXT
#define _HPP_GUI2_VIEW_BITMAPTEXT

#include <map>

#include "../view.hpp"
#include "scene/objects/image2d.hpp"

namespace blunted {

class Gui2BitmapFont {
public:
  struct Glyph {
    int x = 0, y = 0, w = 0, h = 0;
    int bearingX = 0, bearingY = 0;
    int advance = 0;
  };

  // Returns a shared, cached font, or 0 when loading failed.
  static const Gui2BitmapFont* Fetch(const std::string& fntFilename);

  ~Gui2BitmapFont();

  int GetLineHeight() const { return lineHeight; }
  int GetAscent() const { return ascent; }
  int GetTextWidth(const std::string& text) const;
  const Glyph* GetGlyph(int codepoint) const;
  SDL_Surface* GetAtlas() const { return atlas; }

private:
  Gui2BitmapFont() {}
  bool Load(const std::string& fntFilename);

  SDL_Surface* atlas = nullptr;
  int lineHeight = 1;
  int ascent = 1;
  std::map<int, Glyph> glyphs;
};

class Gui2BitmapText : public Gui2View {
public:
  enum class Alignment { left, center, right };

  Gui2BitmapText(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                 float y_percent, float width_percent, float height_percent,
                 const std::string& fntFilename);
  virtual ~Gui2BitmapText();

  virtual void GetImages(std::vector<boost::intrusive_ptr<Image2D>>& target);
  virtual void Redraw();

  void SetText(const std::string& newText);
  void SetAlignment(Alignment newAlignment);
  // 0 == fully transparent, 1 == fully opaque (cross-fades: see
  // src/menu/ingame/formationgraphic.cpp).
  void SetAlpha(float alpha);

protected:
  const Gui2BitmapFont* font;
  std::string text;
  Alignment alignment = Alignment::center;

  boost::intrusive_ptr<Image2D> image;
};

}  // namespace blunted

#endif
