extends SceneTree
## PesBroadcastCamera films play like PES's Dynamic Wide camera
## (docs/re/pes-re-stadium-camera.md 4.3-4.6, docs/re/pes-re-camera-target.md).
## Exit 0/1, 77 skipped.

const SKIP := 77
const FIELD_HALF_WIDTH_M := 34.06  # FieldInfo +0x08 (live); the match takes it from the sim
const FIELD_HALF_LENGTH_M := 52.56  # FieldInfo +0x00 (live)
const STEP_S := 1.0 / 54.0          # PES's frame-rate reference, read from the exe
const SETTLE_STEPS := 600
const POSITION_TOLERANCE_M := 0.05
const LIVE_ZOOM := 0.80            # owner's sliders, read live off the plugin object (camera-live.md 2)
const LIVE_HEIGHT := 0.20          # (the 0.79 fit predates the u x100 correction)
const LIVE_VFOV_DEG := 23.29       # settled live row, target z at the -8 clamp (camera-live.md 3)
const VFOV_TOLERANCE_DEG := 0.1
const NEAR_PLANE_M := 0.9          # InplayCamera::update, modes other than 2/3/4

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


func _initialize() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")
	var cam := PesBroadcastCamera.new()
	check(cam.load(library, FIELD_HALF_WIDTH_M, FIELD_HALF_LENGTH_M) == OK, "camera loads")
	cam.set_sliders(LIVE_ZOOM, LIVE_HEIGHT)
	var camera := Camera3D.new()
	get_root().add_child(camera)
	await process_frame

	# Play on the far side: the target z clamps to the mode-5 minimum, giving the
	# live sample's lens.
	var focus := Vector3(-5.31, 0.11, -20.0)
	cam.reset(focus)
	for i in SETTLE_STEPS:
		cam.update(camera, focus, STEP_S)
	check(absf(camera.fov - LIVE_VFOV_DEG) < VFOV_TOLERANCE_DEG, "live vFOV (%f)" % camera.fov)
	check(camera.global_position.z > 0.0, "camera on the +Z touchline side")
	check(absf(camera.near - NEAR_PLANE_M) < 1e-6, "near plane 0.9")
	var forward := -camera.global_transform.basis.z
	check(forward.z < 0.0 and forward.y < 0.0, "looking across and down at the pitch")

	# Smoothing: one step after the ball jumps, the camera has moved only part way.
	var before := camera.global_position
	cam.update(camera, Vector3(30.0, 0.11, -20.0), STEP_S)
	var after := camera.global_position
	check(after.x > before.x + 0.1, "camera pans towards the play")
	for i in SETTLE_STEPS:
		cam.update(camera, Vector3(30.0, 0.11, -20.0), STEP_S)
	check(camera.global_position.x - after.x > 1.0, "and keeps easing after the first step")
	quit(1 if failures > 0 else 0)
