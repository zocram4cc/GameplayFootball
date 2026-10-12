#!/usr/bin/env bash
# Downloads the pinned Godot editor once, verifies its SHA-512 against the
# release's SHA512-SUMS.txt, and prints the binary path.
set -euo pipefail

GODOT_VERSION=4.7.2-stable
ZIP=Godot_v${GODOT_VERSION}_linux.x86_64.zip
BIN=Godot_v${GODOT_VERSION}_linux.x86_64
URL=https://github.com/godotengine/godot-builds/releases/download/${GODOT_VERSION}
DIR=${XDG_CACHE_HOME:-$HOME/.cache}/godotfootball/godot-${GODOT_VERSION%-stable}

if [ ! -x "$DIR/$BIN" ]; then
  mkdir -p "$DIR"
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  curl -fsSL -o "$tmp/$ZIP" "$URL/$ZIP" >&2
  curl -fsSL -o "$tmp/SHA512-SUMS.txt" "$URL/SHA512-SUMS.txt" >&2
  (cd "$tmp" && grep " $ZIP\$" SHA512-SUMS.txt | sha512sum -c - >&2)
  unzip -q -o "$tmp/$ZIP" -d "$DIR"
  chmod +x "$DIR/$BIN"
fi
echo "$DIR/$BIN"
