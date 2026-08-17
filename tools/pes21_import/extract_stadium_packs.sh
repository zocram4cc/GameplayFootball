#!/bin/bash
# Extracts every .fpk still packed inside a 4cc stadium pack.
#
# A pack is not one model. Planet Namek ships eleven sub-packs and thirteen
# unextracted archives: the staff on the touchline, the tifo and stand flags, the
# TV screens, the cheer sets, the effects, the scarecrow props. Only the centre
# scene, the pitch and the lighting arrive extracted, which is why an imported
# stadium is missing everything the reference broadcast shows around the pitch.
#
# Each archive is extracted next to itself as <name>_fpk_extracted/, matching the
# layout the already-extracted ones use, so the converter finds them the same way.
#
#   extract_stadium_packs.sh <pack dir> [<pack dir> ...]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
total=0
for pack in "$@"; do
  pack=${pack%/}
  name=$(basename "$pack")
  [ -d "$pack/$name" ] && pack="$pack/$name"
  echo "=== $name"
  while IFS= read -r archive; do
    [ -z "$archive" ] && continue
    stem=$(basename "$archive")
    stem=${stem%.fpk}
    stem=${stem%.fpkd}
    dest="$(dirname "$archive")/${stem}_$(case "$archive" in *.fpkd) echo fpkd;; *) echo fpk;; esac)_extracted"
    if [ -d "$dest" ] && [ -n "$(ls -A "$dest" 2>/dev/null)" ]; then
      continue  # already extracted, by us or by whoever built the pack
    fi
    mkdir -p "$dest"
    count=$(python3 "$HERE/fpk.py" "$archive" "$dest" 2>/dev/null | wc -l)
    echo "  $stem: $count file(s)"
    total=$((total + count))
  done < <(find "$pack" -name "*.fpk" -o -name "*.fpkd" | sort)
done
echo "extracted $total file(s)"
echo "--- models now available ---"
for pack in "$@"; do
  pack=${pack%/}
  name=$(basename "$pack")
  [ -d "$pack/$name" ] && pack="$pack/$name"
  find "$pack" -name "*.fmdl" | sed "s|$pack/||" | sort
done
