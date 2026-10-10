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
# The default is catbox.moe's own 200 MB account limit. Litterbox takes 1 GB,
# and squeezing a match down to 200 MB for it costs both encoding time and
# quality for nothing - the first litterbox run spent ten minutes re-encoding
# 564 MB down to a 214 MB file that the host would have taken as it stood. So
# the limit follows the endpoint unless the caller sets one explicitly.
limit_mb=200
limit_explicit=0
litterbox_limit_mb=1000
# Audio capture: "auto" records the default sink's monitor when a Pulse/PipeWire
# recorder is present, 0 disables. The engine mixes through OpenAL Soft into
# whatever the desktop sink is, so recording that monitor is the whole job -
# no engine-side capture needed. Default on because a silent showcase is not a
# showcase.
audio=auto
# Where the finished match goes. catbox.moe closed anonymous uploads, but its
# own temporary host takes them with no account at all: litterbox, same
# operator, 1 GB, files die after the chosen window. "auto" = the account
# endpoint when CATBOX_USERHASH is set, litterbox otherwise, which is the
# anonymous case the owner asked for.
upload=auto
# Whether the AUDIO goes to the host with the video. The only capture that works
# here is a whole-system loopback (a sink's own monitor records digital silence
# on this machine), so the recorded track is whatever the machine plays - a call,
# a notification, a microphone routed to a sink. The first showcase uploaded
# that way and the owner's microphone ended up on a public link. Local file
# keeps its audio either way; this only decides what leaves the machine.
upload_audio=0
catbox_hash="${CATBOX_USERHASH:-}"
# litterbox's longest window. Its shortest is 1h, which is shorter than most
# matches take to produce.
litter_hours=72

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
    --limit-mb) limit_mb="$2"; limit_explicit=1; shift 2 ;;
    --seconds) seconds="$2"; shift 2 ;;
    --audio) audio="$2"; shift 2 ;;
    --upload) upload="$2"; shift 2 ;;
    --upload-audio) upload_audio="$2"; shift 2 ;;
    --litter-hours) litter_hours="$2"; shift 2 ;;
    --catbox-hash) catbox_hash="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[ -x "$bin" ] || { echo "no engine at $bin - build it first" >&2; exit 1; }
[ -f "$base" ] || { echo "no base config at $base" >&2; exit 1; }

# Validate every argument here, before the recorder or the engine starts. A
# check that lives at the end of the script lets a bad flag start a forty-minute
# match and only then refuses to upload it - which is exactly what "unknown
# --upload mode" did.
case "$upload" in
  auto) [ -n "$catbox_hash" ] && upload=account || upload=litterbox ;;
  account|litterbox|none) ;;
  *) echo "unknown --upload mode: $upload" >&2; exit 2 ;;
esac
[ "$upload" = "account" ] && [ -z "$catbox_hash" ] && {
  echo "--upload account needs a userhash (--catbox-hash or CATBOX_USERHASH)" >&2; exit 2; }
case "$audio" in
  auto|0|1) ;;
  *) echo "unknown --audio mode: $audio" >&2; exit 2 ;;
esac

# With the endpoint settled, the size limit can follow it: litterbox's 1 GB
# rather than catbox's 200 MB, unless the caller asked for a specific limit.
if [ "$limit_explicit" = "0" ] && [ "$upload" = "litterbox" ]; then
  limit_mb="$litterbox_limit_mb"
  echo "uploading to litterbox: keeping the recording up to ${limit_mb} MB"
fi

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

# Record the machine's audio through a PipeWire loopback node.
#
# The obvious way - parecord on the default sink's monitor - was tried first and
# records pure silence here: a 440 Hz tone played to that sink came back at
# ffmpeg's -91 dB floor. A loopback adapter with an explicit capture side does
# work (verified: tone captured at -27.3 dB mean), so that is what is used, and
# the parecord path is gone rather than left to fail silently.
#
# Started before the engine so the walk-out is on the track, and torn down by
# the trap below so an interrupted run leaves no recorder or node behind.
wav="${out%.*}.wav"
rec_pid=""
lw_node=""
if [ "$audio" != "0" ] && command -v pw-record >/dev/null && command -v pw-cli >/dev/null; then
  lw_node="gfshowcase$$"
  if pw-cli create-node adapter \
      "{ factory.name=support.null-audio-sink node.name=$lw_node media.class=Stream/Output/Audio object.linger=true audio.position=[FL,FR] capture.props={ media.class=Stream/Input/Audio node.name=$lw_node-in object.linger=true } }" \
      >/dev/null 2>&1; then
    rm -f "$wav"
    pw-record --target="$lw_node-in" --rate=48000 --channels=2 --format=s16 "$wav" \
      >/dev/null 2>&1 &
    rec_pid=$!
    echo "recording audio through the ${lw_node} loopback"
    echo "WARNING: this tap records EVERYTHING the machine plays, not just the match -" >&2
    echo "         a sink's own monitor records silence on this box, which is why the" >&2
    echo "         loopback is used. The local file gets that track; whether it is" >&2
    echo "         uploaded is --upload-audio 1." >&2
  else
    echo "could not create a PipeWire loopback; the showcase will be silent" >&2
    lw_node=""
  fi
else
  echo "no pw-record/pw-cli; the showcase will be silent" >&2
fi

# Leaving a live recorder or a loopback node behind would survive the run and
# keep writing into the wav that the mux is about to read.
cleanup_audio() {
  [ -n "$rec_pid" ] && { kill "$rec_pid" 2>/dev/null; wait "$rec_pid" 2>/dev/null; }
  [ -n "$lw_node" ] && pw-cli destroy "$lw_node" >/dev/null 2>&1
  return 0
}
trap cleanup_audio EXIT INT TERM

# Two halves, the walkout, and the hold on the result, with room to spare - and
# the clock only runs while the ball is in play. A 10-minute-half match with
# five goals, their celebrations and replays, an entrance and a half-time card
# took past 1800 s to reach 87:21, so the old budget cut the match a few minutes
# from full time and the run was still reported as a success (see the teardown
# check below, which SIGTERM survives).
budget=$(( minutes * 60 * 3 + 900 ))
# --seconds N caps the run instead: a fail-fast capture of one moment (a forced
# goal at 0:40, say) that lands as a playable file in a minute or two, rather
# than a match that only finalises at full time.
[ -n "${seconds:-}" ] && budget="$seconds"

echo "recording ${minutes}-minute halves, team $team1 v team $team2 -> $out"
# SDL's offscreen driver renders through EGL straight onto the card - no X
# server and no window, but the real GPU. Under xvfb-run this ran on llvmpipe
# instead, in software: 32 fps, measured as distinct frames in a 60 s window.
# On the card the same match saturates this recorder - 3,599 of 3,600 frames in
# that window are distinct - so the engine is at or above 60 fps and the pacer,
# not the renderer, is now the limit. Set SHOWCASE_SOFTWARE=1 to force the old
# path on a machine with no usable render node.
if [ "${SHOWCASE_SOFTWARE:-0}" = "1" ] || [ ! -e /dev/dri/renderD128 ]; then
  ( cd "$repo" && timeout "$budget" env -u WAYLAND_DISPLAY GF_NO_GAMEPADS=1 SDL_VIDEODRIVER=x11 \
      xvfb-run -a "$bin" "$cfg" ) > "$log" 2>&1
else
  ( cd "$repo" && timeout "$budget" env -u WAYLAND_DISPLAY -u DISPLAY GF_NO_GAMEPADS=1 \
      SDL_VIDEODRIVER=offscreen "$bin" "$cfg" ) > "$log" 2>&1
fi
status=$?

# The engine has stopped writing, so the encoder sees end of file and finishes
# on its own. Killing it here is what truncated the last fragment.
wait "$encoder"

# The engine is the only thing making sound and it has stopped, so stop the
# recorder here rather than at the end: the mux below runs ffmpeg, and a live
# recorder writing the same wav would race it. The trap covers the paths that
# exit before this point.
cleanup_audio
rec_pid=""
lw_node=""

# docs/HARNESSES.md says this harness "refuses to report a run that never
# reached destroying scenemanager". It printed a warning and then reported it
# anyway, exit 0 - so an interrupted match could still be handed over as
# evidence. The frames are kept either way (they are worth looking at); what
# the caller must not get is a success.
# A match that was cut short still tears down cleanly, so teardown alone does
# not say the match finished. In full-match mode the engine prints its own
# completion line, and that is what "complete" means.
if [ -z "${seconds:-}" ] && grep -q '"menu_smoke_test_full_match" "true"' "$cfg" &&
   ! grep -q "Full match complete" "$log"; then
  echo "the match did not reach full time (exit $status)" >&2
  grep -aE "clock [0-9]+:" "$log" | tail -3 >&2
  cp "$log" "${out%.*}.log" 2>/dev/null
  [ -s "$raw" ] && mv "$raw" "${out%.*}.partial.mp4" &&
    echo "partial recording at ${out%.*}.partial.mp4" >&2
  exit 1
fi

if [ -z "${seconds:-}" ] && ! grep -q "destroying scenemanager" "$log"; then
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

# Is there actually sound on the track? An audio stream of pure silence is
# not "it has audio", and the only way to know is to measure the track: -91 dB
# is ffmpeg's floor for digital silence. Reported either way, because the
# difference is invisible in a file listing.
audio_args=()
audio_mean="-91.0dB"
if [ -s "$wav" ]; then
  # ffmpeg prints "mean_volume: -91.0 dB" with a space before the unit, so
  # comparing the whole string against "-91.0dB" never matched and the silence
  # warning below did not fire on a completely silent track. Compare the
  # number, not the formatted string.
  audio_mean=$(ffmpeg -hide_banner -i "$wav" -af volumedetect -f null - 2>&1 |
              sed -n 's/.*mean_volume: \(.*\) dB/\1dB/p' | tail -1)
  audio_mean="${audio_mean:--91.0dB}"
  # -90 dB is ffmpeg's measurement floor; anything at or under it is digital
  # silence, whatever the track says about itself.
  if [ "$(awk -v v="$audio_mean" 'BEGIN{print (v+0 <= -90) ? 1 : 0}')" = "1" ]; then
    echo "WARNING: the audio track is silent ($audio_mean) - the engine produced no sound" >&2
  else
    audio_args=(-i "$wav" -map 0:v:0 -map 1:a:0 -c:a aac -b:a 160k -ac 2 -shortest)
  fi
  echo "audio track: $(ffprobe -v error -show_entries format=duration -of csv=p=0 "$wav" | cut -d. -f1)s, mean volume $audio_mean"
else
  echo "WARNING: no audio was recorded; the mp4 will be silent" >&2
fi

if [ "$limit_mb" -gt 0 ] && [ "$raw_mb" -gt "$limit_mb" ] && [ "${seconds:-0}" -gt 0 ]; then
  # Fit the duration into the limit, holding back a tenth for container
  # overhead and the encoder overshooting its target on busy scenes.
  rate=$(( limit_mb * 8 * 1024 * 9 / 10 / seconds ))
  echo "re-encoding ${raw_mb} MB / ${seconds}s to fit ${limit_mb} MB (${rate}k)"
  ffmpeg -y -loglevel error -i "$raw" "${audio_args[@]}" \
    -c:v libx264 -preset slow -b:v "${rate}k" -maxrate "$(( rate * 3 / 2 ))k" \
    -bufsize "$(( rate * 3 ))k" -pix_fmt yuv420p \
    -movflags +faststart "$out"
else
  ffmpeg -y -loglevel error -i "$raw" "${audio_args[@]}" \
    -c:v copy -movflags +faststart "$out"
fi

# The upload can fail - litterbox was down for a whole session - and must not
# cost the match's log. This copy is deliberately ahead of everything that can
# exit, including the size check below.
cp "$log" "${out%.*}.log"
echo "${out%.*}.log"

# Upload. catbox.moe itself needs an account, so "auto" uses the account
# endpoint when a hash is configured and litterbox - catbox's own anonymous,
# temporary host - when it is not. Anything that is not an http URL is treated
# as a failure: litterbox answers with a plain error string on a rejected
# upload, and a run that printed that as a success would be worse than useless.
if [ "$upload" = "none" ]; then
  echo "upload disabled: $out is local only"
else
  # Only now is an endpoint chosen: "none" used to fall through to litterbox
  # and upload the file anyway, three curl errors and all, which is the
  # opposite of what --upload none asks for.
  # What leaves the machine. The local $out always keeps its audio track.
  send="$out"
  if [ "$upload_audio" != "1" ] && [ -n "${audio_args[*]:-}" ]; then
    send="${out%.mp4}.upload.mp4"
    ffmpeg -y -loglevel error -i "$out" -map 0:v:0 -c:v copy -an -movflags +faststart "$send"
    echo "uploading without audio (--upload-audio 1 to include it)"
  fi
  if [ "$upload" = "account" ]; then
    url=$(curl -fsS --retry 3 -F "reqtype=fileupload" -F "userhash=$catbox_hash" -F "fileToUpload=@$send" https://files.catbox.moe/user/api.php) || url=""
  else
    url=$(curl -fsS --retry 3 -F "reqtype=fileupload" -F "time=${litter_hours}h" -F "fileToUpload=@$send" https://litterbox.catbox.moe/resources/internals/api.php) || url=""
  fi
  [ "$send" != "$out" ] && rm -f "$send"
  case "$url" in
    http*) echo "uploaded ($upload): $url" ;;
    *) echo "upload FAILED for $out: ${url:-no response}" >&2; exit 1 ;;
  esac
fi

echo "$(stat -c %s "$out" | awk '{printf "%.1f MB", $1/1048576}') / ${seconds}s -> $out"
