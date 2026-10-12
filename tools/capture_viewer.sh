#!/usr/bin/env bash
# Renders the viewer on the GPU (gamescope headless) into PNG frames.
#   PES21_ROOT=... tools/capture_viewer.sh --face ID [--clip NAME] [--frames N] [--out DIR] [-- extra viewer args]
#   PES21_ROOT=... tools/capture_viewer.sh --scene res://match/match.tscn [--frames N] [--out DIR] [-- scene args]
set -euo pipefail
cd "$(dirname "$0")/.."

DEFAULT_FRAMES=60          # two seconds at the capture rate
CAPTURE_FPS=30
WIDTH=1280
HEIGHT=720

usage() { echo "usage: PES21_ROOT=DIR $0 (--face ID [--clip NAME] | --scene RES) [--frames N] [--out DIR] [-- args]" >&2; exit 2; }
[ -n "${PES21_ROOT:-}" ] || usage

face="" clip="" scene=res://viewer/viewer.tscn frames=$DEFAULT_FRAMES out="captures/$(date +%Y%m%d-%H%M%S)"
extra=()
while [ $# -gt 0 ]; do
  case "$1" in
    --face) face=$2; shift 2 ;;
    --clip) clip=$2; shift 2 ;;
    --scene) scene=$2; shift 2 ;;
    --frames) frames=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --) shift; extra=("$@"); break ;;
    *) usage ;;
  esac
done
[ -n "$face" ] || [ "$scene" != res://viewer/viewer.tscn ] || usage

GODOT=$(tools/fetch_godot.sh)
[ -f godot/.godot/extension_list.cfg ] || "$GODOT" --headless --path godot --import >/dev/null 2>&1 || true
mkdir -p "$out"
out=$(realpath "$out")
viewer_args=(--pes21-root "$PES21_ROOT")
[ -n "$face" ] && viewer_args+=(--face "$face")
[ -n "$clip" ] && viewer_args+=(--clip "$clip")

gamescope --backend headless -W "$WIDTH" -H "$HEIGHT" -- "$GODOT" --path godot \
  --write-movie "$out/frame.png" --fixed-fps "$CAPTURE_FPS" --quit-after "$frames" \
  "$scene" -- "${viewer_args[@]}" "${extra[@]}" >"$out/godot.log" 2>&1 || true

if ! grep -q "Using Device" "$out/godot.log" || grep -qi "llvmpipe" "$out/godot.log"; then
  echo "GPU check failed (no 'Using Device' line, or llvmpipe): see $out/godot.log" >&2
  exit 1
fi
ls "$out"/frame*.png
