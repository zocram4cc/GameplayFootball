extends Node3D
## Shows one PES 2021 character from the user's install, optionally animated.
## Args after `--`: --pes21-root DIR --face ID [--team ID [--kit N]] [--clip NAME|0xHASH] [--in-place]
##   [--hide PART,...] [--edit-save EDIT00000000]  (each player's look from the user's save)
## With --team the player is assembled as PES does (kit N, 0 = 1st); without it,
## the face pack alone.

const CAMERA_POSITION := Vector3(0.0, 1.0, 3.2)  # metres; in front of a character facing +Z (Fox forward)
const CAMERA_TARGET := Vector3(0.0, 1.0, 0.0)    # about chest height of a 1.8 m player
const CAMERA_FOV_DEG := 40.0                     # frames a T-pose (1.54 m arm span) with margin
const SUN_ROTATION_DEG := Vector3(-50.0, 30.0, 0.0)
const FLOOR_SIZE_M := 6.0                       # ground at y = 0 (Fox and gani root height), for foot contact
const FLOOR_ALBEDO := Color(0.35, 0.45, 0.3)

var character: Node3D
var skeleton: Skeleton3D
var animation: PesAnimation
var in_place := false
var clock := 0.0


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
	if args.has("edit-save") and library.load_edit_save(args["edit-save"]) != OK:
		get_tree().quit(1)
		return
	var builder := PesCharacterBuilder.new()
	if args.has("team"):
		character = builder.build_player(library, int(args["team"]), int(args.get("kit", "0")), int(args["face"]))
	else:
		character = builder.build_face_model(library, int(args["face"]))
	if character == null:
		get_tree().quit(1)
		return
	add_child(character)
	skeleton = character.get_node("Skeleton")
	if args.has("hide"):  # inspection: drop parts by model stem, e.g. --hide socks_middle,arm
		var hidden: PackedStringArray = args["hide"].split(",")
		for m in skeleton.get_children():
			if m is MeshInstance3D and str(m.get_meta("part", "")) in hidden:
				m.visible = false
	in_place = args.has("in-place")
	if args.has("clip"):
		animation = PesAnimation.new()
		if animation.load(library, args["clip"]) != OK:
			push_error("clip not found: %s" % args["clip"])
			get_tree().quit(1)
			return
		print("clip ", args["clip"], ": ", animation.length_seconds(), " s")


func _process(delta: float) -> void:
	if animation == null:
		return
	clock += delta
	animation.apply(skeleton, fmod(clock, animation.length_seconds()), in_place)


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

	var floor_mesh := PlaneMesh.new()
	floor_mesh.size = Vector2(FLOOR_SIZE_M, FLOOR_SIZE_M)
	var floor_material := StandardMaterial3D.new()
	floor_material.albedo_color = FLOOR_ALBEDO
	floor_mesh.material = floor_material
	var ground := MeshInstance3D.new()
	ground.mesh = floor_mesh
	add_child(ground)

	var camera := Camera3D.new()
	camera.fov = CAMERA_FOV_DEG
	add_child(camera)
	camera.look_at_from_position(CAMERA_POSITION, CAMERA_TARGET)
	camera.current = true
