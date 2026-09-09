#include "menuicons.hpp"

#include <cmath>

#include "scene/objects/image2d.hpp"
#include "utils/gui2/widgets/button.hpp"
#include "utils/gui2/widgets/caption.hpp"
#include "utils/gui2/widgets/image.hpp"
#include "utils/gui2/windowmanager.hpp"

using namespace blunted;

namespace MenuIcons {

namespace {

struct Canvas {
  Image2D* image;
  int w, h;
  Vector3 colour;
  // Everything is drawn in a unit square (0..1) mapped to the inner 60% of the
  // surface, so the pictograms read at the same weight whatever the button size.
  int X(float u) const { return (int)std::lround(w * (0.2f + 0.6f * u)); }
  int Y(float v) const { return (int)std::lround(h * (0.2f + 0.6f * v)); }
  int Stroke() const { return std::max(1, (int)std::lround(std::min(w, h) * 0.05f)); }

  void Seg(float x0, float y0, float x1, float y1) const {
    // A thick line as a run of short rectangles, so it reads at any size.
    const int steps = std::max(2, (int)std::hypot((x1 - x0) * w, (y1 - y0) * h) / 2);
    const int s = Stroke();
    for (int i = 0; i <= steps; i++) {
      const float t = i / (float)steps;
      image->DrawRectangle(X(x0 + (x1 - x0) * t) - s / 2, Y(y0 + (y1 - y0) * t) - s / 2, s, s,
                           colour, 255);
    }
  }
  void Box(float x, float y, float bw, float bh) const {
    Seg(x, y, x + bw, y);
    Seg(x + bw, y, x + bw, y + bh);
    Seg(x + bw, y + bh, x, y + bh);
    Seg(x, y + bh, x, y);
  }
  void Fill(float x, float y, float bw, float bh) const {
    image->DrawRectangle(X(x), Y(y), X(x + bw) - X(x), Y(y + bh) - Y(y), colour, 255);
  }
  void Circle(float cx, float cy, float r, int n = 28) const {
    for (int i = 0; i < n; i++) {
      const float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
      Seg(cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1),
          cy + r * std::sin(a1));
    }
  }
  void Triangle(float x0, float y0, float x1, float y1, float x2, float y2) const {
    Seg(x0, y0, x1, y1);
    Seg(x1, y1, x2, y2);
    Seg(x2, y2, x0, y0);
    // Filled by horizontal scanlines between the left edge (x0,y0)-(x1,y1) and
    // the two edges meeting at the apex (x2,y2).
    for (int py = std::min(Y(y0), Y(y1)); py <= std::max(Y(y0), Y(y1)); py++) {
      const float v = (py - Y(0.0f)) / (float)(Y(1.0f) - Y(0.0f));
      const float span = v <= y2 ? (x2 - x0) * (v - y0) / (y2 - y0 + 1e-6f)
                                 : (x2 - x1) * (y1 - v) / (y1 - y2 + 1e-6f);
      image->DrawRectangle(X(x0), py, std::max(0, X(x0 + span) - X(x0)), 1, colour, 255);
    }
  }
  void Arrow(float x, float y0, float y1) const {  // vertical, head at y1
    Seg(x, y0, x, y1);
    const float dir = y1 > y0 ? -1.0f : 1.0f;
    Seg(x, y1, x - 0.18f, y1 + 0.22f * dir);
    Seg(x, y1, x + 0.18f, y1 + 0.22f * dir);
  }
};

}  // namespace

void Paint(Gui2Image* image, Icon icon, const Vector3& colour, const Vector3& plate,
           int plateAlpha) {
  if (!image) return;
  boost::intrusive_ptr<Image2D>& surface = image->GetImage2D();
  if (!surface) return;
  Canvas c{surface.get(), (int)surface->GetSize().coords[0], (int)surface->GetSize().coords[1],
           colour};
  if (c.w <= 0 || c.h <= 0) return;
  surface->DrawRectangle(0, 0, c.w, c.h, plate, plateAlpha);

  switch (icon) {
    case Icon::TeamSheet:
    case Icon::GamePlan:
      c.Box(0.05f, 0.0f, 0.9f, 1.0f);
      c.Seg(0.05f, 0.5f, 0.95f, 0.5f);
      c.Circle(0.5f, 0.5f, 0.16f, 16);
      if (icon == Icon::TeamSheet) c.Fill(0.42f, 0.14f, 0.16f, 0.16f);
      break;
    case Icon::Tactics:
      c.Arrow(0.32f, 0.95f, 0.05f);
      c.Arrow(0.68f, 0.05f, 0.95f);
      break;
    case Icon::Boot:
      c.Seg(0.15f, 0.15f, 0.15f, 0.7f);
      c.Seg(0.15f, 0.7f, 0.95f, 0.85f);
      c.Seg(0.95f, 0.85f, 0.95f, 0.95f);
      c.Seg(0.95f, 0.95f, 0.05f, 0.95f);
      c.Seg(0.05f, 0.95f, 0.05f, 0.85f);
      c.Seg(0.15f, 0.15f, 0.45f, 0.15f);
      c.Seg(0.45f, 0.15f, 0.6f, 0.55f);
      c.Seg(0.6f, 0.55f, 0.95f, 0.75f);
      break;
    case Icon::Gear:
      c.Circle(0.5f, 0.5f, 0.28f, 20);
      c.Circle(0.5f, 0.5f, 0.1f, 12);
      for (int i = 0; i < 8; i++) {
        const float a = 6.2831853f * i / 8;
        c.Seg(0.5f + 0.28f * std::cos(a), 0.5f + 0.28f * std::sin(a), 0.5f + 0.44f * std::cos(a),
              0.5f + 0.44f * std::sin(a));
      }
      break;
    case Icon::Folder:
      c.Box(0.05f, 0.25f, 0.9f, 0.65f);
      c.Seg(0.05f, 0.25f, 0.35f, 0.25f);
      c.Seg(0.35f, 0.25f, 0.45f, 0.1f);
      c.Seg(0.45f, 0.1f, 0.7f, 0.1f);
      c.Seg(0.7f, 0.1f, 0.7f, 0.25f);
      break;
    case Icon::Play:
      c.Triangle(0.2f, 0.05f, 0.2f, 0.95f, 0.9f, 0.5f);
      break;
    case Icon::Records:
      c.Box(0.1f, 0.05f, 0.8f, 0.9f);
      c.Circle(0.5f, 0.3f, 0.12f, 12);
      c.Seg(0.25f, 0.6f, 0.75f, 0.6f);
      c.Seg(0.25f, 0.75f, 0.75f, 0.75f);
      break;
    case Icon::Ball:
      c.Circle(0.5f, 0.5f, 0.45f, 32);
      c.Seg(0.5f, 0.28f, 0.7f, 0.42f);
      c.Seg(0.7f, 0.42f, 0.62f, 0.66f);
      c.Seg(0.62f, 0.66f, 0.38f, 0.66f);
      c.Seg(0.38f, 0.66f, 0.3f, 0.42f);
      c.Seg(0.3f, 0.42f, 0.5f, 0.28f);
      break;
    case Icon::Shield:
      c.Seg(0.15f, 0.1f, 0.85f, 0.1f);
      c.Seg(0.85f, 0.1f, 0.85f, 0.55f);
      c.Seg(0.85f, 0.55f, 0.5f, 0.95f);
      c.Seg(0.5f, 0.95f, 0.15f, 0.55f);
      c.Seg(0.15f, 0.55f, 0.15f, 0.1f);
      break;
    case Icon::Back:
      c.Seg(0.9f, 0.5f, 0.15f, 0.5f);
      c.Seg(0.15f, 0.5f, 0.45f, 0.2f);
      c.Seg(0.15f, 0.5f, 0.45f, 0.8f);
      break;
    case Icon::Camera:
      c.Box(0.05f, 0.25f, 0.65f, 0.55f);
      c.Seg(0.7f, 0.4f, 0.95f, 0.25f);
      c.Seg(0.95f, 0.25f, 0.95f, 0.8f);
      c.Seg(0.95f, 0.8f, 0.7f, 0.65f);
      break;
    case Icon::Substitute:
      c.Arrow(0.3f, 0.95f, 0.05f);
      c.Arrow(0.7f, 0.05f, 0.95f);
      c.Seg(0.3f, 0.5f, 0.7f, 0.5f);
      break;
  }
  surface->OnChange();
}

void PaintPlate(Gui2Image* image, const Vector3& colour, int alpha) {
  if (!image) return;
  boost::intrusive_ptr<Image2D>& surface = image->GetImage2D();
  if (!surface) return;
  const int w = (int)surface->GetSize().coords[0], h = (int)surface->GetSize().coords[1];
  if (w <= 0 || h <= 0) return;
  const int r = std::max(1, std::min(w, h) / 8);
  // Rows of the rounded rectangle: full width in the middle, shortened by the
  // corner circle's chord near the top and bottom.
  for (int y = 0; y < h; y++) {
    int inset = 0;
    const int dy = y < r ? r - y : (y >= h - r ? y - (h - r - 1) : 0);
    if (dy > 0) inset = r - (int)std::sqrt(std::max(0.0f, (float)r * r - (float)dy * dy));
    surface->DrawRectangle(inset, y, w - 2 * inset, 1, colour, alpha);
  }
  surface->OnChange();
}

IconButton MakeIconButton(Gui2WindowManager* windowManager, Gui2View* page,
                          const std::string& name, float x, float y, float w, float h, Icon icon,
                          const std::string& caption) {
  IconButton out;
  out.button = new Gui2Button(windowManager, name, x, y, w, h, " ");
  page->AddView(out.button);
  out.button->Show();
  // Icon in the upper 62% of the plate, square, centred; caption in the band under it.
  const float iconH = h * 0.62f;
  const float iconW = windowManager->GetWidthPercentForHeight(iconH, 1.0f);
  out.icon = new Gui2Image(windowManager, name + "_icon", x + (w - iconW) * 0.5f, y + h * 0.04f,
                           iconW, iconH);
  page->AddView(out.icon);
  out.icon->Show();
  Paint(out.icon, icon, Vector3(235, 240, 250), Vector3(0, 0, 0), 0);
  out.caption = new Gui2Caption(windowManager, name + "_caption", x + w * 0.04f, y + h * 0.68f,
                                w * 0.92f, h * 0.26f, caption);
  page->AddView(out.caption);
  out.caption->Show();
  return out;
}

std::vector<IconButton> MakeIconBar(Gui2WindowManager* windowManager, Gui2View* page,
                                    const std::string& name, float y, float w, float h,
                                    const std::vector<BarItem>& items) {
  std::vector<IconButton> out;
  const float gap = 0.8f;
  const float total = items.size() * w + (items.size() - 1) * gap;
  float x = (100.0f - total) * 0.5f;
  for (size_t i = 0; i < items.size(); i++) {
    out.push_back(MakeIconButton(windowManager, page, name + "_" + std::to_string(i), x, y, w, h,
                                 items[i].icon, items[i].caption));
    x += w + gap;
  }
  return out;
}

}  // namespace MenuIcons
