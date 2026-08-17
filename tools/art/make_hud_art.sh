#!/bin/bash
# Generates the player-indicator art. Our own drawing, not extracted from
# anything: a dark rounded plate, a stamina bar, the attack/defence box and its
# band, and one dial per philosophy.
#
# Shapes and proportions follow the PES indicator measured in
# docs/VGL26_REFERENCE.md, but every pixel here is drawn from scratch, so the
# repository ships no PES artwork.
set -e
OUT=/home/z/Code/GameplayFootball/data/media/menu
mkdir -p "$OUT"

# The plate: dark navy, translucent, rounded, with a lighter top edge the way a
# broadcast lower third catches light.
magick -size 512x96 xc:none \
  -fill "rgba(14,42,66,0.86)" -draw "roundrectangle 0,0 511,95 44,44" \
  -fill "rgba(120,190,230,0.55)" -draw "roundrectangle 0,0 511,4 3,3" \
  -depth 8 PNG32:"$OUT/hud_plate.png"

# The stamina bar along the plate's top edge, and the track it runs in.
# PNG32 throughout: a solid colour written without an alpha channel comes back as
# greyscale or plain RGB, and the engine drew those as multicoloured noise.
magick -size 16x8 xc:"rgb(74,222,66)" -alpha set PNG32:"$OUT/hud_stamina.png"
magick -size 16x8 xc:"rgba(255,255,255,0.18)" -alpha set PNG32:"$OUT/hud_stamina_track.png"

# The attack/defence box, and the white band that marks the rung.
magick -size 28x72 xc:none -fill "rgba(10,12,16,0.9)" -draw "roundrectangle 0,0 27,71 4,4" \
  PNG32:"$OUT/hud_level_box.png"
magick -size 28x12 xc:"rgb(245,245,245)" -alpha set PNG32:"$OUT/hud_level_band.png"

# One dial per philosophy: a circle split between a warm attacking tone and a
# cool defending one, in the proportion HudIndicators::PhilosophyDialSplit gives.
# Order matches TeamPhilosophy::e_Philosophy.
make_dial() {
  local index=$1 split=$2
  local size=64
  local edge
  edge=$(python3 -c "print(int(round($split * $size)))")
  magick -size ${size}x${size} xc:"rgb(90,200,235)" \
    -fill "rgb(235,92,150)" -draw "rectangle 0,0 $((edge - 1)),$((size - 1))" \
    \( -size ${size}x${size} xc:black -fill white \
       -draw "circle $((size / 2)),$((size / 2)) $((size / 2)),1" \) \
    -alpha off -compose copy_opacity -composite \
    PNG32:"$OUT/hud_dial_$index.png"
}
make_dial 0 0.5   # Balanced
make_dial 1 0.75  # Gegenpressing
make_dial 2 0.6   # TikiTaka
make_dial 3 0.3   # ParkTheBus

ls -la "$OUT"/hud_*.png
