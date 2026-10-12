extends Node3D
## A visible AI-vs-AI match: the GF engine (GfMatch) plays, and every frame
## the PES players and the ball are placed where the simulation has them.
## Args (after `--`): --pes21-root DIR --home-team PES_ID --away-team PES_ID
## [--home-kit N --away-kit N] [--edit-save FILE] [--seed N] [--minutes N]
## [--ball ID] [--follow PLAYER_INDEX] (inspection: a close camera on one player).

const GF_HOME_TEAM := 1        # teams in assets/fixtures/teams.sql
const GF_AWAY_TEAM := 2
const DEFAULT_SEED := 1000
const DEFAULT_MINUTES := 10
const TICK_S := 0.01           # GfMatch.step() is one 10 ms tick
const MAX_TICKS_PER_FRAME := 10
# ponytail: PES picks the default ball from the user's settings
# (pes-re-officials-ball.md 1); ball001, the first vanilla ball, until that is read.
const DEFAULT_BALL_ID := 1
const BALL_PACK_FORMAT := "Asset/model/ball/ball%03d/#Win/ball.fpk"
const PITCH_SURROUND_M := 120.0
const PITCH_ALBEDO := Color(0.22, 0.42, 0.18)
const SUN_ROTATION_DEG := Vector3(-55.0, 35.0, 0.0)
const FACE_DIR := "Asset/model/character/face/real/"
const FOLLOW_OFFSET := Vector3(4.0, 2.2, 6.0)  # inspection camera, metres from the followed player
const FOLLOW_TARGET_HEIGHT_M := 1.0

var args := {}
var library: PesLibrary
var sim: GfMatch
var players: Array[Node3D] = []
var ball: Node3D
var locomotion: PesLocomotion
var _skeletons: Array[Skeleton3D] = []
var _phases: PackedFloat64Array = []
var _accumulator := 0.0
var _camera: Camera3D
var _broadcast: PesBroadcastCamera


func configure(a: Dictionary) -> void:
	args = a


func _ready() -> void:
	if args.is_empty():
		args = _parse_args(OS.get_cmdline_user_args())
	if not args.has("pes21-root") or not args.has("home-team") or not args.has("away-team"):
		push_error("usage: -- --pes21-root DIR --home-team PES_ID --away-team PES_ID")
		get_tree().quit(2)
		return
	_build_environment()
	library = PesLibrary.new()
	if library.open(args["pes21-root"]) != OK:
		get_tree().quit(1)
		return
	if args.has("edit-save") and library.load_edit_save(args["edit-save"]) != OK:
		get_tree().quit(1)
		return
	sim = GfMatch.new()
	var sql := ProjectSettings.globalize_path("res://../assets/fixtures/teams.sql")
	var anims := ProjectSettings.globalize_path("res://../assets/gf_anims")
	if sim.start(library, sql, anims, GF_HOME_TEAM, GF_AWAY_TEAM, int(args.get("seed", DEFAULT_SEED)),
			int(args.get("minutes", DEFAULT_MINUTES))) != OK:
		get_tree().quit(1)
		return
	locomotion = PesLocomotion.new()
	if locomotion.load(library) != OK:
		locomotion = null  # bodies keep the bind pose; the match still plays
		push_warning("PES locomotion unavailable")
	_build_players()
	_broadcast = PesBroadcastCamera.new()
	if _broadcast.load(library, sim.field_half_width()) != OK:
		get_tree().quit(1)
		return
	_broadcast.reset(sim.ball_position())
	ball = PesModelBuilder.new().build_model(library, BALL_PACK_FORMAT % int(args.get("ball", DEFAULT_BALL_ID)), "")
	if ball == null:
		push_error("ball model missing")
		get_tree().quit(1)
		return
	add_child(ball)
	_place(0.0)


func _process(delta: float) -> void:
	if sim == null:
		return
	_accumulator += delta
	var ticks := 0
	while _accumulator >= TICK_S and ticks < MAX_TICKS_PER_FRAME and not sim.is_over():
		sim.step()
		_accumulator -= TICK_S
		ticks += 1
	_place(delta)


# ponytail: every player plays steady locomotion; kicks, turns, tackles and
# keeper actions (PES Animetable/Freemove selection) are not chosen yet.
func _place(delta: float) -> void:
	for i in players.size():
		var p: Vector3 = sim.player_position(i)
		players[i].global_position = Vector3(p.x, 0.0, p.z)
		var f: Vector3 = sim.player_facing(i)
		if f.length_squared() > 0.0:
			players[i].rotation.y = atan2(f.x, f.z)  # PES characters face +Z
		if locomotion != null and _skeletons[i] != null:
			var v: Vector3 = sim.player_velocity(i)
			var speed := Vector2(v.x, v.z).length()
			_phases[i] = fmod(_phases[i] + delta * locomotion.phase_rate(speed), 1.0)
			locomotion.apply(_skeletons[i], speed, _phases[i])
	ball.global_position = sim.ball_position()
	if args.has("follow"):
		var p: Vector3 = players[int(args["follow"])].global_position
		_camera.look_at_from_position(p + FOLLOW_OFFSET, p + Vector3.UP * FOLLOW_TARGET_HEIGHT_M)
	elif _broadcast != null:
		_broadcast.update(_camera, sim.ball_position(), delta)


# One PES face per GF slot, in folder order: real/<team><NN>.
# ponytail: squad order and shirt numbers are not read yet (PES team/squad db).
func _team_faces(team: int) -> Array[int]:
	var faces: Array[int] = []
	var prefix := FACE_DIR + str(team)
	for path: String in library.list(prefix):
		var id := int(path.trim_prefix(FACE_DIR).get_slice("/", 0))
		if path.ends_with("/face.fpk") and not faces.has(id):
			faces.append(id)
	faces.sort()
	return faces


func _build_players() -> void:
	var builder := PesCharacterBuilder.new()
	var teams := [int(args["home-team"]), int(args["away-team"])]
	var kits := [int(args.get("home-kit", 0)), int(args.get("away-kit", 1))]
	var faces := [_team_faces(teams[0]), _team_faces(teams[1])]
	var slot := [0, 0]
	for i in sim.player_count():
		var t: int = sim.player_team(i)
		var pool: Array[int] = faces[t]
		var face: int = pool[slot[t] % pool.size()] if not pool.is_empty() else 0
		slot[t] += 1
		var body: Node3D = builder.build_player(library, teams[t], kits[t], face)
		if body == null:
			push_error("player %d (face %d) failed to build" % [i, face])
			body = Node3D.new()
		add_child(body)
		players.append(body)
		_skeletons.append(body.get_node_or_null("Skeleton"))
		_phases.append(0.0)


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
	var sky := Sky.new()
	sky.sky_material = ProceduralSkyMaterial.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.rotation_degrees = SUN_ROTATION_DEG
	add_child(sun)
	# ponytail: flat green until the PES pitch (M5); line-accurate size only.
	var plane := PlaneMesh.new()
	plane.size = Vector2(PITCH_SURROUND_M, PITCH_SURROUND_M)
	var grass := StandardMaterial3D.new()
	grass.albedo_color = PITCH_ALBEDO
	plane.material = grass
	var ground := MeshInstance3D.new()
	ground.mesh = plane
	add_child(ground)
	_camera = Camera3D.new()
	add_child(_camera)
	_camera.current = true
