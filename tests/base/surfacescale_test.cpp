// Downscaling a glyph row is where the match clock became unreadable: SDL_gfx's
// zoomSurface with SMOOTHING_ON is a 2x2 bilinear tap, so shrinking a 52px font
// row to the ~9px the scoreboard gets at 640x360 skips most source rows
// outright. The '4' lost its diagonal and read as a '6' on screen - the clock
// showed "65:00" for a match time of 45:00, and "17:65" for 17:45, a value the
// clock cannot even produce (seconds are computed modulo 60).
//
// These cover the property that was actually missing: every source pixel has to
// contribute to the result, whatever the shrink factor.

#include <gtest/gtest.h>

#include <vector>

#include "utils/gui2/surfacescale.hpp"

namespace {

constexpr int kBytesPerPixel = 4;

// A buffer of fully transparent black.
std::vector<unsigned char> MakeBuffer(int w, int h) {
  return std::vector<unsigned char>(static_cast<size_t>(w) * h * kBytesPerPixel, 0);
}

void SetPixel(std::vector<unsigned char>& buf, int w, int x, int y, unsigned char r,
              unsigned char g, unsigned char b, unsigned char a) {
  unsigned char* p = &buf[(static_cast<size_t>(y) * w + x) * kBytesPerPixel];
  p[0] = b;
  p[1] = g;
  p[2] = r;
  p[3] = a;
}

unsigned char Alpha(const std::vector<unsigned char>& buf, int w, int x, int y) {
  return buf[(static_cast<size_t>(y) * w + x) * kBytesPerPixel + 3];
}

}  // namespace

// The bug, reduced: a one-pixel-wide vertical stroke shrunk by 6x. Bilinear
// point-sampling lands between strokes and returns nothing; an area average
// always carries some of the ink through.
TEST(SurfaceScale, ThinStrokeSurvivesHeavyShrink) {
  const int srcW = 24, srcH = 52;
  std::vector<unsigned char> src = MakeBuffer(srcW, srcH);
  for (int y = 0; y < srcH; ++y)
    SetPixel(src, srcW, 11, y, 255, 255, 255, 255);

  const int dstW = 4, dstH = 9;
  std::vector<unsigned char> dst = MakeBuffer(dstW, dstH);
  blunted::DownscaleAverageRGBA(src.data(), srcW, srcH, srcW * kBytesPerPixel, dst.data(), dstW,
                                dstH, dstW * kBytesPerPixel);

  for (int y = 0; y < dstH; ++y) {
    int rowInk = 0;
    for (int x = 0; x < dstW; ++x)
      rowInk += Alpha(dst, dstW, x, y);
    EXPECT_GT(rowInk, 0) << "row " << y << " lost the stroke entirely";
  }
}

// Every source pixel contributes exactly once, so a uniform field keeps its
// value and a half-covered field averages to half - no pixels skipped, none
// counted twice.
TEST(SurfaceScale, PreservesMeanCoverage) {
  const int srcW = 16, srcH = 16;
  std::vector<unsigned char> src = MakeBuffer(srcW, srcH);
  for (int y = 0; y < srcH; ++y)
    for (int x = 0; x < srcW; ++x)
      SetPixel(src, srcW, x, y, 255, 255, 255, ((x + y) % 2) ? 255 : 0);

  const int dstW = 4, dstH = 4;
  std::vector<unsigned char> dst = MakeBuffer(dstW, dstH);
  blunted::DownscaleAverageRGBA(src.data(), srcW, srcH, srcW * kBytesPerPixel, dst.data(), dstW,
                                dstH, dstW * kBytesPerPixel);

  for (int y = 0; y < dstH; ++y)
    for (int x = 0; x < dstW; ++x)
      EXPECT_NEAR(Alpha(dst, dstW, x, y), 128, 2) << "at " << x << "," << y;
}

// Colour has to be weighted by alpha, or the transparent side of a glyph edge
// drags its (arbitrary) colour into the visible result and the text fringes.
TEST(SurfaceScale, TransparentPixelsDoNotTintTheResult) {
  const int srcW = 4, srcH = 4;
  std::vector<unsigned char> src = MakeBuffer(srcW, srcH);
  for (int y = 0; y < srcH; ++y)
    for (int x = 0; x < srcW; ++x) {
      if (x < 2)
        SetPixel(src, srcW, x, y, 255, 0, 0, 255);  // opaque red
      else
        SetPixel(src, srcW, x, y, 0, 255, 0, 0);  // transparent green
    }

  const int dstW = 1, dstH = 1;
  std::vector<unsigned char> dst = MakeBuffer(dstW, dstH);
  blunted::DownscaleAverageRGBA(src.data(), srcW, srcH, srcW * kBytesPerPixel, dst.data(), dstW,
                                dstH, dstW * kBytesPerPixel);

  EXPECT_EQ(dst[2], 255) << "red channel should survive intact";
  EXPECT_EQ(dst[1], 0) << "transparent green must not bleed in";
  EXPECT_NEAR(dst[3], 128, 2) << "half the area was opaque";
}

// Upscaling is left to the caller; asking this for a larger size is a no-op
// rather than a buffer overrun.
TEST(SurfaceScale, RejectsUpscale) {
  const int srcW = 2, srcH = 2;
  std::vector<unsigned char> src = MakeBuffer(srcW, srcH);
  SetPixel(src, srcW, 0, 0, 255, 255, 255, 255);

  std::vector<unsigned char> dst = MakeBuffer(4, 4);
  EXPECT_FALSE(blunted::DownscaleAverageRGBA(src.data(), srcW, srcH, srcW * kBytesPerPixel,
                                             dst.data(), 4, 4, 4 * kBytesPerPixel));
}
