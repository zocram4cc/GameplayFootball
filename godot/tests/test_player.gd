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

	var uniform_surfaces := 0
	for m in meshes:
		var mat: Material = m.mesh.surface_get_material(0)
		if mat.resource_name.begins_with(UNIFORM_MATERIAL_PREFIX):
			uniform_surfaces += 1
			var albedo: Texture2D = (mat as StandardMaterial3D).albedo_texture
			check(albedo != null and albedo.resource_name == KIT_ATLAS,
					"%s (%s) wears the kit atlas" % [m.name, mat.resource_name])
	check(uniform_surfaces > 0, "uniform materials present")

	for m in meshes:
		var mat: StandardMaterial3D = m.mesh.surface_get_material(0)
		for prefix in SKIN_BY_MATERIAL:
			if mat.resource_name.begins_with(prefix):
				check(mat.albedo_texture != null and mat.albedo_texture.resource_name.get_file() == SKIN_BY_MATERIAL[prefix],
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
