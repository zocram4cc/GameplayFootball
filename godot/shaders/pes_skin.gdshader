// PES skin: pes3DDF_Skin(_Occ) (thighs, neck) and pes3DDF_Skin_Face, per
// docs/re/pes-re-shaders.md 3.4 / 3.5. Uniform names are PES's param and
// texture-role names; the builder binds the fmdl's values by name. Facial
// wrinkle/rotation normals are not drawn (their params are set live only).
shader_type spatial;
#include "res://shaders/pes_common.gdshaderinc"

uniform sampler2D Base_Tex_SRGB : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2D NormalMap_Tex_NRM : hint_normal, filter_linear_mipmap;
uniform bool has_NormalMap_Tex_NRM = false;
uniform sampler2D SpecularMap_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform sampler2D Translucent_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform sampler2D Dirty_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform vec4 Incidence_Color = vec4(0.0);
uniform vec4 Incidence_Roughness = vec4(1.0);
uniform vec4 Dirty_Mask = vec4(0.0);
uniform bool is_face = false;  // pes3DDF_Skin_Face: no 143/255 roughness floor

void fragment() {
	vec3 base = texture(Base_Tex_SRGB, UV).rgb;
	vec4 srm = texture(SpecularMap_Tex_LIN, UV);
	float dirt = clamp(dot(Dirty_Mask.xyz, texture(Dirty_Tex_LIN, UV).rgb), 0.0, 1.0);
	float g = srm.g;
	float dk = clamp(0.93 - 0.008 * (255.0 * g - 205.0), 0.73, 0.93);
	vec3 alb = mix(base, base * dk, dirt);
	float rim = pow(clamp(1.0 - dot(NORMAL, VIEW), 0.0, 1.0), Incidence_Roughness.x) * srm.r * Incidence_Color.w;
	ALBEDO = mix(alb, Incidence_Color.rgb, rim);
	float gd = mix(g, g * min(clamp((255.0 * g - 170.0) * 0.011, 0.0, 1.0), 0.66), dirt);
	ROUGHNESS = pes_f(is_face ? gd : max(gd, min(g, 143.0 / 255.0)));
	SPECULAR = pes_specular(srm.r);
	if (has_NormalMap_Tex_NRM) {
		NORMAL_MAP = pes_normal(texture(NormalMap_Tex_NRM, UV)) * 0.5 + 0.5;
	}
	// Translucency T = TRM.g (wrap lighting in PES; Godot's SSS strength here).
	SSS_STRENGTH = texture(Translucent_Tex_LIN, UV).g;
}
