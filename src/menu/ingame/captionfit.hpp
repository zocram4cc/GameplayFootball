// Making a Gui2Caption respect a box.
//
// Gui2Caption renders at whatever width its text happens to need and, when
// that overruns the box it was given, simply resizes itself past it (see
// caption.cpp's Redraw). Every broadcast-presentation widget draws names
// that come from a squad file and can be arbitrarily long, so each of them
// needs the same treatment: shrink the type until it fits, and only once
// that hits a legibility floor, cut the text and mark the cut.
//
// The measuring and cutting maths itself is pure and unit-tested in
// FormationGraphicLayout (FitTextHeight / TruncateToFit); this is the thin
// Gui2 binding that applies it to a live caption.

#ifndef _HPP_MENU_INGAME_CAPTIONFIT
#define _HPP_MENU_INGAME_CAPTIONFIT

#include "utils/gui2/widgets/caption.hpp"

namespace blunted {

// Shrinks, then truncates, `caption` until it fits `maxWidthPercent`.
// Returns the width it ended up rendering at.
float FitCaption(Gui2Caption* caption, float maxWidthPercent, float naturalHeightPercent,
                 float minHeightPercent);

// As FitCaption, then centres what is left horizontally on `centreX`,
// keeping the caption's current y.
void FitAndCentreCaption(Gui2Caption* caption, float centreX, float maxWidthPercent,
                         float naturalHeightPercent, float minHeightPercent);

// As FitCaption, then pins the caption's left edge to `leftX`.
void FitAndLeftAlignCaption(Gui2Caption* caption, float leftX, float maxWidthPercent,
                            float naturalHeightPercent, float minHeightPercent);

}  // namespace blunted

#endif
