#!/usr/bin/env bash
# Runs the pinned Godot binary, fetching it first if needed. A fresh checkout
# has no .godot/extension_list.cfg, without which the GDExtension is not
# loaded, so the project is imported once before the first run.
set -euo pipefail
GODOT=$("$(dirname "$0")/fetch_godot.sh")
PROJECT="$(cd "$(dirname "$0")/../godot" && pwd)"
if [ ! -f "$PROJECT/.godot/extension_list.cfg" ]; then
  "$GODOT" --headless --path "$PROJECT" --import >/dev/null 2>&1 || true
fi
exec "$GODOT" "$@"
