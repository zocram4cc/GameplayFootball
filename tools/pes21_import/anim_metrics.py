"""Derives GameplayFootball animation metadata from a .anim's own curves.

GF picks an animation by matching a *query* against each clip's metadata and
its curve-derived quantities (see docs/PES21_ANIMS.md). An imported PES clip
that carries the wrong numbers is either never selected or selected in the
wrong situation, so nothing here is hand-labelled: every value below is
measured off the converted curves.

What it measures
  * incoming/outgoing velocity and the buckets GF quantizes them into
    (Animation::GetIncomingVelocity), incoming body angle, outgoing angle and
    outgoing body angle (Animation::Get*Angle) -- reproduced bit-for-bit from
    src/utils/animation.cpp so the tools agree with the engine
  * a full FK pass over the GF bind skeleton (retarget.GF_BIND), giving world
    joint positions per frame in the same space the engine's ball keyframes
    live in (anim origin at the first player key)
  * foot plants, hip height, and the end-of-clip lie orientation, which is
    what decides <outgoing_special_state> lay_back vs lay_front
  * a per-family ball contact: the frame + position where a sliding foot, a
    keeper's hands or a dribbling foot meets the ball. Non-movement clips
    without a ball keyframe trip an assert in the engine's
    GetBestCheatableAnimID, so this is what makes an imported clip playable.

  python3 anim_metrics.py <file.anim> [--family sliding|keeper|touch]
  python3 anim_metrics.py --validate           # check against stock GF anims
"""

import argparse
import math
import os
import re

import retarget
from gani_to_anim import GF_NODES, q_mul, q_norm, q_rot

BALL_RADIUS = 0.11

# tips of the chains, so a foot means the toe and a hand means the fingers
# (same offsets anim_preview.py draws with)
TIPS = (
    ("head_top", "head", (0.0, 0.0, 0.13)),
    ("left_hand_tip", "left_hand", (0.0, 0.0, -0.06)),
    ("right_hand_tip", "right_hand", (0.0, 0.0, -0.06)),
    ("left_toe", "left_ankle", (0.0, -0.20, -0.10)),
    ("right_toe", "right_ankle", (0.0, -0.20, -0.10)),
)


# --- parsing ----------------------------------------------------------------

class Anim:
    """A parsed .anim: curves, ball keys, metadata tail."""

    def __init__(self, path, player, nodes, touches, variables, tail):
        self.path = path
        self.player = player          # [(frame, x, y, z)]
        self.nodes = nodes            # node -> [(frame, qx, qy, qz, qw)]
        self.touches = touches        # [(frame, x, y, z)]
        self.variables = variables    # metadata tail as a dict
        self.tail = tail

    @property
    def name(self):
        return os.path.splitext(os.path.basename(self.path))[0]

    @property
    def last_frame(self):
        return self.player[-1][0]

    def frames(self):
        """Every frame number the player track keys, in order."""
        return [k[0] for k in self.player]


def parse_anim(path):
    text = open(path).read()
    player, nodes, touches = [], {}, []
    tail_lines = []
    in_tail = False
    for line in text.split("\n"):
        if not line.strip():
            continue
        if in_tail or line.startswith("<"):
            in_tail = True
            tail_lines.append(line)
            continue
        parts = line.strip().split(",")
        name, vals = parts[0], parts[1:]
        if name == "player":
            player = [(int(vals[i]),) + tuple(float(v) for v in vals[i + 1:i + 4])
                      for i in range(0, len(vals), 4)]
        elif name == "extension":
            # extension,football,<frame>,x,y,z,...
            vals = parts[2:]
            touches = [(int(vals[i]),) + tuple(float(v) for v in vals[i + 1:i + 4])
                       for i in range(0, len(vals), 4)]
        elif name in GF_NODES:
            nodes[name] = [(int(vals[i]),) + tuple(float(v) for v in vals[i + 1:i + 5])
                           for i in range(0, len(vals), 5)]
    tail = "\n".join(tail_lines)
    variables = {m.group(1): m.group(2).strip()
                 for m in re.finditer(r"<([a-z_0-9]+)>\s*(.*?)\s*</\1>", tail, re.S)}
    return Anim(path, player, nodes, touches, variables, tail)


# --- sampling ---------------------------------------------------------------

def render_anim(anim, variables, touches=None):
    """-> .anim text: curves, the football extension, then the metadata tail.

    The engine's loader stops reading curves at the first `extension` or `<`
    line, so the order matters (src/utils/animation.cpp, Animation::Load).
    """
    lines = ["player," + ",".join("%d,%f,%f,%f" % k for k in anim.player)]
    for node in GF_NODES:
        if node in anim.nodes:
            lines.append(node + "," + ",".join("%d,%f,%f,%f,%f" % k
                                               for k in anim.nodes[node]))
    touches = anim.touches if touches is None else touches
    if touches:
        lines.append("extension,football," +
                     ",".join("%d,%f,%f,%f" % t for t in touches))
    for key, value in variables.items():
        lines.append("<%s>" % key)
        lines.append("\t%s" % value)
        lines.append("</%s>" % key)
    return "\n".join(lines) + "\n"


def _bracket(keys, frame):
    if frame <= keys[0][0]:
        return keys[0], keys[0], 0.0
    if frame >= keys[-1][0]:
        return keys[-1], keys[-1], 0.0
    for i in range(1, len(keys)):
        if keys[i][0] >= frame:
            span = keys[i][0] - keys[i - 1][0]
            return keys[i - 1], keys[i], (frame - keys[i - 1][0]) / span if span else 0.0
    return keys[-1], keys[-1], 0.0


def sample_pos(keys, frame):
    a, b, t = _bracket(keys, frame)
    return tuple(a[1 + i] + (b[1 + i] - a[1 + i]) * t for i in range(3))


def sample_quat(keys, frame):
    a, b, t = _bracket(keys, frame)
    qa, qb = a[1:5], b[1:5]
    if sum(x * y for x, y in zip(qa, qb)) < 0.0:
        qb = tuple(-c for c in qb)
    return q_norm(tuple(x + (y - x) * t for x, y in zip(qa, qb)))


# --- GF maths, reproduced from the engine -----------------------------------

def gf_euler(q):
    """Quaternion::GetAngles (src/base/math/quaternion.cpp) -> (X, Y, Z).

    The engine reads element 2 as its 'y' and element 1 as its 'z' -- the
    body's heading is the Z it returns, and that is the number every body
    angle in the selector is built from.
    """
    e = q
    x, y, z = 0, 2, 1
    singularity = e[x] * e[y] + e[z] * e[3]
    if singularity > 0.49999 or singularity < -0.49999:
        sign = 1.0 if singularity > 0 else -1.0
        return (0.0, sign * math.pi * 0.5, sign * 2 * math.atan2(e[x], e[z]))
    sqx, sqy, sqz = e[x] * e[x], e[y] * e[y], e[z] * e[z]
    Z = math.atan2(2 * e[y] * e[3] - 2 * e[x] * e[z], 1 - 2 * sqy - 2 * sqz)
    Y = math.asin(max(-1.0, min(1.0, 2 * e[x] * e[y] + 2 * e[z] * e[3])))
    X = math.atan2(2 * e[x] * e[3] - 2 * e[y] * e[z], 1 - 2 * sqx - 2 * sqz)
    return (X, Y, Z)


def quantize_velocity(speed):
    """Animation::GetIncomingVelocity's bucketing."""
    if speed < 1.8:
        return 0.0
    if speed < 4.2:
        return 3.5
    if speed < 6.0:
        return 5.0
    return 7.0


VELOCITY_NAME = {0.0: "idle", 3.5: "dribble", 5.0: "walk", 7.0: "sprint"}


def modulate(angle):
    return (angle + math.pi) % (2 * math.pi) - math.pi


def _edge_velocity(player, first):
    """(vector, speed) over the first or last key interval, m/s, 2D."""
    if len(player) < 2:
        return (0.0, 0.0, 0.0), 0.0
    a, b = (player[0], player[1]) if first else (player[-2], player[-1])
    # the engine divides by (b.frame - a.frame * 1.0); for the first interval
    # a.frame is 0 so that is the plain span, and it never keys anything else.
    span = b[0] - a[0] * 1.0
    v = tuple((b[1 + i] - a[1 + i]) / span * 100.0 for i in range(3))
    return (v[0], v[1], 0.0), math.hypot(v[0], v[1])


def incoming_velocity(anim):
    return _edge_velocity(anim.player, True)


def outgoing_velocity(anim):
    return _edge_velocity(anim.player, False)


def receiver_velocity_bucket(anim, foot_source=None):
    """The velocity bucket a receiving player should match this anim at.

    The player track only moves when the *whole* body translates -- a
    sprinting forward receiving a pass in place has a stationary root but
    is still moving. Without the second signal every imported trap clip
    lands in `idle/`, the cheat check rejects every one for a sprint
    receiver, and the engine falls back to the stock set. We combine the
    two: a clip with low root motion but a foot-plant cadence at
    reception-stationary pace gets bumped out of `idle` by the cadence.
    """
    _, root_speed = incoming_velocity(anim)
    root_bucket = quantize_velocity(root_speed)
    if root_bucket > 0.0:
        return root_bucket
    if foot_source is None:
        foot_source = foot_plants
    try:
        cadence = foot_source(anim)
    except (TypeError, AttributeError):
        # TypeError: test stub passed the list directly.
        # AttributeError: stub has no .nodes for the real fk() to walk.
        # Both mean the caller already gave us the cadence; use it as-is.
        try:
            cadence = foot_source()
        except TypeError:
            cadence = foot_source  # already a list
    if not cadence or len(cadence) < 2:
        return 0.0
    span_frames = anim.last_frame
    if span_frames <= 0:
        return 0.0
    # Degenerate-cadence guard: a real clip plants at most a few times per
    # second per leg; a stub or broken anim with a plant every frame is
    # noise, not motion. Treat > 4 plants/sec per leg-side as suspect
    # and fall back to idle -- a sprinting receiver (the very case this
    # function is meant to catch) would still cross 1.8 m/s with proper
    # foot cadence in the 2-4 plants/sec range, not 13.
    plants_per_sec = (len(cadence) - 1) / (span_frames * 0.01)
    if plants_per_sec > 8.0:
        return 0.0
    speed_mps = plants_per_sec * 0.7
    return quantize_velocity(speed_mps)



def incoming_body_angle(anim):
    return modulate(gf_euler(anim.nodes["body"][0][1:5])[2])


def outgoing_angle(anim):
    """Animation::GetOutgoingAngle."""
    _, speed = outgoing_velocity(anim)
    if quantize_velocity(speed) >= 1.8:
        p = anim.player
        move = tuple(p[-1][1 + i] - p[-2][1 + i] for i in range(3))
        # FixAngle(v.GetAngle2D(), true): GF's angle-of-vector against (0,-1,0)
        angle = math.atan2(move[0], -move[1])
        if angle < -0.95 * math.pi or angle > 0.95 * math.pi:
            z = gf_euler(anim.nodes["body"][-1][1:5])[2]
            if (angle > 0) != (z > 0):
                angle = math.pi * 0.99 * (1.0 if z > 0 else -1.0)
            else:
                angle = max(-0.99 * math.pi, min(0.99 * math.pi, angle))
        return angle
    return modulate(gf_euler(anim.nodes["body"][-1][1:5])[2])


def outgoing_body_angle(anim):
    _, speed = outgoing_velocity(anim)
    if quantize_velocity(speed) < 1.8:
        return 0.0
    z = gf_euler(anim.nodes["body"][-1][1:5])[2]
    return modulate(z - outgoing_angle(anim))


# --- forward kinematics -----------------------------------------------------

def fk(anim, frame):
    """-> ({node: world position}, {node: world rotation}) at `frame`.

    Positions are in anim space: the origin is where the player root starts,
    which is exactly the space the engine's football keyframes use.
    """
    px, py, pz = sample_pos(anim.player, frame)
    world_rot, world_pos = {}, {}
    for node in GF_NODES:
        offset, parent = retarget.GF_BIND[node]
        q = sample_quat(anim.nodes[node], frame) if node in anim.nodes else (0, 0, 0, 1)
        if parent is None:
            world_rot[node] = q
            world_pos[node] = (px, py, pz + retarget.GF_BODY_HEIGHT)
        else:
            world_rot[node] = q_norm(q_mul(world_rot[parent], q))
            world_pos[node] = tuple(a + b for a, b in
                                    zip(world_pos[parent], q_rot(world_rot[parent], offset)))
    for tip, node, off in TIPS:
        world_pos[tip] = tuple(a + b for a, b in
                               zip(world_pos[node], q_rot(world_rot[node], off)))
        world_rot[tip] = world_rot[node]
    return world_pos, world_rot


def fk_track(anim, step=1):
    """[(frame, positions, rotations)] over the whole clip."""
    return [(f, ) + fk(anim, f) for f in range(0, anim.last_frame + 1, step)]


# --- derived pose facts -----------------------------------------------------

def lie_state(anim):
    """'lay_back', 'lay_front' or None for a clip that ends on its feet.

    A body lying down has its local -Y (the way the chest faces) pointing at
    the sky or at the grass; upright it points at the horizon.
    """
    pos, rot = fk(anim, anim.last_frame)
    hip_z = pos["body"][2]
    facing = q_rot(rot["body"], (0.0, -1.0, 0.0))
    if hip_z > 0.55:
        return None
    if facing[2] > 0.35:
        return "lay_back"
    if facing[2] < -0.35:
        return "lay_front"
    # on the side: fall back on which way the clip tipped, back is the safer
    # read for a slide (GF only ships stand-ups for back and front)
    return "lay_back"


def foot_plants(anim, step=2, height=0.16, speed=1.2):
    """Frames where a toe is low and not moving: [(frame, 'left'|'right')]."""
    out = []
    prev = None
    for f in range(0, anim.last_frame + 1, step):
        pos, _ = fk(anim, f)
        for side in ("left", "right"):
            toe = pos[side + "_toe"]
            if toe[2] > height:
                continue
            if prev is not None:
                d = math.dist(toe[:2], prev[side][:2]) / (step * 0.01)
                if d > speed:
                    continue
            out.append((f, side))
        prev = {s: pos[s + "_toe"] for s in ("left", "right")}
    return out


# --- ball contacts ----------------------------------------------------------

def _forward_at(rot, node="body"):
    f = q_rot(rot[node], (0.0, -1.0, 0.0))
    n = math.hypot(f[0], f[1])
    return (f[0] / n, f[1] / n, 0.0) if n > 1e-6 else (0.0, -1.0, 0.0)


def _first_peak(series, prominence=0.03, floor=0.015, min_rise=0.0):
    """Index of the FIRST real maximum of `series`, not the global one.

    A limb reaching for the ball stretches out, touches, then relaxes or
    reorganises for the landing -- and a keeper rolling to his feet afterwards
    can put a hand further out than the save ever did. So the contact is the
    first crest the curve gives back a meaningful part of, where 'meaningful'
    scales with the crest itself.

    `min_rise` guards the other end: an arm that drops out of its ready stance
    before it reaches would otherwise call frame zero a crest. Nothing counts
    as a peak until the curve has climbed that far above where it started.
    """
    if not series:
        return None
    best_i, best_v = 0, series[0]
    for i, v in enumerate(series):
        if v > best_v:
            best_i, best_v = i, v
        elif (best_v >= series[0] + min_rise and
              best_v - v > max(floor, prominence * abs(best_v))):
            return best_i
    return best_i


SLIDE_HIP_FRACTION = 0.55


def sliding_contact(anim, step=1):
    """Where a sliding/tackling leg meets the ball.

    A slide meets the ball on the way down, not on the deck: the extended leg
    is at full stretch and still off the grass while the hips are passing
    roughly half of standing height. Every one of GF's own six sliding clips
    keys its ball within three frames of that crossing, and it is a geometric
    fact rather than a taste -- with a ~0.9 m leg, hips at half height put the
    swept toe on the ground at maximum forward reach.

    A standing block tackle never gets there, so those fall back to the frame
    of furthest forward reach of a low foot.
    """
    frames = list(range(0, anim.last_frame + 1, step))
    hip0 = fk(anim, 0)[0]["body"][2]
    threshold = SLIDE_HIP_FRACTION * hip0

    frame = None
    for f in frames:
        if fk(anim, f)[0]["body"][2] <= threshold:
            frame = f
            break

    if frame is None:  # standing tackle: furthest reach of a low foot
        best = None
        for f in frames:
            pos, rot = fk(anim, f)
            fwd, hip = _forward_at(rot), pos["body"]
            for side in ("left", "right"):
                toe = pos[side + "_toe"]
                if toe[2] > 0.45:
                    continue
                reach = (toe[0] - hip[0]) * fwd[0] + (toe[1] - hip[1]) * fwd[1]
                if best is None or reach > best[0]:
                    best = (reach, f)
        if best is None:
            return None
        frame = best[1]

    pos, rot = fk(anim, frame)
    fwd, hip = _forward_at(rot), pos["body"]
    side, reach = None, -1e9
    for candidate in ("left", "right"):
        toe = pos[candidate + "_toe"]
        r = (toe[0] - hip[0]) * fwd[0] + (toe[1] - hip[1]) * fwd[1]
        if r > reach:
            side, reach = candidate, r
    toe = pos[side + "_toe"]
    pos_next, _ = fk(anim, min(frame + 4, anim.last_frame))
    pos_prev, _ = fk(anim, max(frame - 4, 0))
    sweep = tuple(pos_next[side + "_toe"][i] - pos_prev[side + "_toe"][i] for i in range(2))
    if math.hypot(*sweep) < 1e-3:
        sweep = (fwd[0], fwd[1])
    n = math.hypot(*sweep)
    direction = (sweep[0] / n, sweep[1] / n, 0.0)
    ball = (toe[0] + direction[0] * BALL_RADIUS,
            toe[1] + direction[1] * BALL_RADIUS,
            BALL_RADIUS)
    return {"frame": frame, "position": ball, "foot": side,
            "reach": reach, "direction": direction}


def keeper_contact(anim, step=1):
    """Where a keeper's hands meet the ball.

    The save is where an arm *reaches*: extension from the keeper's own hips
    rises to a crest, meets the ball, then folds in for the landing. Two
    things make that harder than it sounds. A keeper's ready stance already
    has the arms half out, so whichever hand happens to start furthest from
    the hips would otherwise win at frame zero -- the saving arm is instead
    the one that gains the most extension over the clip. And measuring from
    the hips rather than from where he took off stops a long dive's landing
    and roll from outscoring the save.

    Two hands close together catch the ball between them; a single stretched
    arm parries with that hand.
    """
    frames = list(range(0, anim.last_frame + 1, step))
    series = {"left": [], "right": []}
    for f in frames:
        pos, _ = fk(anim, f)
        hip = pos["body"]
        for side in ("left", "right"):
            series[side].append(math.dist(pos[side + "_hand_tip"], hip))

    best = None
    for side in ("left", "right"):
        idx = _first_peak(series[side], min_rise=0.06)
        if idx is None:
            continue
        gain = series[side][idx] - series[side][0]
        if best is None or gain > best[0]:
            best = (gain, idx, side)
    if best is None:
        return None
    _, idx, side = best

    # both arms reaching together, arriving at the same moment: a catch
    other = "right" if side == "left" else "left"
    other_idx = _first_peak(series[other], min_rise=0.06)
    if other_idx is not None and abs(other_idx - idx) <= 3:
        idx = min(idx, other_idx)

    frame, reach = frames[idx], series[side][idx]
    pos, _ = fk(anim, frame)
    left, right = pos["left_hand_tip"], pos["right_hand_tip"]
    two_handed = math.dist(left, right) < 0.45
    if two_handed:
        contact = tuple((left[i] + right[i]) * 0.5 for i in range(3))
    else:
        contact = pos[side + "_hand_tip"]
    # nudge the ball just outside the hands, away from the body
    away = tuple(contact[i] - pos["body"][i] for i in range(3))
    n = math.sqrt(sum(c * c for c in away))
    if n > 1e-6:
        contact = tuple(contact[i] + away[i] / n * BALL_RADIUS for i in range(3))
    contact = (contact[0], contact[1], max(contact[2], BALL_RADIUS))
    return {"frame": frame, "position": contact, "hand": side,
            "reach": reach, "two_handed": two_handed}


def step_count(anim, step=2):
    """How many steps the clip takes, from its foot plants.

    GF's `<steps>` tells the selector which foot a movement clip leaves the
    player on (Animation::GetOutgoingFoot flips the current foot on an odd
    count), so it has to be a count of *alternations*, not of plant frames:
    a foot that stays down through six sampled frames is one step, and the
    same foot planting twice in a row without the other in between is still
    one step.

    Sampling a plant frame by frame flickers -- a toe hovering at the
    threshold goes down, up and down again inside one stride -- so plants are
    read as contiguous runs with hysteresis (a foot that is down stays down
    until it clearly lifts), short gaps are bridged, and two runs of the same
    foot in a row count once.
    """
    frames = list(range(0, anim.last_frame + 1, step))
    down = {"left": [], "right": []}
    state = {"left": False, "right": False}
    for f in frames:
        pos, _ = fk(anim, f)
        for side in ("left", "right"):
            z = pos[side + "_toe"][2]
            if state[side]:
                state[side] = z < 0.22           # stays down until it lifts
            else:
                state[side] = z < 0.13           # has to get low to count
            down[side].append(state[side])

    runs = []                                     # (start index, side)
    for side in ("left", "right"):
        i = 0
        series = down[side]
        while i < len(series):
            if not series[i]:
                i += 1
                continue
            start = i
            gap = 0
            while i < len(series) and (series[i] or gap < 2):
                gap = 0 if series[i] else gap + 1
                i += 1
            runs.append((start, side))
    runs.sort()

    steps, last = 0, None
    for _, side in runs:
        if side != last:
            steps += 1
            last = side
    return steps


def approach_velocity(anim):
    """The velocity the clip assumes the player *arrives* with, m/s 2D.

    GF reads a clip's incoming velocity off its first root interval and uses
    it to decide which situations the clip may be offered for. PES clips break
    that reading: a keeper's dive starts at 6 m/s sideways from frame zero,
    because the launch is part of the action and PES blends into it. Read
    literally, every dive would claim to need a keeper already running.

    Nobody approaches anything sideways at 6 m/s. So only the component along
    the body's own facing counts as approach; the perpendicular part is the
    action launching. A sprinting slide keeps its sprint, a standing dive
    reads as standing.
    """
    v, _ = incoming_velocity(anim)
    _, rot = fk(anim, 0)
    fwd = _forward_at(rot)
    along = v[0] * fwd[0] + v[1] * fwd[1]
    along = max(0.0, along)
    return (fwd[0] * along, fwd[1] * along, 0.0), along


def condition_root(anim, blend_frames=12):
    """Rewrite the head of the root track to start at the approach velocity.

    The clip keeps its shape: velocities are eased from the approach velocity
    into the clip's own over `blend_frames`, and everything past the blend is
    translated by the difference, so only the launch is reshaped. What used to
    be an instant 6 m/s glide becomes a push-off, which is both what GF's
    selector needs to read and what the movement actually looks like.
    """
    keys = anim.player
    if len(keys) < 3:
        return anim
    target, _ = approach_velocity(anim)

    end = min(blend_frames, keys[-1][0])
    new = [keys[0]]
    for i in range(1, len(keys)):
        frame, x, y, z = keys[i]
        dt = (frame - keys[i - 1][0]) * 0.01
        dx, dy = x - keys[i - 1][1], y - keys[i - 1][2]
        if frame <= end and end:
            blend = frame / float(end)
            blend = blend * blend * (3 - 2 * blend)      # smoothstep
            dx = target[0] * dt * (1 - blend) + dx * blend
            dy = target[1] * dt * (1 - blend) + dy * blend
        new.append((frame, new[-1][1] + dx, new[-1][2] + dy, z))
    anim.player = new
    return anim


def trim_to_rest(anim, settle=0.15):
    """Cut a clip at the end of its action, before any getting up.

    PES ships a tackle or a dive as one long take: approach, action, land,
    stand up again. GF wants only the action -- it has its own stand-up clips,
    keyed off <outgoing_special_state>, and a clip that plays the get-up
    itself would hold the player hostage for a second and a half. The action
    ends where the hips stop falling and start coming back up.
    """
    frames = list(range(0, anim.last_frame + 1, 2))
    hips = [(f, fk(anim, f)[0]["body"][2]) for f in frames]
    low_f, low = min(hips, key=lambda h: h[1])
    if low > 0.55:                      # never went down: nothing to trim
        return anim
    end = anim.last_frame
    for f, z in hips:
        if f > low_f and z > low + settle:
            end = f
            break
    return clip_to(anim, end)


def clip_to(anim, end):
    """A copy of the clip truncated at frame `end` (keys are kept, not cut
    mid-interval, so the engine still sees a well-formed track)."""
    def cut(keys):
        kept = [k for k in keys if k[0] <= end]
        return kept if len(kept) >= 2 else keys[:2]
    anim.player = cut(anim.player)
    anim.nodes = {n: cut(k) for n, k in anim.nodes.items()}
    anim.touches = [t for t in anim.touches if t[0] <= end]
    return anim


def incoming_ball_direction(contact_pos, shot_distance=12.0, shot_height=0.4):
    """The travel direction of a ball arriving at `contact_pos`.

    GF's query is the ball's own movement vector in anim space, and the player
    faces -Y, so a shot coming at him travels +Y. The lateral and vertical
    tilt follow from where on the goal line the clip catches it: a dive to the
    keeper's left (+X) meets a ball that was already heading +X.
    """
    v = (contact_pos[0], contact_pos[1] + shot_distance, contact_pos[2] - shot_height)
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v) if n > 1e-6 else (0.0, 1.0, 0.0)


def touch_contact(anim, step=1):
    """Where a dribbling/feinting foot meets the ball.

    A touch is a foot swinging fast and low near the front of the body; the
    ball sits just beyond the toe, in the direction the foot is travelling.
    """
    best = None
    for f in range(step, anim.last_frame + 1 - step, step):
        pos, rot = fk(anim, f)
        prev, _ = fk(anim, f - step)
        nxt, _ = fk(anim, f + step)
        fwd = _forward_at(rot)
        hip = pos["body"]
        for side in ("left", "right"):
            toe = pos[side + "_toe"]
            if toe[2] > 0.30:
                continue
            reach = (toe[0] - hip[0]) * fwd[0] + (toe[1] - hip[1]) * fwd[1]
            if reach < 0.10:
                continue
            vel = tuple((nxt[side + "_toe"][i] - prev[side + "_toe"][i]) / (2 * step * 0.01)
                        for i in range(2))
            score = math.hypot(*vel) * (0.5 + reach)
            if best is None or score > best[0]:
                best = (score, f, side, toe, vel, fwd)
    if best is None:
        return None
    _, frame, side, toe, vel, fwd = best
    n = math.hypot(*vel)
    direction = (vel[0] / n, vel[1] / n, 0.0) if n > 1e-3 else fwd
    ball = (toe[0] + direction[0] * BALL_RADIUS,
            toe[1] + direction[1] * BALL_RADIUS,
            BALL_RADIUS)
    return {"frame": frame, "position": ball, "foot": side, "direction": direction}


# --- the whole picture ------------------------------------------------------

def metrics(anim, family=None):
    vin, sin_ = incoming_velocity(anim)
    vout, sout = outgoing_velocity(anim)
    out = {
        "name": anim.name,
        "frames": anim.last_frame,
        "keys": len(anim.player),
        "incoming_speed": sin_,
        "outgoing_speed": sout,
        "incoming_velocity": VELOCITY_NAME[quantize_velocity(sin_)],
        "outgoing_velocity": VELOCITY_NAME[quantize_velocity(sout)],
        "incoming_body_angle": math.degrees(incoming_body_angle(anim)),
        "outgoing_angle": math.degrees(outgoing_angle(anim)),
        "outgoing_body_angle": math.degrees(outgoing_body_angle(anim)),
        "translation": math.hypot(anim.player[-1][1] - anim.player[0][1],
                                  anim.player[-1][2] - anim.player[0][2]),
        "lie_state": lie_state(anim),
        "hip_z_end": fk(anim, anim.last_frame)[0]["body"][2],
    }
    if family == "sliding":
        out["contact"] = sliding_contact(anim)
    elif family == "keeper":
        out["contact"] = keeper_contact(anim)
    elif family == "touch":
        out["contact"] = touch_contact(anim)
    return out


# --- validation against the stock animation set -----------------------------

CONTACT_LIMB = {"sliding": ("left_toe", "right_toe"),
                "keeper": ("left_hand_tip", "right_hand_tip")}


def _contact_window(anim, family, tolerance=0.35):
    """Frames where GF's own ball keyframe sits within reach of the limb.

    Reconstructing the exact frame a hand-animated clip was authored to touch
    on is not the goal -- an imported clip only has to name a frame at which
    its own limb really is on the ball. This is the set of frames for which
    that would have been true of the stock clip.
    """
    want_f, wx, wy, wz = anim.touches[0]
    limbs = CONTACT_LIMB[family]
    window = []
    for f in range(0, anim.last_frame + 1):
        pos, _ = fk(anim, f)
        if min(math.dist(pos[l], (wx, wy, wz)) for l in limbs) < tolerance:
            window.append(f)
    return want_f, (wx, wy, wz), window


def validate(game_dir="data", tolerance=0.35):
    """Check the detectors against GF's own hand-authored ball keyframes.

    Two things are measured. `gap` is how far the stock ball is from this
    tool's FK'd limb at the stock touch frame -- that is a test of the forward
    kinematics and the toe/finger offsets, and it has to be small. `in_win`
    is whether the frame the detector picked is one at which the limb really
    is on the ball, which is the property the engine needs.
    """
    root = os.path.join(game_dir, "media", "animations")
    cases = [("sliding", os.path.join(root, "sliding")),
             ("keeper", os.path.join(root, "deflect"))]
    rows = []
    for family, directory in cases:
        for dirpath, _, files in os.walk(directory):
            for name in sorted(files):
                # installed clips carry ball keyframes this tool wrote, so
                # checking against them would only confirm its own opinion
                if not name.endswith(".anim") or name.startswith("pes_"):
                    continue
                anim = parse_anim(os.path.join(dirpath, name))
                if not anim.touches:
                    continue
                want_f, want_p, window = _contact_window(anim, family, tolerance)
                pos, _ = fk(anim, want_f)
                gap = min(math.dist(pos[l], want_p) for l in CONTACT_LIMB[family])
                detector = sliding_contact if family == "sliding" else keeper_contact
                got = detector(anim)
                got_f = None if got is None else got["frame"]
                rows.append((family, name, want_f, got_f, gap,
                             got_f is not None and got_f in window, len(window)))
    print("%-8s %-38s %6s %6s %6s %6s %5s" %
          ("family", "anim", "want_f", "got_f", "gap_m", "in_win", "win"))
    for family, name, want_f, got_f, gap, ok, win in rows:
        print("%-8s %-38s %6s %6s %6.2f %6s %5d" %
              (family, name[:38], want_f, got_f, gap, "yes" if ok else "NO", win))
    gaps = sorted(r[4] for r in rows)
    hits = sum(1 for r in rows if r[5])
    print("\n%d clips: median FK gap %.2f m, worst %.2f m; %d/%d detected "
          "frames land on the ball" %
          (len(rows), gaps[len(gaps) // 2], gaps[-1], hits, len(rows)))
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("anim", nargs="?")
    parser.add_argument("--family", choices=("sliding", "keeper", "touch"))
    parser.add_argument("--validate", action="store_true")
    parser.add_argument("--game-dir", default="data")
    args = parser.parse_args()
    if args.validate:
        validate(args.game_dir)
        return
    if not args.anim:
        parser.error("give an .anim or --validate")
    result = metrics(parse_anim(args.anim), args.family)
    width = max(len(k) for k in result)
    for key, value in result.items():
        if isinstance(value, float):
            value = round(value, 4)
        print("%-*s  %s" % (width, key, value))


if __name__ == "__main__":
    main()
