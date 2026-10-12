#!/usr/bin/env bash
# gfsim_run as a user runs it. Usage: gfsim_run_test.sh <gfsim_run> <repo root>
set -u
RUN=$1
ROOT=$2
FIXTURE="$ROOT/assets/fixtures/teams.sql"
fail() { echo "FAIL: $*"; exit 1; }

# Without a PES install there is no skeleton to animate: refuse, with exit 2.
err=$(env -u PES21_ROOT "$RUN" --db-sql "$FIXTURE" --home 1 --away 2 2>&1 >/dev/null)
code=$?
[ $code -eq 2 ] || fail "missing --pes21-root exited $code, expected 2"
[[ "$err" == *"need --pes21-root"* ]] || fail "missing --pes21-root message: $err"

if [ -z "${PES21_ROOT:-}" ]; then
  echo "SKIP: PES21_ROOT not set, match runs not exercised"
  exit 77
fi

# One-minute matches, same seed: identical fingerprint, full report, exit 0.
MINUTES=1
SEED=7
a=$(cd "$ROOT" && "$RUN" --db-sql "$FIXTURE" --home 1 --away 2 --seed $SEED --minutes $MINUTES --pes21-root "$PES21_ROOT") \
  || fail "first run exited $?"
b=$(cd "$ROOT" && "$RUN" --db-sql "$FIXTURE" --home 1 --away 2 --seed $SEED --minutes $MINUTES --pes21-root "$PES21_ROOT") \
  || fail "second run exited $?"
[[ "$a" == *"Full match complete: HOM "* ]] || fail "no full-time line"
da=$(grep '^\[determinism\]' <<<"$a")
db=$(grep '^\[determinism\]' <<<"$b")
[ -n "$da" ] || fail "no determinism line"
[ "$da" == "$db" ] || fail "same seed, different matches: $da vs $db"
echo "OK: $da"
