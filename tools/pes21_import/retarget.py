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
    "neck": ["sk_neck", "sk_head", "dsk_head"],
    "left_shoulder": ["sk_upperarm_l", "dsk_upperarm_l", "sk_clavicle_l", "dsk_deltoid_l"],
    "left_elbow": ["sk_forearm_l", "dsk_forearm_l", "sk_hand_l", "dsk_hand_l"],
    "right_shoulder": ["sk_upperarm_r", "dsk_upperarm_r", "sk_clavicle_r", "dsk_deltoid_r"],
    "right_elbow": ["sk_forearm_r", "dsk_forearm_r", "sk_hand_r", "dsk_hand_r"],
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
