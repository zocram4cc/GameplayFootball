// fox3DFW_ConstantSRGB_NDR (no "_Solid"): unlit, alpha blended. INF from the
// name pair; the technique's blend state is not read from PES yet.
shader_type spatial;
render_mode unshaded, blend_mix;
#define CONSTANT_BLEND
#include "res://shaders/pes_constant.gdshaderinc"
