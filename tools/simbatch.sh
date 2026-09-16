#!/usr/bin/env bash
# Run N full matches headless and report the scoreline distribution.
#
# Balance work needs a distribution, not an anecdote: six matches read as
# anything you like (the same build produced 0,1,1,1,0,2 and 1,1,2,2,0,3 on
# different seeds). A match takes about a minute of wall time at 6x time scale
# and the sim is CPU bound, so this runs them in parallel and parses the
# engine's own [balance] lines rather than adding telemetry.
#
#   tools/simbatch.sh --matches 12 --team1 16 --team2 13
#
# Every match is a fixed "random_seed", so a before/after pair over the same
# seed range is comparable.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$repo/build/gameplayfootball"
base="$repo/data/menu_smoke_2hug_smbg.config"
matches=12
concurrency=6
seed0=1000
team1=16
team2=13
minutes=25
timescale=6
stadium="media/objects/stadiums/pes_st002/pes_st002.object"
entrance="none"
out="$(mktemp -d)"

while [ $# -gt 0 ]; do
  case "$1" in
    --matches) matches="$2"; shift 2 ;;
    --concurrency) concurrency="$2"; shift 2 ;;
    --seed0) seed0="$2"; shift 2 ;;
    --team1) team1="$2"; shift 2 ;;
    --team2) team2="$2"; shift 2 ;;
    --timescale) timescale="$2"; shift 2 ;;
    --minutes) minutes="$2"; shift 2 ;;
    --stadium) stadium="$2"; shift 2 ;;
    --entrance) entrance="$2"; shift 2 ;;
    --bin) bin="$2"; shift 2 ;;
    --base) base="$2"; shift 2 ;;
    --out) out="$2"; mkdir -p "$out"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[ -x "$bin" ] || { echo "no binary at $bin" >&2; exit 2; }
[ -f "$base" ] || { echo "no config at $base" >&2; exit 2; }

run_one() {
  local this_seed="$1"
  local cfg="$out/s$this_seed.config"
  # Strip every key the harness owns and write our own, so a leftover in the
  # base config (recording path, teams, stadium, entrance, duration, seed, the
  # full-match flag, the game-over hold, the time scale) can never leak into a
  # numbers run as a DUPLICATE key. Same strip-list idea as tools/showcase.sh,
  # extended to every key appended below.
  grep -vE '"(frame_recording_path|showcase_team1|showcase_team2|stadium_object|match_duration_minutes|entrance_id|random_seed|menu_smoke_test_full_match|menu_smoke_gameover_hold_ms|menu_smoke_match_time_scale)"' \
    "$base" > "$cfg"
  cat >> "$cfg" <<EOF
"menu_smoke_test_full_match" "true"
"menu_smoke_gameover_hold_ms" "1000"
"match_duration_minutes" "$minutes"
"menu_smoke_match_time_scale" "$timescale"
"showcase_team1" "$team1"
"showcase_team2" "$team2"
"stadium_object" "$stadium"
"entrance_id" "$entrance"
"random_seed" "$this_seed"
EOF
  (cd "$repo/data" && timeout 1800 env -u WAYLAND_DISPLAY -u DISPLAY GF_NO_GAMEPADS=1 \
      SDL_VIDEODRIVER=offscreen "$bin" "$cfg" 2>&1) |
    grep -aE "Full match complete|^\[balance|^\[balance-passing\]" > "$out/s$this_seed.txt"
}

echo "running $matches matches, $concurrency at a time, team $team1 v team $team2, ${minutes}-minute duration, ${timescale}x time scale"
# Throttle on the number of children actually alive. `wait -n` returns
# immediately once a job has already been reaped, so it never throttled: a
# 12-match 6-way run launched 12 at once (measured: 9 configs spawned with 3
# matches in flight, load average 18 on 16 cores, every match slower).
for i in $(seq 0 $((matches - 1))); do
  while [ "$(jobs -rp | wc -l)" -ge "$concurrency" ]; do sleep 1; done
  run_one $((seed0 + i)) &
done
wait

cat "$out"/s*.txt > "$out/all.txt"
python3 - "$out/all.txt" <<'PYEOF'
import re, sys, collections, statistics as st
text = open(sys.argv[1]).read()
scores = [(int(a), int(b)) for a, b in
          re.findall(r"Full match complete: \S+ (\d+) - (\d+) \S+", text)]
bal = [(int(s1), int(s2), int(t1), int(t2), int(v1), int(v2), float(x1), float(x2),
        int(g1), int(g2), int(c1), int(c2), int(f1), int(f2), int(u1), int(u2),
        int(w1), int(w2))
       for s1, s2, t1, t2, v1, v2, x1, x2, g1, g2, c1, c2, f1, f2, u1, u2, w1, w2 in
       re.findall(r"\[balance\] shots (\d+)-(\d+) \| on target (\d+)-(\d+) \| saves (\d+)-(\d+)"
                  r" \| xg ([\d.]+)-([\d.]+) \| goals (\d+)-(\d+)"
                  r" \| crossings (\d+)-(\d+) \(in frame (\d+)-(\d+)\)"
                  r" \| crossings-no-shot (\d+)-(\d+) \(in frame (\d+)-(\d+)\)", text)]
pas = [(int(p1), int(p2), int(a1), int(a2))
       for p1, p2, a1, a2 in
       re.findall(r"\[balance-passing\] passes (\d+)-(\d+) \| accuracy (\d+)%-(\d+)%", text)]
if not scores:
    print("no completed matches"); sys.exit(1)
totals = [a + b for a, b in scores]
hist = collections.Counter(totals)
print("matches %d" % len(totals))
print("goals per match: mean %.2f  median %.1f  range %d-%d" %
      (st.mean(totals), st.median(totals), min(totals), max(totals)))
print("distribution:", " ".join("%d:%d" % (g, hist[g]) for g in range(0, max(totals) + 1)))
if bal:
    shots = [s for r in bal for s in r[0:2]]
    on = [s for r in bal for s in r[2:4]]
    saves = [s for r in bal for s in r[4:6]]
    xg = [s for r in bal for s in r[6:8]]
    goals = [s for r in bal for s in r[8:10]]
    print("per team: shots %.1f  on target %.1f (%.0f%%)  saves %.1f  xG %.2f  goals %.2f" %
          (st.mean(shots), st.mean(on), 100.0 * sum(on) / max(1, sum(shots)), st.mean(saves),
           st.mean(xg), st.mean(goals)))
    print("           keeper saves %.0f%% of the projected on-target; %.0f%% of it became goals" %
          (100.0 * sum(saves) / max(1, sum(on)), 100.0 * sum(goals) / max(1, sum(on))))
    # `on target` above is the projection: the strike is aimed at the frame and
    # the projection uses the same formula the aiming band clamps, so it cannot
    # disagree with it. This is where the ball actually met the plane.
    cross = [s for r in bal for s in r[10:12]]
    inframe = [s for r in bal for s in r[12:14]]
    noshot = [s for r in bal for s in r[14:16]]
    noshotframe = [s for r in bal for s in r[16:18]]
    print("measured:  shots that reached the line %.1f (%.0f%%)  in frame %.1f (%.0f%% of shots, "
          "%.0f%% of projected)" %
          (st.mean(cross), 100.0 * sum(cross) / max(1, sum(shots)), st.mean(inframe),
           100.0 * sum(inframe) / max(1, sum(shots)),
           100.0 * sum(inframe) / max(1, sum(on))))
    print("           plus %.1f reached it with no shot in flight (%.1f in frame)" %
          (st.mean(noshot), st.mean(noshotframe)))
if pas:
    attempts = [p for r in pas for p in r[0:2]]
    weighted = sum(p * a for r in pas for p, a in zip(r[0:2], r[2:4]))
    print("per team: passes %.0f  accuracy %.0f%%" %
          (st.mean(attempts), weighted / max(1, sum(attempts))))
print("target:   xG 2.00-3.00 per team, goals 2-4 per match peaking at 3-4, pass accuracy ~80pct")
PYEOF
echo "logs in $out"
