"""Renders a GameplayFootball .anim as a stick-figure filmstrip PNG.

Runs GF's forward kinematics (player.object hierarchy + node quaternions)
independently of the engine, which makes it a visual check on converted
PES animations: if the filmstrip looks human, the retarget is right.

  python3 anim_preview.py file.anim out.png [--frames 8]
"""

import argparse
import math

import retarget
from gani_to_anim import GF_NODES, q_mul, q_rot, q_norm


def parse_anim(path):
    """-> (player keys [(f,x,y,z)], node -> [(f,qx,qy,qz,qw)])."""
    player = []
    nodes = {}
    for line in open(path):
        if line.startswith("<"):
            break
        parts = line.strip().split(",")
        name = parts[0]
        vals = parts[1:]
        if name == "player":
            player = [(int(vals[i]), float(vals[i + 1]), float(vals[i + 2]),
                       float(vals[i + 3])) for i in range(0, len(vals), 4)]
        elif name in GF_NODES:
            nodes[name] = [(int(vals[i]),) + tuple(float(v) for v in
                           vals[i + 1:i + 5]) for i in range(0, len(vals), 5)]
    return player, nodes


def sample(keys, frame, width):
    """Nearest-key sample (previews don't need interpolation)."""
    best = min(keys, key=lambda k: abs(k[0] - frame))
    return best[1:1 + width]


def fk(player, nodes, frame):
    """-> {node: world position} at the given frame."""
    px, py, pz = sample(player, frame, 3)
    world_rot = {}
    world_pos = {}
    for node in GF_NODES:
        offset, parent = retarget.GF_BIND[node]
        q = q_norm(sample(nodes[node], frame, 4)) if node in nodes else (0, 0, 0, 1)
        if parent is None:
            world_rot[node] = q
            world_pos[node] = (px, py, pz + retarget.GF_BODY_HEIGHT)
        else:
            world_rot[node] = q_norm(q_mul(world_rot[parent], q))
            world_pos[node] = tuple(a + b for a, b in
                                    zip(world_pos[parent],
                                        q_rot(world_rot[parent], offset)))
    # limb endpoints beyond the last node of each chain
    tips = {}
    for tip, node, off in (("head_top", "head", (0, 0, 0.13)),
                           ("left_hand_tip", "left_hand", (0, 0, -0.06)),
                           ("right_hand_tip", "right_hand", (0, 0, -0.06)),
                           ("left_toe", "left_ankle", (0, -0.20, -0.10)),
                           ("right_toe", "right_ankle", (0, -0.20, -0.10))):
        tips[tip] = tuple(a + b for a, b in
                          zip(world_pos[node], q_rot(world_rot[node], off)))
    world_pos.update(tips)
    return world_pos


BONES = [("body", "middle"), ("middle", "neck"), ("neck", "head"),
         ("head", "head_top"),
         ("middle", "left_shoulder"), ("left_shoulder", "left_elbow"),
         ("left_elbow", "left_hand"), ("left_hand", "left_hand_tip"),
         ("middle", "right_shoulder"), ("right_shoulder", "right_elbow"),
         ("right_elbow", "right_hand"), ("right_hand", "right_hand_tip"),
         ("body", "left_thigh"), ("left_thigh", "left_knee"),
         ("left_knee", "left_ankle"), ("left_ankle", "left_toe"),
         ("body", "right_thigh"), ("right_thigh", "right_knee"),
         ("right_knee", "right_ankle"), ("right_ankle", "right_toe")]


def render(anim_path, out_png, n_frames=8):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    player, nodes = parse_anim(anim_path)
    last = player[-1][0]
    picks = [round(i * last / (n_frames - 1)) for i in range(n_frames)]

    fig, axes = plt.subplots(1, n_frames, figsize=(2.2 * n_frames, 4.4))
    for ax, frame in zip(axes, picks):
        pos = fk(player, nodes, frame)
        for a, b in BONES:
            pa, pb = pos[a], pos[b]
            side = a.startswith("left") or b.startswith("left")
            color = "#d33" if side else ("#36c" if a.startswith("right")
                                         or b.startswith("right") else "#222")
            ax.plot([pa[1], pb[1]], [pa[2], pb[2]], "-o",
                    color=color, markersize=2, linewidth=2)
        ax.set_xlim(pos["body"][1] - 1.1, pos["body"][1] + 1.1)
        ax.set_ylim(-0.1, 2.1)
        ax.set_aspect("equal")
        ax.set_title("f%d" % frame, fontsize=8)
        ax.axis("off")
    fig.suptitle(anim_path.split("/")[-1])
    fig.tight_layout()
    fig.savefig(out_png, dpi=110)
    print("wrote", out_png)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("anim")
    parser.add_argument("png")
    parser.add_argument("--frames", type=int, default=8)
    args = parser.parse_args()
    render(args.anim, args.png, args.frames)
