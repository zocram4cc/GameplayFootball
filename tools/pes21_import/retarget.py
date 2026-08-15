"""PES <-> GameplayFootball skeleton bridge.

Since the native-rig migration the engine's skeleton IS the PES animated
rig, 1:1: GameplayFootball's player.object carries twenty nodes, one per
bone of PES's body animation rig (body_skel.frig), at the PES bind
positions (Fox coords mapped (x, y, z) -> (x, -z, y) into GF's Z-up,
faces -Y frame), with identity bind rotations (Fox skeletons are
world-aligned). The sixteen legacy node names are kept -- engine code
refers to them -- and four nodes are new: hip (dsk_hip), chest (sk_chest),
left_clavicle / right_clavicle (sk_shoulder_l/r).

Everything else PES has (dsk_* skin helpers, skh_* fingers, skf_* face
muscles, cloth bones) is never independently animated by the body ganis;
under the engine's inverse-bind skinning, collapsing such a bone onto the
animated bone it rigidly follows is mathematically lossless, so skin
weights resolve through HELPER_TO_GF/resolve_bone below. (What IS lost:
Fox's runtime constraints -- twist distribution on dsk_forearm/dsk_thigh,
clavicle aim -- which GF does not emulate. Documented, not accidental.)

The authoritative bone data comes from the base package's body.skl
(pes_skl.py); PES_BIND below matches it and body_skel.frig.
"""

# ---------------------------------------------------------------------------
# Animation-skeleton data (decoded from dt13 body_skel.frig + .gani files).
#
# A PES body gani has 15 track units / 27 segments. Units follow the frig's
# order (unit 0's name hash is StrCode32("RIG_ROOT")); the map below assigns
# each rotation segment to its bone. The vec3 segments inside the chain units
# (legs seg0, arms seg1) are auxiliary IK channels, often 0xFF-filled - skip.
PES_TRACK_MAP = {
    (2, 0): "dsk_hip",
    (3, 1): "sk_thigh_l", (3, 2): "sk_leg_l", (4, 0): "sk_foot_l",
    (5, 1): "sk_thigh_r", (5, 2): "sk_leg_r", (6, 0): "sk_foot_r",
    (7, 0): "sk_belly", (8, 0): "sk_chest",
    (9, 0): "sk_neck", (10, 0): "sk_head",
    (11, 0): "sk_shoulder_l", (11, 2): "sk_upperarm_l", (11, 3): "sk_forearm_l",
    (12, 0): "sk_hand_l",
    (13, 0): "sk_shoulder_r", (13, 2): "sk_upperarm_r", (13, 3): "sk_forearm_r",
    (14, 0): "sk_hand_r",
}
# root motion: unit 0 = RIG_ROOT (quat + XZ position), unit 1 = the pelvis
# mover ("motion" node, hash 3ca4c491): quat + Y position, bind-relative.
PES_ROOT_UNIT = 0
PES_MOTION_UNIT = 1

# Position tracks store IEEE half-floats with the exponent rebased +7 (x128).
# Reading the result as millimetres (metres = raw / 128 / 1000) is what the
# entrance/cutscene export was built on, and it is kept here so that content
# does not move under anyone's feet.
PES_POS_TO_M = 1.0 / 128000.0

# For match animation that value is demonstrably too small by ~6x: at
# 1/128000 a sprinter's ankles never come within 15 cm of the pitch and a
# sliding tackle keeps its pelvis at standing height, swimming through the
# air. GameplayFootball reads velocity and every ball contact off the root
# track, so the scale was measured rather than assumed --
# calibrate_pos_scale.py sweeps it against three properties any human
# animation has (stance feet do not slide, ankles reach the grass and no
# further, a player lying down has his pelvis ~0.14 m up). Those three agree
# on a shallow optimum around 1/20000; 1/20480 = 2^11 * 10 sits inside it and
# is the kind of constant an engine actually stores.
PES_POS_TO_M_GAMEPLAY = 1.0 / 20480.0

# Bind pose (Fox coords: Y up, +Z forward, metres), from the base package's
# body.skl (== the fmdl bone tables); animation frames are world-aligned
# (identity bind rotations - Fox skeletons store positions only).
PES_BIND = {
    # bone: (global bind position, parent)
    "motion":        ((0.0000, 0.9921, -0.0184), None),        # = dsk_hip pos
    "dsk_hip":       ((0.0000, 0.9921, -0.0184), "motion"),
    "sk_thigh_l":    ((0.0900, 0.9600, 0.0529), "dsk_hip"),
    "sk_leg_l":      ((0.1413, 0.5433, 0.0878), "sk_thigh_l"),
    "sk_foot_l":     ((0.1940, 0.1072, 0.0408), "sk_leg_l"),
    "sk_thigh_r":    ((-0.0900, 0.9600, 0.0529), "dsk_hip"),
    "sk_leg_r":      ((-0.1413, 0.5433, 0.0878), "sk_thigh_r"),
    "sk_foot_r":     ((-0.1940, 0.1072, 0.0408), "sk_leg_r"),
    "sk_belly":      ((0.0000, 1.0961, 0.0000), "motion"),
    "sk_chest":      ((0.0000, 1.2628, 0.0138), "sk_belly"),
    "sk_neck":       ((0.0000, 1.5321, 0.0335), "sk_chest"),
    "sk_head":       ((0.0000, 1.6401, 0.0543), "sk_neck"),
    "sk_shoulder_l": ((0.1051, 1.4671, 0.0335), "sk_chest"),
    "sk_upperarm_l": ((0.1950, 1.4671, 0.0335), "sk_shoulder_l"),
    "sk_forearm_l":  ((0.3991, 1.2620, 0.0138), "sk_upperarm_l"),
    "sk_hand_l":     ((0.6035, 1.0639, 0.0695), "sk_forearm_l"),
    "sk_shoulder_r": ((-0.1051, 1.4671, 0.0335), "sk_chest"),
    "sk_upperarm_r": ((-0.1950, 1.4671, 0.0335), "sk_shoulder_r"),
    "sk_forearm_r":  ((-0.3991, 1.2620, 0.0138), "sk_upperarm_r"),
    "sk_hand_r":     ((-0.6035, 1.0639, 0.0695), "sk_forearm_r"),
}


def fox_to_gf(v):
    """Fox coords (Y up, +Z forward) -> GF coords (Z up, faces -Y)."""
    return (v[0], -v[2], v[1])


# ---------------------------------------------------------------------------
# The engine's native skeleton: GF node -> PES bone, in player.object DFS
# order. The list order IS the joint-ID order (vertex-colour weight
# encoding: channel = jointID*10 + weight*9).
GF_NODES = [
    # (GF node, PES bone, GF parent)
    ("body",           "motion",        None),
    ("hip",            "dsk_hip",       "body"),
    ("left_thigh",     "sk_thigh_l",    "hip"),
    ("left_knee",      "sk_leg_l",      "left_thigh"),
    ("left_ankle",     "sk_foot_l",     "left_knee"),
    ("right_thigh",    "sk_thigh_r",    "hip"),
    ("right_knee",     "sk_leg_r",      "right_thigh"),
    ("right_ankle",    "sk_foot_r",     "right_knee"),
    ("middle",         "sk_belly",      "body"),
    ("chest",          "sk_chest",      "middle"),
    ("neck",           "sk_neck",       "chest"),
    ("head",           "sk_head",       "neck"),
    ("left_clavicle",  "sk_shoulder_l", "chest"),
    ("left_shoulder",  "sk_upperarm_l", "left_clavicle"),
    ("left_elbow",     "sk_forearm_l",  "left_shoulder"),
    ("left_hand",      "sk_hand_l",     "left_elbow"),
    ("right_clavicle", "sk_shoulder_r", "chest"),
    ("right_shoulder", "sk_upperarm_r", "right_clavicle"),
    ("right_elbow",    "sk_forearm_r",  "right_shoulder"),
    ("right_hand",     "sk_hand_r",     "right_elbow"),
]

GF_JOINT_ORDER = [name for name, _, _ in GF_NODES]
JOINT_ID = {name: i for i, name in enumerate(GF_JOINT_ORDER)}
GF_PARENT = {name: parent for name, _, parent in GF_NODES}
PES_OF_GF = {name: bone for name, bone, _ in GF_NODES}
GF_OF_PES = {bone: name for name, bone, _ in GF_NODES}


def _build_gf_bind():
    """GF node -> (local offset from parent, parent), GF coords, from PES_BIND."""
    world = {bone: fox_to_gf(pos) for bone, (pos, _) in PES_BIND.items()}
    bind = {}
    for name, bone, parent in GF_NODES:
        w = world[bone]
        if parent is None:
            bind[name] = (w, None)
        else:
            p = world[PES_OF_GF[parent]]
            bind[name] = ((w[0] - p[0], w[1] - p[1], w[2] - p[2]), parent)
    return bind


# GF node -> (offset from parent node, parent node). This is exactly what
# data/media/objects/players/player.object encodes (see
# gen_player_object.py, which writes that file from this table).
GF_BIND = _build_gf_bind()

# The body node's height above the ground in the bind pose (the .anim
# player-line origin): PES's "motion" mover.
GF_BODY_HEIGHT = PES_BIND["motion"][0][1]


def gf_world_bind():
    """GF node -> world bind position (GF coords)."""
    out = {}
    for name in GF_JOINT_ORDER:
        offset, parent = GF_BIND[name]
        if parent is None:
            out[name] = offset
        else:
            p = out[parent]
            out[name] = (p[0] + offset[0], p[1] + offset[1], p[2] + offset[2])
    return out


# ---------------------------------------------------------------------------
# Helper-bone collapse: every PES bone that is not independently animated
# resolves to the animated GF node it rigidly follows. Curated by anatomy
# from the body.skl bone list; resolve_bone() falls back on name patterns
# and, given bind positions, on proximity.
HELPER_TO_GF = {
    # pelvis / trunk
    "dsk_back": "middle", "dsk_belly_scale": "middle",
    "dsk_hip_l": "hip", "dsk_hip_r": "hip",
    "dsk_pants_l": "hip", "dsk_pants_r": "hip",
    "dsk_pos_pants_l": "hip", "dsk_pos_pants_r": "hip",
    "dsk_sternum_l": "chest", "dsk_sternum_r": "chest",
    "dsk_pectoralis_l": "chest", "dsk_pectoralis_r": "chest",
    "dsk_pos_pectoralis_l": "chest", "dsk_pos_pectoralis_r": "chest",
    "dsk_scapula_l": "chest", "dsk_scapula_r": "chest",
    "dsk_pos_scapula_l": "chest", "dsk_pos_scapula_r": "chest",
    # clavicle skin rides the animated clavicle bone; collar skin the chest
    "dsk_clavicle_l": "left_clavicle", "dsk_clavicle_r": "right_clavicle",
    "dsk_trapezius_l": "left_clavicle", "dsk_trapezius_r": "right_clavicle",
    "dsk_collar_l": "chest", "dsk_collar_r": "chest",
    # neck / head
    "dsk_scm": "neck", "dsk_neckback": "neck",
    # arms (deltoid/underarm/sleeve cluster sit on the upper arm)
    "dsk_deltoid_l": "left_shoulder", "dsk_deltoid_r": "right_shoulder",
    "dsk_underarm_l": "left_shoulder", "dsk_underarm_r": "right_shoulder",
    "dsk_underarm_ba_l": "left_shoulder", "dsk_underarm_ba_r": "right_shoulder",
    "dsk_elbow_l": "left_elbow", "dsk_elbow_r": "right_elbow",
    "dsk_forearm_t_l": "left_elbow", "dsk_forearm_t_r": "right_elbow",
    # wrist-twist carrier: statically parented to the forearm (candy-wrapper
    # twist distribution is a Fox constraint GF does not emulate)
    "dsk_forearm_l": "left_elbow", "dsk_forearm_r": "right_elbow",
    "dsk_wrist_l": "left_hand", "dsk_wrist_r": "right_hand",
    # legs
    "dsk_thighmain_l": "left_thigh", "dsk_thighmain_r": "right_thigh",
    "dsk_thigh_l": "left_thigh", "dsk_thigh_r": "right_thigh",
    "dsk_pos_thigh_l": "left_thigh", "dsk_pos_thigh_r": "right_thigh",
    "dsk_knee_l": "left_knee", "dsk_knee_r": "right_knee",
    "dsk_kneeback_l": "left_knee", "dsk_kneeback_r": "right_knee",
    "dsk_leg_l": "left_knee", "dsk_leg_r": "right_knee",
    "dsk_foot_l": "left_ankle", "dsk_foot_r": "right_ankle",
    "dsk_toe_l": "left_ankle", "dsk_toe_r": "right_ankle",
    "sk_toe_l": "left_ankle", "sk_toe_r": "right_ankle",
}
# name-pattern families (prefix match), checked after the exact table
_HELPER_FAMILIES = [
    ("dsk_hem_", {"_l": "left_thigh", "_r": "right_thigh"}),
    ("dsk_pos_hem_", {"_l": "left_thigh", "_r": "right_thigh"}),
    ("dsk_belly_", {"_l": "middle", "_r": "middle"}),
    ("dsk_pos_belly_", {"_l": "middle", "_r": "middle"}),
    ("dsk_sleeve_", {"_l": "left_shoulder", "_r": "right_shoulder"}),
    ("tip_dsk_sleeve_", {"_l": "left_shoulder", "_r": "right_shoulder"}),
    ("tip_dsk_pos_sleeve_", {"_l": "left_shoulder", "_r": "right_shoulder"}),
    ("pos_arm_target_", {"_l": "left_shoulder", "_r": "right_shoulder"}),
    ("dsk_upperarm_", {"_l": "left_shoulder", "_r": "right_shoulder"}),
    ("dsk_ear", {"_l": "head", "_r": "head"}),
    ("tip_dsk_toe", {"_l": "left_ankle", "_r": "right_ankle"}),
    # fingers (hand_[lr].skl / hand meshes) and face muscles
    ("skh_", {"_l": "left_hand", "_r": "right_hand"}),
    ("skf_", {"_l": "head", "_r": "head", "": "head"}),
]


def resolve_bone(bone_name, bind_positions=None):
    """PES bone name -> GF node name (None if unresolvable).

    bind_positions: optional {bone: (x,y,z) Fox coords} (e.g. an fmdl bone
    table) for a nearest-animated-bone fallback on unknown names.
    """
    if bone_name in GF_OF_PES:
        return GF_OF_PES[bone_name]
    if bone_name in HELPER_TO_GF:
        return HELPER_TO_GF[bone_name]
    for prefix, sides in _HELPER_FAMILIES:
        if bone_name.startswith(prefix):
            if bone_name.endswith("_l") or "_l_" in bone_name[len(prefix):]:
                return sides.get("_l")
            if bone_name.endswith("_r") or "_r_" in bone_name[len(prefix):]:
                return sides.get("_r")
            for key in ("", "_l"):
                if key in sides:
                    return sides[key]
    # dsk_x -> sk_x (twist helper sharing its master's name)
    if bone_name.startswith("dsk_") and ("sk_" + bone_name[4:]) in GF_OF_PES:
        return GF_OF_PES["sk_" + bone_name[4:]]
    if bind_positions is not None and bone_name in bind_positions:
        p = bind_positions[bone_name]
        best, best_d = None, None
        for gf_name, bone, _ in GF_NODES:
            q = PES_BIND[bone][0]
            d = sum((a - b) ** 2 for a, b in zip(p, q))
            if best_d is None or d < best_d:
                best, best_d = gf_name, d
        return best
    return None
