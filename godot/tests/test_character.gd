extends SceneTree
## Builds a PES face pack headless and checks the scene the renderer relies on.
## Exit 0 pass, 1 fail, 77 skipped (no PES21_ROOT).
##
## The subject is a dt-shipped face pack; a 4cc one is only a fallback for an
## install that ships none. The mesh count is not pinned: packs differ in which
## passes they ship (4cc face packs are head-only and drop the outline shell),
## so the check is that every drawn mesh is a real, skinned part of the pack.

const SKIP := 77
const FACE_CANDIDATES := [100117, 100135, 100393, 70922, 76711, 75403]
const RIG_JOINTS := 58               # 20 body + 38 finger joints
const TPOSE_LEVEL_TOLERANCE_M := 1e-4
const MIN_DRAWN_MESHES := 1          # a face pack always draws at least one mesh

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
	var character: Node3D = null
	for face in FACE_CANDIDATES:
		character = PesCharacterBuilder.new().build_face_model(library, face)
		if character != null:
			break
	if character == null:
		print("SKIP: no face pack in this install to build")
		quit(SKIP)
		return
	var skeleton := character.get_node("Skeleton") as Skeleton3D
	check(skeleton != null, "Character/Skeleton is a Skeleton3D")
	check(skeleton.get_bone_count() == RIG_JOINTS, "skeleton has the rig's joints")
	check(skeleton.find_bone("body") == 0, "root joint is body")

	var meshes := skeleton.get_children().filter(func(n): return n is MeshInstance3D)
	check(meshes.size() >= MIN_DRAWN_MESHES, "a face pack draws at least one mesh (%d)" % meshes.size())
	for m in meshes:
		check(m.skin != null and m.skin.get_bind_count() == RIG_JOINTS, "%s skinned to every joint" % m.name)
		check(m.skeleton == NodePath(".."), "%s bound to its parent skeleton" % m.name)

	# Rest pose = the anim skeleton's T-pose: hands level with the shoulders.
	for side in ["left", "right"]:
		var hand := skeleton.get_bone_global_rest(skeleton.find_bone(side + "_hand")).origin
		var shoulder := skeleton.get_bone_global_rest(skeleton.find_bone(side + "_shoulder")).origin
		check(absf(hand.y - shoulder.y) < TPOSE_LEVEL_TOLERANCE_M, side + " arm level at rest")

	character.free()
	quit(1 if failures > 0 else 0)
