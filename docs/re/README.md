# PES 2021 reverse-engineering reports

How PES 2021 does things, documented precisely enough to reimplement them
1:1: file formats, runtime assembly, selection logic. Every claim is tagged
**OBS** (observed, and how) or **INF** (inferred, with confidence). Killed
hypotheses are recorded with what killed them. Where the implementation later
proved a claim wrong, the report carries the correction and says so.

Sources: static analysis of the game executable, reads of a running game's
memory (never written), and the user's own install through `src/pes`.
Verbatim PES values appear here as evidence (owner decision, AGENTS.md); code
never takes a value from these reports, it reads it from the install at the
address given.

|Report|Covers|
|---|---|
|`pes-kit-assembly.md`|kit configs (`UniformParameter.bin`, realUni), part selection, kit texture binding, the 4cc composition, live draw lists, the EDIT save cipher and appearance records, boots/gloves, skin tone|
|`pes-re-shaders.md`|shader packs, the deferred G-buffer and lighting model, per-shader texture roles and formulas, a Godot recipe|
|`pes-re-ftex.md`|`.ftex` arrays, cubes, volumes, chunk rules, a census of all 65,339 textures|
|`pes-re-animselect.md`|motion ids, 30 fps clips, 1/16384 root scale, situation tables, locomotion graph, blends, the hand rig|
|`pes-re-physique.md`|height/weight/sliders to per-bone scale, global scale and stride|
|`pes-re-markings.md`|numbers, names, captain armband, badges, GK and referee kits|
|`pes-re-collision.md`|`.frig` format (incl. the hand unit map), ball/player capsules, contact data|
|`pes-re-face.md`|real-face and edit-face heads, `face_diff.bin`|
|`pes-re-camera-target.md`|Dynamic Wide camera internals: focus and its lead, target.y lift, the k spring, clamp blend, the zoom/height sliders, mode ids|
|`pes-re-stadium-camera.md`|nested stadium cpks, pitch dimensions, crowd, the in-play camera|
|`pes-re-lighting.md`|light rigs, sun, sky, fog, weather, the post chain|
|`pes-re-cutscenes.md`|demo tables, the `.fdc` cut format, selection, replays|
|`pes-re-matchlogic.md`|player and team records, tuning files, ball parameters, formations, AI map|
|`pes-re-officials-ball.md`|referees and linesmen, ball selection, the ball's flight model|
|`pes-re-uniform-params.md`|uniform shader parameters the engine writes at run time: Repetition and pattern, number/name scales|
|`pes-re-cloth-nets.md`|shirt/pants cloth (`.fclo`, cloth skeletons), goal nets, corner flags, other physics props|
|`pes-re-fox2-format.md`|the binary fox2 (FoxDataFile) layout: header, string table, entity and property blocks, containers, value types|
|`pes-re-player-ai.md`|ball-carrier think units, off-ball runs, team plans and pressing, keepers, attribute/style inputs, AI tables|
|`pes-re-sim-motion-mapping.md`|GF simulation states to PES motion selection (partial: agent stopped)|

Paths in the reports are placeholders: `$PES21_ROOT` is your install,
`<scratch>` the analyst's scratch folder (its scripts are not in the repo),
`<repo>` this repository. Refresh from the analysts' report folder with
`tools/dev/archive_re.sh REPORT_DIR`.
