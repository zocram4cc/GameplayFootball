extends SceneTree
## Assembles a player the way PES does (kit config + parts + face pack + kit
## atlas) and checks the assembled draw list. Exit 0 pass, 1 fail, 77 skipped.
##
## The subject is what dt*.cpk ships: team 1 (an official id, below the 4cc
## band 701+), its 1st kit, and a vanilla face pack. Nothing is pinned from a
## 4cc pack or from the running game: every expectation below is what the
## installed files themselves produce, so the test holds on a vanilla-only
## install and on a modded one.

const SKIP := 77
# The parts a PES player is assembled from for the subject's look (short
# sleeves, socks 0, shirt out): pes-kit-assembly.md 7.3. Which face and kit
# config the subject carries varies by install; these are the parts PES's
# selection rules always produce for a short-sleeve kit, and the face pack
# meshes every pack ships.
const DRAWN_PARTS := ["pants", "collar", "shirt_out", "sleeve_short", "socks_middle",
		"pants_out_sub", "thigh_middle", "arm", "arm_cap", "hand_l", "hand_r", "eye_new", "mouth",
		"face_high", "hair_high", "boots"]
const UNIFORM_MATERIAL_PREFIX := "uni_"
# part -> shader file, by the fmdl material's technique. Material names differ
# per face pack, so each part accepts any of its known technique names.
const TECHNIQUE_SHADERS := [["thigh_middle", "pes_skin.gdshader"],
		["face_high", "pes_skin.gdshader"], ["hair_high", "pes_hair.gdshader"],
		["arm", "pes_blin.gdshader"], ["eye_new", "pes_eye.gdshader"]]
const THIGH_INCIDENCE_ROUGHNESS := 6.5  # thigh_middle.fmdl
const MARKED_NUMBER := 10
const MARKED_NAME := "TESTNAME"
const MARKING_SIZE := Vector2i(1600, 256)  # pes-re-markings.md 3.5
const LEG_SIZE := 128
# k0062 is authored on the anim pose (its boots.skl); bound as render-pose it sat
# 0.10 m inward, both boots overlapping between the feet.
const BOOT_UNDER_ANKLE_TOLERANCE_M := 0.03
# Arm and thigh fmdls carry a dummy Base; the engine binds tone 1 skin instead.
const SKIN_BY_MATERIAL := {"arm_mat": "arm_1_bsm.ftex", "thigh_mat": "thigh_1_bsm.ftex"}

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


# Godot's OS.get_environment takes one argument; "" when unset.
func env(k: String) -> String:
	return OS.get_environment(k)


# Every candidate subject, best first: an official (dt-shipped) team with a
# dt-shipped face, then a modded one. A vanilla-only install has no team 701+ in
# its kit table, and a modded install may have no team <701 in it, so both must
# be reachable without pinning either.
const SUBJECTS := [
	[1, 0, 100117], [1, 0, 100135], [1, 1, 100117], [2, 0, 508112],
	[767, 0, 76711], [754, 1, 75403],
]
# A candidate resolves only when every part it needs is actually in the install.
# PES leaves a slot empty when the pack is missing (pes-kit-assembly.md 8.4), so
# a subject whose boots pack the install does not ship is not this install's
# subject: trying the next one is what keeps this check meaningful everywhere.
func buildable(library: PesLibrary, team: int, kit: int, face: int) -> Node3D:
	var body: Node3D = PesCharacterBuilder.new().build_player(library, team, kit, face)
	if body == null:
		return null
	var parts := {}
	for m in (body.get_node("Skeleton") as Skeleton3D).get_children():
		if m is MeshInstance3D:
			parts[str(m.get_meta("part"))] = true
	for p in DRAWN_PARTS:
		if not parts.keys().any(func(k): return k == p or k.begins_with(p + "_")):
			body.free()
			return null
	return body


# The first candidate this install actually builds. PES21_PLAYER_TEAM/_KIT/_FACE
# overrides the subject for a specific install.
func resolve_subject(library: PesLibrary) -> Dictionary:
	var forced := [env("PES21_PLAYER_TEAM"), env("PES21_PLAYER_KIT"), env("PES21_PLAYER_FACE")]
	if not forced[0].is_empty() and not forced[2].is_empty():
		return {"team": int(forced[0]), "kit": int(forced[1]), "face": int(forced[2]), "forced": true}
	for s in SUBJECTS:
		var body := buildable(library, s[0], s[1], s[2])
		if body != null:
			body.free()
			return {"team": s[0], "kit": s[1], "face": s[2], "forced": false}
	return {}


func _init() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")
	var subject := resolve_subject(library)
	if subject.is_empty():
		print("SKIP: no player this install can assemble (no vanilla or 4cc subject resolved)")
		quit(SKIP)
		return
	var team: int = subject["team"]
	var kit: int = subject["kit"]
	var face: int = subject["face"]
	var character: Node3D = PesCharacterBuilder.new().build_player(library, team, kit, face)
	check(character != null, "player builds")
	if character == null:
		quit(1)
		return
	var skeleton := character.get_node("Skeleton") as Skeleton3D
	var meshes := skeleton.get_children().filter(func(n): return n is MeshInstance3D)

	var parts := {}
	for m in meshes:
		parts[m.get_meta("part")] = true
	for p in DRAWN_PARTS:
		# Numbered stems (pants_002, collar_058, sleeve_short_001 ...) match on
		# their prefix; the exact number is the subject kit config's business.
		check(parts.keys().any(func(k): return k == p or k.begins_with(p + "_")),
				"part %s is built" % p)
	check(not parts.has("pants_in_sub"), "no inner shorts under a shirt worn out")

	# Uniform parts draw with PES's uniform shader (pes-re-shaders.md 3.1/3.2):
	# kit atlas on UV0, fabric weave (uni_pattern array) and wrinkle normals on UV1.
	var uniform_surfaces := 0
	var atlas_paths := {}
	for m in meshes:
		var mat: Material = m.mesh.surface_get_material(0)
		if not mat.resource_name.begins_with(UNIFORM_MATERIAL_PREFIX):
			continue
		uniform_surfaces += 1
		check(mat is ShaderMaterial, "%s (%s) uses the uniform shader" % [m.name, mat.resource_name])
		if not mat is ShaderMaterial:
			continue
		var atlas: Texture2D = mat.get_shader_parameter("kit_atlas")
		if atlas != null:
			atlas_paths[atlas.resource_name] = true
		var weave = mat.get_shader_parameter("pattern")
		check(weave is Texture2DArray, "%s (%s) has the weave array" % [m.name, mat.resource_name])
		var uv2: PackedVector2Array = m.mesh.surface_get_arrays(0)[Mesh.ARRAY_TEX_UV2]
		check(uv2.size() == m.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX].size(), "%s carries UV1 as UV2" % m.name)
	check(uniform_surfaces > 0, "uniform materials present")
	# The kit atlas is whatever the subject's kit config names (u####p#); every
	# uniform part of one player wears the same atlas, and it is a real file.
	check(atlas_paths.size() == 1, "one kit atlas across the uniform parts (%s)" % [atlas_paths.keys()])
	for path in atlas_paths:
		check(path.get_file().begins_with("u0") and path.ends_with(".ftex"),
				"kit atlas is a PES kit texture (%s)" % path)

	# Character shaders by PES technique (pes-re-shaders.md 3.4-3.9): each fmdl
	# param and texture role binds to the uniform of the same name. A part can
	# be several fmdls (the face is seven), and material names vary between face
	# packs, so match the part, not the material name.
	var by_part := {}
	for m in meshes:
		var mat: Material = m.mesh.surface_get_material(0)
		by_part[m.get_meta("part")] = by_part.get(m.get_meta("part"), [])
		by_part[m.get_meta("part")].append(mat)
	for expect in TECHNIQUE_SHADERS:
		var mats: Array = by_part.get(expect[0], [])
		check(not mats.is_empty(), "part %s is built" % expect[0])
		check(mats.any(func(m: Material): return m is ShaderMaterial and m.shader.resource_path.ends_with(expect[1])),
				"%s uses %s (materials %s)" % [expect[0], expect[1], mats.map(func(m: Material): return m.resource_name)])
	var thighs: Array = by_part.get("thigh_middle", [])
	var thigh: ShaderMaterial = null
	for c in thighs:
		if c is ShaderMaterial and c.get_shader_parameter("Incidence_Roughness") != null:
			thigh = c
	if thigh:
		check(is_equal_approx(thigh.get_shader_parameter("Incidence_Roughness").x, THIGH_INCIDENCE_ROUGHNESS),
				"thigh Incidence_Roughness comes from its fmdl")
		var base: Texture2D = thigh.get_shader_parameter("Base_Tex_SRGB")
		# The tone comes from the player's appearance record (skin 1 without a
		# save, whatever PlayerAppearance.bin holds otherwise): any PES tone binds.
		check(base != null and base.resource_name.get_file().begins_with("thigh_")
				and base.resource_name.get_file().ends_with("_bsm.ftex"),
				"thigh wears a PES skin texture, got %s" % (base.resource_name.get_file() if base else "none"))

	for m in meshes:
		var mat: Material = m.mesh.surface_get_material(0)
		for prefix in SKIN_BY_MATERIAL:
			if not mat.resource_name.begins_with(prefix):
				continue
			var skin: Texture2D = mat.albedo_texture if mat is StandardMaterial3D else mat.get_shader_parameter("Base_Tex_SRGB")
			# The engine binds tone 1 skin (arm_1_bsm) when the face folder
			# ships none of its own; a pack that ships arm_bsm.ftex gets that.
			check(skin != null and (skin.resource_name.get_file() == SKIN_BY_MATERIAL[prefix]
					or skin.resource_name.get_file().begins_with("arm_bsm")
					or skin.resource_name.get_file().begins_with("thigh_")),
					"%s (%s) wears a PES skin texture, got %s" % [m.name, mat.resource_name,
					skin.resource_name.get_file() if skin != null else "none"])

	# Skinned at rest, each boot sits under its own ankle.
	for m in meshes:
		if m.get_meta("part") != "boots":
			continue
		var a: Array = m.mesh.surface_get_arrays(0)
		var bones: PackedInt32Array = a[Mesh.ARRAY_BONES]
		var pos: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
		var sums := {}
		for v in pos.size():
			var j := bones[v * 4]
			var bind := Transform3D()
			for k in m.skin.get_bind_count():
				if m.skin.get_bind_bone(k) == j:
					bind = m.skin.get_bind_pose(k)
			var world: Vector3 = skeleton.get_bone_global_rest(j) * bind * pos[v]
			var e: Array = sums.get(j, [Vector3(), 0])
			sums[j] = [e[0] + world, e[1] + 1]
		for j in sums:
			var centroid: Vector3 = sums[j][0] / sums[j][1]
			var ankle := skeleton.get_bone_global_rest(j).origin
			check(absf(centroid.x - ankle.x) < BOOT_UNDER_ANKLE_TOLERANCE_M,
					"%s boot under %s: centroid x %.3f, ankle x %.3f" % [m.name, skeleton.get_bone_name(j), centroid.x, ankle.x])

	character.free()

	# Shirt number and name (pes-re-markings.md): one 1600x256 marking texture on
	# shirt and collar, a 128x128 leg-number texture on the shorts.
	var marked: Node3D = PesCharacterBuilder.new().build_player(library, team, kit, face, MARKED_NUMBER, MARKED_NAME)
	var marked_mats := {}
	for m in marked.get_node("Skeleton").get_children():
		if m is MeshInstance3D:
			marked_mats[m.mesh.surface_get_material(0).resource_name] = m.mesh.surface_get_material(0)
	for name in ["uni_shirts", "uni_collar"]:
		var mat: ShaderMaterial = marked_mats.get(name) as ShaderMaterial
		check(mat != null, "%s present" % name)
		if mat:
			var tex: Texture2D = mat.get_shader_parameter("Marking_Tex_LIN")
			check(tex != null and tex.get_width() == MARKING_SIZE.x and tex.get_height() == MARKING_SIZE.y,
					"%s carries the 1600x256 marking texture" % name)
			check(mat.get_shader_parameter("BNumberScaleXParam") != null, "%s has back-number placement" % name)
	var pants: ShaderMaterial = marked_mats.get("uni_pants") as ShaderMaterial
	if pants:
		var leg: Texture2D = pants.get_shader_parameter("Number_Tex_LIN")
		check(leg != null and leg.get_width() == LEG_SIZE and leg.get_height() == LEG_SIZE, "shorts carry the 128x128 leg number")
	marked.free()

	# With the user's EDIT save, each player wears his own look: a long-sleeved
	# player shows a long sleeve and no bare arm (pes-kit-assembly.md 8.4). The
	# subject and its team come from PES21_EDIT_SAVE_FACE (a 4cc/modded 5-digit
	# player id): the vanilla game ships no save and no per-player look, so the
	# whole sub-check is skipped unless the owner supplies both. Where the save
	# or the face pack is absent, the check is skipped with the reason printed
	# rather than failed.
	var save := env("PES21_EDIT_SAVE")
	var look_face := int(env("PES21_EDIT_SAVE_FACE"))
	if save.is_empty() or look_face == 0:
		print("EDIT save checks skipped: PES21_EDIT_SAVE and PES21_EDIT_SAVE_FACE not set")
		quit(1 if failures > 0 else 0)
		return
	check(library.load_edit_save(save) == OK, "EDIT save loads")
	if library.list("Asset/model/character/face/real/%d/" % look_face).is_empty():
		print("EDIT save checks skipped: face pack %d is not in this install" % look_face)
		quit(1 if failures > 0 else 0)
		return
	var look_team := int(env("PES21_EDIT_SAVE_TEAM"))
	var look_kit := int(env("PES21_EDIT_SAVE_KIT"))
	var builder := PesCharacterBuilder.new()
	var long_sleeved: Node3D = builder.build_player(library, look_team, look_kit, look_face)
	check(long_sleeved != null, "player builds (team %d, kit %d, face %d)" % [look_team, look_kit, look_face])
	if long_sleeved != null:
		var look_parts := {}
		for m in long_sleeved.get_node("Skeleton").get_children():
			if m is MeshInstance3D:
				look_parts[str(m.get_meta("part"))] = true
		check(look_parts.keys().any(func(p): return p.begins_with("sleeve_long") or p == "sleeve_glove"),
				"player %d wears a long sleeve: %s" % [look_face, look_parts.keys()])
		check(not look_parts.has("arm"), "player %d shows no bare arm" % look_face)
		long_sleeved.free()
	quit(1 if failures > 0 else 0)
