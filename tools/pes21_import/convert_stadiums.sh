#!/bin/bash
# Converts every 4cc stadium pack in a directory.
#
# A pack is a directory named "<slot> - <name>" holding
#   #Win/st<slot>_fpk_extracted/center1.fmdl        the scene
#   sourceimages/tga/#windx11/                      its textures
#   turf3d/sourceimages/#windx11/                   the 3D turf's textures
#   light/#Win/light_st<slot>_a[fr]_fpk_extracted/sky_dome.fmdl
#
# and becomes data/media/objects/stadiums/pes_st<slot>/. The sky dome is
# converted separately into its own object: the engine loads one through
# "skydome_object" and keeps it out of the shadow map, which is what a dome
# enclosing the whole stadium needs.
#
# --max-extent has to be large for packs whose surroundings *are* the view (Namek's
# terrain is 276 m across and its dome 1154 m); the 260 m default is meant for a
# real stadium's car park.
#
#   convert_stadiums.sh <packs dir> [<fmdl-lib>] [<out root>]
set -u
PACKS=${1:?usage: convert_stadiums.sh <packs dir> [fmdl-lib] [out root]}
FMDL_LIB=${2:-/home/z/Code/GameplayFootball/4cc Blender Starter Pack/scripts/addons/pes-fmdl}
OUT_ROOT=${3:-/home/z/Code/GameplayFootball/data/media/objects/stadiums}
HERE=$(cd "$(dirname "$0")" && pwd)
MAX_EXTENT=${MAX_EXTENT:-1300}

converted=0
skipped=0
for pack in "$PACKS"/*/; do
  pack=${pack%/}
  name=$(basename "$pack")
  # a nested directory of the same name is how some packs are unzipped
  [ -d "$pack/$name" ] && pack="$pack/$name"
  slot=$(printf '%s' "$name" | grep -oE '^[0-9]{3}')
  if [ -z "$slot" ]; then
    echo "SKIP $name: no slot number in the directory name"
    skipped=$((skipped + 1)); continue
  fi
  scene=$(find "$pack/#Win" -maxdepth 2 -name "*.fmdl" 2>/dev/null | head -1)
  if [ -z "$scene" ]; then
    echo "SKIP $name: no scene fmdl under #Win"
    skipped=$((skipped + 1)); continue
  fi
  out="$OUT_ROOT/pes_st$slot"
  echo "=== $name -> $out"
  python3 "$HERE/stadium_to_gf.py" "$scene" "$out" \
    --fmdl-lib "$FMDL_LIB" \
    --textures "$pack/sourceimages/tga/#windx11" \
    --textures "$pack/turf3d/sourceimages/#windx11" \
    --name "pes_st$slot" --max-extent "$MAX_EXTENT" || { skipped=$((skipped + 1)); continue; }

  # The sky dome, if the pack has one. "af" and "ar" are the pack's two lighting
  # sets; the first that converts is used.
  for variant in af ar; do
    dome=$(find "$pack/light/#Win" -maxdepth 2 -path "*_${variant}_*" -name "sky_dome.fmdl" \
           2>/dev/null | head -1)
    [ -z "$dome" ] && continue
    domedir=$(dirname "$dome")
    python3 "$HERE/stadium_to_gf.py" "$dome" "$out/sky" \
      --fmdl-lib "$FMDL_LIB" \
      --textures "$pack/sourceimages/tga/#windx11" \
      --textures "$domedir/../../sourceimages/#windx11" \
      --name sky --max-extent 0 && break
  done
  converted=$((converted + 1))
done
echo "converted $converted stadium(s), skipped $skipped"
