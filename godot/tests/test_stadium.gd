extends Node
## PesModelBuilder.build_stadium(library, stadium_id): PES's own stadium scene,
## pitch, goals, nets and corner flags, each model placed at the transform PES's
## fox2 gives it. Exit 0/1, 77 skipped.

const SKIP := 77
const STADIUM_ID := 2  # st002, dt-shipped vanilla (mount report: dt30_g4.cpk > st002.cpk)

# PES's placements (pes::LoadStadiumScene on this install; stadium-camera RE 1.3):
# the goal mesh is authored at -X and mirrored for +X with a 180-degree Y quat.
const GOAL_FMDL := "Assets/pes16/model/bg/common/goal/scenes/goal.fmdl"
const GOAL_AUTHORED_FACE_X_M := -54.584  # goal.fmdl AABB min x
const GOAL_FRAME_FACE_X_M := 52.44       # the other end of that AABB, at the line
const GOAL_CROSSBAR_TOP_M := 2.56
const GOAL_HALF_WIDTH_M := 3.78
const GOAL_OPENINGS := 2
const NET_FMDL_PREFIX := "Assets/pes16/model/bg/common/goal/scenes/goal2"
const NET_LINE_X_M := 55.456             # goal2[LR]_net fmdl AABB |x|, behind the frame
const CORNER_LINE_X_M := 52.49
const CORNER_LINE_Z_M := 34.0           # PES FieldInfo +0x08, half width
const CORNER_FOOT_Y_M := -0.0672         # cf_st002_camp fmdl AABB min y
const CORNER_POLE_TOP_M := 1.57
const TOLERANCE_M := 0.02

var failures := 0


func check(cond: bool, what: String) -> void:
	if not cond:
		failures += 1
		printerr("FAIL: ", what)


func meshes_of(n: Node) -> Array[MeshInstance3D]:
	var out: Array[MeshInstance3D] = []
	for c in n.find_children("*", "MeshInstance3D", true, false):
		out.append(c)
	return out


func world_bounds(n: Node) -> AABB:
	var box := AABB()
	var first := true
	for m in meshes_of(n):
		var b: AABB = m.global_transform * m.mesh.get_aabb()
		box = b if first else box.merge(b)
		first = false
	return box


# The placed models of the stadium, grouped by the fmdl each one is.
func placements(scene: Node3D) -> Dictionary:
	var out := {}
	for c in scene.get_children():
		if c.has_meta("pes_member"):
			var key: String = c.get_meta("pes_member")
			if not out.has(key): out[key] = []
			out[key].append(c)
	return out


func _ready() -> void:
	var root := OS.get_environment("PES21_ROOT")
	if root.is_empty():
		print("SKIP: PES21_ROOT not set")
		get_tree().quit(SKIP)
		return
	var library := PesLibrary.new()
	check(library.open(root) == OK, "library opens")
	var builder := PesModelBuilder.new()

	var scene: Node3D = builder.build_stadium(library, STADIUM_ID)
	check(scene != null, "stadium 2 builds")
	if scene == null:
		get_tree().quit(1)
		return
	# In the tree, as in the match: global_transform is only maintained there.
	add_child(scene)
	var placed: Dictionary = placements(scene)

	# --- the pitch, PES's own mesh and shader ---
	check(placed.has("Assets/pes16/model/bg/st002/scenes/pitch_st002.fmdl"), "the pitch is placed")
	if placed.has("Assets/pes16/model/bg/st002/scenes/pitch_st002.fmdl"):
		var pitch: Node3D = placed["Assets/pes16/model/bg/st002/scenes/pitch_st002.fmdl"][0]
		var meshes := meshes_of(pitch)
		# The turf and the lines are in every pack; the per-stadium re-turf
		# patch (pitch_scratch) is an extra some packs ship and some do not, so
		# the test requires the two that are structural and reports the rest.
		check(meshes.size() >= 2, "the pitch pack draws the turf and the lines (%d)" % meshes.size())
		var turf := meshes[0]
		var b := turf.mesh.get_aabb()
		check(absf(b.position.x + 58.4125) < 0.001, "pitch x starts outside the touchline (%f)" % b.position.x)
		check(absf(b.end.x - 58.4125) < 0.001, "pitch x ends outside the touchline (%f)" % b.end.x)
		check(absf(b.position.z + 39.4176) < 0.001, "pitch z starts outside the goal line (%f)" % b.position.z)
		check(absf(b.end.z - 37.0525) < 0.001, "pitch z ends outside the goal line (%f)" % b.end.z)
		check(b.position.y >= 0.0 and b.end.y < 0.01, "pitch flat at y 0..0.008 (%s)" % b)
		var mat = turf.mesh.surface_get_material(0)
		check(mat is ShaderMaterial, "the turf is a ShaderMaterial")
		if mat is ShaderMaterial:
			var sm: ShaderMaterial = mat
			check(sm.shader.resource_path == "res://shaders/pes_pitch.gdshader", "turf shader is pes_pitch")
			check(sm.get_shader_parameter("Base_Tex_2_SRGB") != null, "turf reads the common grass colour")
			check(sm.get_shader_parameter("Grain_Tex_LIN") != null, "turf reads the grain map")
			check(sm.get_shader_parameter("SlopeTable_Tex_LIN") != null, "turf reads the slope table")
			check(sm.get_shader_parameter("Base_Tex_SRGB") != null, "turf reads the per-stadium pitch map")
			# Roles the pitch PS does not bind stay empty (report 7.6).
			check(sm.get_shader_parameter("NormalMap_Tex_NRM") != null, "the normal role is still bound by name")
		check(meshes[1].mesh.surface_get_material(0) is ShaderMaterial, "the lines are a shader material")
		# UVs are a float32x2 stream in this pack; the reader must land them in 0..1.
		var uv: PackedVector2Array = turf.mesh.surface_get_arrays(0)[4]
		var mn := Vector2(1e9, 1e9)
		var mx := Vector2(-1e9, -1e9)
		for v in uv:
			mn = Vector2(minf(mn.x, v.x), minf(mn.y, v.y))
			mx = Vector2(maxf(mx.x, v.x), maxf(mx.y, v.y))
		check(mn.x >= 0.0 and mn.y >= 0.0 and mx.x <= 1.0 and mx.y <= 1.0,
				"pitch UVs in 0..1 (%s .. %s)" % [mn, mx])

	# --- the two goals where PES places them ---
	check(placed.has(GOAL_FMDL), "the goal posts are placed")
	if placed.has(GOAL_FMDL):
		var goals: Array = placed[GOAL_FMDL]
		check(goals.size() == GOAL_OPENINGS, "two goal posts (%d)" % goals.size())
		if goals.size() == GOAL_OPENINGS:
			check(float(goals[0].get_meta("pes_rotation_y")) != float(goals[1].get_meta("pes_rotation_y")),
					"one goal mirrored 180 deg about Y")
			for g in goals:
				var gb: AABB = world_bounds(g)
				# The authored AABB is [-54.584 .. -52.44]; the frame face is the
				# end PES puts toward the pitch centre, the net end is the far one.
				var face: float = minf(absf(gb.position.x), absf(gb.end.x))
				var net_end: float = maxf(absf(gb.position.x), absf(gb.end.x))
				check(absf(face - GOAL_FRAME_FACE_X_M) < TOLERANCE_M,
						"goal frame face on the goal line at |x| %f" % face)
				check(net_end > face + 1.0, "the goal has depth behind the line (%f..%f)" % [face, net_end])
				check(absf(gb.end.y - GOAL_CROSSBAR_TOP_M) < TOLERANCE_M, "crossbar top 2.56 (%f)" % gb.end.y)
				check(absf(absf(gb.position.z) - GOAL_HALF_WIDTH_M) < TOLERANCE_M
						and absf(absf(gb.end.z) - GOAL_HALF_WIDTH_M) < TOLERANCE_M,
						"goal centred on the touchline axis (z %.2f..%.2f)" % [gb.position.z, gb.end.z])
		# The authored, unmirrored goal is at -X, where the report says PES ships it.
		var author_pos: Vector3 = world_bounds(goals[0]).position
		check(author_pos.x < 0.0, "the authored goal is at -x (%s)" % author_pos)

	# --- the nets behind each frame ---
	var nets := 0
	for key: String in placed:
		if key.begins_with(NET_FMDL_PREFIX): nets += placed[key].size()
	check(nets == GOAL_OPENINGS, "two nets (%d)" % nets)
	for key: String in placed:
		if not key.begins_with(NET_FMDL_PREFIX): continue
		for net in placed[key]:
			var nb: AABB = world_bounds(net)
			var nx: float = maxf(absf(nb.position.x), absf(nb.end.x))
			check(absf(nx - NET_LINE_X_M) < 0.05, "net frame behind the goal line at |x| %f" % nx)

	# --- the corner flags at the four corners ---
	var flags: Array = []
	for key: String in placed:
		if key.contains("cf_st002_camp_vat00"): flags.append_array(placed[key])
	check(flags.size() == 4, "four corner flags (%d)" % flags.size())
	for f in flags:
		var fb: AABB = world_bounds(f)
		check(absf(fb.position.y - CORNER_FOOT_Y_M) < TOLERANCE_M, "flag foot on the grass (%f)" % fb.position.y)
		check(absf(fb.end.y - CORNER_POLE_TOP_M) < TOLERANCE_M, "flag pole 1.57 high (%f)" % fb.end.y)
		var fx: Vector3 = f.position
		check(absf(absf(fx.x) - CORNER_LINE_X_M) < 0.1 and absf(absf(fx.z) - CORNER_LINE_Z_M) < TOLERANCE_M,
				"flag at a corner (%s)" % fx)

	# --- no flag stands on the centre spot ---
	# The cornerflag pack's MobCharacterModel is a skinned flag rig (it carries
	# sklFile, docs/re/pes-re-fox2-format.md 191), so PES positions it at runtime,
	# not by its fox2 TransformEntity. Its identity transform lands a flag pole in
	# the middle of the pitch, which PES never draws, so the builder must not
	# place it. Where PES draws it (the staff package) is an open RE item:
	# docs/re/pes-re-fox2-stadium.md 4.2, pes-re-stadium-camera.md.
	# The test walks the tree by hand (cheap, and it works before the scene is in
	# the tree) and reports any placed model whose vertices fall within 4 m of
	# the origin. The pitch itself covers the playing area by design, so its
	# members are excluded; the four corner flags are the models that must not
	# be near the centre.
	const CENTRE_CLEAR_RADIUS_M := 4.0
	const ON_PITCH := "Assets/pes16/model/bg/st002/scenes/pitch_st002.fmdl"
	var intruders: Array[String] = []
	var stack: Array = [[scene, Transform3D.IDENTITY]]
	while not stack.is_empty():
		var e: Array = stack.pop_back()
		var node: Node3D = e[0]
		var xf: Transform3D = e[1] * node.transform
		var mem := ""
		if node.has_meta("pes_member"): mem = node.get_meta("pes_member")
		if mem != "" and mem != ON_PITCH:
			var hit := false
			for m in node.find_children("*", "MeshInstance3D", true, false):
				var mi: MeshInstance3D = m
				var verts: PackedVector3Array = mi.mesh.surface_get_arrays(0)[0]
				var mxf: Transform3D = xf * mi.transform
				for v in verts:
					var p: Vector3 = mxf * v
					if absf(p.x) < CENTRE_CLEAR_RADIUS_M and absf(p.z) < CENTRE_CLEAR_RADIUS_M and p.y > -1.0 and p.y < 3.0:
						hit = true
						break
				if hit: break
			if hit: intruders.append(mem)
		for c in node.get_children():
			if c is Node3D:
				stack.append([c, xf])
	check(intruders.is_empty(), "no model stands on the centre spot (%s)" % [", ".join(intruders)])

	# --- the stadium scene surrounds the pitch, centred on it ---
	var scene_meshes: Array = []
	for key: String in placed:
		if key.contains("/st002/scenes/"): scene_meshes.append_array(meshes_of(placed[key][0]))
	check(scene_meshes.size() > 0, "the stadium scene is around the pitch (%d meshes)" % scene_meshes.size())
	var ob := AABB()
	var first := true
	for m in scene_meshes:
		var b2: AABB = m.global_transform * m.mesh.get_aabb()
		ob = b2 if first else ob.merge(b2)
		first = false
	check(ob.size.x > 100.0 and ob.size.z > 100.0, "the surround is bigger than the pitch (%s)" % ob)
	# The surround covers the pitch: the pitch is inside it on every axis.
	var pitch_box: AABB = world_bounds(placed["Assets/pes16/model/bg/st002/scenes/pitch_st002.fmdl"][0])
	check(pitch_box.position.x > ob.position.x and pitch_box.end.x < ob.end.x
			and pitch_box.position.z > ob.position.z and pitch_box.end.z < ob.end.z
			and pitch_box.position.y > ob.position.y,
			"the surround encloses the pitch (%s in %s)" % [pitch_box, ob])
	check(ob.position.y < 0.0 and ob.end.y > 5.0, "the surround rises above the pitch (%s)" % ob)

	# --- every mesh is inside the stadium's own authored bounds ---
	for m in meshes_of(scene):
		var b3: AABB = m.global_transform * m.mesh.get_aabb()
		check(b3.position.y >= -400.0, "nothing buries below the stadium shell: min y %f" % b3.position.y)
		check(b3.end.y <= 1785.0, "nothing rises above the stadium shell: max y %f" % b3.end.y)

	var missing: Node3D = builder.build_stadium(library, 9999)
	check(missing == null, "an unknown stadium builds nothing")
	get_tree().quit(1 if failures > 0 else 0)
