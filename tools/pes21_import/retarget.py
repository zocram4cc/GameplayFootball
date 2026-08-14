"""PES <-> GameplayFootball skeleton bridge.

GameplayFootball animates fifteen named nodes (see data/media/animations):
player (root position), body, middle, neck, left/right shoulder, elbow,
thigh, knee, ankle. PES models (.fmdl) are skinned to the Fox body skeleton
(sk_* bones, plus dsk_* twist helpers that carry no unique animation).

The map below drives both mesh segmentation (which GF body part each PES bone's
vertices belong to) and, once .gani curves are decoded, animation retargeting.
"""

# GF node -> the PES bones whose vertices/curves it absorbs. Order matters for
# mesh segmentation: earlier entries win when weights tie.
GF_FROM_PES = {
    "body": ["sk_hip", "dsk_hip"],
    "middle": ["sk_belly", "sk_chest", "sk_spine"],
    "neck": ["sk_neck"],
    "head": ["sk_head", "dsk_head"],
    "left_shoulder": ["sk_upperarm_l", "dsk_upperarm_l", "sk_clavicle_l", "dsk_deltoid_l"],
    "left_elbow": ["sk_forearm_l", "dsk_forearm_l"],
    "left_hand": ["sk_hand_l", "dsk_hand_l"],
    "right_shoulder": ["sk_upperarm_r", "dsk_upperarm_r", "sk_clavicle_r", "dsk_deltoid_r"],
    "right_elbow": ["sk_forearm_r", "dsk_forearm_r"],
    "right_hand": ["sk_hand_r", "dsk_hand_r"],
    "left_thigh": ["sk_thigh_l", "dsk_thigh_l"],
    "left_knee": ["sk_leg_l", "dsk_leg_l"],
    "left_ankle": ["sk_foot_l", "dsk_foot_l", "sk_toe_l", "dsk_toe_l"],
    "right_thigh": ["sk_thigh_r", "dsk_thigh_r"],
    "right_knee": ["sk_leg_r", "dsk_leg_r"],
    "right_ankle": ["sk_foot_r", "dsk_foot_r", "sk_toe_r", "dsk_toe_r"],
}

# GF body part -> the .ase GEOMOBJECT the engine attaches to that node.
GF_GEOMOBJECT = {
    "body": "pelvis",
    "middle": "trunk",
    "neck": "head",
    "head": "head",
    "left_hand": "lowerarm_left",
    "right_hand": "lowerarm_right",
    "left_shoulder": "upperarm_left",
    "left_elbow": "lowerarm_left",
    "right_shoulder": "upperarm_right",
    "right_elbow": "lowerarm_right",
    "left_thigh": "upperleg_left",
    "left_knee": "lowerleg_left",
    "left_ankle": "foot_left",
    "right_thigh": "upperleg_right",
    "right_knee": "lowerleg_right",
    "right_ankle": "foot_right",
}


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

# Position tracks store IEEE half-floats with the exponent rebased +7 (x128),
# in millimetres: metres = raw / 128 / 1000.
PES_POS_TO_M = 1.0 / 128000.0

# Bind pose (Fox coords: Y up, +Z forward, metres), harvested from HDG
# full-body .fmdl bone tables; frames are world-aligned (identity bind
# rotations - Fox skeletons store positions only).
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

# GameplayFootball's bind skeleton (from data/media/objects/players/
# player.object): Z up, character faces -Y, node offsets in metres.
GF_BIND = {
    "body": ((0.0, 0.0, 0.96), None),
    "middle": ((0.0, 0.0, 0.15), "body"),
    "neck": ((0.0, -0.03, 0.5), "middle"),
    "head": ((0.0, -0.01, 0.09), "neck"),
    "left_shoulder": ((0.16, -0.01, 0.48), "middle"),
    "left_elbow": ((-0.01, 0.0, -0.33), "left_shoulder"),
    "left_hand": ((0.0, 0.0, -0.28), "left_elbow"),
    "right_shoulder": ((-0.16, -0.01, 0.48), "middle"),
    "right_elbow": ((0.01, 0.0, -0.33), "right_shoulder"),
    "right_hand": ((0.0, 0.0, -0.28), "right_elbow"),
    "left_thigh": ((0.087, 0.0, -0.01), "body"),
    "left_knee": ((0.0, 0.0, -0.42), "left_thigh"),
    "left_ankle": ((0.0, -0.04, -0.44), "left_knee"),
    "right_thigh": ((-0.087, 0.0, -0.01), "body"),
    "right_knee": ((0.0, 0.0, -0.42), "right_thigh"),
    "right_ankle": ((0.0, -0.04, -0.44), "right_knee"),
}
GF_BODY_HEIGHT = 0.96


def pes_to_gf():
    """Inverted map: PES bone name -> GF node."""
    out = {}
    for gf_node, pes_bones in GF_FROM_PES.items():
        for bone in pes_bones:
            out[bone] = gf_node
    return out


def gf_node_for_bone(bone_name: str):
    """Best-effort lookup, tolerant of suffix variations (sk_hand_l_01...)."""
    table = pes_to_gf()
    if bone_name in table:
        return table[bone_name]
    for pes, gf_node in table.items():
        if bone_name.startswith(pes):
            return gf_node
    return None
