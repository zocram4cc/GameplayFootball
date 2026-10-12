// PES fox3DFW_ConstantSRGB_NDR[_Solid]: unlit; the base texture is the colour.
uniform sampler2D Base_Tex_SRGB : source_color, filter_linear_mipmap_anisotropic;
uniform bool alpha_tested = false;
// ponytail: PES's alpha-test threshold is unresolved (pes-re-shaders.md 8); 0.5 as pes_blin.
const float ALPHA_TEST = 0.5;

void fragment() {
	vec4 base = texture(Base_Tex_SRGB, UV);
	ALBEDO = base.rgb;
#ifdef CONSTANT_BLEND
	ALPHA = base.a;
#else
	if (alpha_tested) {
		ALPHA = base.a;
		ALPHA_SCISSOR_THRESHOLD = ALPHA_TEST;
	}
#endif
}
