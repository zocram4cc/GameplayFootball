extends SceneTree
## GfMatch drives the GameplayFootball engine from Godot, tick by tick, and
## reports the match in Fox/Godot space (Y up, metres). Exit 0/1, 77 skipped.

const SKIP := 77
const HOME := 1
const AWAY := 2
const SEED := 1000
const MINUTES := 1
const TICKS := 600              # 6 s of play at 10 ms
const PLAYERS := 22
# GF's pitch is 110 x 72 m incl. surroundings; everything stays inside it.
const PITCH_HALF_LENGTH_M := 60.0
const PITCH_HALF_WIDTH_M := 40.0
const MIN_TRAVEL_M := 1.0

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


func run(library: PesLibrary, ticks: int) -> GfMatch:
	var m := GfMatch.new()
	var sql := ProjectSettings.globalize_path("res://../assets/fixtures/teams.sql")
	var anims := ProjectSettings.globalize_path("res://../assets/gf_anims")
	check(m.start(library, sql, anims, HOME, AWAY, SEED, MINUTES) == OK, "match starts")
	for i in ticks:
		m.step()
	return m


func _init() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")

	var m := GfMatch.new()
	check(m.start(library, ProjectSettings.globalize_path("res://../assets/fixtures/teams.sql"),
			ProjectSettings.globalize_path("res://../assets/gf_anims"), HOME, AWAY, SEED, MINUTES) == OK, "match starts")
	check(m.player_count() == PLAYERS, "22 players on the pitch (%d)" % m.player_count())
	var start := []
	for i in m.player_count():
		start.append(m.player_position(i))
	for t in TICKS:
		m.step()
	var moved := 0
	for i in m.player_count():
		var p: Vector3 = m.player_position(i)
		check(absf(p.x) < PITCH_HALF_LENGTH_M and absf(p.z) < PITCH_HALF_WIDTH_M and p.y > -0.5,
				"player %d on the pitch in Fox space (%s)" % [i, p])
		if p.distance_to(start[i]) > MIN_TRAVEL_M:
			moved += 1
		check(m.player_team(i) == (0 if i < PLAYERS / 2 else 1), "player %d team" % i)
	check(moved > 0, "players move during play (%d moved)" % moved)
	check(m.ball_position().y >= 0.0, "ball above the grass")

	# Same seed, same match: the GF engine is deterministic.
	var again := run(library, TICKS)
	for i in m.player_count():
		check(again.player_position(i).is_equal_approx(m.player_position(i)), "player %d deterministic" % i)
	quit(1 if failures > 0 else 0)
