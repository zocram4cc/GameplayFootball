extends SceneTree
## Builds a PES face pack headless and checks the scene the renderer relies on.
## Exit 0 pass, 1 fail, 77 skipped (no PES21_ROOT).

const SKIP := 77
const HEAD_PACK_FACE := 70922        # head-only pack: 3 drawn meshes, outline pass dropped
const HEAD_PACK_DRAWN_MESHES := 3
const RIG_JOINTS := 58               # 20 body + 38 finger joints
const TPOSE_LEVEL_TOLERANCE_M := 1e-4

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
	var character: Node3D = PesCharacterBuilder.new().build_face_model(library, HEAD_PACK_FACE)
	check(character != null, "face pack builds")
	if character == null:
		quit(1)
		return
	var skeleton := character.get_node("Skeleton") as Skeleton3D
	check(skeleton != null, "Character/Skeleton is a Skeleton3D")
	check(skeleton.get_bone_count() == RIG_JOINTS, "skeleton has the rig's joints")
	check(skeleton.find_bone("body") == 0, "root joint is body")

	var meshes := skeleton.get_children().filter(func(n): return n is MeshInstance3D)
	check(meshes.size() == HEAD_PACK_DRAWN_MESHES, "drawn mesh count %d" % meshes.size())
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
