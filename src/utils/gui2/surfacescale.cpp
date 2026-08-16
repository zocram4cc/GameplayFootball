#include "surfacescale.hpp"

#include <cmath>

namespace blunted {

namespace {

// How much of source pixel `i` falls inside the destination pixel's span
// [begin, end), both in source-pixel units.
inline double Coverage(int i, double begin, double end) {
  const double lo = begin > i ? begin : i;
  const double hi = end < i + 1 ? end : i + 1;
  return hi > lo ? hi - lo : 0.0;
}

}  // namespace

bool DownscaleAverageRGBA(const unsigned char* src, int srcW, int srcH, int srcPitch,
                          unsigned char* dst, int dstW, int dstH, int dstPitch) {
  if (!src || !dst)
    return false;
  if (srcW < 1 || srcH < 1 || dstW < 1 || dstH < 1)
    return false;
  if (dstW > srcW || dstH > srcH)
    return false;  // shrinking only; upscaling stays with the caller

  const double xScale = static_cast<double>(srcW) / dstW;
  const double yScale = static_cast<double>(srcH) / dstH;

  for (int dy = 0; dy < dstH; ++dy) {
    const double yBegin = dy * yScale;
    const double yEnd = yBegin + yScale;
    const int y0 = static_cast<int>(yBegin);
    const int y1 = static_cast<int>(std::ceil(yEnd)) > srcH ? srcH : static_cast<int>(std::ceil(yEnd));

    for (int dx = 0; dx < dstW; ++dx) {
      const double xBegin = dx * xScale;
      const double xEnd = xBegin + xScale;
      const int x0 = static_cast<int>(xBegin);
      const int x1 =
          static_cast<int>(std::ceil(xEnd)) > srcW ? srcW : static_cast<int>(std::ceil(xEnd));

      double area = 0.0;      // total covered source area
      double alphaSum = 0.0;  // alpha integrated over that area
      double colour[3] = {0.0, 0.0, 0.0};  // colour integrated, weighted by alpha

      for (int sy = y0; sy < y1; ++sy) {
        const double wy = Coverage(sy, yBegin, yEnd);
        if (wy <= 0.0)
          continue;
        const unsigned char* row = src + static_cast<size_t>(sy) * srcPitch;
        for (int sx = x0; sx < x1; ++sx) {
          const double w = wy * Coverage(sx, xBegin, xEnd);
          if (w <= 0.0)
            continue;
          const unsigned char* p = row + static_cast<size_t>(sx) * 4;
          const double a = p[3];
          area += w;
          alphaSum += w * a;
          const double wa = w * a;
          colour[0] += wa * p[0];
          colour[1] += wa * p[1];
          colour[2] += wa * p[2];
        }
      }

      unsigned char* out = dst + static_cast<size_t>(dy) * dstPitch + static_cast<size_t>(dx) * 4;
      if (area <= 0.0 || alphaSum <= 0.0) {
        out[0] = out[1] = out[2] = out[3] = 0;
        continue;
      }
      // Colour is the alpha-weighted mean, so fully transparent neighbours
      // contribute nothing to it; alpha is the plain mean over the area.
      for (int c = 0; c < 3; ++c) {
        const double v = colour[c] / alphaSum;
        out[c] = static_cast<unsigned char>(v < 0.0 ? 0.0 : (v > 255.0 ? 255.0 : v + 0.5));
      }
      const double a = alphaSum / area;
      out[3] = static_cast<unsigned char>(a < 0.0 ? 0.0 : (a > 255.0 ? 255.0 : a + 0.5));
    }
  }
  return true;
}

}  // namespace blunted
