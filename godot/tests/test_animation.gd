extends Node
## PesAnimation drives a PES body clip onto the character skeleton. Runs as a
## scene so the skeleton is inside the tree, as in the viewer: Godot 4.7's pose
## setters only invalidate cached global poses for skeletons inside the tree.
## Exit 0 pass, 1 fail, 77 skipped (no PES21_ROOT).

const SKIP := 77
const FACE := 70922
const CLIP := "sliding_0_0_000"       # a PES body clip (resolved by name)
const CLIP_BY_HASH := "0x2c5e8a2e"    # the first clip of body_anime_file0, by hash
const MOVED_DOT := 0.9999             # |dot(rest, posed)| below this = a visibly rotated joint
const BODY_HEIGHT_RANGE_M := Vector2(0.05, 2.0)
# The linesman's flag-hand layer: one hand grips the flag (hatanigiri), the other
# keeps another pose (pes-re-animselect.md 11.5).
const LINESMAN_FLAG_CLIP := "layer_assistant_flag_hand_R"

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


func _ready() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		get_tree().quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")
	var character: Node3D = PesCharacterBuilder.new().build_face_model(library, FACE)
	var skeleton := character.get_node("Skeleton") as Skeleton3D

	var anim := PesAnimation.new()
	check(anim.load(library, CLIP) == OK, "clip loads by name")
	check(anim.length_seconds() > 0.0, "clip has a length")
	check(PesAnimation.new().load(library, CLIP_BY_HASH) == OK, "clip loads by hash")
	check(PesAnimation.new().load(library, "no_such_clip_anywhere") != OK, "unknown clip fails")

	# Out of the tree a pose would silently never reach the global poses.
	check(anim.apply(skeleton, 0.0, false) == ERR_UNCONFIGURED, "apply refuses an out-of-tree skeleton")

	add_child(character)
	var hip := skeleton.find_bone("hip")
	var thigh := skeleton.find_bone("left_thigh")
	var rest_thigh_global := skeleton.get_bone_global_pose(thigh)
	check(anim.apply(skeleton, anim.length_seconds() * 0.5, false) == OK, "apply in tree")
	var moved := 0
	for j in skeleton.get_bone_count():
		if absf(skeleton.get_bone_pose_rotation(j).dot(Quaternion.IDENTITY)) < MOVED_DOT:
			moved += 1
	check(moved >= 10, "a sliding tackle rotates many joints (%d moved)" % moved)
	check(not skeleton.get_bone_global_pose(thigh).is_equal_approx(rest_thigh_global), "global pose updated after apply")

	var body_y := skeleton.get_bone_pose_position(0).y
	check(body_y > BODY_HEIGHT_RANGE_M.x and body_y < BODY_HEIGHT_RANGE_M.y, "body height plausible (%f)" % body_y)

	anim.apply(skeleton, anim.length_seconds(), true)
	var body := skeleton.get_bone_pose_position(0)
	check(is_zero_approx(body.x) and is_zero_approx(body.z), "in-place zeroes body XZ")
	check(skeleton.get_bone_pose_rotation(hip).is_normalized(), "hip rotation is a unit quaternion")

	# Hand rig: every clip poses the fingers (its own timeline, or the engine's
	# default pose), and the linesman's two hands differ.
	var fingers := ["left_index_pip", "left_middle_mcp", "right_index_pip"]
	anim.apply(skeleton, anim.length_seconds() * 0.5, true)
	for name in fingers:
		var q := skeleton.get_bone_pose_rotation(skeleton.find_bone(name))
		check(absf(q.dot(Quaternion.IDENTITY)) < MOVED_DOT, "%s is posed, not at rest" % name)
	var flag := PesAnimation.new()
	check(flag.load(library, LINESMAN_FLAG_CLIP) == OK, "linesman flag clip loads")
	flag.apply(skeleton, flag.length_seconds() * 0.5, true)
	var l := skeleton.get_bone_pose_rotation(skeleton.find_bone("left_index_pip"))
	var r := skeleton.get_bone_pose_rotation(skeleton.find_bone("right_index_pip"))
	var r_mirrored := Quaternion(r.x, -r.y, -r.z, r.w)
	check(absf(l.dot(r_mirrored)) < MOVED_DOT, "linesman hands hold different poses")

	get_tree().quit(1 if failures > 0 else 0)
