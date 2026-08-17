// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not
// be used for anything important. i do not offer support, so don't ask. to be used for inspiration
// :)

#include "caption.hpp"

#include <mutex>

#include "utils/gui2/fontlock.hpp"

#include <cmath>

#include "../surfacescale.hpp"
#include "../windowmanager.hpp"
#include "SDL2/SDL2_rotozoom.h"

namespace blunted {

Gui2Caption::Gui2Caption(Gui2WindowManager* windowManager, const std::string& name, float x_percent,
                         float y_percent, float width_percent, float height_percent,
                         const std::string& caption)
    : Gui2View(windowManager, name, x_percent, y_percent, width_percent, height_percent) {
  renderedTextHeightPix = 0;
  transparency = 0.0f;

  int x, y, w, h;
  windowManager->GetCoordinates(x_percent, y_percent, width_percent, height_percent, x, y, w, h);
  image = windowManager->CreateImage2D(name, w, h, true);

  color = windowManager->GetStyle()->GetColor(e_DecorationType_Bright1);
  outlineColor = windowManager->GetStyle()->GetColor(e_DecorationType_Dark1);

  SetCaption(caption);
}

Gui2Caption::~Gui2Caption() {}

void Gui2Caption::GetImages(std::vector<boost::intrusive_ptr<Image2D>>& target) {
  target.push_back(image);
  Gui2View::GetImages(target);
}

void Gui2Caption::SetColor(const Vector3& color) {
  if (color != this->color) {
    this->color = color;
    Redraw();
  }
}

void Gui2Caption::SetOutlineColor(const Vector3& outlineColor) {
  if (outlineColor != this->outlineColor) {
    this->outlineColor = outlineColor;
    Redraw();
  }
}

void Gui2Caption::SetTransparency(float transparency) {
  if (transparency != this->transparency) {
    this->transparency = transparency;
    Redraw();
  }
}

void Gui2Caption::Redraw() {
  int x, y, w, h;
  windowManager->GetCoordinates(x_percent, y_percent, width_percent, height_percent, x, y, w, h);
  int x_margin = 0;
  int y_margin = 0;
  int outlineWidth =
      TTF_GetFontOutline(windowManager->GetStyle()->GetFont(e_TextType_DefaultOutline));

  Vector3 textColor = color;
  SDL_Color textColorSDL = {Uint8(textColor.coords[0]), Uint8(textColor.coords[1]),
                            Uint8(textColor.coords[2]), 255};
  Vector3 textOutlineColor = outlineColor;
  SDL_Color textOutlineColorSDL = {Uint8(textOutlineColor.coords[0]),
                                   Uint8(textOutlineColor.coords[1]),
                                   Uint8(textOutlineColor.coords[2]), 255};

  // A TTF_Font carries a mutable glyph cache, so two threads rendering from the
  // same font corrupt it. The engine hands each sequence's phases to a shared
  // worker pool, so this is reachable from more than one thread at a time - see
  // utils/gui2/fontlock.hpp.
  SDL_Surface* textSurfTmp = nullptr;
  SDL_Surface* textOutlineSurfTmp = nullptr;
  int resW = 0, resH = 0;
  {
    std::lock_guard<std::mutex> fontLock(FontMutex());
    textSurfTmp = TTF_RenderUTF8_Blended(
        windowManager->GetStyle()->GetFont(e_TextType_Caption), caption.c_str(), textColorSDL);
    textOutlineSurfTmp =
        TTF_RenderUTF8_Blended(windowManager->GetStyle()->GetFont(e_TextType_DefaultOutline),
                               caption.c_str(), textOutlineColorSDL);
    TTF_SizeUTF8(windowManager->GetStyle()->GetFont(e_TextType_DefaultOutline), caption.c_str(),
                 &resW, &resH);
  }

  // A render can fail - it did, and the null went straight into ->h below.
  if (!textOutlineSurfTmp || !textSurfTmp) {
    if (textSurfTmp) SDL_FreeSurface(textSurfTmp);
    if (textOutlineSurfTmp) SDL_FreeSurface(textOutlineSurfTmp);
    Log(e_Warning, "Gui2Caption", "Redraw", "could not render \"" + caption + "\"");
    return;
  }

  float zoomy;
  renderedTextHeightPix = (float)textOutlineSurfTmp->h;
  zoomy = (float)(h - y_margin * 2) / renderedTextHeightPix;

  // Compose the fill onto the outline at the size they were rendered at, then
  // scale once.
  //
  // Scaling the two layers separately gave them different effective factors -
  // an outline 43 px tall going to 22 is 0.5116, the fill's 39 to 20 is 0.5128 -
  // so at overlay sizes they stopped registering, and the outline showed through
  // where the fill should be. Letters with fine horizontal detail lost it first:
  // an E, three thin bars and two gaps inside twenty pixels, came out a solid
  // dark block, which is what "the E looks cancelled, black on black" was.
  SDL_Surface* composed = CreateSDLSurface(textOutlineSurfTmp->w, textOutlineSurfTmp->h);
  if (composed) {
    SDL_Rect at;
    at.x = 0;
    at.y = 0;
    at.w = 10000;
    at.h = 10000;
    SDL_BlitSurface(textOutlineSurfTmp, nullptr, composed, &at);
    // the fill sits exactly outlineWidth inside the outline, in unscaled pixels
    at.x = outlineWidth;
    at.y = outlineWidth;
    at.w = 10000;
    at.h = 10000;
    SDL_BlitSurface(textSurfTmp, nullptr, composed, &at);
  }
  // Shrinking past half size needs an area average, not zoomSurface's 2x2 tap:
  // beyond that, whole source rows fall between the taps and are never read. The
  // fonts are opened at 32 pt with a 2 px outline, so a glyph arrives about 44 px
  // tall; anything asking for a line under about 22 px - the in-match
  // notification strip asks for 19 - lost the thin white fill between its two
  // dark outline edges and came out grey on black. Same defect as the scoreboard
  // clock reading 65:00 at 45:00 (see utils/gui2/surfacescale.hpp).
  SDL_Surface* toScale = composed ? composed : textOutlineSurfTmp;
  SDL_Surface* textOutlineSurf = nullptr;
  const int scaledW = (int)(toScale->w * zoomy + 0.5f);
  const int scaledH = (int)(toScale->h * zoomy + 0.5f);
  if (zoomy < 1.0f && scaledW >= 1 && scaledH >= 1) {
    textOutlineSurf = CreateSDLSurface(scaledW, scaledH);
    if (textOutlineSurf &&
        !DownscaleAverageRGBA(static_cast<const unsigned char*>(toScale->pixels), toScale->w,
                              toScale->h, toScale->pitch,
                              static_cast<unsigned char*>(textOutlineSurf->pixels),
                              textOutlineSurf->w, textOutlineSurf->h, textOutlineSurf->pitch)) {
      SDL_FreeSurface(textOutlineSurf);
      textOutlineSurf = nullptr;
    }
  }
  if (!textOutlineSurf) textOutlineSurf = zoomSurface(toScale, zoomy, zoomy, 1);
  SDL_Surface* textSurf = nullptr;
  if (composed) SDL_FreeSurface(composed);
  SDL_FreeSurface(textOutlineSurfTmp);
  SDL_FreeSurface(textSurfTmp);

  textWidth_percent = windowManager->GetWidthPercent(resW * zoomy);

  image->DrawRectangle(0, 0, w, h, Vector3(0, 0, 0), 0);

  if (resW * zoomy > int(image->GetSize().coords[0]) ||
      resH * zoomy > int(image->GetSize().coords[1])) {  // todo: also when smaller, maybe?
    image->Resize(resW * zoomy, resH * zoomy);
    // printf("RESIZING\n");
    width_percent = windowManager->GetWidthPercent(resW * zoomy);
    height_percent = windowManager->GetHeightPercent(resH * zoomy);
  }

  boost::intrusive_ptr<Resource<Surface>> surfaceRes = image->GetImage();
  surfaceRes->resourceMutex.lock();
  SDL_Surface* surface = surfaceRes->GetResource()->GetData();

  Uint32 color32 = SDL_MapRGBA(surface->format, 0, 0, 0, 0);
  sdl_rectangle_filled(surface, 0, 0, surface->w, surface->h, color32);
  // printf("%i %i - %i %i - %i %i\n", surface->w, surface->h, textOutlineSurf->w,
  // textOutlineSurf->h, textSurf->w, textSurf->h);

  SDL_Rect dstRect;
  dstRect.x = 0;
  dstRect.y = 0;
  dstRect.w = 10000;
  dstRect.h = 10000;
  // One layer now: outline and fill are already composed and in register.
  SDL_BlitSurface(textOutlineSurf, nullptr, surface, &dstRect);
  if (transparency > 0.0f) {
    sdl_setsurfacealpha(surface, (1.0f - transparency) * 255);
  }
  surfaceRes->resourceMutex.unlock();

  SDL_FreeSurface(textOutlineSurf);
  if (textSurf) SDL_FreeSurface(textSurf);

  image->OnChange();
}

void Gui2Caption::SetCaption(const std::string& newCaption) {
  std::string adaptedCaption = newCaption;
  if (adaptedCaption.empty())
    adaptedCaption = " ";
  if (caption != adaptedCaption) {
    caption = adaptedCaption;
    std::transform(caption.begin(), caption.end(), caption.begin(), ::toupper);
    Redraw();
  }
}

float Gui2Caption::GetTextWidthPercent(int subStrLength) {
  int x, y, w, h;
  windowManager->GetCoordinates(x_percent, y_percent, width_percent, height_percent, x, y, w, h);

  int resW, resH;
  TTF_SizeUTF8(windowManager->GetStyle()->GetFont(e_TextType_DefaultOutline),
               caption.substr(0, subStrLength).c_str(), &resW, &resH);

  float zoomy;
  zoomy = (float)h / (float)renderedTextHeightPix;

  return windowManager->GetWidthPercent(resW * zoomy);
}

}  // namespace blunted
