extends Node
## The match scene builds 22 PES players and keeps them where the simulation
## says they are. Exit 0/1, 77 skipped.

const SKIP := 77
const HOME_PES_TEAM := 767
const AWAY_PES_TEAM := 754
const PLAYERS := 22
const FRAMES := 30
const POSITION_TOLERANCE_M := 1e-3

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
	var scene: Node3D = load("res://match/match.tscn").instantiate()
	scene.configure({"pes21-root": root, "home-team": str(HOME_PES_TEAM), "away-team": str(AWAY_PES_TEAM)})
	add_child(scene)
	for i in FRAMES:
		await get_tree().process_frame
	var players: Array = scene.players
	check(players.size() == PLAYERS, "22 players built (%d)" % players.size())
	for i in players.size():
		var body: Node3D = players[i]
		check(body.get_node_or_null("Skeleton") != null, "player %d is a skinned PES character" % i)
		var want: Vector3 = scene.sim.player_position(i)
		check(body.global_position.distance_to(Vector3(want.x, 0.0, want.z)) < POSITION_TOLERANCE_M,
				"player %d stands where the simulation puts him" % i)
	check(scene.ball.global_position.distance_to(scene.sim.ball_position()) < POSITION_TOLERANCE_M, "ball follows the simulation")
	get_tree().quit(1 if failures > 0 else 0)
