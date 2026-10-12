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
		"face_high", "hair_high"]
const UNIFORM_MATERIAL_PREFIX := "uni_"
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

	character.free()
	quit(1 if failures > 0 else 0)
