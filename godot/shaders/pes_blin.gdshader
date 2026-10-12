// PES fox3DDF_Blin (arms, hands, generic), per docs/re/pes-re-shaders.md 3.6:
// base albedo unmodified, alpha test on base.a, specular anti-aliasing on the
// roughness. Engine-set skin params are ignored by this shader in PES too.
shader_type spatial;
#include "res://shaders/pes_common.gdshaderinc"

uniform sampler2D Base_Tex_SRGB : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2D NormalMap_Tex_NRM : hint_normal, filter_linear_mipmap;
uniform bool has_NormalMap_Tex_NRM = false;
uniform sampler2D SpecularMap_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform bool alpha_tested = false;
// ponytail: PES's alpha-test threshold is a runtime constant not yet resolved
// (pes-re-shaders.md 8); 0.5 until it is.
const float ALPHA_TEST = 0.5;
const float SPECULAR_AA_MAX = 0.30001;

void fragment() {
	vec4 base = texture(Base_Tex_SRGB, UV);
	ALBEDO = base.rgb;
	if (alpha_tested) {
		ALPHA = base.a;
		ALPHA_SCISSOR_THRESHOLD = ALPHA_TEST;
	}
	vec4 srm = texture(SpecularMap_Tex_LIN, UV);
	vec3 n = has_NormalMap_Tex_NRM ? pes_normal(texture(NormalMap_Tex_NRM, UV)) : vec3(0.0, 0.0, 1.0);
	vec3 nw = normalize(TANGENT * n.x + BINORMAL * n.y + NORMAL * n.z);
	float s = min(0.5 * (length(dFdx(nw)) + length(dFdy(nw))), SPECULAR_AA_MAX);
	ROUGHNESS = pes_f(sqrt(srm.g * srm.g + 2.0 * s * s));
	SPECULAR = pes_specular(srm.r);
	if (has_NormalMap_Tex_NRM) {
		NORMAL_MAP = n * 0.5 + 0.5;
	}
}
