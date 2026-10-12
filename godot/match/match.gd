extends Node3D
## A visible AI-vs-AI match: the GF engine (GfMatch) plays, and every frame
## the PES players and the ball are placed where the simulation has them.
## Args (after `--`): --pes21-root DIR --home-team PES_ID --away-team PES_ID
## [--home-kit N --away-kit N] [--edit-save FILE] [--seed N] [--minutes N].

const GF_HOME_TEAM := 1        # teams in assets/fixtures/teams.sql
const GF_AWAY_TEAM := 2
const DEFAULT_SEED := 1000
const DEFAULT_MINUTES := 10
const TICK_S := 0.01           # GfMatch.step() is one 10 ms tick
const MAX_TICKS_PER_FRAME := 10
const BALL_RENDER_RADIUS_M := 0.110  # PES ball.fmdl bbox (docs/re/pes-re-officials-ball.md 6)
const BALL_ALBEDO := Color(0.95, 0.95, 0.95)
const PITCH_SURROUND_M := 120.0
const PITCH_ALBEDO := Color(0.22, 0.42, 0.18)
const SUN_ROTATION_DEG := Vector3(-55.0, 35.0, 0.0)
const CAMERA_POSITION := Vector3(0.0, 28.0, 62.0)  # placeholder overview until M4's broadcast camera
const CAMERA_FOV_DEG := 50.0
const FACE_DIR := "Asset/model/character/face/real/"
const PLAYERS_PER_TEAM := 11

var args := {}
var library: PesLibrary
var sim: GfMatch
var players: Array[Node3D] = []
var ball: MeshInstance3D
var _accumulator := 0.0


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
	_build_players()
	ball = MeshInstance3D.new()
	var sphere := SphereMesh.new()
	sphere.radius = BALL_RENDER_RADIUS_M
	sphere.height = 2.0 * BALL_RENDER_RADIUS_M
	var ball_material := StandardMaterial3D.new()
	ball_material.albedo_color = BALL_ALBEDO
	sphere.material = ball_material
	ball.mesh = sphere  # ponytail: plain sphere; the PES ball.fmdl comes with the stadium props (M5)
	add_child(ball)
	_place()


func _process(delta: float) -> void:
	if sim == null:
		return
	_accumulator += delta
	var ticks := 0
	while _accumulator >= TICK_S and ticks < MAX_TICKS_PER_FRAME and not sim.is_over():
		sim.step()
		_accumulator -= TICK_S
		ticks += 1
	_place()


func _place() -> void:
	for i in players.size():
		var p: Vector3 = sim.player_position(i)
		players[i].global_position = Vector3(p.x, 0.0, p.z)
		var f: Vector3 = sim.player_facing(i)
		if f.length_squared() > 0.0:
			players[i].rotation.y = atan2(f.x, f.z)  # PES characters face +Z
	ball.global_position = sim.ball_position()


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
	var camera := Camera3D.new()
	camera.fov = CAMERA_FOV_DEG
	add_child(camera)
	camera.look_at_from_position(CAMERA_POSITION, Vector3.ZERO)
	camera.current = true
