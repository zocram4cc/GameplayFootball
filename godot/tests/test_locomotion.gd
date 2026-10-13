extends SceneTree
## PesLocomotion plays PES's speed blend (Parametricblendtable group 80) at a
## shared phase (docs/re/pes-re-animselect.md 2.6, 4.3.3, 5.2). Exit 0/1, 77 skipped.

const SKIP := 77
const LIVE_RATE_HALF_SPEED_MPS := 3.88  # live: blend rate 0.5 held 3.88 m/s (report 5.2)
const LIVE_RATE_HALF := 0.5
const RATE_TOLERANCE := 0.05
const STILL_SPEED_MPS := 0.3            # under PES's "still" class bound
const WALK_SPEED_MPS := 1.5
const SPRINT_SPEED_MPS := 8.5
const MIN_POSE_CHANGE := 0.05           # quaternion component distance, summed over joints
# A vanilla face pack first (dt-shipped), then the 4cc ones this owner has.
const FACE_CANDIDATES := [100117, 100135, 100393, 76711, 75403, 70922]

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


func pose_of(skeleton: Skeleton3D) -> Array:
	var out := []
	for j in skeleton.get_bone_count():
		out.append(skeleton.get_bone_pose_rotation(j))
	return out


func difference(a: Array, b: Array) -> float:
	var d := 0.0
	for j in a.size():
		# Component distance: angle_to's acos reads float32 noise as ~7e-4 rad.
		var qa: Quaternion = a[j]
		var qb: Quaternion = b[j]
		d += minf((qa - qb).length(), (qa + qb).length())
	return d


func _initialize() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")
	var loco := PesLocomotion.new()
	check(loco.load(library) == OK, "locomotion loads")
	check(absf(loco.rate_for_speed(LIVE_RATE_HALF_SPEED_MPS) - LIVE_RATE_HALF) < RATE_TOLERANCE,
			"rate at 3.88 m/s is PES's live 0.5 (%f)" % loco.rate_for_speed(LIVE_RATE_HALF_SPEED_MPS))
	check(loco.is_still(STILL_SPEED_MPS) and not loco.is_still(WALK_SPEED_MPS), "still below PES's walk class")
	check(loco.phase_rate(SPRINT_SPEED_MPS) > loco.phase_rate(WALK_SPEED_MPS), "cadence rises with speed")

	# The skeleton under test is whatever face pack this install ships: the
	# locomotion tables are install-wide, and the body rig is the same one.
	var character: Node3D = null
	for face in FACE_CANDIDATES:
		character = PesCharacterBuilder.new().build_face_model(library, face)
		if character != null:
			break
	if character == null:
		print("SKIP: no face pack in this install to skeletonise")
		quit(SKIP)
		return
	get_root().add_child(character)
	await process_frame  # the root enters the tree after _initialize
	var skeleton: Skeleton3D = character.get_node("Skeleton")
	check(loco.apply(skeleton, WALK_SPEED_MPS, 0.0) == OK, "apply at walk")
	var walk0 := pose_of(skeleton)
	loco.apply(skeleton, WALK_SPEED_MPS, 0.5)
	check(difference(walk0, pose_of(skeleton)) > MIN_POSE_CHANGE, "the stride moves with the phase")
	loco.apply(skeleton, SPRINT_SPEED_MPS, 0.0)
	check(difference(walk0, pose_of(skeleton)) > MIN_POSE_CHANGE, "a sprint differs from a walk at the same phase")
	loco.apply(skeleton, WALK_SPEED_MPS, 0.0)
	check(difference(walk0, pose_of(skeleton)) < 1e-4, "same speed and phase, same pose")
	var hips: Vector3 = skeleton.get_bone_pose_position(0)
	check(absf(hips.x) < 1e-6 and absf(hips.z) < 1e-6, "in place: the simulation moves the player")
	quit(1 if failures > 0 else 0)
