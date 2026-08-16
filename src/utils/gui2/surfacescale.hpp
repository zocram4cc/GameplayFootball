// Area-average downscaling for 32-bit RGBA pixel buffers.
//
// SDL_gfx's zoomSurface(..., SMOOTHING_ON) interpolates from a 2x2 neighbourhood
// around each destination pixel. That is fine while shrinking by less than half,
// and wrong beyond it: source rows and columns between the taps are never read
// at all. The scoreboard shrinks a 52-pixel glyph row to around 9 pixels at
// 640x360, which dropped the diagonal out of '4' and made the match clock read
// "65:00" when the match time was 45:00.
//
// Averaging over the whole source rectangle a destination pixel covers fixes
// that: every source pixel contributes, weighted by how much of it is covered.

#ifndef _HPP_UTILS_GUI2_SURFACESCALE
#define _HPP_UTILS_GUI2_SURFACESCALE

namespace blunted {

// Box-filters `src` down into `dst`. Bytes are [0]=blue [1]=green [2]=red
// [3]=alpha, matching the ARGB8888 surfaces the GUI composes into on a
// little-endian host. Colour is averaged weighted by alpha, so fully
// transparent pixels contribute no colour.
//
// Returns false (leaving `dst` untouched) unless both destination dimensions
// are at least one and no larger than the source: this only shrinks.
bool DownscaleAverageRGBA(const unsigned char* src, int srcW, int srcH, int srcPitch,
                          unsigned char* dst, int dstW, int dstH, int dstPitch);

}  // namespace blunted

#endif
