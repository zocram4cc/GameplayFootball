#!/usr/bin/env python3
"""Reference implementation of PES 2021's foul-scoring and card pipeline.

This is a *reference*, not an importer: nothing here reads a PES file. It
transcribes the referee logic traced in the community engine research
(``User_Aoba_PES2021 Engine Research - Rigged Wiki.pdf``) so the constants can
be exercised, plotted and argued about while porting an equivalent model into
GameplayFootball. See ``docs/RULESET_AUDIT.md`` gap 3.

The pipeline
------------
Three scorers are summed into an unsigned byte, then thresholded::

    A (contact intensity, 0-40)
  + B (relative velocity,  0-30)
  + C (context,            0-80)
  = combined  ->  ScoreToSeverity(combined, T1, T2, T3)  ->  0..4

Thresholds depend only on whether the foul was inside the penalty area --
PES has no per-referee strictness at all::

    outside the box:  T1=40  T2=80  T3=110
    inside  the box:  T1=60  T2=80  T3=110

so a foul needs 50% more severity to be *called* inside the box, while the
yellow-card threshold is the same either way. Severity 4 is then capped to 3 by
a gate implementing the IFAB 2016 abolition of triple punishment -- which is why
every red card in normal PES play is a second yellow.

Usage::

    foul_severity.py table                     # severity bands, both locations
    foul_severity.py sweep                     # score vs speed/contact grid
    foul_severity.py eval --contact 120 --speed 18 --angle 90 [options]
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass

# --- action ids from the 126-entry AI action table -------------------------

WAIT_TIMER = 17  # a standing/static challenge
MOVE_ON_PASS = 16
GOAL_NET_DODGE = 11
POST_DODGE = 12
GOAL_KEEPER_DODGE = 13
BLOCK = 56
PASS_COURSE_CUT = 61
KP_BLOCK_LATE = 72

CARD_STATE_NAMES = ["NONE", "NORMAL", "CAUTION", "YELLOW", "RED"]


@dataclass
class Contact:
    """Everything the three scorers read."""

    # Scoring A
    contact_intensity: float = 0.0  # engine units; the default branch spans 40..180
    fouled_action: int = 0
    fouling_action: int = 0
    fouled_approach_deg: float = 0.0  # 0 = hit from the front, 180 = from behind
    fouling_approach_deg: float = 0.0  # 0 = ran straight at him, 180 = backing in

    # Scoring B
    relative_speed_kmh: float = 0.0

    # Scoring C
    dangerous_context: bool = False
    gk_contact: bool = False
    substate: int = 0  # +0x44: 2 or 3 select the lower base scores
    approach_angle_deg: float = 0.0  # from the fouled player's tactical facing
    run_distance_m: float = 0.0  # only used when substate == 3
    dogso: bool = False

    # Location and match state
    inside_penalty_area: bool = False
    cards_disabled: bool = False  # "no cards" match rule
    ball_in_special_state: bool = False  # dead-ball / set-piece setup
    scene_node_flag: bool = False  # suspected advantage or cutscene


# --- Scoring A: contact intensity -> 0..40 ---------------------------------


def _normalise(value: float, threshold: float, cap: float) -> float:
    if value <= threshold:
        return 0.0
    if value >= cap:
        return 1.0
    return (value - threshold) / (cap - threshold)


def scoring_a(c: Contact) -> int:
    """Physical intensity of the contact, adjusted by approach trajectory."""
    intensity = c.contact_intensity
    threshold, cap, multiplier = 40.0, 180.0, 40.0

    if c.fouled_action in (GOAL_NET_DODGE, POST_DODGE):
        # Approach-trajectory branch: a clean angle *reduces* effective contact.
        cap = 160.0
        if intensity > 80.0:
            # The fouled player's normalisation rises with a side/behind hit.
            if c.fouled_approach_deg <= 40.0:
                fouled_norm = 0.0
            elif c.fouled_approach_deg >= 45.0:
                fouled_norm = 1.0
            else:
                fouled_norm = (c.fouled_approach_deg - 40.0) / 5.0
            # The fouler's is inverted: lunging from standing scores highest.
            if c.fouling_approach_deg <= 35.0:
                fouling_norm = 1.0
            elif c.fouling_approach_deg >= 45.0:
                fouling_norm = 0.0
            else:
                fouling_norm = (45.0 - c.fouling_approach_deg) / 10.0
            # (80 - intensity) is negative here, so a clean approach forgives.
            intensity += fouled_norm * fouling_norm * (80.0 - intensity)
    elif c.fouled_action == GOAL_KEEPER_DODGE and c.substate == 2:
        multiplier = 20.0
    elif c.fouled_action == GOAL_KEEPER_DODGE and c.substate == 3:
        threshold, cap = 150.0, 180.0
    elif c.fouled_action == KP_BLOCK_LATE:
        multiplier = 2.0

    score = _normalise(intensity, threshold, cap) * multiplier

    # A standing challenge takes no reduction; everything else is forgiven 30%.
    if c.fouling_action != WAIT_TIMER:
        score *= 0.7
    if c.gk_contact and c.substate == 3:
        score *= 0.5

    return int(score)


# --- Scoring B: relative velocity -> 0..30 ---------------------------------


def scoring_b(c: Contact) -> int:
    """How fast the two players were closing on each other."""
    score = int(_normalise(c.relative_speed_kmh, 2.0, 30.0) * 30.0)

    # Standing still and initiating contact is never free.
    if c.fouling_action == WAIT_TIMER:
        score = max(score, 5)
    # A keeper's late block is almost entirely forgiven for speed.
    if c.fouled_action == KP_BLOCK_LATE:
        score //= 20

    return score


# --- Scoring C: context -> 0..80 -------------------------------------------


def _angle_score(angle_deg: float) -> int:
    """Fouls from behind score higher than face-to-face ones."""
    if angle_deg < 22.5:
        return 0
    if angle_deg >= 60.0:
        return 30
    if angle_deg <= 45.0:
        return int((angle_deg - 22.5) / 22.5 * 30.0)
    return int((angle_deg - 45.0) / 15.0 * 30.0)


def scoring_c(c: Contact) -> int:
    """What the two players were doing, and how dangerous that made it."""
    if c.dangerous_context or c.fouled_action in (
        GOAL_NET_DODGE,
        POST_DODGE,
        BLOCK,
        PASS_COURSE_CUT,
    ):
        score, kind = 35, "dangerous"
    elif c.gk_contact:
        if c.substate == 2:
            score, kind = 5, "substate"
        elif c.substate == 3:
            score, kind = 15, "substate"
        else:
            score, kind = 30, "gk"
    else:
        score, kind = 0, "default"

    if c.fouling_action == WAIT_TIMER:
        score += {"dangerous": 15, "gk": 12, "substate": 8, "default": 10}[kind]
    elif c.fouling_action == MOVE_ON_PASS:
        score += 5

    # Denying an obvious goal-scoring opportunity from a standing challenge.
    if c.dogso and (c.dangerous_context or kind == "dangerous"):
        if c.fouling_action == WAIT_TIMER:
            score += 30

    # The angle term applies only on the sub-state-dependent branch, which is
    # what keeps this component inside 0..80: the two maxima the research gives
    # are 35 + 15 + 30 (dangerous + standing + DOGSO) and 15 + 8 + 30 + 20
    # (sub-state 3 + standing + angle + distance).
    if kind == "substate":
        angle = _angle_score(c.approach_angle_deg)
        if c.fouling_action not in (WAIT_TIMER, MOVE_ON_PASS):
            angle >>= 1
        score += angle

    if c.substate == 3 and c.run_distance_m > 2.0:
        score += 20 if c.run_distance_m >= 3.0 else int((c.run_distance_m - 2.0) * 20.0)

    return score


# --- thresholds and severity ------------------------------------------------


def get_thresholds(inside_penalty_area: bool) -> tuple[int, int, int]:
    """T1 (foul), T2 (yellow), T3 (red). No per-referee variation exists."""
    return (60, 80, 110) if inside_penalty_area else (40, 80, 110)


def score_to_severity(combined: int, t1: int, t2: int, t3: int) -> int:
    """0 none, 1 foul, 2 foul + accumulate, 3 yellow, 4 red (pre-gate)."""
    midpoint = t1 + (t2 - t1) // 2
    if combined < t1:
        return 0
    if combined < midpoint:
        return 1
    if combined < t2:
        return 2
    if combined < t3:
        return 3
    return 4


def apply_gates(severity: int, c: Contact) -> int:
    """The four downgrade gates, in the order the engine applies them."""
    # Gate 0 -- IFAB 2016: DOGSO in the box is a penalty plus a caution, not a
    # sending-off. PES applies the cap everywhere, so no straight red is
    # reachable at all. GameplayFootball should not copy that half.
    if severity == 4:
        severity = 3
    # Gate 1 -- scene node flag (suspected advantage / cutscene).
    if c.scene_node_flag and severity in (3, 4):
        severity = 1
    # Gate 2 -- "no cards" match rule.
    if c.cards_disabled and severity in (3, 4):
        severity = 1
    # Gate 3 -- ball in a special state; suppresses *any* severity.
    if c.ball_in_special_state and severity != 0:
        severity = 1
    return severity


def evaluate(c: Contact) -> dict:
    a, b, cc = scoring_a(c), scoring_b(c), scoring_c(c)
    combined = min(a + b + cc, 255)
    t1, t2, t3 = get_thresholds(c.inside_penalty_area)
    raw = score_to_severity(combined, t1, t2, t3)
    final = apply_gates(raw, c)
    return {
        "a": a,
        "b": b,
        "c": cc,
        "combined": combined,
        "thresholds": (t1, t2, t3),
        "severity_raw": raw,
        "severity": final,
        "card": CARD_STATE_NAMES[final],
    }


# --- CLI --------------------------------------------------------------------


def cmd_table(_args) -> None:
    for inside in (False, True):
        t1, t2, t3 = get_thresholds(inside)
        where = "inside the penalty area" if inside else "outside the penalty area"
        mid = t1 + (t2 - t1) // 2
        restart = "penalty kick" if inside else "free kick"
        print(f"\n{where}   T1={t1} T2={t2} T3={t3}")
        print(f"  {0:>3}-{t1 - 1:<3}  0  no foul")
        print(f"  {t1:>3}-{mid - 1:<3}  1  {restart}, no card")
        print(f"  {mid:>3}-{t2 - 1:<3}  2  {restart} + stored for accumulation")
        print(f"  {t2:>3}-{t3 - 1:<3}  3  {restart} + yellow card")
        print(f"  {t3:>3}+     4  capped to 3 by the IFAB 2016 gate")


def cmd_sweep(_args) -> None:
    speeds = [2, 5, 10, 15, 20, 25, 30]
    contacts = [40, 60, 80, 100, 120, 150, 180]
    for standing in (False, True):
        label = "standing challenge" if standing else "running challenge"
        print(f"\ncombined score -- {label}, outside the box, hit from behind")
        print("contact\\speed " + "".join(f"{s:>6}" for s in speeds))
        for contact in contacts:
            row = []
            for speed in speeds:
                c = Contact(
                    contact_intensity=float(contact),
                    relative_speed_kmh=float(speed),
                    fouling_action=WAIT_TIMER if standing else 0,
                    approach_angle_deg=90.0,
                    dangerous_context=True,
                )
                row.append(evaluate(c)["combined"])
            print(f"{contact:>13} " + "".join(f"{v:>6}" for v in row))


def cmd_eval(args) -> None:
    c = Contact(
        contact_intensity=args.contact,
        relative_speed_kmh=args.speed,
        approach_angle_deg=args.angle,
        fouling_action=WAIT_TIMER if args.standing else 0,
        dangerous_context=args.dangerous,
        dogso=args.dogso,
        inside_penalty_area=args.inside_box,
    )
    r = evaluate(c)
    print(f"  A contact intensity : {r['a']:>3}  / 40")
    print(f"  B relative velocity : {r['b']:>3}  / 30")
    print(f"  C context           : {r['c']:>3}  / 80")
    print(f"  combined            : {r['combined']:>3}")
    print(f"  thresholds          : T1={r['thresholds'][0]} "
          f"T2={r['thresholds'][1]} T3={r['thresholds'][2]}")
    print(f"  severity            : {r['severity_raw']} -> {r['severity']} "
          f"({r['card']})")
    if r["severity_raw"] == 4 and r["severity"] == 3:
        print("  note: red downgraded to yellow by the IFAB 2016 gate")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("table", help="severity bands for both pitch locations").set_defaults(
        func=cmd_table
    )
    sub.add_parser("sweep", help="combined score over a speed/contact grid").set_defaults(
        func=cmd_sweep
    )

    ev = sub.add_parser("eval", help="score a single challenge")
    ev.add_argument("--contact", type=float, default=100.0,
                    help="contact intensity, engine units (40-180 is the useful span)")
    ev.add_argument("--speed", type=float, default=15.0,
                    help="relative closing speed in km/h")
    ev.add_argument("--angle", type=float, default=90.0,
                    help="approach angle in degrees; 0 face-to-face, 180 from behind")
    ev.add_argument("--standing", action="store_true",
                    help="the tackler was static (WAIT_TIMER) -- judged harder")
    ev.add_argument("--dangerous", action=argparse.BooleanOptionalAction, default=True,
                    help="the fouled player was in a dangerous action state")
    ev.add_argument("--dogso", action="store_true",
                    help="the foul denied an obvious goal-scoring opportunity")
    ev.add_argument("--inside-box", action="store_true",
                    help="the foul occurred inside the penalty area")
    ev.set_defaults(func=cmd_eval)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
