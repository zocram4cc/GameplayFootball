// PES pes3DDF_Pitch2020 — the in-play pitch, ported from
// docs/re/pes-re-pitch-grass.md (priorities 1-4, verified 315/315 pixels against
// the compiled DXBC). Uniform names are PES's material-param and texture-role
// names; PesModelBuilder binds the fmdl's values by name.
//
// Every numbered deviation from PES is marked [DEVIATION n] in place and
// restated in the list at the bottom. The report's formula block (§7.2) is
// followed line by line.
shader_type spatial;

// ---- PES material params, exactly as fmdl/packed (report 1.1, 3.1) ----------
uniform vec4 AOParamX = vec4(1.0);
uniform vec4 AOParamY = vec4(0.1);
uniform vec4 AOParamZ = vec4(1.5);
uniform vec4 AOParamW = vec4(0.85);
uniform vec4 URepeat_UV = vec4(1.0);
uniform vec4 VRepeat_UV = vec4(1.0);
uniform vec4 NormalFlatness = vec4(0.5);
uniform vec4 RepairColor = vec4(0.31, 0.4, 0.0, 1.0);
uniform vec4 CameraVectorBlend = vec4(0.0);
uniform vec4 WithersParameter0 = vec4(0.78);
uniform vec4 WithersParameter1 = vec4(0.78);
uniform vec4 WithersParameter2 = vec4(0.96);
uniform vec4 WithersParameter3 = vec4(0.1);
uniform vec4 WithersParameter4 = vec4(0.07);
uniform vec4 WithersParameter5 = vec4(0.04);
uniform vec4 SrmParameter0 = vec4(1.0);
uniform vec4 SrmParameter1 = vec4(0.24);
uniform vec4 SrmParameter2 = vec4(0.2);
uniform vec4 SrmParameter3 = vec4(0.0);
uniform vec4 SrmParameter4 = vec4(0.76);
uniform vec4 SrmParameter5 = vec4(0.2);
uniform vec4 EmissiveParam0 = vec4(0.05);
uniform vec4 EmissiveParam1 = vec4(0.1);
uniform vec4 EmissiveParam2 = vec4(17.09);
// t15, the 8x8 screen-space dither PES tests visibility against, has no fmdl
// texture role in st002 (and no such role at all: the PS samples it as an
// engine-bound constant). Dropping it is [DEVIATION 4].

// ---- texture roles (report 3.2: t0..t7 of the PES PS) ----------------------
// t0: per-stadium. R = density multiplier, G = grain weight, B = on-pitch mask,
// A = the AO term (report 3.7, 3.9). Sampled untiled, at UV0 as PES does.
uniform sampler2D Base_Tex_SRGB : source_color, filter_linear_mipmap_anisotropic;
// t3: the shared turf colour. RGB = grass colour, A = per-texel density (5.3).
// Sampled tiled by URepeat_UV x VRepeat_UV. PES binds the sampler POINT
// (report 3.2, s3 "point wrap"), but with 32x24 tiling a point sampler resamples
// one 1024² texel per 4 cm and aliases into the mowing-band artefacts seen in the
// frame. [DEVIATION 9] Bilinear tiling is the closest Godot allows without
// re-authoring the map.
uniform sampler2D Base_Tex_2_SRGB : source_color, filter_linear_mipmap, repeat_enable;
// t4: per-stadium fine grain normal, RG only, tiled as t3 (report 3.10).
uniform sampler2D GrainNormal_Tex_LIN : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
// t7: per-stadium blade-orientation grain. (R,B,G) = the grass-blade normal
// that builds the tangent frame, A = the repair-colour blend weight (3.4, 3.9).
// Sampled tiled as t3, bilinear for the same reason. [DEVIATION 9]
uniform sampler2D Grain_Tex_LIN : filter_linear_mipmap, repeat_enable;
// t5: the common 128x128 view-tilt LUT (report 5.4), sampled linear CLAMP.
uniform sampler2D SlopeTable_Tex_LIN : filter_linear, repeat_disable;
// t6: Mask_Tex_SRGB. Never bound in st002 (report 8 item 1), so the builder
// binds nothing and PES's own documented zero default applies: a = Base.a,
// gW = Base.g, m2 = 1 and the Srm curves the bare SrmParameter*.
uniform sampler2D Mask_Tex_SRGB : hint_default_black, filter_nearest;

// ---- shading model ID 3, hard-coded in PES (report 3.8) -------------------
const vec3 ID3_TINT = vec3(0.9, 1.0, 0.8);
const float ID3_F0 = 0.04;              // 0.04 + 0.96*matalness, and PES emits metalness 0
const float ID3_TRANSLUCENCY = 0.0;     // GB1.T for the pitch (asm 165)
// PES's linear conversion (asm 83-85) is of the sampled value, done in the
// GPU-side texture stage, not in the shader. Godot's source_color tag does the
// same thing to the sampler, so the exponent is named and left as a no-op here
// rather than applied twice. [DEVIATION 3]
const float PES_LINEAR_EXPONENT = 1.0 / 2.2;
// The mowing pattern lives on the separate turf3d grass mesh with a world-space
// Grass_MaskMap_Tex_LIN lookup (report 5.2); this material has no stripe source.
const float FALLBACK_MIN_ROUGHNESS = 0.02;
void fragment() {
	vec2 tiled = UV * vec2(URepeat_UV.x, VRepeat_UV.x);

	// ---- 1. density scalar (report 3.7) -----------------------------------
	vec4 base2 = texture(Base_Tex_2_SRGB, tiled);
	vec4 p0 = texture(Base_Tex_SRGB, UV);
	float lin = pow(base2.a, PES_LINEAR_EXPONENT);
	float v = lin * p0.r;

	// ---- 2. height terms --------------------------------------------------
	// PES takes log2 of the world view vector's height and re-raises it, which
	// is the identity for AOParamX = 1 but is what the data says. VIEW is the
	// world-space camera direction already normalised. [DEVIATION 1]
	float h = log2(max(VIEW.y, 1e-5));
	float qt = exp2(h * AOParamX.x);
	float pw = exp2(h * EmissiveParam0.x);

	// ---- 3. roughness / specular exponents (report 3.7) ------------------
	float vlog = log2(max(v, 1e-5));
	float aoy = exp2(vlog * AOParamY.x);
	float aoz = exp2(vlog * AOParamZ.x);
	float em1 = exp2(vlog * EmissiveParam1.x);
	float em2 = exp2(vlog * EmissiveParam2.x);

	float wither = 1.0 - (qt * (aoz - aoy) + aoy);
	float match = 1.0 - wither * AOParamW.x;

	// ---- 4. slope table (report 3.6, 5.4) --------------------------------
	// The grain normal: (R,B,G) per the asm's .xzy swizzle, then the tangent
	// frame. PES builds the second basis vector with a surface-gradient
	// Gram-Schmidt in the VS; Godot has no surface gradients in the fragment
	// shader, so the frame is built locally against the surface normal.
	// [DEVIATION 2]
	vec4 grainTex = texture(Grain_Tex_LIN, tiled);
	vec3 ng = normalize(vec3(grainTex.r, grainTex.b, grainTex.g) * 2.0 - 1.0);
	vec3 tgx = normalize(ng - NORMAL * dot(NORMAL, ng) + vec3(1e-6));
	vec3 tgy = cross(NORMAL, tgx);
	float dvn = dot(VIEW, ng);
	float dvt = dot(VIEW, tgx);
	// Both asm lookups use the same UV in this build; s0's .x and .z feed the
	// roughness/specular chain and its .y the albedo spend term. Report 7.2
	// notes a two-sample port is exact; one sample suffices here because the
	// two UVs are identical. Report 8 item 3: the value channel could be R or A
	// without a GPU run; R is used.
	vec4 st = texture(SlopeTable_Tex_LIN, vec2(0.5 + 0.5 * dvn, 0.5 + 0.5 * dvt));
	float spend = 1.0 - st.y * match;

	// ---- 5. albedo (report 3.9) ------------------------------------------
	vec3 base2c = base2.rgb;
	vec3 repaired = base2c + grainTex.a * (RepairColor.rgb - base2c);
	vec3 curve;
	curve.r = clamp(base2c.r * WithersParameter0.x + WithersParameter3.x, 0.0, 1.0);
	curve.g = clamp(base2c.g * WithersParameter1.x + WithersParameter4.x, 0.0, 1.0);
	curve.b = clamp(base2c.b * WithersParameter2.x + WithersParameter5.x, 0.0, 1.0);
	vec3 tint = curve - repaired;
	vec4 mask = texture(Mask_Tex_SRGB, UV);
	// The AO term is a = max(Base_Tex_SRGB.a, Mask_Tex_SRGB.r). st002's pitch_alp
	// alpha is essentially empty (OBS on the decoded file: 0.68 % of texels
	// nonzero, and 0.0 from mip 6 up, which is the mip a match camera samples),
	// and the mask texture is never bound in st002 (report 8 item 1). PES's own
	// formula therefore also produces a = 0 here, and its own AO term is a
	// no-op on this stadium. The AO is kept exactly as the report writes it, so
	// a stadium whose pitch_alp does carry the AO channel gets it back: the
	// darkening seen on st002 is PES's own behaviour, not a port defect.
	// ponytail: a full AO lift on the vanilla stadiums needs the pitch_alp of
	// every stadium dumped; do not fake it with a constant.
	float a = max(p0.a, mask.r);      // AO
	float gW = max(p0.g, mask.g);     // grain weight
	vec3 y = gW * tint + repaired;
	vec3 c = vec3(1.0) - (vec3(1.0) - base2c) * spend;
	vec3 n = c * y;
	float m2 = 1.0 - clamp(mask.b * 0.1, 0.0, 1.0);
	vec3 tOut = n * m2;
	vec3 uOut = vec3(0.8) - tOut;
	ALBEDO = clamp(a * uOut + tOut, 0.0, 1.0);

	// ---- 6. normal (report 3.10) -----------------------------------------
	// PES decodes the flat detail normal, flattens it by NormalFlatness against
	// (0,0,1), re-orthogonalises it against the grain frame, mixes the two by
	// the albedo curve's blue, projects into the view basis and octahedrally
	// encodes. Godot takes a normal map directly; the re-projection, view basis
	// and encode are Godot's own. [DEVIATION 2]
	vec4 nrm4 = texture(GrainNormal_Tex_LIN, tiled);
	vec2 n2 = vec2(nrm4.r * nrm4.a, nrm4.g) * 2.0 - 1.0;
	vec3 n1 = vec3(n2, sqrt(max(0.0, 1.0 - dot(n2, n2))));
	vec3 grainN = n1 + NormalFlatness.x * (vec3(0.0, 0.0, 1.0) - n1);
	// Worn/discoloured grass (high curve.b) gets a flatter, dirtier normal.
	vec3 nBlend = mix(grainN, n1, curve.b);
	vec3 nw = normalize(tgx * nBlend.x + tgy * nBlend.y + NORMAL * nBlend.z);
	if (nw.z < 0.0) nw = -nw;
	NORMAL_MAP = nw * 0.5 + 0.5;

	// ---- 7. GB2 (roughness / specular) (report 3.8) ----------------------
	float s0 = clamp(lin * SrmParameter0.x + SrmParameter3.x, 0.0, 1.0);
	float s1 = clamp(lin * SrmParameter1.x + SrmParameter4.x, 0.0, 1.0);
	float m4 = 1.0 - clamp(mask.b * 1.0, 0.0, 1.0);
	float r6z = pw * (em2 - em1) + em1;
	float r0x = st.x * match * s0;
	ROUGHNESS = clamp(s1, FALLBACK_MIN_ROUGHNESS, 1.0);
	METALLIC = 0.0;  // asm 179, always
	// PES's GB2.y is the scalar specular of the ID-3 lighting; Godot multiplies
	// its dielectric BRDF by SPECULAR, so the scalar is pre-scaled by the
	// species' F0 and the ID-3 tint instead of inside light(), which cannot
	// read it back. [DEVIATION 8]
	SPECULAR = ID3_F0 * m4 * r0x;
	// Report 7.6 skip: the VS height-fog polynomial and GB3/GB4 — Godot's fog
	// and depth replace them.
}

// ---- 8. shading model ID 3 (report 3.8, 7.3) ------------------------------
// PES: diffuse = lightColor * max(saturate(NdotL + T*(1-NdotL^2)), wrap*T) with
// T = 0 and wrap = 1; a specular of ID3_F0 with the Fresnel shape PES could not
// locate (report 8 item 9), which the report treats as Schlick; and a greenish
// specular tint. Godot's own BRDF stays; the tint and the constants are PES's.
// [DEVIATION 8] Godot's light() outputs DIFFUSE_LIGHT/SPECULAR_LIGHT and
// ATTENUATION/LIGHT_COLOR only; ALBEDO is not writable there, so the tint is
// multiplied in fragment() where PES's deferred pass applies it after GB0.
void light() {
	float NdotL = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
	float NdotV = clamp(dot(NORMAL, VIEW), 0.0, 1.0);
	vec3 H = normalize(LIGHT + VIEW);
	float NdotH = clamp(dot(NORMAL, H), 0.0, 1.0);

	// T = 0, so PES's max(sat(NdotL), wrap*T) is sat(NdotL). LIGHT_COLOR carries
	// this light's colour and energy; multiplying by it instead of replacing it
	// keeps Godot's own scaling when the director's lights are retuned.
	DIFFUSE_LIGHT = NdotL * LIGHT_COLOR * ID3_TINT;

	float r = max(ROUGHNESS, FALLBACK_MIN_ROUGHNESS);
	float a2 = r * r;
	float d = a2 * a2 / pow(NdotH * NdotH * (a2 * a2 - 1.0) + 1.0, 2.0);
	float k = a2 * 0.5;
	float vis = 1.0 / ((NdotL * (1.0 - k) + k) * (NdotV * (1.0 - k) + k));
	float f = ID3_F0 + (1.0 - ID3_F0) * pow(1.0 - clamp(dot(H, LIGHT), 0.0, 1.0), 5.0);
	SPECULAR_LIGHT = f * d * vis * NdotL * 0.25 * ID3_TINT;
}

// ---- DEVIATIONS from PES ---------------------------------------------------
// [DEVIATION 1] The two height terms (Qt, Pw) and the CameraVectorBlend lerp
//   toward -sun: AOParamX is 1.0 on this material, making Qt the identity, and
//   CameraVectorBlend is 0.0, making V the plain view vector. Godot's VIEW is
//   already normalised, so those two branches collapse; the params stay bound
//   so a material with other values still ports the exponent curves.
// [DEVIATION 2] The grain tangent frame and the normal re-projection: PES builds
//   them in the VS from a surface-gradient frame and projects into the view
//   basis. Godot gives TANGENT/BINORMAL/NORMAL only, so the frame is built in
//   the fragment shader with a local Gram-Schmidt step against the surface
//   normal, and the view-basis projection and octahedral encode are Godot's.
//   Report 3.10 itself flags the re-projection as approximate.
// [DEVIATION 3] Godot's source_color tag converts the sampler to linear, which
//   also makes PES's asm 83-85 pow(a, 1/2.2) a no-op for the sampled value;
//   the exponent is kept named so the intent survives the convention.
// [DEVIATION 4] t15, the 8x8 screen dither, and the unconditional part of the
//   visibility test are dropped: PES discards outside pitch_alp.B, st002's mask
//   covers the whole drawn pitch, and Godot has no 8x8 dither texture bound.
//   The pitch therefore never discards.
// [DEVIATION 5] The mowing stripes: on the turf3d grass mesh with a world-space
//   Grass_MaskMap_Tex_LIN (report 5.2), not in this material. Nothing to port.
// [DEVIATION 6] MatParamIndex_0 (205 here) selects nothing in PES's build either
//   — no instruction reads cb4[0].x and the pitch emits shading-model ID 3,
//   which never indexes the material preset (report 5.1). So no preset is
//   applied; the ID-3 constants above are what lights the pitch.
// [DEVIATION 7] Translucency: GB1.T for the pitch is 0, and Godot's subsurface
//   strength has no matching meaning for a zero term, so it is left unset.
// [DEVIATION 8] Godot's light() cannot rewrite ALBEDO (only DIFFUSE_LIGHT and
//   SPECULAR_LIGHT), so the ID-3 tint is applied in light()'s outputs and in
//   fragment()'s albedo, where PES's deferred pass tints after GB0.
// [DEVIATION 9] The two tiled samplers (t3 turf colour, t7 grain) are sampled
//   POINT in PES ("point wrap", report 3.2). At 32x24 tiling that resamples one
//   1024² texel per 4 cm and moires into visible bands; both are bilinear here.
