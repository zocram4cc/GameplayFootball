// Shared PES character-shading helpers (docs/re/pes-re-shaders.md).

// Normal maps: x = R·A·2−1, y = G·2−1, z reconstructed (BC5 and DXT5nm alike).
vec3 pes_normal(vec4 t) {
	vec2 xy = vec2(t.r * t.a, t.g) * 2.0 - 1.0;
	return vec3(xy, sqrt(max(0.0, 1.0 - dot(xy, xy))));
}

// Roughness remap of skin, face, arms, hair and eye (5th-order polynomial).
float pes_f(float r) {
	return 0.15 + 0.85 * (15.0 * r + 15.0 * r * r + 10.0 * r * r * r + 5.0 * r * r * r * r + 2.0 * r * r * r * r * r) / 47.0;
}

// PES specular is SRM.r scaled by a preset F0. Godot's dielectric F0 is
// 0.16·specular²; at Godot's default F0 (0.04) that gives sqrt(SRM.r)/2.
// ponytail: preset F0 from material_params.fmtt not applied yet.
float pes_specular(float srm_r) {
	return sqrt(max(srm_r, 0.0)) * 0.5;
}
