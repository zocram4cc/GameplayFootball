#include "bitmaptext.hpp"

#include <fstream>
#include <sstream>

#include "../windowmanager.hpp"
#include "base/log.hpp"
#include "base/sdl_surface.hpp"
#include "SDL2/SDL2_rotozoom.h"

namespace blunted {

// ----- Gui2BitmapFont -------------------------------------------------------

const Gui2BitmapFont* Gui2BitmapFont::Fetch(const std::string& fntFilename) {
  static std::map<std::string, Gui2BitmapFont*> cache;
  auto iter = cache.find(fntFilename);
  if (iter != cache.end())
    return iter->second;

  Gui2BitmapFont* font = new Gui2BitmapFont();
  if (!font->Load(fntFilename)) {
    delete font;
    font = nullptr;
  }
  cache[fntFilename] = font;  // negative results cached too
  return font;
}

Gui2BitmapFont::~Gui2BitmapFont() {
  if (atlas)
    SDL_FreeSurface(atlas);
}

bool Gui2BitmapFont::Load(const std::string& fntFilename) {
  std::ifstream file(fntFilename.c_str());
  if (!file.is_open()) {
    Log(e_Warning, "Gui2BitmapFont", "Load", "Could not open \"" + fntFilename + "\"");
    return false;
  }

  std::string atlasFilename;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream tokens(line);
    std::string key;
    tokens >> key;
    if (key == "atlas") {
      tokens >> atlasFilename;
    } else if (key == "line_height") {
      tokens >> lineHeight;
    } else if (key == "ascent") {
      tokens >> ascent;
    } else if (key == "glyph") {
      int codepoint = 0;
      Glyph glyph;
      tokens >> codepoint >> glyph.x >> glyph.y >> glyph.w >> glyph.h >> glyph.bearingX >>
          glyph.bearingY >> glyph.advance;
      if (!tokens.fail())
        glyphs[codepoint] = glyph;
    }
  }
  if (lineHeight < 1)
    lineHeight = 1;

  if (atlasFilename.empty() || glyphs.empty()) {
    Log(e_Warning, "Gui2BitmapFont", "Load", "Malformed font file \"" + fntFilename + "\"");
    return false;
  }

  std::string directory;
  std::string::size_type slash = fntFilename.find_last_of('/');
  if (slash != std::string::npos)
    directory = fntFilename.substr(0, slash + 1);
  const std::string atlasPath = directory + atlasFilename;
  atlas = IMG_Load(atlasPath.c_str());
  if (!atlas) {
    Log(e_Warning, "Gui2BitmapFont", "Load",
        "Failed to load atlas \"" + atlasPath + "\": " + IMG_GetError());
    return false;
  }
  // glyph rects are copied verbatim (alpha included) into the text row
  SDL_SetSurfaceBlendMode(atlas, SDL_BLENDMODE_NONE);
  return true;
}

const Gui2BitmapFont::Glyph* Gui2BitmapFont::GetGlyph(int codepoint) const {
  auto iter = glyphs.find(codepoint);
  if (iter == glyphs.end())
    return nullptr;
  return &iter->second;
}

int Gui2BitmapFont::GetTextWidth(const std::string& text) const {
  int width = 0;
  for (char c : text) {
    const Glyph* glyph = GetGlyph(static_cast<unsigned char>(c));
    if (glyph)
      width += glyph->advance;
  }
  return width;
}

// ----- Gui2BitmapText -------------------------------------------------------

Gui2BitmapText::Gui2BitmapText(Gui2WindowManager* windowManager, const std::string& name,
                               float x_percent, float y_percent, float width_percent,
                               float height_percent, const std::string& fntFilename)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent),
      font(Gui2BitmapFont::Fetch(fntFilename)) {
  int x, y, w, h;
  windowManager->GetCoordinates(x_percent, y_percent, width_percent, height_percent, x, y, w, h);
  image = windowManager->CreateImage2D(name, std::max(w, 1), std::max(h, 1), true);
}

Gui2BitmapText::~Gui2BitmapText() {}

void Gui2BitmapText::GetImages(std::vector<boost::intrusive_ptr<Image2D>>& target) {
  target.push_back(image);
  Gui2View::GetImages(target);
}

void Gui2BitmapText::SetText(const std::string& newText) {
  if (text == newText)
    return;
  text = newText;
  Redraw();
}

void Gui2BitmapText::SetAlignment(Alignment newAlignment) {
  if (alignment == newAlignment)
    return;
  alignment = newAlignment;
  Redraw();
}

void Gui2BitmapText::Redraw() {
  int x, y, w, h;
  windowManager->GetCoordinates(x_percent, y_percent, width_percent, height_percent, x, y, w, h);
  if (w <= 0 || h <= 0)
    return;

  SDL_Surface* box = CreateSDLSurface(w, h);  // transparent
  if (!box)
    return;

  const int rowWidth = font ? font->GetTextWidth(text) : 0;
  if (font && rowWidth > 0) {
    const int rowHeight = font->GetLineHeight() + 1;  // +1: descenders (e.g. '-'-derived dots)
    SDL_Surface* row = CreateSDLSurface(rowWidth, rowHeight);
    if (row) {
      int pen = 0;
      for (char c : text) {
        const Gui2BitmapFont::Glyph* glyph = font->GetGlyph(static_cast<unsigned char>(c));
        if (!glyph)
          continue;
        if (glyph->w > 0 && glyph->h > 0) {
          SDL_Rect src;
          src.x = glyph->x;
          src.y = glyph->y;
          src.w = glyph->w;
          src.h = glyph->h;
          SDL_Rect dst;
          dst.x = pen + glyph->bearingX;
          dst.y = font->GetAscent() - glyph->bearingY;
          SDL_BlitSurface(font->GetAtlas(), &src, row, &dst);
        }
        pen += glyph->advance;
      }

      double zoom = static_cast<double>(h) / rowHeight;
      if (rowWidth * zoom > w)
        zoom = static_cast<double>(w) / rowWidth;
      SDL_Surface* scaledRow = zoomSurface(row, zoom, zoom, SMOOTHING_ON);
      SDL_FreeSurface(row);
      if (scaledRow) {
        SDL_Rect dst;
        if (alignment == Alignment::left)
          dst.x = 0;
        else if (alignment == Alignment::right)
          dst.x = w - scaledRow->w;
        else
          dst.x = (w - scaledRow->w) / 2;
        dst.y = (h - scaledRow->h) / 2;
        SDL_SetSurfaceBlendMode(scaledRow, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(scaledRow, nullptr, box, &dst);
        SDL_FreeSurface(scaledRow);
      }
    }
  }

  boost::intrusive_ptr<Resource<Surface>> surfaceRes = image->GetImage();
  surfaceRes->resourceMutex.lock();
  surfaceRes->GetResource()->SetData(box);
  surfaceRes->resourceMutex.unlock();
  image->OnChange();
}

}  // namespace blunted
