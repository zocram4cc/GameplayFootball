# PES 2021 reverse-engineering reports

How PES 2021 does things, documented precisely enough to reimplement them
1:1: file formats, runtime assembly, selection logic. Every claim is tagged
**OBS** (observed, and how) or **INF** (inferred, with confidence). Killed
hypotheses are recorded with what killed them.

Sources: static analysis of the game executable, reads of a running game's
memory (never written), and the user's own install through `src/pes`. No
PES bytes are reproduced here; layouts, offsets and individual field values
only.

|Report|Covers|
|---|---|
|`pes-kit-assembly.md`|kit configs (`UniformParameter.bin`, realUni), part selection, kit texture binding, 4cc composition|
|`pes-re-*.md`|one report per subsystem (cutscenes, match logic, animation selection, shaders, stadium/camera, faces)|

Paths in the reports are placeholders: `$PES21_ROOT` is your install,
`<scratch>` the analyst's scratch folder (its scripts are not in the repo),
`<repo>` this repository. Refresh from the analysts' report folder with
`tools/dev/archive_re.sh REPORT_DIR`.
