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
trap 'rm -rf "$work"' EXIT

# The base config keeps whatever recording path it was last used with. Strip
# every key this harness owns and write our own, so the engine can only ever
# record into the fifo below.
grep -vE '"(frame_recording_path|showcase_team1|showcase_team2|stadium_object|match_duration_minutes)"' \
  "$base" > "$cfg"
{
  echo "\"frame_recording_path\" \"$fifo\""
  echo "\"showcase_team1\" \"$team1\""
  echo "\"showcase_team2\" \"$team2\""
  echo "\"stadium_object\" \"$stadium\""
  echo "\"match_duration_minutes\" \"$minutes.000000\""
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

# Two halves, the walkout, and the hold on the result, with room to spare.
budget=$(( minutes * 60 * 2 + 600 ))

echo "recording ${minutes}-minute halves, team $team1 v team $team2 -> $out"
( cd "$repo" && timeout "$budget" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 \
    xvfb-run -a "$bin" "$cfg" ) > "$log" 2>&1
status=$?

# The engine has stopped writing, so the encoder sees end of file and finishes
# on its own. Killing it here is what truncated the last fragment.
wait "$encoder"

if ! grep -q "destroying scenemanager" "$log"; then
  echo "run did not reach teardown (exit $status); this is not a complete match" >&2
  tail -20 "$log" >&2
fi

[ -s "$raw" ] || { echo "no frames were recorded" >&2; exit 1; }

seconds=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$raw" | cut -d. -f1)
raw_mb=$(( $(stat -c %s "$raw") / 1048576 ))

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
