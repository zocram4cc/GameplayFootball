// How fast the exposure is allowed to change.
//
// PES scales every frame so its brightness lands on a key value (gameKeyValue in the
// atmosphere, with gameMinExposure/gameMaxExposure around it). The first attempt at
// that here measured the frame inside postprocess.frag and applied the result the
// same frame, which cannot work: a fragment shader remembers nothing, sixteen taps at
// fixed screen positions see entirely different things as a camera moves, and the
// gain therefore jumped every frame. Measured off a recorded match, the picture's
// mean brightness moved by 0.02 from frame to frame through the opening cutscene,
// with single-frame jumps of -0.073 and +0.038 - a flicker on every cut and pan.
//
// So the frame is measured on this side, from a readback, and the gain approaches
// what the measurement asks for instead of snapping to it. The shader is handed one
// number.

#ifndef _HPP_SYSTEMS_GRAPHICS_RENDERING_AUTOEXPOSURE
#define _HPP_SYSTEMS_GRAPHICS_RENDERING_AUTOEXPOSURE

#include <cstddef>

namespace AutoExposure {

// The mean displayed luminance of `count` RGBA8 pixels, or a negative number when
// there is nothing to measure. Displayed rather than linear: the key is a brightness
// as seen, and a byte in the back buffer already is that.
float MeanDisplayedLuminance(const unsigned char* rgba, size_t count);

// The gain the frame is asking for, bounded. A negative measurement - no readback
// yet - asks for 1, so a match looks like itself until the first one lands.
float TargetGain(float measuredDisplayed, float key, float minGain, float maxGain);

// The latest measurement, handed from the render thread to whoever sets the uniform.
// Negative until the first readback lands.
void SetMeasuredBrightness(float displayed);
float GetMeasuredBrightness();

// One step toward it. halfLife is how long the gain takes to cover half the distance
// remaining, so the response is exponential and independent of frame rate; 0 turns
// the smoothing off.
float Adapt(float currentGain, float targetGain, float dt_seconds, float halfLife_seconds);

}  // namespace AutoExposure

#endif
