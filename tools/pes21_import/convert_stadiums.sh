#!/bin/bash
# Converts every 4cc stadium pack in a directory.
#
# A pack is a directory named "<slot> - <name>" - or "st<slot>", which is how the
# stadium cpks unpack - holding
#   #Win/st<slot>_fpk_extracted/center1.fmdl        the scene
#   sourceimages/tga/#windx11/                      its textures
#   turf3d/sourceimages/#windx11/                   the 3D turf's textures
#   light/#Win/light_st<slot>_a[fr]_fpk_extracted/sky_dome.fmdl
#   staff/#Win/staff_st<slot>_fpk_extracted/                the touchline staff
#
# and becomes data/media/objects/stadiums/pes_st<slot>/, complete: the scene, its
# textures, the sun the pack ships (lighting.txt) and its own touchline staff. The
# sky dome is
# converted separately into its own object: the engine loads one through
# "skydome_object" and keeps it out of the shadow map, which is what a dome
# enclosing the whole stadium needs.
#
# --max-extent has to be large for packs whose surroundings *are* the view (Namek's
# terrain is 276 m across and its dome 1154 m; benuldys has backdrop pieces
# 4.7 km across); stadium_to_gf.py's own 260 m default is meant for a real
# stadium's car park.
#
# COMMON_STAFF=<dir> is the game's own bg/common (or its staff subtree), used for
# the touchline staff of any pack that ships none itself. COMMON_TEXTURES=<dir> is
# any other tree of the game's textures to dress them from; a pack's staff wear
# skins it ships itself, PES's own wear stock coach kit that lives elsewhere.
#
#   convert_stadiums.sh <packs dir> [<fmdl-lib>] [<out root>]
set -u
PACKS=${1:?usage: convert_stadiums.sh <packs dir> [fmdl-lib] [out root]}
FMDL_LIB=${2:-/home/z/Code/GameplayFootball/4cc Blender Starter Pack/scripts/addons/pes-fmdl}
OUT_ROOT=${3:-/home/z/Code/GameplayFootball/data/media/objects/stadiums}
HERE=$(cd "$(dirname "$0")" && pwd)
# Large by default. A mesh over the limit is dropped, and on a pack whose
# surroundings are the view that throws the view away: benuldys lost seventeen
# meshes at 1300 m, every one of which is recognised as a backdrop dome at 6000
# and goes to sky/sky.object, where the engine draws it out of the shadow map.
MAX_EXTENT=${MAX_EXTENT:-6000}

converted=0
skipped=0
for pack in "$PACKS"/*/; do
  pack=${pack%/}
  name=$(basename "$pack")
  # a nested directory of the same name is how some packs are unzipped
  [ -d "$pack/$name" ] && pack="$pack/$name"
  # "019 - somewhere" from a pack download, "st019" from an unpacked cpk
  slot=$(printf '%s' "$name" | grep -oE '^(st)?[0-9]{3}' | grep -oE '[0-9]{3}')
  if [ -z "$slot" ]; then
    echo "SKIP $name: no slot number in the directory name"
    skipped=$((skipped + 1)); continue
  fi
  # Whatever is still packed: a pack download leaves its staff, flags, TV screens
  # and effects as .fpk archives, and a stadium cpk unpacks the same way. The
  # converter only sees models, so unpack first - it is idempotent.
  bash "$HERE/extract_stadium_packs.sh" "$pack" >/dev/null 2>&1 || true

  # The centre scene is the stadium itself. It sits at any depth: a pack download
  # extracts it as #Win/st<slot>_fpk_extracted/center1.fmdl, a cpk keeps the game's
  # own tree, #Win/st<slot>_fpk_extracted/Assets/pes16/model/bg/st<slot>/scenes/.
  scene=$(find "$pack/#Win" -name "*center1.fmdl" 2>/dev/null | sort | head -1)
  [ -z "$scene" ] && scene=$(find "$pack/#Win" -name "*center*.fmdl" 2>/dev/null | sort | head -1)
  [ -z "$scene" ] && scene=$(find "$pack/#Win" -maxdepth 2 -name "*.fmdl" 2>/dev/null | head -1)
  if [ -z "$scene" ]; then
    echo "SKIP $name: no scene fmdl under #Win"
    skipped=$((skipped + 1)); continue
  fi
  out="$OUT_ROOT/pes_st$slot"
  echo "=== $name -> $out"
  # A reconversion is a fresh import: everything in here was written by this
  # pipeline, and leaving an older run's meshes and textures behind makes the
  # result depend on what was converted before it.
  rm -rf "$out"
  python3 "$HERE/stadium_to_gf.py" "$scene" "$out" \
    --fmdl-lib "$FMDL_LIB" \
    --textures "$pack/sourceimages/tga/#windx11" \
    --textures "$pack/turf3d/sourceimages/#windx11" \
    --name "pes_st$slot" --max-extent "$MAX_EXTENT" || { skipped=$((skipped + 1)); continue; }

  # No sky node unless the centre scene had a dome of its own. The lighting pack
  # is not a second chance at one: PES keeps a 16 m unit-scale dome in there for
  # the game to blow up around the camera at runtime (512 verts, 8 m tall), and
  # importing that puts a small lit ball over the pitch, while the 4cc packs ship
  # the same file empty - Namek's sky_dome.fmdl holds no meshes at all. A ground
  # with no dome of its own gets the engine's gradient, tinted by sky.txt if the
  # converter had a dome texture to sample.
  if [ "$(grep -c GEOMOBJECT "$out/sky/sky.ase" 2>/dev/null || echo 0)" -eq 0 ]; then
    rm -rf "$out/sky"
  fi

  # What PES paints on this pitch: the mowing bands, the worn goalmouths, the crest
  # mowed into it. The pack's pitch model carries them as decals, and the engine
  # already blends one image over the whole of its own pitch by alpha, so they are
  # rasterised into that (pitch_overlay.png). PES's line markings are left out -
  # the engine paints its own, and two sets never quite agree.
  python3 "$HERE/pitch_overlay.py" "$pack" --out "$out" \
    --fmdl-lib "$FMDL_LIB" \
    --textures "$pack/sourceimages/tga/#windx11" || true

  # Where this ground's sun is. Every pack carries a place, a date and a time in
  # light/#Win/.../*.fox2.xml, and how much fog its atmosphere wants; without this
  # the engine rolls dice for the sun in every kickoff.
  python3 "$HERE/stadium_lighting.py" "$pack" --out "$out" || true

  # The touchline staff - coaches, ball boys, stretcher bearers. A pack built from
  # a stadium cpk carries its own under staff/; a pack download usually leaves a
  # 48-byte stub there, because in PES those models live in the game's shared
  # bg/common/staff and every ground just points at them. Extract that once and
  # pass it as COMMON_STAFF and any pack that has none of its own borrows it.
  staffsrc=""
  if [ -n "$(find "$pack/staff" -name '*.fmdl' 2>/dev/null | head -1)" ]; then
    staffsrc="$pack/staff"
  elif [ -n "${COMMON_STAFF:-}" ] && [ -d "${COMMON_STAFF:-}" ]; then
    staffsrc="$COMMON_STAFF"
    echo "  staff: none in the pack, using $COMMON_STAFF"
  fi
  if [ -n "$staffsrc" ]; then
    # Skins come from the stadium's own sourceimages and from the shared packs:
    # a 4cc author dresses his staff in the stadium pack, PES's wear stock coach
    # kit that lives with the game's common models.
    python3 "$HERE/stadium_staff.py" "$staffsrc" "$out/staff" \
      --fmdl-lib "$FMDL_LIB" \
      --textures "$pack/sourceimages/tga/#windx11" \
      ${COMMON_STAFF:+--textures "$COMMON_STAFF"} \
      ${COMMON_TEXTURES:+--textures "$COMMON_TEXTURES"} \
      --asset-dir "pes_st$slot/staff" || true
  fi
  converted=$((converted + 1))
done
echo "converted $converted stadium(s), skipped $skipped"
