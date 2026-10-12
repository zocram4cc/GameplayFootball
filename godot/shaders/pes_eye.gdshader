// PES pes3DDF_Eye, per docs/re/pes-re-shaders.md 3.9: sclera base, parallax
// iris from the lens height map, catch-light cube masked by the lens alpha.
shader_type spatial;
#include "res://shaders/pes_common.gdshaderinc"

uniform sampler2D Base_Tex_SRGB : source_color, filter_linear_mipmap;
uniform sampler2D Base_Tex2_SRGB : source_color, filter_linear_mipmap;  // iris colour
uniform sampler2D NormalMap_Tex_NRM : hint_normal, filter_linear_mipmap;
uniform sampler2D NormalMap2_Tex_NRM : hint_normal, filter_linear_mipmap;  // reflection only
uniform sampler2D SpecularMap_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform sampler2D Translucent_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform sampler2D LensHeight_Tex_LIN : hint_default_black, filter_linear_mipmap;
uniform samplerCube CubeMap_Tex_LIN : filter_linear_mipmap;
uniform bool has_CubeMap_Tex_LIN = false;
uniform vec4 HeightScale = vec4(0.0);
uniform vec4 AlbedoCubeScale = vec4(0.0);
const float PARALLAX_SCALE = 0.25;  // the shader's own factor on h·HeightScale

void fragment() {
	vec3 v = normalize(vec3(dot(VIEW, TANGENT), dot(VIEW, BINORMAL), dot(VIEW, NORMAL)));
	float h = texture(LensHeight_Tex_LIN, UV).g;
	vec2 uv_iris = UV - v.xy * h * HeightScale.x * PARALLAX_SCALE / max(v.z, 0.05);
	vec4 base = texture(Base_Tex_SRGB, UV);
	vec3 alb = mix(base.rgb, texture(Base_Tex2_SRGB, uv_iris).rgb, 1.0 - pow(1.0 - h, 4.0));
	vec4 trm = texture(Translucent_Tex_LIN, UV);
	if (has_CubeMap_Tex_LIN) {
		vec3 rn = pes_normal(texture(NormalMap2_Tex_NRM, UV));
		vec3 nw = normalize(TANGENT * rn.x + BINORMAL * rn.y + NORMAL * rn.z);
		vec3 r = (INV_VIEW_MATRIX * vec4(reflect(-VIEW, nw), 0.0)).xyz;
		alb += texture(CubeMap_Tex_LIN, r).rgb * AlbedoCubeScale.x * trm.a;
	}
	ALBEDO = alb;
	vec4 srm = texture(SpecularMap_Tex_LIN, UV);
	ROUGHNESS = pes_f(srm.g);
	SPECULAR = pes_specular(srm.r);
	NORMAL_MAP = pes_normal(texture(NormalMap_Tex_NRM, UV)) * 0.5 + 0.5;
}
