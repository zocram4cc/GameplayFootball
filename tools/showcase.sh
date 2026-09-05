#!/usr/bin/env bash
# Record a full match to an mp4, headless.
#
# The three mistakes this exists to make impossible are in docs/HARNESSES.md:
# a config still pointing at somebody else's recording path (which fills a disk
# with raw frames), an encoder that loses the whole file when the run is cut
# short, and a run that never reached teardown being reported as evidence.
#
#   tools/showcase.sh --team1 16 --team2 13 --minutes 10 --out /tmp/match.mp4
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

bin="$repo/build/gameplayfootball"
base="$repo/data/menu_smoke_2hug_smbg.config"
team1=16
team2=13
stadium="media/objects/stadiums/pes_st017/pes_st017.object"
# The walk-out. "" lets the engine pick one for the stadium, "none" skips it;
# an id names one of the nineteen families under media/cutscenes/ent.
entrance=""
minutes=10
out=/tmp/showcase.mp4
width=1280
height=720
# catbox.moe rejects anything larger; 0 disables the second pass entirely.
limit_mb=200

while [ $# -gt 0 ]; do
  case "$1" in
    --team1) team1="$2"; shift 2 ;;
    --team2) team2="$2"; shift 2 ;;
    --stadium) stadium="$2"; shift 2 ;;
    --entrance) entrance="$2"; shift 2 ;;
    --minutes) minutes="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --base) base="$2"; shift 2 ;;
    --bin) bin="$2"; shift 2 ;;
    --limit-mb) limit_mb="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[ -x "$bin" ] || { echo "no engine at $bin - build it first" >&2; exit 1; }
[ -f "$base" ] || { echo "no base config at $base" >&2; exit 1; }

work="$(mktemp -d)"
fifo="$work/frames"
raw="$work/raw.mp4"
cfg="$work/run.config"
log="$work/run.log"
# Interrupted run: keep what was recorded. The encoder writes fragmented mp4
# precisely so a cut-short file still plays, and deleting the work directory
# threw that away - a 22-minute match was lost to this trap, having survived
# the kill it was designed to survive. The frames themselves are the only
# thing worth saving; everything else here is regenerable.
salvage() {
  local code=$?
  if [ ! -s "$out" ] && [ -s "$raw" ]; then
    mv "$raw" "${out%.*}.partial.mp4" 2>/dev/null &&
      echo "kept the partial recording at ${out%.*}.partial.mp4" >&2
    cp "$log" "${out%.*}.log" 2>/dev/null
  fi
  rm -rf "$work"
  exit $code
}
trap salvage EXIT INT TERM HUP

# The base config keeps whatever recording path it was last used with. Strip
# every key this harness owns and write our own, so the engine can only ever
# record into the fifo below.
grep -vE '"(frame_recording_path|showcase_team1|showcase_team2|stadium_object|match_duration_minutes|entrance_id)"' \
  "$base" > "$cfg"
{
  echo "\"frame_recording_path\" \"$fifo\""
  echo "\"showcase_team1\" \"$team1\""
  echo "\"showcase_team2\" \"$team2\""
  echo "\"stadium_object\" \"$stadium\""
  echo "\"entrance_id\" \"$entrance\""
  # The engine's key is the whole match: it scales real time against a 90
  # minute clock (MatchDurationFactorFromMinutes), so half-time falls at half
  # of it. --minutes is per HALF, which is how this script has always described
  # itself and what the budget below counts, so it is doubled here - it used to
  # be written straight through, and every "10-minute halves" run recorded five.
  echo "\"match_duration_minutes\" \"$(( minutes * 2 )).000000\""
} >> "$cfg"

mkfifo "$fifo"
# Without this the engine would create a regular file and fill it with
# uncompressed frames - 32 GB in about forty minutes at 1280x720x60.
[ -p "$fifo" ] || { echo "$fifo is not a fifo, refusing to record" >&2; exit 1; }

# Fragmented mp4: the index is written as it goes, so a run that is interrupted
# still leaves a playable file. A plain mp4 killed before its moov atom is
# written is not recoverable, which cost one complete 1.3 GB match.
ffmpeg -y -loglevel error \
  -f rawvideo -pixel_format rgba -video_size "${width}x${height}" -framerate 60 \
  -i "$fifo" \
  -c:v libx264 -preset fast -crf 26 -pix_fmt yuv420p \
  -movflags +frag_keyframe+empty_moov+default_base_moof \
  "$raw" &
encoder=$!

# Two halves, the walkout, and the hold on the result, with room to spare - and
# the clock only runs while the ball is in play. A 10-minute-half match with
# five goals, their celebrations and replays, an entrance and a half-time card
# took past 1800 s to reach 87:21, so the old budget cut the match a few minutes
# from full time and the run was still reported as a success (see the teardown
# check below, which SIGTERM survives).
budget=$(( minutes * 60 * 3 + 900 ))

echo "recording ${minutes}-minute halves, team $team1 v team $team2 -> $out"
# SDL's offscreen driver renders through EGL straight onto the card - no X
# server and no window, but the real GPU. Under xvfb-run this ran on llvmpipe
# instead, in software: 32 fps, measured as distinct frames in a 60 s window.
# On the card the same match saturates this recorder - 3,599 of 3,600 frames in
# that window are distinct - so the engine is at or above 60 fps and the pacer,
# not the renderer, is now the limit. Set SHOWCASE_SOFTWARE=1 to force the old
# path on a machine with no usable render node.
if [ "${SHOWCASE_SOFTWARE:-0}" = "1" ] || [ ! -e /dev/dri/renderD128 ]; then
  ( cd "$repo" && timeout "$budget" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 \
      xvfb-run -a "$bin" "$cfg" ) > "$log" 2>&1
else
  ( cd "$repo" && timeout "$budget" env -u WAYLAND_DISPLAY -u DISPLAY \
      SDL_VIDEODRIVER=offscreen "$bin" "$cfg" ) > "$log" 2>&1
fi
status=$?

# The engine has stopped writing, so the encoder sees end of file and finishes
# on its own. Killing it here is what truncated the last fragment.
wait "$encoder"

# docs/HARNESSES.md says this harness "refuses to report a run that never
# reached destroying scenemanager". It printed a warning and then reported it
# anyway, exit 0 - so an interrupted match could still be handed over as
# evidence. The frames are kept either way (they are worth looking at); what
# the caller must not get is a success.
# A match that was cut short still tears down cleanly, so teardown alone does
# not say the match finished. In full-match mode the engine prints its own
# completion line, and that is what "complete" means.
if grep -q '"menu_smoke_test_full_match" "true"' "$cfg" &&
   ! grep -q "Full match complete" "$log"; then
  echo "the match did not reach full time (exit $status)" >&2
  grep -aE "clock [0-9]+:" "$log" | tail -3 >&2
  cp "$log" "${out%.*}.log" 2>/dev/null
  [ -s "$raw" ] && mv "$raw" "${out%.*}.partial.mp4" &&
    echo "partial recording at ${out%.*}.partial.mp4" >&2
  exit 1
fi

if ! grep -q "destroying scenemanager" "$log"; then
  echo "run did not reach teardown (exit $status); this is not a complete match" >&2
  tail -20 "$log" >&2
  cp "$log" "${out%.*}.log" 2>/dev/null
  [ -s "$raw" ] && mv "$raw" "${out%.*}.partial.mp4" &&
    echo "partial recording at ${out%.*}.partial.mp4" >&2
  exit 1
fi

[ -s "$raw" ] || { echo "no frames were recorded" >&2; exit 1; }

seconds=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$raw" | cut -d. -f1)
raw_mb=$(( $(stat -c %s "$raw") / 1048576 ))

# A fragmented file can answer "duration" with nothing at all, and then the
# limit was skipped in silence and the caller got a file too big for wherever
# it was going. Count the frames instead; the recorder's rate is fixed.
if [ -z "${seconds:-}" ] || [ "$seconds" -le 0 ] 2>/dev/null; then
  frames=$(ffprobe -v error -count_packets -select_streams v \
    -show_entries stream=nb_read_packets -of csv=p=0 "$raw")
  seconds=$(( ${frames:-0} / 60 ))
  echo "no duration in the container; counted ${frames:-0} frames -> ${seconds}s" >&2
fi

if [ "$limit_mb" -gt 0 ] && [ "$raw_mb" -gt "$limit_mb" ] && [ "${seconds:-0}" -gt 0 ]; then
  # Fit the duration into the limit, holding back a tenth for container
  # overhead and the encoder overshooting its target on busy scenes.
  rate=$(( limit_mb * 8 * 1024 * 9 / 10 / seconds ))
  echo "re-encoding ${raw_mb} MB / ${seconds}s to fit ${limit_mb} MB (${rate}k)"
  ffmpeg -y -loglevel error -i "$raw" \
    -c:v libx264 -preset slow -b:v "${rate}k" -maxrate "$(( rate * 3 / 2 ))k" \
    -bufsize "$(( rate * 3 ))k" -pix_fmt yuv420p \
    -movflags +faststart "$out"
else
  ffmpeg -y -loglevel error -i "$raw" -c copy -movflags +faststart "$out"
fi

echo "$(stat -c %s "$out" | awk '{printf "%.1f MB", $1/1048576}') / ${seconds}s -> $out"

# The engine's log is the only record of what the match actually did - which
# celebration was cast, when the replay fired - and the work directory goes on
# exit. Keep it beside the video it explains.
cp "$log" "${out%.*}.log"
echo "${out%.*}.log"
