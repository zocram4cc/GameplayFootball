#include "captionfit.hpp"

#include "formationgraphiclayout.hpp"

namespace blunted {

float FitCaption(Gui2Caption* caption, float maxWidthPercent, float naturalHeightPercent,
                 float minHeightPercent) {
  if (!caption || maxWidthPercent <= 0.0f) return 0.0f;

  // Start from the natural size every time. Captions that outlive their
  // content - a banner slot reused for the next message, the panel's team
  // tag across a team switch - would otherwise stay stuck at whatever
  // smaller size the previous, longer text was shrunk to, since fitting
  // only ever shrinks. (Gui2Caption::Redraw also rewrites its own
  // width/height when text overruns the box, so the current size cannot be
  // compared against the natural one to skip this.)
  caption->SetSize(maxWidthPercent, naturalHeightPercent);
  caption->Redraw();

  float width = caption->GetTextWidthPercent();
  if (width <= maxWidthPercent) return width;

  const float fitted = FormationGraphicLayout::FitTextHeight(width, naturalHeightPercent,
                                                             maxWidthPercent, minHeightPercent);
  if (fitted < naturalHeightPercent) {
    caption->SetSize(maxWidthPercent, fitted);
    caption->Redraw();
    width = caption->GetTextWidthPercent();
  }

  if (width > maxWidthPercent) {
    // Shrinking alone could not do it - the text is simply too long for this
    // box at any readable size - so cut it and mark the cut.
    caption->SetCaption(FormationGraphicLayout::TruncateToFit(
        caption->GetCaption(), maxWidthPercent,
        [caption](int characters) { return caption->GetTextWidthPercent(characters); }));
    width = caption->GetTextWidthPercent();
  }
  return width;
}

void FitAndCentreCaption(Gui2Caption* caption, float centreX, float maxWidthPercent,
                         float naturalHeightPercent, float minHeightPercent) {
  const float width = FitCaption(caption, maxWidthPercent, naturalHeightPercent, minHeightPercent);
  float x, y;
  caption->GetPosition(x, y);
  caption->SetPosition(centreX - width * 0.5f, y);
}

void FitAndLeftAlignCaption(Gui2Caption* caption, float leftX, float maxWidthPercent,
                            float naturalHeightPercent, float minHeightPercent) {
  FitCaption(caption, maxWidthPercent, naturalHeightPercent, minHeightPercent);
  float x, y;
  caption->GetPosition(x, y);
  caption->SetPosition(leftX, y);
}

}  // namespace blunted
