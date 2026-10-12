// PES pes3DDF_Hair2 (fox_hair_mat, fox_head_shell_mat), per
// docs/re/pes-re-shaders.md 3.7: optional recolour, α' = max(a·157/255,
// a ≥ 250/255), rim luma boost, highlight cube. PES dithers α' and resolves it
// with TAA; ALPHA_HASH is the Godot equivalent.
shader_type spatial;
render_mode depth_prepass_alpha;
#include "res://shaders/pes_common.gdshaderinc"

uniform sampler2D Base_Tex_SRGB : source_color, filter_linear_mipmap;
uniform sampler2D NormalMap_Tex_NRM : hint_normal, filter_linear_mipmap;
uniform bool has_NormalMap_Tex_NRM = false;
uniform sampler2D SpecularMap_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform sampler2D Translucent_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform samplerCube CubeMap_Tex_LIN : filter_linear_mipmap;
uniform bool has_CubeMap_Tex_LIN = false;
uniform vec4 HairColor = vec4(1.0);
uniform vec4 HairColor_Screen = vec4(0.0);
uniform vec4 HairColor_Multiply = vec4(1.0);
uniform vec4 SpecularCubeScale = vec4(0.0);
uniform vec4 AlbedoCubeScale = vec4(0.0);

void fragment() {
	vec4 base = texture(Base_Tex_SRGB, UV);
	if (HairColor.w == 0.0) {  // recolour (edit hair): screen, overlay, multiply
		vec3 c = base.rgb + HairColor_Screen.rgb - base.rgb * HairColor_Screen.rgb;
		vec3 lo = 2.0 * c * HairColor.rgb, hi = 1.0 - 2.0 * (1.0 - c) * (1.0 - HairColor.rgb);
		base.rgb = mix(lo, hi, step(0.5, c)) * HairColor_Multiply.rgb;
	}
	ALPHA = max(base.a * 157.0 / 255.0, step(250.0 / 255.0, base.a));
	ALPHA_HASH_SCALE = 1.0;
	vec4 srm = texture(SpecularMap_Tex_LIN, UV);
	float cube = 0.0;
	if (has_CubeMap_Tex_LIN) {
		vec3 r = (INV_VIEW_MATRIX * vec4(reflect(-VIEW, NORMAL), 0.0)).xyz;
		cube = texture(CubeMap_Tex_LIN, r).r * srm.r * (FRONT_FACING ? 1.0 : 0.0);
	}
	// lumaBoost = YUV -> RGB of (1.5·Y, U, V): base + 0.5·Y in every channel.
	float y = dot(base.rgb, vec3(0.299, 0.587, 0.114));
	ALBEDO = mix(base.rgb, base.rgb + 0.5 * y, pow(1.0 - clamp(dot(NORMAL, VIEW), 0.0, 1.0), 4.0) * srm.r)
			+ cube * AlbedoCubeScale.x;
	ROUGHNESS = pes_f(srm.g);
	SPECULAR = pes_specular(cube * AlbedoCubeScale.x * srm.r * SpecularCubeScale.x);
	if (has_NormalMap_Tex_NRM) {
		NORMAL_MAP = pes_normal(texture(NormalMap_Tex_NRM, UV)) * 0.5 + 0.5;
	}
	SSS_STRENGTH = texture(Translucent_Tex_LIN, UV).g;
}
