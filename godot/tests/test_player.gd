extends SceneTree
## Assembles a player the way PES does (kit config + parts + face pack + kit
## atlas) and checks it against the draw list read from the running game for
## player 76711, team 767, 1st kit. Exit 0 pass, 1 fail, 77 skipped.

const SKIP := 77
const TEAM := 767
const FIRST_KIT := 0
const FACE := 76711
const KIT_ATLAS := "Asset/model/character/uniform/texture/#windx11/u0767p1.ftex"
# Parts in the live draw list (pes-kit-assembly.md 7.3), by model stem.
const DRAWN_PARTS := ["pants_016", "collar_105", "shirt_out", "sleeve_short_001", "socks_middle",
		"pants_out_sub", "thigh_middle", "arm", "arm_cap", "hand_l", "hand_r", "eye_new", "mouth",
		"face_high", "hair_high", "boots"]  # boots: BootsList default k0062 (76711 unlisted)
const UNIFORM_MATERIAL_PREFIX := "uni_"
# fmdl material -> shader file, by its technique.
# part:material -> shader file, by the material's technique (two parts may share a
# material name: 76711's face and helmet are both fox_skin_mat).
const TECHNIQUE_SHADERS := [["thigh_middle:thigh_mat", "pes_skin.gdshader"],
		["face_high:fox_skin_mat", "pes_skin.gdshader"], ["hair_high:fox_skin_mat", "pes_hair.gdshader"],
		["arm:arm_mat", "pes_blin.gdshader"], ["eye_new:eye_mat", "pes_eye.gdshader"]]
const THIGH_INCIDENCE_ROUGHNESS := 6.5  # thigh_middle.fmdl
# k0062 is authored on the anim pose (its boots.skl); bound as render-pose it sat
# 0.10 m inward, both boots overlapping between the feet.
const BOOT_UNDER_ANKLE_TOLERANCE_M := 0.03
# Arm and thigh fmdls carry a dummy Base; the engine binds tone 1 skin for 76711,
# whose face folder has no arm_bsm of its own.
const SKIN_BY_MATERIAL := {"arm_mat": "arm_1_bsm.ftex", "thigh_mat": "thigh_1_bsm.ftex"}

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


func _init() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")
	var character: Node3D = PesCharacterBuilder.new().build_player(library, TEAM, FIRST_KIT, FACE)
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
		check(parts.has(p), "part %s is built" % p)
	check(not parts.has("pants_in_sub"), "no inner shorts under a shirt worn out")

	# Uniform parts draw with PES's uniform shader (pes-re-shaders.md 3.1/3.2):
	# kit atlas on UV0, fabric weave (uni_pattern array) and wrinkle normals on UV1.
	var uniform_surfaces := 0
	for m in meshes:
		var mat: Material = m.mesh.surface_get_material(0)
		if not mat.resource_name.begins_with(UNIFORM_MATERIAL_PREFIX):
			continue
		uniform_surfaces += 1
		check(mat is ShaderMaterial, "%s (%s) uses the uniform shader" % [m.name, mat.resource_name])
		if not mat is ShaderMaterial:
			continue
		var kit: Texture2D = mat.get_shader_parameter("kit_atlas")
		check(kit != null and kit.resource_name == KIT_ATLAS, "%s (%s) wears the kit atlas" % [m.name, mat.resource_name])
		var weave = mat.get_shader_parameter("pattern")
		check(weave is Texture2DArray, "%s (%s) has the weave array" % [m.name, mat.resource_name])
		var uv2: PackedVector2Array = m.mesh.surface_get_arrays(0)[Mesh.ARRAY_TEX_UV2]
		check(uv2.size() == m.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX].size(), "%s carries UV1 as UV2" % m.name)
	check(uniform_surfaces > 0, "uniform materials present")

	# Character shaders by PES technique (pes-re-shaders.md 3.4-3.9): each fmdl
	# param and texture role binds to the uniform of the same name.
	var by_material := {}
	for m in meshes:
		var mat: Material = m.mesh.surface_get_material(0)
		by_material["%s:%s" % [m.get_meta("part"), mat.resource_name]] = mat
	for expect in TECHNIQUE_SHADERS:
		var mat: Material = by_material.get(expect[0])
		check(mat is ShaderMaterial and mat.shader.resource_path.ends_with(expect[1]),
				"%s uses %s" % [expect[0], expect[1]])
	var thigh: ShaderMaterial = by_material.get("thigh_middle:thigh_mat") as ShaderMaterial
	if thigh:
		check(is_equal_approx(thigh.get_shader_parameter("Incidence_Roughness").x, THIGH_INCIDENCE_ROUGHNESS),
				"thigh Incidence_Roughness comes from its fmdl")
		var base: Texture2D = thigh.get_shader_parameter("Base_Tex_SRGB")
		check(base != null and base.resource_name.get_file() == "thigh_1_bsm.ftex", "thigh wears the engine-bound skin")

	for m in meshes:
		var mat: Material = m.mesh.surface_get_material(0)
		for prefix in SKIN_BY_MATERIAL:
			if not mat.resource_name.begins_with(prefix):
				continue
			var skin: Texture2D = mat.albedo_texture if mat is StandardMaterial3D else mat.get_shader_parameter("Base_Tex_SRGB")
			check(skin != null and skin.resource_name.get_file() == SKIN_BY_MATERIAL[prefix],
					"%s (%s) wears %s" % [m.name, mat.resource_name, SKIN_BY_MATERIAL[prefix]])

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

	# With the user's EDIT save, each player wears his own look. 75403 played in
	# long sleeves for team 754's 2nd kit in a live match: a long sleeve, no
	# bare arm (pes-kit-assembly.md 8.4).
	var save := OS.get_environment("PES21_EDIT_SAVE")
	if not save.is_empty():
		check(library.load_edit_save(save) == OK, "EDIT save loads")
		var long_sleeved: Node3D = PesCharacterBuilder.new().build_player(library, 754, 1, 75403)
		var parts75403 := {}
		for m in long_sleeved.get_node("Skeleton").get_children():
			if m is MeshInstance3D:
				parts75403[str(m.get_meta("part"))] = true
		check(parts75403.keys().any(func(p): return p.begins_with("sleeve_long") or p == "sleeve_glove"),
				"75403 wears a long sleeve: %s" % [parts75403.keys()])
		check(not parts75403.has("arm"), "75403 shows no bare arm")
		long_sleeved.free()
	else:
		print("EDIT save checks skipped: PES21_EDIT_SAVE not set")
	quit(1 if failures > 0 else 0)
