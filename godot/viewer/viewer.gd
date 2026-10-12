extends Node3D
## Shows one PES 2021 character from the user's install.
## Args after `--`: --pes21-root DIR --face ID [--clip NAME|0xHASH] [--in-place]

const CAMERA_POSITION := Vector3(0.0, 1.0, 3.2)  # metres; in front of a character facing +Z (Fox forward)
const CAMERA_TARGET := Vector3(0.0, 1.0, 0.0)    # about chest height of a 1.8 m player
const CAMERA_FOV_DEG := 40.0                     # frames a T-pose (1.54 m arm span) with margin
const SUN_ROTATION_DEG := Vector3(-50.0, 30.0, 0.0)

var character: Node3D


func _ready() -> void:
	var args := _parse_args(OS.get_cmdline_user_args())
	if not args.has("pes21-root") or not args.has("face"):
		push_error("usage: -- --pes21-root DIR --face ID [--clip NAME|0xHASH] [--in-place]")
		get_tree().quit(2)
		return
	_build_environment()
	var library := PesLibrary.new()
	if library.open(args["pes21-root"]) != OK:
		get_tree().quit(1)
		return
	character = PesCharacterBuilder.new().build_face_model(library, int(args["face"]))
	if character == null:
		get_tree().quit(1)
		return
	add_child(character)


func _parse_args(raw: PackedStringArray) -> Dictionary:
	var out := {}
	var i := 0
	while i < raw.size():
		var key := raw[i].trim_prefix("--")
		if i + 1 < raw.size() and not raw[i + 1].begins_with("--"):
			out[key] = raw[i + 1]
			i += 2
		else:
			out[key] = true
			i += 1
	return out


func _build_environment() -> void:
	var sky_material := ProceduralSkyMaterial.new()
	var sky := Sky.new()
	sky.sky_material = sky_material
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.ssao_enabled = true
	env.glow_enabled = false
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.rotation_degrees = SUN_ROTATION_DEG
	add_child(sun)

	var camera := Camera3D.new()
	camera.fov = CAMERA_FOV_DEG
	add_child(camera)
	camera.look_at_from_position(CAMERA_POSITION, CAMERA_TARGET)
	camera.current = true
