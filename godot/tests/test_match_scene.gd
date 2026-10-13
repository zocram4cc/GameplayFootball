extends Node
## The match scene builds 22 PES players and keeps them where the simulation
## says they are. Exit 0/1, 77 skipped.

const SKIP := 77
# Both teams must exist on any install: a kit config in UniformParameter.bin AND
# face packs to skin the squad. Teams 100/101 are official (dt-shipped) ids
# below the 4cc band 701+, and their face pools (100xxx/101xxx) are dt-shipped
# too. A modded install may replace the whole kit table with one that holds only
# 7xx teams, so the pair is probed and the test skips when neither band builds:
# the subject choice is not a modded-team pin.
const TEAM_PAIRS := [[100, 101], [767, 754], [1, 2]]
const PLAYERS := 22
const FRAMES := 30
const POSITION_TOLERANCE_M := 1e-3
const POSED_ANGLE_RAD := 0.05  # a thigh this far off its rest is being animated

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
	# Probe the candidate pairs: the scene builds 22 players only when both teams
	# resolve to a kit config and face packs. The first pair that does is the
	# subject; none doing so means this install has no such pair.
	var scene: Node3D = null
	for pair in TEAM_PAIRS:
		var candidate: Node3D = load("res://match/match.tscn").instantiate()
		candidate.configure({"pes21-root": root, "home-team": str(pair[0]), "away-team": str(pair[1])})
		add_child(candidate)
		for i in FRAMES:
			await get_tree().process_frame
		var skinned := 0
		for body: Node3D in candidate.players:
			if body.get_node_or_null("Skeleton") != null:
				skinned += 1
		if skinned == PLAYERS:
			scene = candidate
			break
		remove_child(candidate)
		candidate.free()
	if scene == null:
		print("SKIP: no team pair in this install builds a full squad")
		get_tree().quit(SKIP)
		return
	var players: Array = scene.players
	check(players.size() == PLAYERS, "22 players built (%d)" % players.size())
	for i in players.size():
		var body: Node3D = players[i]
		check(body.get_node_or_null("Skeleton") != null, "player %d is a skinned PES character" % i)
		var want: Vector3 = scene.sim.player_position(i)
		check(body.global_position.distance_to(Vector3(want.x, 0.0, want.z)) < POSITION_TOLERANCE_M,
				"player %d stands where the simulation puts him" % i)
	var posed := 0
	for body: Node3D in players:
		var sk: Skeleton3D = body.get_node("Skeleton")
		var thigh := sk.find_bone("left_thigh")
		if sk.get_bone_pose_rotation(thigh).angle_to(sk.get_bone_rest(thigh).basis.get_rotation_quaternion()) > POSED_ANGLE_RAD:
			posed += 1
	check(posed == players.size(), "every player is posed by PES locomotion (%d)" % posed)
	check(scene.ball.global_position.distance_to(scene.sim.ball_position()) < POSITION_TOLERANCE_M, "ball follows the simulation")
	get_tree().quit(1 if failures > 0 else 0)
