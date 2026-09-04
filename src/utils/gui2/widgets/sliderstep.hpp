// The arithmetic behind a slider that has a finite number of positions.
//
// A slider drawn as a bare groove says nothing about how many values it has or
// which one is set: the owner's complaint was that the game plan's and the
// pre-match's sliders are ambiguous ("finite number of steps, indicated by the
// UI"). Every slider now carries its step count, shows which step it is on, and
// draws a tick per step while there are few enough to tell apart.
//
// Kept free of SDL and of the window manager so it can be unit-tested.

#ifndef _HPP_GUI2_WIDGET_SLIDERSTEP
#define _HPP_GUI2_WIDGET_SLIDERSTEP

#include <string>

namespace blunted {
namespace SliderStep {

// Above this many positions the ticks would be closer together than the bar is
// wide, so the groove is drawn plain - but the count itself still means
// something, and the speed sliders (a step per 0.1 m/s) have more positions
// than this. So the two limits are separate: ticks up to kMaxDrawnSteps,
// the "8/17" label up to kMaxCountedSteps.
constexpr int kMaxDrawnSteps = 21;
constexpr int kMaxCountedSteps = 40;

// Which step a 0..1 value sits on, 1-based: 0.0 is step 1, 1.0 is step `steps`.
int IndexFor(float value, int steps);

// "4/11", or "" when the slider has more positions than a reader would count.
std::string Label(float value, int steps);

// Whether ticks should be drawn for this many steps.
bool DrawsTicks(int steps);

}  // namespace SliderStep
}  // namespace blunted

#endif
