// PES pes3DDF_Pitch2020 (pitch_mat). ponytail: provisional until PitchGrassRE
// maps the disassembly: the pitch-wide base (pitch_alp: mowing and colour) on
// UV0, turf normal and SRM tiled by URepeat_UV x VRepeat_UV.
shader_type spatial;
#include "res://shaders/pes_common.gdshaderinc"

uniform sampler2D Base_Tex_SRGB : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2D NormalMap_Tex_NRM : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool has_NormalMap_Tex_NRM = false;
uniform sampler2D SpecularMap_Tex_LIN : hint_default_black, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec4 URepeat_UV = vec4(1.0);
uniform vec4 VRepeat_UV = vec4(1.0);

void fragment() {
	vec2 tiled = UV * vec2(URepeat_UV.x, VRepeat_UV.x);
	ALBEDO = texture(Base_Tex_SRGB, UV).rgb;
	vec4 srm = texture(SpecularMap_Tex_LIN, tiled);
	ROUGHNESS = pes_f(srm.g);
	SPECULAR = pes_specular(srm.r);
	if (has_NormalMap_Tex_NRM) {
		NORMAL_MAP = pes_normal(texture(NormalMap_Tex_NRM, tiled)) * 0.5 + 0.5;
	}
}
