"""Checks for anim_metrics: the maths must agree with the engine's own.

Run from the repo root (it reads the stock animation set):

    python3 tools/pes21_import/test_anim_metrics.py

Pytest picks the same functions up if it is installed, but plain python3 is
enough -- everything here is an assert.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import anim_metrics as am

GAME_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "..", "data")
ANIMS = os.path.join(GAME_DIR, "media", "animations")


# --- gf_euler mirrors Quaternion::GetAngles ---------------------------------

def test_gf_euler_identity():
    x, y, z = am.gf_euler((0.0, 0.0, 0.0, 1.0))
    assert abs(x) < 1e-9 and abs(y) < 1e-9 and abs(z) < 1e-9


def test_gf_euler_reads_heading_off_element_one():
    """GF's 'Z' (the body heading) is a rotation about the model's Z axis.

    The engine indexes x=0, y=2, z=1, so a heading quaternion built the way
    Quaternion::SetAngleAxis(angle, (0,0,1)) builds it must come back out of
    GetAngles as that same angle in the third slot.
    """
    for angle in (0.0, 0.3, -0.75, 1.4, -2.9):
        q = (0.0, 0.0, math.sin(angle / 2), math.cos(angle / 2))
        _, _, z = am.gf_euler(q)
        assert abs(am.modulate(z - angle)) < 1e-6, (angle, z)


def test_gf_euler_singularity_is_finite():
    x, y, z = am.gf_euler((0.0, math.sqrt(0.5), math.sqrt(0.5), 0.0))
    for v in (x, y, z):
        assert math.isfinite(v)


# --- velocity bucketing mirrors Animation::GetIncomingVelocity --------------

def test_quantize_velocity_buckets():
    assert am.quantize_velocity(0.0) == 0.0
    assert am.quantize_velocity(1.79) == 0.0
    assert am.quantize_velocity(1.8) == 3.5
    assert am.quantize_velocity(4.19) == 3.5
    assert am.quantize_velocity(4.2) == 5.0
    assert am.quantize_velocity(5.99) == 5.0
    assert am.quantize_velocity(6.0) == 7.0
    assert am.quantize_velocity(99.0) == 7.0


def test_velocity_names_cover_every_bucket():
    for bucket in (0.0, 3.5, 5.0, 7.0):
        assert bucket in am.VELOCITY_NAME


# --- first-peak picking -----------------------------------------------------

def test_first_peak_prefers_the_first_crest_not_the_biggest():
    series = [0.0, 0.5, 1.0, 0.6, 0.4, 0.9, 2.0]
    assert am._first_peak(series) == 2


def test_first_peak_ignores_noise_below_the_prominence():
    series = [0.0, 0.5, 1.0, 0.995, 1.5, 2.0]
    assert am._first_peak(series) == 5


def test_first_peak_of_a_monotonic_rise_is_its_end():
    assert am._first_peak([0.0, 1.0, 2.0, 3.0]) == 3


def test_first_peak_of_nothing_is_nothing():
    assert am._first_peak([]) is None


# --- incoming ball direction ------------------------------------------------

def test_incoming_ball_direction_points_at_the_keeper():
    """A ball arriving anywhere in front travels roughly +Y in anim space."""
    for contact in ((0.0, -0.5, 0.2), (1.4, -0.3, 0.9), (-1.4, -0.3, 2.1)):
        d = am.incoming_ball_direction(contact)
        assert abs(math.sqrt(sum(c * c for c in d)) - 1.0) < 1e-9
        assert d[1] > 0.8, d


def test_incoming_ball_direction_leans_towards_the_save_side():
    left = am.incoming_ball_direction((1.5, -0.3, 0.5))
    right = am.incoming_ball_direction((-1.5, -0.3, 0.5))
    assert left[0] > 0.05 and right[0] < -0.05


# --- against the engine's own animation set ---------------------------------

def _stock(path):
    return am.parse_anim(os.path.join(ANIMS, path))


def test_parses_a_stock_clip_whole():
    anim = _stock("sliding/sprint/000.anim")
    assert len(anim.player) > 5
    assert set(anim.nodes) >= {"body", "middle", "left_ankle", "right_ankle"}
    assert anim.touches == [(32, 0.0, -3.389997, 0.11)]
    assert anim.variables["type"] == "sliding"
    assert anim.variables["outgoing_special_state"] == "lay_back"


def test_velocity_of_stock_clips_matches_the_directory_they_live_in():
    """GF files its movement clips by incoming velocity, so the derived
    bucket has to reproduce the author's own filing."""
    for path, want in (("sliding/sprint/000.anim", "sprint"),
                       ("sliding/walk/000.anim", "walk"),
                       ("sliding/idle/000.anim", "idle"),
                       ("deflect/idle/000_high_holdball.anim", "idle")):
        anim = _stock(path)
        _, speed = am.incoming_velocity(anim)
        got = am.VELOCITY_NAME[am.quantize_velocity(speed)]
        assert got == want, (path, got, want, speed)


def test_lie_state_of_stock_clips_matches_their_declared_special_state():
    """Clips that declare where they leave the player must read that way."""
    checked = 0
    for dirpath, _, files in os.walk(ANIMS):
        for name in files:
            if not name.endswith(".anim"):
                continue
            anim = am.parse_anim(os.path.join(dirpath, name))
            want = anim.variables.get("outgoing_special_state")
            if want not in ("lay_back", "lay_front"):
                continue
            assert am.lie_state(anim) == want, (anim.path, am.lie_state(anim), want)
            checked += 1
    assert checked >= 20, checked


def test_step_count_matches_what_stock_movement_clips_declare():
    """GF flips the current foot on an odd <steps>, so the count -- and above
    all its parity -- has to agree with the authors'."""
    rows = []
    for dirpath, _, files in os.walk(os.path.join(ANIMS, "movement")):
        for name in files:
            if not name.endswith(".anim") or name.startswith("pes_"):
                continue
            anim = am.parse_anim(os.path.join(dirpath, name))
            want = anim.variables.get("steps")
            if want:
                rows.append((name, int(want), am.step_count(anim)))
    assert len(rows) >= 10, len(rows)
    exact = sum(1 for _, w, g in rows if w == g)
    parity = sum(1 for _, w, g in rows if (w % 2) == (g % 2))
    assert all(abs(w - g) <= 1 for _, w, g in rows), \
        [r for r in rows if abs(r[1] - r[2]) > 1]
    assert exact >= 0.8 * len(rows), (exact, len(rows))
    assert parity >= 0.8 * len(rows), (parity, len(rows))


def test_forward_kinematics_lands_on_the_stock_ball_keyframes():
    """The FK must put the limb where the hand-authored ball is.

    This is the load-bearing check for every imported clip: contact positions
    are computed from this FK, so if it drifts, imported keepers grab at air.
    """
    rows = am.validate(GAME_DIR)
    gaps = sorted(r[4] for r in rows)
    assert len(rows) >= 30
    assert gaps[len(gaps) // 2] < 0.20, gaps[len(gaps) // 2]
    assert gaps[-1] < 0.40, gaps[-1]


def test_detected_contact_frames_land_on_the_ball():
    rows = am.validate(GAME_DIR)
    hits = sum(1 for r in rows if r[5])
    assert hits >= 0.75 * len(rows), (hits, len(rows))
    sliding = [r for r in rows if r[0] == "sliding"]
    assert all(r[5] for r in sliding), sliding


def test_keeper_contact_position_sits_in_the_hands():
    """Whatever frame is chosen, the ball must be at those hands, then."""
    for path in ("deflect/idle/000_high_deflect_far.anim",
                 "deflect/idle/090_ground_deflect_far.anim",
                 "deflect/walk/angled/090_high_holdball_close.anim"):
        anim = _stock(path)
        got = am.keeper_contact(anim)
        pos, _ = am.fk(anim, got["frame"])
        gap = min(math.dist(pos[l], got["position"])
                  for l in ("left_hand_tip", "right_hand_tip"))
        assert gap < am.BALL_RADIUS + 0.05, (path, gap)


def test_sliding_contact_puts_the_ball_on_the_grass_under_the_foot():
    """A tackled ball is rolling, so it is at ball height under the boot --
    only the horizontal placement follows the foot."""
    for path in ("sliding/sprint/000.anim", "sliding/idle/090.anim",
                 "sliding/walk/000.anim"):
        anim = _stock(path)
        got = am.sliding_contact(anim)
        pos, _ = am.fk(anim, got["frame"])
        assert abs(got["position"][2] - am.BALL_RADIUS) < 1e-9, path
        gap = min(math.dist(pos[l][:2], got["position"][:2])
                  for l in ("left_toe", "right_toe"))
        assert gap < am.BALL_RADIUS + 0.05, (path, gap)


def main():
    tests = [(n, f) for n, f in sorted(globals().items())
             if n.startswith("test_") and callable(f)]
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print("ok   %s" % name)
        except AssertionError as exc:
            failed += 1
            print("FAIL %s: %s" % (name, exc))
    print("\n%d/%d passed" % (len(tests) - failed, len(tests)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
