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
|`pes-ai-map.md`|**the AI index**: every PES team and player AI system, its RE status, the report that holds it and the GF code it replaces; the documentation standard a port needs|
|`pes-re-protection.md`|the protection (Denuvo: moved plain code, ~99 mutated bodies, ~183 VM-entered Denuvo routines); live code equals disk; the full-image Ghidra project PESLive and the resolve/dec/annot/emu recipe; keeper deflect, referee set-piece positions and hidden constants unblocked|
|`pes-re-fox-registry.md`|there is no Fox method registry: the dispatch records are MSVC RTTI and every vtable is static (13,816 resolved); the action dispatch chain, the corner-kick state machine, the sequencer holds, and why post/net response is not virtual|
|`pes-re-tooling-ghidra.md`|the RE toolchain: GhidraMCP 1.4 on Ghidra 12.1.2 (two fixes), the headless postScript path, and how agents call both|
|`pes-kit-assembly.md`|kit configs (`UniformParameter.bin`, realUni), part selection, kit texture binding, the 4cc composition, live draw lists, the EDIT save cipher and appearance records, boots/gloves, skin tone|
|`pes-re-shaders.md`|shader packs, the deferred G-buffer and lighting model, per-shader texture roles and formulas, a Godot recipe|
|`pes-re-ftex.md`|`.ftex` arrays, cubes, volumes, chunk rules, a census of all 65,339 textures|
|`pes-re-anim-blend.md`|animation playback: linear crossfades with caller-set durations, the two-layer stack (base, officials-only upper), mask types 4-6, the startTime formula (no RNG), playbackSpeed per clip family, root motion with the AdjustTrans correction|
|`pes-re-locomotion.md`|player movement: no speed formula - displacement is the clip root track times AnimMovePlugin scale and playbackSpeed; speed classes, member root speeds, turn buckets, the plugin scale clamp; playbackSpeed driver and the Speed coefficient open|
|`pes-re-animselect.md`|motion ids, 30 fps clips, 1/16384 root scale, situation tables, locomotion graph, blends, the hand rig|
|`pes-re-physique-helpers.md`|the physique helpers: slider decode and normalisers, the per-bone scale tables, the global scale and how it reaches the skeleton and animation|
|`pes-re-physique.md`|height/weight/sliders to per-bone scale, global scale and stride|
|`pes-re-marking-pixels.md`|texel-exact marking rasterisation: digit cells, name layout, the leg-number transform, armband and badges|
|`pes-re-markings.md`|numbers, names, captain armband, badges, GK and referee kits|
|`pes-re-collision.md`|`.frig` format (incl. the hand unit map), ball/player capsules, contact data|
|`pes-re-face-rig.md`|the face rig (face_anim_skel.ask and its repair), the 181 face clips and their channels, and the edit-face composition|
|`pes-re-face.md`|real-face and edit-face heads, `face_diff.bin`|
|`pes-re-camera-target.md`|Dynamic Wide camera internals: focus and its lead, target.y lift, the k spring, clamp blend, the zoom/height sliders, mode ids|
|`pes-re-pitch-grass.md`|the pitch, line and turf shaders as port-ready formulas, verified numerically against the compiled DXBC|
|`pes-re-stadium-camera.md`|nested stadium cpks, pitch dimensions, crowd, the in-play camera|
|`pes-re-effects-particles.md`|the Tpp Effect system: the runtime path, the particle update rule, weather, the post chain, and the negative result on in-play emitters|
|`pes-re-audio.md`|the audio system (in-exe CRIWARE ADX2), the match sound inventory, the trigger-to-cue condition trees, and the crowd|
|`pes-re-lighting.md`|light rigs, sun, sky, fog, weather, the post chain|
|`pes-re-hud-ui.md`|the match HUD as a Flash/AS3 system: every overlay element with its region, trigger and data, plus the layout and scaling rules|
|`pes-re-duels-fouls.md`|duels (no stat roll; contact frames and capsules decide, abilities gate), the Judge foul points and 40/80/110 thresholds to card levels, the penalty-area rules, advantage (0.8 s hold, 4 s window), dives, offside involvement; the offside line evaluation still open|
|`pes-re-offside.md`|offside detection: writer `FUN_140a7b7a0` per ball event, line = min(halfway, second-rearmost defender's rear surface, ball), body-point margins, 0.15/0.30 m tolerance, categories 0-3 (3 = onside near-level); live-checked 2,498/0|
|`pes-re-defensive-shape.md`|open-play defensive line height (ball-depth ramp, sliders, ≥10 m behind ball), team length, width/lateral shift, the action-state priority gate, MATCH_UP/cover pressing, marking order and hand-off; live-checked; corrects team-plans §2.1|
|`pes-re-defender-decisions.md`|the engaging defender: a 17-entry first-true reaction table each frame (receive, tackle, slide, keep, dodges, block, ...), standing-tackle and slide gates with live JSON constants, the block radii, `ai+0x134` = human-controlled challenge (never set for CPU), no RNG|
|`pes-re-throwin-choreography.md`|throw-ins: the seamless pickup path vs the out-of-play scene path, restart point, taker rules, the throw action phase machines and release frames, the launch band, AI timing, team positioning; no foul throws; throwin.o table corrected|
|`pes-re-setpiece-choreography.md`|kick-off, corner, free kick and wall, goal kick, penalty: the post-wipe step program, the 20-tick teleport placement, kicker selection, positions (positionCK tables, wall at 9.15 m), holds and kick waits, validated on ten live restarts|
|`pes-re-setpiece-kicker.md`|set-piece taker selection (corners/penalties = sheet taker, FK waterfall), preparation, run-up/strike timing, aim rules (corners 26.8 m/s, FK/goal kick 26.1–26.2 m/s, penalties 1 of 7 fixed points), wall 9.15 m and run timing|
|`pes-re-foul-choreography.md`|fouls, cards and offside on screen: scene choice (FUN_14047f170), table_foul.bin fully decoded, the stoppage step list and holds, per-player reaction roles, referee gestures, the linesman flag; slot-1 remap and offside detection open|
|`pes-re-cutscenes.md`|demo tables, the `.fdc` cut format, selection, replays|
|`pes-re-camera-live.md`|the live in-play camera: Dynamic Wide mode 5, slider values and offsets, camera distance (52.7 m) and vFOV (23.68°) for a known target, the PosY model check|
|`pes-re-stamina-drain.md`|kick-off tank from the Stamina ability (x1.1/1.2/1.3/1.4/95+5, 400/second + kind/condition terms), the per-tick drain (speed classes, +19/-5/-9), the breath reserve (+5/-3, capped at max(pct,20)*1920/100), the half-time +9000/+3600/+7200, the A5 fatigue effect (0.8+0.2*clamp) and the injury roll; live-verified 97.4% of single-tick steps|
|`pes-re-stamina-live.md`|live match structures (match context, roster and player blocks) and the stamina field search, partial; superseded by pes-re-stamina-drain for the drain/recovery formulas|
|`pes-re-injury-fatigue.md`|stamina tiers, the fatigue and injury clip families, the injury state machine and the constant archive layout that carries the values|
|`pes-re-setpiece-referee.md`|set-piece state machines and constants, the placement tables, the officials, and where every value lives|
|`pes-re-matchlogic.md`|player and team records, tuning files, ball parameters, formations, AI map|
|`pes-re-rng.md`|PES's RNG: math::Random/GameRandom layout, the 31-bit LCG and 15-bit slot-3 LCG literals, range/normal draws, the kick-off seed commit in the MatchControl ctor (clock-derived t, t+1, t+2), sub-generators, a consumer census, live step verification, the seed-pinning procedure|
|`pes-re-kick-launch.md`|kick launch: the launch writer FUN_1407324b0, ability/foot/ball-height terms, the logistic error scale and exponential scatter, the per-action spin tilt, the per-player LCG; partial (per-family ability index, live validation open)|
|`pes-re-ball-contact-2.md`|the bounce: a min() between a speed-derived impulse and vy gives reflection or restitution, which reproduces the live 0.37-2.26 band; the mode-byte writer (dry normal pitch = mode 2); corrections to part 1|
|`pes-re-ball-collision.md`|ball vs posts, crossbar, boards, stands, flags and player bodies: `FUN_14086d6f0` via PostCollisionListener; restitution/retention per object and bone, recent-hit softening, speed caps 70/50 km/h, analytic goal-frame cylinders, and the GoalnetFilm net model; nine live contacts reproduced|
|`pes-re-ball-contact.md`|ball ground contact, the tangential and spin response, and rolling, every constant mapped to a named ball.o field|
|`pes-re-ball-constants.md`|where PES ball physics constants live: ball.o in constant_match.bin, the named field map, and the runtime-written spin scale|
|`pes-re-officials-ball.md`|referees and linesmen, ball selection, the ball's flight model|
|`pes-re-leftovers.md`|small closures: the match-ball query (dead code, no static root), late-card window = 16 game-minutes, `MI+0x158c == 1` = foul without a card, `injury.o` fields (`constant::Injury`, id 0x0b; reader open), officials' ids are setup reads (pick rule open)|
|`pes-re-uniform-params.md`|uniform shader parameters the engine writes at run time: Repetition and pattern, number/name scales|
|`pes-re-cloth-nets.md`|shirt/pants cloth (`.fclo`, cloth skeletons), goal nets, corner flags, other physics props|
|`pes-re-fox2-stadium.md`|stadium assembly: Stadium.bin, the scene/pitch/goal/net/corner-flag placements, goal and corner-flag transforms|
|`pes-re-fox2-format.md`|the binary fox2 (FoxDataFile) layout: header, string table, entity and property blocks, containers, value types|
|`pes-re-goalkeeper.md`|goalkeeper: base position and sweeper rush, the shot-response timing window and Reflexes/Reach weight, dive urgency, catch/punch/block gates, crosses, throw distribution, penalty phases; deflect side and distribution choice in packed code|
|`pes-re-keeper-port.md`|the keeper's deflect point (2 m at the requested angle inside 120 degrees of his heading, else 1 m at heading -/+60), the after-catch carry and step, the throw/kick request switch; ported in 703ad29|
|`pes-re-carrier-ai.md`|the ball carrier: plan choice, first-surviving-unit adoption with vetoes (no score competition), the 5-frame think cadence, difficulty reaction delays, shoot zone and shot choice, pass receivers and the openness formula (live-validated), pass choosers, pressure and clearance, the 72-direction dribble search, the skill/style bit order|
|`pes-re-carrier-port.md`|what of the carrier AI is ported and how: think cadence, shoot zone, openness ladder, PassForward tiers, plan roll; constants table with VAs; the not-ported list; open shoot-zone sample mismatch|
|`pes-re-team-plans.md`|team AI: the team-brain class inventory, formation slot to target position, advanced-instruction enums, off-ball run selector gates, the press trigger/target/count, ZoneDefenceManager marking, pass/shoot/dribble scoring skeletons|
|`pes-re-player-ai.md`|ball-carrier think units, off-ball runs, team plans and pressing, keepers, attribute/style inputs, AI tables|
|`pes-re-tactics-clock.md`|the match clock as a u32 percentage of minutes x 1920, the stoppage formula, the period state machine, and how GF differs|
|`pes-re-sim-motion-mapping.md`|GF simulation states to PES motion selection: the action layer, kick and trap families, keepers, set pieces, officials|

Paths in the reports are placeholders: `$PES21_ROOT` is your install,
`<scratch>` the analyst's scratch folder (its scripts are not in the repo),
`<repo>` this repository. Refresh from the analysts' report folder with
`tools/dev/archive_re.sh REPORT_DIR`.
