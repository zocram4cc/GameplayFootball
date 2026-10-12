// PES 2021 uniform cloth: pes3ddf_{shirt_nb,collar,sleeve,pants_nb}_nc and, with
// is_socks, pes3DDF_Uniform. Formulas from docs/re/pes-re-shaders.md 3.1 / 3.2:
// kit atlas on UV0 (alpha = kit coverage a), fabric weave (uni_pattern array)
// and wrinkle normals (shirts_normal / pants_normal arrays) on UV1. Markings,
// stain and wetness are not drawn yet.
shader_type spatial;
render_mode cull_disabled;  // _NC = no cull; back faces flip the normal

uniform sampler2D kit_atlas : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2D kit_srm : hint_default_white, filter_linear_mipmap;  // R spec, G rough
uniform bool has_kit_srm = false;
uniform sampler2DArray pattern : filter_linear_mipmap;   // uni_pattern
uniform sampler2DArray wrinkles : filter_linear_mipmap;  // shirts_normal / pants_normal
uniform bool has_wrinkles = false;
uniform sampler2D socks_normal : hint_normal, filter_linear_mipmap;  // socks only, UV1
uniform vec4 repetition = vec4(80.0, 0.8, 1.25, 0.0);    // RepetitionParam
uniform vec4 blend_normal_x = vec4(0.0);                  // BlendNormalXParam
uniform bool is_socks = false;

// The shirt family draws an opaque kit at 85% of its texel colour.
const float OPAQUE_KIT_ALBEDO = 0.85;

// PES normal encoding: x = R·A·2−1, y = G·2−1, z reconstructed.
vec3 pes_normal(vec4 t) {
	vec2 xy = vec2(t.r * t.a, t.g) * 2.0 - 1.0;
	return vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy))));
}

void fragment() {
	vec4 base = texture(kit_atlas, UV);
	float a = base.a;
	vec2 weave_uv = is_socks ? UV * repetition.x : UV2 * repetition.xy;
	float slice = 2.0 * repetition.z;
	vec4 p0 = texture(pattern, vec3(weave_uv, slice));
	vec4 p1 = texture(pattern, vec3(weave_uv, slice + 1.0));
	vec2 srm = has_kit_srm ? texture(kit_srm, UV).rg : vec2(p1.x, p1.y);

	ALBEDO = is_socks ? base.rgb * max(a, p0.x) : base.rgb * mix(p0.x, OPAQUE_KIT_ALBEDO, a);
	ROUGHNESS = mix(p1.y, srm.g, a);
	SPECULAR = mix(p1.x, srm.r, a);

	vec3 n = vec3(0.0, 0.0, 1.0);
	if (is_socks) {
		n = pes_normal(texture(socks_normal, UV2));
	} else if (has_wrinkles) {
		n = pes_normal(texture(wrinkles, vec3(UV2, blend_normal_x.y)));
	}
	vec3 weave_n = vec3(vec2(p0.x * p0.w, p0.y) * 2.0 - 1.0, 1.0);
	vec3 nd = normalize(mix(weave_n, n, a));
	vec3 nt = normalize(vec3(nd.xy + n.xy, n.z));
	NORMAL_MAP = nt * 0.5 + 0.5;
}
