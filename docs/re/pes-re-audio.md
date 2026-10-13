# PES 2021 — match audio and crowd (RE report)

Scope: the sound system behind an AI-vs-AI match, the match sound inventory with its files and
triggers, where the trigger→sound mapping lives at run time, and the crowd model/animation system.
Read-only RE of the PES 2021 install VFS and of the exe image (`<image>/pesmem.bin`, the 64 MB live
image at base 0x140000000, plus the on-disk `PES2021.exe`). Ghidra MCP and headless Ghidra were both
unavailable this session (headless fails with "Failed to get OSGi bundle containing script"); every
code datum below is from raw disassembly (`objdump -D -b binary -m i386:x86-64 --adjust-vma=<VA>` on
bytes extracted from the image) or from data reads. The game is **not running** (`pgrep -f PES2021.exe`
empty); nothing was launched.

Companion reports: `pes-re-stadium-camera.md` §3 (crowd models/areas), `pes-re-sim-motion-mapping.md`
(the in-play events), `pes-re-matchlogic.md` (match state). Scratch: `<scratch>/AudioRE/`.

Tags: **OBS** = observed, with the method. **INF** = inferred, with confidence.
File-offset mapping for the image: `.trace`/`.rdata` offset = VA − 0x140000A00, `.data` offset =
VA − 0x140000E00 (`objdump -h PES2021.exe`; `.trace` VA 0x140001000 ↔ off 0x600, `.rdata` VA
0x14252f000 ↔ off 0x252e600, `.data` VA 0x14339b000 ↔ off 0x339a200).

---

## 0. Status per priority item

| # | Item | Status | Where |
|---|---|---|---|
| 1 | Audio architecture (engine layers, trigger path, resource format, mix/voice/priority) | **closed** — CRI ADX2 (statically linked) underneath a Fox `app@sound` AI layer; acb/awb resource model; mix = `PES2021.acf` (categories/buses/AISAC/ducking/selectors), all observed | §1, §2 |
| 2 | Match sound inventory (kicks by power × surface, frame/net, tackles/falls, whistle, referee, keeper, crowd, ambient, TTS/commentary, UI) | **closed** — every kind mapped to an acb and a cue-name family | §2, §4 |
| 3 | Where the data lives (VFS paths, exe path tables, WESYS constants) | **closed** — physical cpk identified, runtime `cpk_snd/…` paths and their exe VAs listed | §3 |
| 4 | Trigger→file mapping format | **closed for the data layer** — the five `*_config.bin` compiled trees decoded (section table, node/data section pair, shared-suffix string pool), plus `mob_anime_control.xml` cue→crowd-state map; the per-node semantic decoder (which condition binds which cue) is partial | §4 |
| 5 | Crowd system (model files, audiarea format, animation, reaction drivers) | **closed** — carried from the stadium report + the crowd-reaction trigger map extracted here | §5 |
| 6 | What an implementation can play, per kind | **closed** — source file + trigger + parameters per kind | §6 |
| — | Open items | listed with what was tried | §7 |

Everything an implementer needs to *produce* the sounds is OBS (paths, formats, cue names, decoders).
The residual unknown is only the fine semantics of individual condition-tree nodes (§7).

---

## 1. Audio architecture

### 1.1 Two layers: CRI ADX2 underneath, a Fox sound-AI layer on top

**OBS (imports/strings).** `PES2021.exe` has **no CRI DLL in its import table** (`objdump -p`: no
`criAtom*.dll`), but the image contains the CRIWARE ADX2 API as inline code and strings:
`criAtomEx_Finalize_WASAPI`, `criAtomExPlayer_Start` (image string scan, `<scratch>/AudioRE/exe_strings.txt`),
plus the ADX2 error-format strings `E2015051807:Stream awb` and
`W2010053100:An AWB file [%s.awb` at **0x142c4cbe8** / **0x142c51270**. So the Atom mixer/decoder is
**statically linked into the exe**, output via WASAPI. This is the "rigdio layer" the port notes
mention; there is no separate Fox audio engine — sound is ADX2 plus game-side glue.

**OBS (RTTI, `.data` scan for `@…@sound@@` descriptors).** The game-side layer is a namespace tree
of C++ classes, all visible in the live image (`<scratch>/AudioRE/snd_rtti.txt`):

```
sys@sound    AudioOutputSettings, IAudioOutputSettings, InitCallback@Interface,
             TitlePlaybackControl(Base), Random@util, XmlDocument@util,
             List@chant, Player@chant, Loader@BaseName@chant, Loader@BranchData@cheer,
             Loader@ListData@chant, Loader@PlayerName@edit, Loader@TeamName@edit,
             Loader@TeamDirectionName
app@sound    Match, Sequence, Listener@base, Command@base
  ai@app@sound        PitchSound, CrowdNoise, Chant, ChantCommand, Commentary,
                      PAannouncement, Sequence
  ai_selector@app@sound  Base, Interface, CommandBase, SysControlOneUnit,
                      SysControlTreeBase / …Continue / …Wait,
                      SysFunc / SysFuncArg / SysFuncs / SysParam / SysParamCondition /
                      SysParamData (each in a Binary and an Xml variant),
                      SysLoaderBinary / SysLoaderXml / SysLoaderInterface
  sound_3d@app@sound  CommandListenerPos, CommandListenerOrientation,
                      CommandListenerForcusPoint [sic], CommandListenerType,
                      CommandPlayerRunStatus
  core@app@sound      ExecMain, ExecListener
```

Reading (INF, high): `app@sound` is the match sound application. The five `ai@app@sound` classes are
the **sound behaviour controllers** — `PitchSound` (ball/player/pitch events), `CrowdNoise` (crowd
reaction and ambience), `Chant`, `PAannouncement` (stadium announcements), `Commentary`.
`ai_selector` is a generic condition-tree interpreter: `SysControlTree*` = tree states (base /
continue / wait), `SysParamCondition*` = the condition nodes, `SysFunc*` = the leaf actions,
`SysLoader{Binary,Xml}` = the two implementations of the same tree format. The `*Binary` classes
consume the shipped `*_config.bin` files (§4); the `*Xml` classes consume the authoring XMLs
(`se_list.xml` etc., only present under the dev path — see §4.1). `sound_3d` adds the 3-D command
listeners (position, orientation, focus point, listener type, player-run status) that turn a play
command into a positioned ADX2 voice.

### 1.2 How a match event becomes a sound

**OBS (code).** The path observed is:

1. gameplay code posts a sound event into the `app@sound` layer. The event carries a *match-event
   selector*, not a file name: the compiled trees' own condition names include
   `CheckMatchEvent`, `RecordKind`, `CheckBallTouchRestartKind`, `CheckBallCollisionPart`,
   `ObjectTouchKind` with enum values `RESTART_READY`, `MATCH_STATUS`, `CORNER_KICK`, `CROSSBAR`,
   `TOUCH`, `Numeric` (OBS: inline strings in `common/sound/config/Se/se_config.bin`, §4).
2. `ExecMain`/`ExecListener` walk the matching `SysControlTree` (`SE_*` trees for pitch sounds,
   `SU_*` for supporter/crowd, `SA_*` for announcements, `BG_*/CH_*/CA_*` for chants, `SQ_ROOT` for
   sequences) — INF (high), from the tree names and the ai_selector class set.
3. A `SysFunc` leaf fires a CRI cue. For ball/frame sounds the exe also calls directly with a
   hard-coded cue name chosen by speed tier: **OBS** at `0x14048fdae` the code picks
   `SE_PL_BALLHIT_IRON_01` (string at **0x1425a2d68**), `_02` (0x1425a2d80) or `_03` (0x1425a2dd2)
   by comparing the ball speed against the floats at 0x142588210 / 0x14259be1c after scaling by
   the float at 0x14259ce38 (`mulss` then `comiss` cascade at 0x14048fd99–0x14048fe1b). So at least
   the frame-hit family is a **coded speed→variant picker**, independent of the trees.
4. The cue is played through a `sound_3d` command listener when the event has a world position
   (the `*_POS` cue family, §2.9) and modulated by AISAC graphs (§1.4).

### 1.3 What a sound resource is (OBS, from the archives)

CRI ADX2 resource pair, both CRIWARE `@UTF` table containers:

- **`.acb`** = cue container. Tables present in every match acb (OBS, `utfparse.py` in scratch):
  `CueTable`, `CueNameTable`, `SynthTable`, `WaveformTable`, `AisacTable`, `GraphTable`,
  `SequenceTable`, `TrackTable`, `TrackCommandTable`, `TrackEventTable`, `SeqCommandTable`,
  `SynthCommandTable`, `AisacControlNameTable`, `StringValueTable`, `AcfReferenceTable`,
  `WaveformExtensionDataTable`, and for streaming acbs `StreamAwbHash` + `StreamAwbAfs2Header`.
  A cue row = `{CueId, ReferenceType=3 (synth), ReferenceIndex → SynthTable row,
  ReferenceItems{referenceType, waveformIndex} → WaveformTable row}` (OBS, decoded with
  `<scratch>/AudioRE/dumpacb2.py`; example: `SE_PL_BALLHIT_INSIDE_01` = cue id 14, synth item 1,
  MemoryAwbId 48).
- **`.awb`** = waveform archive, either a sibling file (`common/sound/match/awb/<name>.awb`, CRI
  `AFS2` header: `n` entries, per-entry id + u32 offsets) or **embedded inside the acb**: OBS,
  `SE_Movement.acb` (437,152 B) carries an `AFS2` block at file offset **0x91a0** with **187**
  waveforms (offsize 4) — i.e. its waveforms are memory-resident inside the acb. Cheer acbs with a
  sibling `.awb` stream from it; several cheer acbs have **no** sibling awb and embed theirs the
  same way (`SE_Cheers_CM_Common.acb` 6.6 MB with 68 waveforms).
- Waveform encoding: `EncodeType=2` = **HCA** at 48 kHz (24 kHz for a few movement layers), mono or
  stereo; commentary acbs are 44.1/48 kHz mono. OBS via the census script
  (`<scratch>/AudioRE/census.py` output in `dec_test.txt`-style listing, all 51 acbs).

### 1.4 Mix, voice and priority parameters (OBS)

- **Buses.** Every match acb references `MasterOut` and `BUS1..BUS7` (`StringValueTable` /
  `AcfReferenceTable` string scans). The exe names the buses it drives, at
  **0x142963540–0x1429637b8**: `BUS3_PauseON`, `BUS3_PauseON_CZ`, `BUS3_CommentaryON`,
  `BUS5_EntranceON`, `BUS5_HighlightON`, `BUS6_CZ`, `BUS7_ReverbON`, `BUS2_MenuBGMMuteON`
  (each appears twice — the table holds two copies for the two sound mounts).
  Reading (INF, high): BUS2 = menu BGM, BUS3 = gameplay/commentary (+ a crowd-zone variant `_CZ`),
  BUS5 = entrance/highlight, BUS6 = crowd zone, BUS7 = reverb send.
- **AISAC.** Per-cue control graphs live in `AisacTable` + `GraphTable` (curve points as
  f32 control/destination pairs). Named controls (OBS, `AisacControlNameTable`):
  `AISAC_Footstep_Speed` (control id 1009) and `AISAC_Footstep_Distance` (1010) in `SE_Movement.acb`;
  `AISAC_ExciteControl_H` / `AISAC_ExciteControl_A` (home/away crowd excitement) in
  `SE_Cheers_CM_Common.acb` — INF (high): this is the "crowd response level" input, set by the
  `CrowdNoise` controller.
- **User options** (OBS, exe strings at 0x142963dd0–0x142963e50): `Def_Sound_Sys_Cheers_Enable`,
  `Def_Sound_Sys_Cheers_TeamID`, `Def_Sound_Sys_Cheers_Scale` — crowd-sound mix toggles.
- **Voice priority / limits** live in the ACF (`ReactTable` ducking, `VoiceLimitGroup_0`,
  `RegistrationOption`), §1.5 — not in the acb.

### 1.5 The global mixer: `common/sound/menu/PES2021.acf` (OBS — the ADX2 project file)

21,344 B CRI `@UTF` container (VFS path `common/sound/menu/PES2021.acf`; read with
`<scratch>/AudioRE` utfparse). It is the **global mix/voice/priority ruleset**; every acb's cues
route through its categories and buses:

| table | rows | content (OBS) |
|---|---|---|
| `CategoryNameTable` | 100 | the routing categories: `MatchReact`, `MenuReact`, `CK`, `ChantReact_Receive`, `ChantReact_Trigger`, `Chant_AwayCall`, `CheersVol_Base`, `CheersVol_Excite`, `CheersVol_Goal`, `CheersVol_Reaction`, `CheersVol_ReactionBig`, `CommentaryVol_<18 languages>`, `DefVol_Chant{,_Base}`, `DefVol_Cheers`, `DefVol_Commentary`, `DefVol_HUD`, `DefVol_Ingame_SE`, `DefVol_Menu_Music`, `DefVol_Menu_SFX`, `DefVol_Match_Music` … |
| `BusTable` + `BusNameTable` | 72 / 10 | 8 named buses `BUS1..BUS7` + MasterOut, each with `Volume`, `Pan3dVolume/Angle/Distance`, FX and bus-link counts (BUS0 volume 0.9; BUS2 0.7; BUS6/BUS7 start at 0.0 = muted until their snapshot fires) |
| `DspFxTable` | 72 | insert FX chain: `CRIWARE/AmplitudeAnalyzer`, `Surrounder`, `Compressor`, `MultiTapDelay`, `Bandpass` ×2, `I3DL2Reverb` (the BUS7 reverb send), `3BandsEQ` |
| `ReactTable` | 35 | **the ducking/priority matrix**: `{Src category → Dest category, DecrementTime, IncrementTime, Level (0.3…0.6), HoldType/Time, curve types}` — e.g. src 18→dest 1 at Level 0.6 with 4500 ms up / 800 ms down; src 34→1 at 0.3. INF (high): a louder reaction ducks other categories by the given level with those ramp times |
| `VoiceLimitGroupTable` | 1 | `VoiceLimitGroup_0`, `MaxNumbers = 1` (one voice per limit group) |
| `AisacControlNameTable` / `AisacControlIdTable` | 16 | `AISAC_ExciteControl_H` (1000), `AISAC_ExciteControl_A` (1001), `AisacControl02..06` (1002–1006), `AISAC_Excite_Home` (1007), `AISAC_Excite_Away` (1008), `AISAC_Footstep_Speed` (1009), `AISAC_Footstep_Distance` (1010) |
| `GlobalAisacTable` + `GraphTable` | 3 + 4 | global AISAC curves `Chant_VolControl_H_Ex`, `Chant_VolControl_A_Ex` (graph f32 control→destination point lists) |
| `SelectorTable` | 9 | the runtime variant switches: `Match_Cheer` → labels `Rival, Final, Normal, Neutral`; `Match_Chant` → `Final, Final_South_America, Normal`; `Stadium` → `Stadium_1, _7, _11, _14, _16, _22, _23, _24, _27, _28, _30, _31, _38, _39, _29, _33…_42` (22 stadiums); `Weather` → `Fine, Rain`; `Country` → 8 country ids; `Himno` → `Long_Clasico, Long_Normal, Short_Normal`; `MyClub_BGM` → 18 labels |
| `DspSettingSnapshotTable` | 8 | the named mixer snapshots the exe applies: `BUS2_MenuBGMMuteON`, `BUS3_PauseON`, `BUS3_PauseON_CZ`, `BUS5_EntranceON`, `BUS5_HighlightON`, `BUS6_CZ`, `BUS7_ReverbON` (+ default) — matching the exe strings of §1.4 one-for-one |
| `GameVariableTable` | 1 | `Default` = 0.0 |
| `AutoModulationTable` | 1 | type 1, trigger 1, 5000 ms, key 10 |
| `RegistrationOption` | 1 | `HcamxMaxVoices 16`, output 2 ch @ 48 kHz, `DefaultDspBusSettingName = DspBusSetting_01` |

Implementation consequence: the mixer is a small fixed graph — 7 named buses, ~100 categories,
35 ducking rules, 2 global AISAC curves (home/away excitement → chant/crowd volume), and 9
selectors (stadium/weather/cheer-mode/chant-mode/anthem-length/country). Reproduce those tables and
the cue routing follows the acb `AcfReferenceTable`/`StringValueTable` references (`MasterOut`,
`BUS6` observed in the match acbs).

`common/sound/menu/playlist/Playlist.xml` (6,046 B) is the menu-BGM playlist (OBS listing).

---

## 2. Match sound inventory

All paths are VFS paths (`common/sound/…`, readable with `pesfmt_dump cat`). Cue names are verbatim
from the acbs' `CueNameTable` (dumped to `<scratch>/AudioRE/all_cues.txt`). Counts from
`census.py`.

### 2.1 Ball contact by surface and power — `common/sound/match/acb/SE_Movement.acb` (126 cues, 187 waveforms, embedded AWB)

The kick family is a **surface × 5-tier** matrix (`_01`…`_05`, INF high: tier = contact speed/power,
the frame-hit picker in §1.2 confirms the tier mechanism):

| surface | cues |
|---|---|
| inside foot | `SE_PL_BALLHIT_INSIDE_01..05` |
| instep | `SE_PL_BALLHIT_INSTEP_01..05` |
| in-front (laces/toe area) | `SE_PL_BALLHIT_INFRONT_01..05` |
| outside | `SE_PL_BALLHIT_OUTSIDE_01..05` |
| head | `SE_PL_BALLHIT_HEAD_01..05` |
| generic kick | `SE_PL_BALLHIT_GENE_01..05`, `ball_hit_shoot`, `SE_PL_BALLHIT_CK_01..05` (corner-kick; OBS: the CK names head the se_config cue pool) |
| shoot-specific | `SE_PL_BALLHIT_SHOOT_04/05` (OBS from the se_config pool strings) |

Bounce: `SE_PL_BALLHIT_BOUND_01..05`. Soft touches: `region_ballhit_drible_sml`,
`region_ballhit_drible_mid`, `region_ballhit_gene_01..03`.

### 2.2 Ball against frame and net — same acb

| target | cues |
|---|---|
| goal post | `SE_PL_BALLHIT_GOALPOST_01..05` |
| goal bar | `SE_PL_BALLHIT_GOALBAR_01..05` |
| whole frame | `SE_PL_BALLHIT_FRAME_01..05`, `SE_PL_BALLHIT_IRON_01..05` (the `IRON_*` names are in the **exe**, the frame picker of §1.2) |
| net | `SE_PL_BALLHIT_GOALNET_01..05` |
| ad board | `SE_PL_BALLHIT_BOARD_01..05` |
| big-screen panel | `SE_PL_BALLHIT_PANEL_01..05` |
| corner pylon | `SE_PL_BALLHIT_PYLON_01..05` |

### 2.3 Tackles, jostles, falls, footsteps — same acb

`SE_PL_JOSTLE_01/02`, `region_jostle_A`, `SE_PL_TACKLE_SLIDING_01`, `SE_PL_BODYFALL_01`,
`SE_PL_FOOT_FAR_01`, footsteps `footstep_run_para_A/B/C` + `footstep_cloth_para_A` (AISAC-distance/
speed parametric steps), `region_foot_setA/B/C`, breath `region_breath_A/B`.
The exe also carries the positional event names `SE_JOSTLE_POS`, `SE_SLIDING_POS`,
`SE_FALL_LANDING_POS`, `SE_BALL_POS`, `SE_BALL_BOUND_POS`, `SE_REFEREE_POS`, `SE_TARGET_POS`
(OBS: image strings at 0x1429512b0–0x142951320).

### 2.4 Whistle and referee — `common/sound/match/acb/referee/SE_Referee_{A..F}.acb` (15 cues each)

Identical cue set in all six packs (OBS dump of A; INF high: the letter is the referee's whistle
voice pack, chosen per referee):

`SE_PL_WHISTLE_LINEOUT`, `SE_PL_WHISTLE_FOUL_01..03`, `SE_PL_WHISTLE_GOAL`,
`SE_PL_WHISTLE_RESTART`, `SE_PL_WHISTLE_KICKOFF`, `SE_PL_WHISTLE_HALFTIME`,
`SE_PL_WHISTLE_TIMEUP`, `SE_PL_WHISTLE_OFFSIDE`, `Whistle_long_region`, `Whistle_short_region`
(waveform ids 0–4, 57.6 k–112.7 k samples @48 kHz mono).

### 2.5 Keeper actions — same SE_Movement acb

`SE_PL_GK_CATCH_01..05`, `SE_PL_GK_PUNCH_01..05`.

### 2.6 Crowd reaction — `common/sound/match/acb/cheers/SE_Cheers_CM_*.acb`

The crowd cues are split across **34 themed acbs** (OBS listing):

| acb | family (cue-name pattern) |
|---|---|
| `SE_Cheers_CM_Goal.acb` (41 cues) | `SE_H_GL_01/02`, `SE_A_GL_01/02`, `SE_H_GL_OWN`, `SE_A_GL_OWN`, `SE_H_GL_OFFSIDE_01`, `SE_A_GL_OFFSIDE_01`, `SE_H_GL_PLAYERCALL*`, `SE_HLT_H_GL_01` (highlight), `region_goalatk*` (main/sub/_away/_PK/_short_release) |
| `SE_Cheers_CM_Goal_OHH/YEAH.acb` | same goal family, alternate vox |
| `SE_Cheers_CM_Common.acb` (275) | line-outs `SE_H_LO_SIDELINE_01..03/_GENE/_MISS`, restarts `SE_H_KICKOFF_RESTART/_ZOOM`, and the AISAC `ExciteControl_H/A` |
| `SE_Cheers_CM_Anger.acb` (58) | in-play contact `SE_H_IP_CONTACT_00/01/02/_DIVE/_PA` |
| `SE_Cheers_CM_Surprise(.WOO_A).acb` | chances: `SE_H_CHANCE_FAILED_LV2/LV3_01/02`, `SE_H_SF_FAILED_FAR/NEAR_*` (shot failed), `SE_H_SF_HITBAR_01/02`, `SE_H_SF_SAVED_CATCH_*`, `SE_H_SF_SAVED_PUNCH_01..06`, `SE_H_TURNOVER_01/02` |
| `SE_Cheers_CM_Special_{GK,.._A..D}.acb` | set pieces: `SE_H_ST_GK_01/02/_KICK_01`, `SE_H_ST_FK_01`, `SE_H_ST_PK_01`, `SE_H_PKMT_*`, `SE_H_FL_CARD_YELLOW/RED/NOCARD`, `SE_H_FL_JUDGE_GETFOUL_01/_GENE`, `SE_H_FL_JUDGE_NOFOUL_01` |
| `SE_Cheers_CZ_Base/Common.acb` | crowd-zone ambience: `SE_AMBIENT_NORM`, `SE_WALLA_01`, `region_norm*`, `region_walla`, `SE_H_AMBIENT_EXCITE`, `SE_A_AMBIENT_EXCITE`, `region_excite_home_lp/_away_lp`, `region_boowind_sml_ger`, `region_ahh_mid_A`, `region_yeh_big_A_4ch` |

Home/away split is **in the cue name**: prefix `SE_H_` = home crowd, `SE_A_` = away crowd (OBS: the
complete `SE_H_*`/`SE_A_*` list is in `common/mob/control/mob_anime_control.xml`, §4.3 — 176 names,
each mapped to a crowd animation state per stand group).

### 2.7 Chants — `common/sound/match/acb/chant/CHANT.acb` (341 cues) + `CHANT_Base.acb` (54)

Cue names are `SP_R<region>_<NNN><variant>`, e.g. `SP_R0108_005A`, `SP_R4137_001A` (OBS dump).
INF (high): `R<NNNN>` is the region/team code; the per-team chant is selected through
`common/sound/match/ChantList.bin` / `ChantBaseName.bin` (§3.3). Base pack = 4-channel generic chants.

### 2.8 Announcements and commentary

- Stadium announcements: `common/sound/match/acb/announce/SA_{ENG,SPA,ITA,POR,FRA,BRA,MEX}.acb`
  (1.1 k–2.5 k cues each), paired with same-name `.awb`. Names like `SA_2HF_A_01..05`,
  `SA_2HF_B_01..05`, `SA_2HF_C_01..05` (OBS: `SA_ENG` dump) — INF: `<situation>_<voice-pack>_<NN>`.
- Commentary (the "TTS-like" layer): `eng/sound/acb/{00_TEAM,10_PLAYER,20_MAIN,30_MAIN,40_MAIN,50_MAIN}.acb`
  (4,660 / 24,011 / 6,280 / 3,231 / 240 cues, streaming via `StreamAwbHash`), language-packed per
  install language (`dt20_<lang>_x64.cpk`; this install has `eng` and `use`). Cue names in
  `00_TEAM`/`10_PLAYER` are the team/player-name utterances; `20_MAIN`+ are the line corpus (OBS
  census + name-table row counts).
- Player voice: `common/sound/match/acb/playervoice/PV[_<LANG>].acb` — `SE_PV_DAMAGE_01`,
  `player_gaya_long/short`, `pv_shout_gene`, `SE_PV_OF_CROSS`, `SE_PV_SF_FAILED_*` … (OBS).

### 2.9 UI / HUD

`common/sound/match/acb/hud/SE_HUD.acb`: `SE_HUD_BIG_IN/OUT`, `SE_HUD_MID_IN/OUT`,
`SE_HUD_SML_IN/OUT`, `SE_WIPE_IN_01`. Menu sounds: `common/sound/menu/acb/{BGM,BGM_P2P,MA_Scenario,SE_Mode,SE_System}.acb`
(`BGM.acb` 40 cues incl. the `BGM0_LIC_MENU_*` and `BGM0_ORG_*` families seen as exe strings at
0x1429639f8–0x142963cd8). Match BGM: `common/sound/match/acb/BGM_Match{,_P2P}.acb`
(`BGM1_LIC_ENTER_*`, `BGM1_LIC_ANTH_T0108_01` (anthem per competition/team), `SA_BELL`,
`BGM1_TIMEUP_ETC_REQ` …).

### 2.10 Event control (sequences, no audio of their own)

`common/sound/match/acb/Event_Control.acb` — a **pure sequence** acb: `CueTable` 7 rows but
`SynthTable`/`WaveformTable` absent; it has `SequenceTable`/`TrackTable`/`TrackCommandTable`/
`TrackEventTable`/`SeqCommandTable`. Cues: `EC_GOAL_C0001`, `EC_GOAL_R0103`, `EC_GOAL_R0108`,
`EC_GOAL_R0119`, `EC_GOAL_R0120`, `EC_GOAL_R0172`, `EC_GOAL_Z0001` (OBS). INF (high): these are
goal-event sequences that stage the layered crowd reaction (boos → swell → goal roar), keyed by
region/stanza ids.

---

## 3. Where the data lives at run time

### 3.1 Physical archives (OBS)

| VFS path | physical carrier |
|---|---|
| `common/sound/match/acb/**`, `common/sound/match/awb/**`, `common/sound/match/*.bin`, `common/sound/config/**` | **`Data/dt44_all.cpk`** (74 entries — exactly the acb count of `common/sound/match/acb`) |
| `eng/sound/acb/**` (commentary) | `Data/dt20_<lang>_x64.cpk` (6 entries each; `dt20_eng_x64.cpk`, `dt20_use_x64.cpk` here) |
| crowd models/textures `Asset/model/bg/common/audi/**` | `Data/dt00_x64.cpk` (per the stadium report; re-verified here via `pesfmt_dump ls`) |

The exe's own path templates use a **`cpk_snd` mount alias**: `cpk_snd/common/sound/match/acb/…`
(OBS: image strings, table below). `cpk_snd` is the runtime mount that resolves to the sound cpk;
`/common/sound/match/awb/…` is the streaming awb path (note the leading `/`).

### 3.2 The exe's acb path table (OBS — image string region 0x1429634a0–0x142964460)

```
0x1429634a0  cpk_snd/common/sound/match/acb            0x142963508  /common/sound/match/awb
0x142963588  cpk_snd/common/sound/match/acb            0x1429635e0  /common/sound/match/awb
0x142963630  cpk_snd/common/sound/match/acb            0x142963670  /common/sound/match/awb
0x1429636c8  cpk_snd/common/sound/match/acb
0x142963738  cpk_snd/common/sound/match/acb            0x1429637c0  /sound/acb   0x1429637d8 /sound/awb
0x142963820  cpk_snd/common/sound/menu/acb             0x142963870  /common/sound/menu/awb
0x1429638d8  cpk_snd/common/sound/match/acb            0x142963930  /common/sound/match/awb
0x142963968  /sound/acb                               0x142963980  /sound/awb
0x142963998  DevelopData/sound/inGamePreview/Event_Control.acb
0x1429639d0  DevelopData/sound/inGamePreview/Event_Control.awb
0x142963a08  cpk_snd/common/sound/match/acb/Event_Control.acb
0x142963a40  /common/sound/match/awb/Event_Control.awb
0x142963aa8  cpk_snd/common/sound/menu/acb             0x142963af8  /common/sound/menu/awb
0x142963b60  cpk_snd/common/sound/menu/acb             0x142963c50  /common/sound/menu/awb
0x142963cb8  cpk_snd/common/sound/match/acb            0x142963d08  /common/sound/match/awb
0x142963d30  DevelopData/sound/inGamePreview/SE_Movement.acb
0x142963d60  cpk_snd/common/sound/match/acb/SE_Movement.acb
0x142963e00  cpk_snd/common/sound/match/acb
0x142963ea0  cpk_snd/common/sound/match/acb/cheers/SE_Cheers   0x142963f08  /common/sound/match/awb/cheers/SE_Cheers
0x142963f3c  CHANT   0x142963f48  CHANT_Base   0x142963f88  cpk_snd/common/sound/match/acb/Chant/   0x142963fe0  /common/sound/match/awb/Chant/
0x142964008  DevelopData/sound/inGamePreview/SA_    0x142964030  cpk_snd/common/sound/match/acb/announce/SA_   0x142964070  /common/sound/match/awb/announce/SA_
0x1429640a0  DevelopData/sound/inGamePreview/PV      0x1429640c8  cpk_snd/common/sound/match/acb/playervoice/PV
0x142964110  DevelopData/sound/inGamePreview/SE_HUD  0x142964138  cpk_snd/common/sound/match/acb/hud/SE_HUD
0x142964178  00_TEAM  0x142964180 10_PLAYER  0x142964190 20_MAIN  0x142964198 30_MAIN
0x1429641a0  40_MAIN   0x1429641a8 50_MAIN   0x1429641b0  cpk_snd/xxx   0x1429641c0  /sound/acb/   0x1429641d8  /sound/awb/
0x1429642d8  cpk_snd/common/sound/match/acb/   0x142964330  /common/sound/match/awb/
```

So an acb is addressed as `<prefix><family/variant><.acb>` and its streaming awb as
`<awb-prefix><same-tail><.awb>`; `cpk_snd/xxx` + `/sound/acb/<name>.acb` is the generic form used
for the language commentary packs (`eng` + name). Each family has a `DevelopData/sound/inGamePreview/…`
twin — the dev-preview override path (present in the exe, not in the shipped cpk).

### 3.3 The name/table files beside the acbs (all in `dt44_all.cpk`, OBS listing)

```
common/sound/match/ChantBaseName.bin    2,112 B     (SCBD container, §4.4)
common/sound/match/ChantList.bin      104,112 B    (SCBD container)
common/sound/match/CheersBranchName.bin 22,056 B   (SECD container, §4.4)
common/sound/match/TeamDirectionName.bin   952 B   (STDD container, §4.4)
common/sound/config/Se/se_config.bin       6,310 B (WESYS+zlib, §4.1)
common/sound/config/Supporter/su_config.bin 19,740 B (WESYS+zlib, §4.2)
common/sound/config/Announce/sa_config.bin 12,359 B (WESYS+zlib)
common/sound/config/Chant/ch_config.bin     7,604 B (WESYS+zlib)
common/sound/config/Sequence/sq_config.bin    433 B (WESYS+zlib)
common/sound/menu/PES2021.acf            21,344 B (CRI @UTF, the global mixer, §1.5)
common/sound/menu/playlist/Playlist.xml   6,046 B (menu-BGM playlist)
```

Their exe path strings are at **0x14294d13f** (`sound/config/Se/`), **0x14294d150**
(`se_config.bin`), **0x14294d33f** (`sound/config/Supporter/`), **0x14294d358** (`su_config.bin`),
**0x14294e3c0** (`sa_config.bin`), **0x14294e4df** (`sound/config/Sequence/`), **0x14294e4f8**
(`sq_config.bin`), **0x14294e900** (`ch_config.bin`); the chant name bins at **0x14296666c**
(`CheersBranchName.bin`), **0x142966814** (`ChantList.bin`), **0x142966cec** (`ChantBaseName.bin`),
**0x14296562c** (`TeamDirectionName.bin`). The `SeConfig` class vtable is at **0x14294d300**
(slots `0x14143ebe0`, `0x14143ec20`); the five config loaders are at **0x14143eb60** (Se),
**0x14143f2b0** (Supporter), **0x1414401c0** (Sequence), **0x14143f7f0** (Announce),
**0x1414406a0** (Chant). The crowd control XML is `common/mob/control/mob_anime_control.xml`
(27,799 B; exe string at 0x142617b50).

There is **no WESYS constant object** for sound in `common/match/constant/` (that family holds the
stadium/camera constants only, per the stadium report). The sound equivalents are the five
`*_config.bin` trees above plus the global mixer acf. No image constants: every cue → waveform
binding is inside the acb/awb pair.

---

## 4. The trigger→file mapping, literally

### 4.1 Container: WESYS+zlib

All five config bins share the WESYS wrapper the repo already reads: 16-byte header
(`ff 10 11 57 'WESYS'`, u32 flags/version, u32 decompressed size) then a zlib stream from +0x10.
Decompressed sizes: se 52,480; su 143,104; sa 106,480; ch 62,816; sq 1,472 (OBS, `zlib.decompress`).

### 4.2 Compiled tree format (OBS, byte-level; scripts `<scratch>/AudioRE/cfg/`)

```
+0x00  u32 sectionCount                     ; se=24, su=152, sa=44, ch=82, sq=4
+0x04  sectionCount × { u32 tag, u32 offset, u32 size }     ; 12 B each; tag=8 marks the first
                                                            ; (root) node section, 0 otherwise
then, pairwise, a "node" section followed by its "data" section:
  node section:  u32 nameLen; char name[nameLen+1, 4-padded]; u32 nodeCount; u32 1; u32 0x152
                 nodeCount children, each:  u32 childNameLen; char name[]; u32 poolOffA; u32 poolOffB
  data section:  u32 0x0000ff01; u32 0x07e5; u32 nodeCount (matches); u32 1; u32 0x152
                 serialized node payload with the property/enum names inline as C strings
  final two sections: a u32 offset table (lengths ≈ 0x1c) and the string pool (suffix-shared)
```

`0x152` (=338) is the format stamp; `0x07e5` (=2021) the data-section magic. OBS evidence:

- The paired counts match: `SE_ROOT` node has `nodeCount=13` and its data section repeats 13.
- The child `poolOffA/B` are **offsets into the last section's pool** (base = that section's
  offset): OBS, `SE_ROOT` children reference 0x131/0x144/0x14c/0x135/0x202/0x24c/0x350/0x436/0x641
  and reading `pool[0x131]` gives `INSIDE_04`, `pool[0x144]` → `LHIT_INSIDE_05`,
  `pool[0x202]` → `_PL_BALLHIT_INSTEP_03`, `pool[0x436]` → `K_PUNCH_05` — i.e. the pool stores
  **cue names with shared suffixes** (only the differing suffix per string).
- The se pool itself is the cue vocabulary: it begins
  `SE_PL_BALLHIT_CK_01..05, SE_PL_BALLHIT_SHOOT_04/05, SE_PL_BALLHIT_INSIDE_01..05,
  SE_PL_BALLHIT_INFRONT_01..05, SE_PL_BALLHIT_INSTEP_01..05, SE_PL_BALLHIT_OUTSIDE_01..05, …`
  (OBS, string scan of section 23).
- The data sections hold the condition vocabulary verbatim: `ConditionNone`, `PRId`,
  `CheckMatchEvent`, `RecordKind` with values `RESTART_READY`, `MATCH_STATUS`,
  `CheckBallTouchRestartKind`, `CheckBallCollisionPart`, `ObjectTouchKind`, `CROSSBAR`,
  `CORNER_KICK`, `TOUCH`, `Numeric`, `SoundTarget` (OBS).

### 4.3 The trees, by name (OBS — every node section's name)

- **Se** (24 sections = 11 trees): `SE_ROOT` (children `SE_01, SE_02, SE_03, SE_04, SE_05, SE_PV`),
  `SE_01`…`SE_05`, `SE_PV`, `SE_PV_01`…`SE_PV_04`.
  INF (high): `SE_0N` are the pitch-sound behaviour trees; `SE_PV*` are player-voice.
- **Supporter** (152 sections = 75 trees): `SU_00`, `SU_00_A`, `SU_00_A_ANT_END`, `SU_01`,
  `SU_01_B/C`, `SU_02{A,B}`, `SU_03{,_2,_A,_B,_D}`, `SU_04{A,C}`, `SU_05`, `SU_06_1..5`, `SU_07{,_1}`,
  `SU_08{A,B}`, `SU_09{,_A}`, `SU_10{,_2,_A.._F}`, `SU_11`, `SU_12{,_A,_B}`, `SU_13_A/B`, `SU_14_A/B`,
  `SU_15`, `SU_16{,_A,_B}`, `SU_17`, `SU_18{,_2,_A.._E}`, `SU_19{,_A,_B}`, `SU_20{,_C}`, `SU_21`,
  `SU_22`, `SU_23`, `SU_25`, `SU_26{,_1}`, `SU_39`, `SU_39A` … (full list in scratch).
  INF (high): these are the crowd-reaction behaviours, one tree per reaction situation.
- **Announce** (44 sections): `SA_01` (+ `SA_01_ENTER`, `SA_01_INTRO1`, `SA_01_MEMBER`, `SA_01_SBQ`,
  `SA_01_VS`), `SA_02`, `SA_03` (+`SA_03_SUB`), `SA_04`, `SA_05`, `SA_06_01/02`, `SA_HALF_ROOT`.
- **Chant** (82 sections): `BG_00` (+ `BG_00_01..04`, `BG_00_GN`, `BG_00_SB`, `BG_00_TM`),
  `BG_37..BG_39`, `CA_REPLAY_RO…`, `CA_ROOT`, `CH_00`, …
- **Sequence** (4 sections): `SQ_ROOT` (9 children) + data + offset table + empty pool.

### 4.4 The crowd trigger→state map: `common/mob/control/mob_anime_control.xml` (OBS, plain XML)

177 `<Data>` rows; each row is one crowd sound cue and the crowd animation state it forces per
stand group. Attributes: `Sound` (the cue name), and per group `Home`, `Away`, `SouthHome`,
`SouthAway`, `CoachHome`, `CoachAway`, `StandsFlagHome`, `StandsFlagAway` (the animation state
name, empty = no change). Example rows verbatim:

```xml
<Data Away="lost_goal_level3" Home="get_goal_level3" Sound="SE_H_GL_01" …/>
<Data Away="get_goal_level3" Home="lost_goal_level3" Sound="SE_A_GL_01" …/>
<Data Home="chance_level3" SouthHome="chance_sa_level3" Sound="SE_H_EXCITE_LV3_01" …/>
<Data Home="middle_idole" Sound="SE_H_EXCITE_LV2_01" …/>
<Data Home="idole" Sound="SE_H_ST_GK_01" …/>
<Data Home="idole_action_passcut" Sound="SE_H_IP_DISTURB_PASSCUT_01" …/>
```

- 176 distinct cue names (the full `SE_H_*`/`SE_A_*` list of §2.6), covering goal, own goal,
  offside goal, chance (excite LV2/LV3), shot failed (far/near, hit bar, saved catch/punch),
  in-play contact, dribble/pass-cut/press, keeper set piece, free kick, penalty (PKMT), line-out,
  restart/kickoff, card events, turnovers, time-up (won/lost/draw/tie/behind).
- Crowd states observed: Home 22, Away 21, SouthHome 3, SouthAway 3 — `get_goal_level3`,
  `lost_goal_level3`, `chance_level3`, `not_goal_level3`, `not_goal_pk_joy`, `foul_in_play`,
  `idole`, `idole_action`, `idole_action_passcut`, `idole_action_turnover(2)`, `idole_pk`,
  `idole_pk_provocation`, `idole_pk_smartPhone`, `middle_idole`, `time_up_good/bad/enjoy/anger`,
  `yale`, `demo_enter_clap`, `demo_foul_anger`, `demo_foul_yale`, `return`; South groups use
  `idole_sa`, `chance_sa_level3`, `return_sa`.
- INF (high): the crowd *sound* and the crowd *animation* are the same event — the animation
  system subscribes to the same cue names the sound system plays.

### 4.5 Name-table containers (SBCD/STDD/SECD)

`ChantBaseName.bin` starts `"SCBD"`, `TeamDirectionName.bin` `"STDD"` + version 0x2018,
`CheersBranchName.bin` `"SECD"` + version 0x2015 (OBS, hexdump). Layout (OBS): u32 count, a slot
table of fixed-width records padded with `0xffffffff`, and a trailing string block referenced by
u32 offsets (e.g. `ChantBaseName` records at +0x180: `{u32 strOff, u32 idx, u32 1, u32 0}`).
INF (med-high): these are the id→name dictionaries the chant/cheer selection resolves against
(region → chant branch, team → direction/cheer branch); `sys@sound` has the matching loader classes
(`Loader@BaseName@chant`, `Loader@BranchData@cheer`, `Loader@TeamDirectionName`).

---

## 5. The crowd system

Carried from the stadium report §3 unless marked new; crowd-audio findings are new here.

### 5.1 Models and placement (OBS)

- Models: `Asset/model/bg/common/audi/#Win/` in `dt00_x64.cpk` — `audi_model.fpk`
  (`au00.skl` + 14 spectator fmdls `au00_mouthOpen, au02, au03_mouthOpen, … au17`, fpkd
  `AudienceModel{models[14], sklFile=au00.skl}`), `audiParts_model.fpk` (accessories),
  `audiLow_model.fpk` (`au_Low.fmdl`, `au_sky.fmdl`), `audiLowParts_model.fpk`, `audi_seat_model.fpk`
  / `seatOnly_model.fpk` (`chair.fmdl`); textures `au_h_*`, `au_l_*`, `o_phonelight_alp`,
  `o_strobo_alp` under `audi/sourceimages/#windx11/`.
- Placement: per stadium `Asset/model/bg/stNNN/audi/#Win/audiarea_stNNN.fpk` → `audiarea.bin`
  referenced by `AudienceAreaData{areaFile}`. Format (OBS, stadium report §3): u32 version=1;
  a header slot table of u32 offsets to stand blocks; each block = `{u16 block-id pair, u32 AABB
  offset (= block+8), 6 f32 AABB, u32 record count, pad}`; then records of **96 B**:
  `{f32 rowStep, 2 f32 scale, 12 f32 = 4 corners of a sloped stand quad, padding, trailing u32}`
  (st071: rowStep 1.0, scale 0.86; st002: rowStep 2.1).

### 5.2 Rendering (OBS, RTTI + live heap from the stadium report)

GPU-instanced skinned meshes with an impostor LOD: `fox::gr::Crowd`, `CrowdModel`,
`PluginCrowd{,Deferred,Depth,Impostor,GenerateMips}`, jobs `CrowdSetupJob`,
`CrowdVisibilityTestJob`, `CrowdInstanceUpdateJob`, `CrowdInstanceAnimeBufferUpdateJob`,
`BuildCrowdCommandsJob`, `BuildCrowdImpostorCommandsJob`; per-match texture compositing
(`AudienceColorTextureCompositor`, `AudienceTextureMergeJob`, …). Live during a match:
`AudienceModel` ×5, `Crowd` ×4, `CrowdModel` ×209. The animation is a per-instance clip
(`audi_motion.bin`, `audi_animeset.bin`, `audi_transition.bin`, `common/mob/audience_BB/motion/*.gani`
≈190 clips: sit/stand idle, clap, angry, frustrate, goal, flag, smartPhone, enter_SA…), skeleton
`common/mob/audience_BB/skeleton/mob_high_skeletons.hkx` (2,576 B; `audience_skeleton_list.xml`).

### 5.3 What drives the reaction (new, OBS)

The driver is the **sound event**: `mob_anime_control.xml` (§4.3) is keyed by cue name and sets the
crowd animation state per group; the trees that select those cues are the `SU_*` trees in
`su_config.bin` (§4.3) driven by `CrowdNoise@ai@app@sound`; the ambience level is
`SE_AMBIENT_NORM` ↔ `SE_H/A_AMBIENT_EXCITE` (cz acbs, §2.6) modulated by the
`AISAC_ExciteControl_H/A` controls (§1.4). Stand flags wave via
`common/mob/prop/standsFlag/*` (`stands_flag_animeset.bin`, `stands_flag_transition.bin`).
The exe's user options gate the crowd mix: `Def_Sound_Sys_Cheers_Enable/TeamID/Scale` (§1.4).

---

## 6. What an implementation can play, per kind

Per kind: source file(s) → trigger → parameters. All paths are VFS paths; all cue names verbatim.

| kind | source | trigger | parameters |
|---|---|---|---|
| kick by surface/power | `common/sound/match/acb/SE_Movement.acb` (embedded AWB) | contact event: surface ∈ {inside, instep, infront, outside, head, gene, ck, shoot} × tier 1..5 → `SE_PL_BALLHIT_<SURFACE>_0N` | play at the contact position (`CommandListenerPos`); INF: tier from contact speed, cf. the `IRON` picker §1.2 |
| ball bounce | same | ground contact → `SE_PL_BALLHIT_BOUND_01..05` | tier by bounce speed |
| frame/net/board hits | same | collision target: post/bar/frame/iron/net/board/panel/pylon × tier | position = contact point; the frame family is the documented speed picker (`SE_PL_BALLHIT_IRON_01..03` at 3 tiers) |
| tackle/jostle/fall | same | `SE_PL_JOSTLE_01/02`, `SE_PL_TACKLE_SLIDING_01`, `SE_PL_BODYFALL_01`, `SE_PL_FOOT_FAR_01`; positional variants `SE_JOSTLE_POS`, `SE_SLIDING_POS`, `SE_FALL_LANDING_POS` | position of the players |
| keeper | same | catch → `SE_PL_GK_CATCH_01..05`; punch/parry → `SE_PL_GK_PUNCH_01..05` | tier by shot speed |
| footsteps | same | stride event | parametric cues `footstep_run_para_A/B/C`, `footstep_cloth_para_A` with AISAC `Footstep_Distance` (1010) + `Footstep_Speed` (1009) |
| whistle/referee | `common/sound/match/acb/referee/SE_Referee_<A..F>.acb` | restart/foul/goal/kickoff/halftime/timeup/offside/lineout → `SE_PL_WHISTLE_<EVENT>` | referee pack selected by referee id (INF); positional via `SE_REFEREE_POS` |
| crowd reaction | `common/sound/match/acb/cheers/SE_Cheers_CM_*.acb` (34 themed packs) | match event → cue (§2.6 table); home/away split by `SE_H_`/`SE_A_` prefix | `AISAC_ExciteControl_H/A` level; user scale `Def_Sound_Sys_Cheers_Scale` |
| crowd reaction animation | `common/mob/control/mob_anime_control.xml` | same cue name → per-group state (`Home/Away/SouthHome/SouthAway/Coach*/StandsFlag*`) | state machine `audi_animeset.bin` + `audi_transition.bin` |
| crowd ambience | `cheers/SE_Cheers_CZ_Base/Common.acb` | ambient loop `region_norm*`, `SE_AMBIENT_NORM`; excitement `SE_H/A_AMBIENT_EXCITE`, `region_excite_*_lp` | continuous loop; excitement level crossfades |
| chants | `chant/CHANT.acb` (+ `CHANT_Base.acb`) | team/region selection via `ChantList.bin` + `ChantBaseName.bin` → cue `SP_R<region>_<NNN><v>` | per-team chant book |
| announcements | `announce/SA_<LANG>.acb` (+ awb) | `SA_<situation>_<pack>_<NN>` via `sa_config.bin` trees `SA_01…SA_06` | language = install language |
| commentary | `eng/sound/acb/{00_TEAM,10_PLAYER,20_MAIN,30_MAIN,40_MAIN,50_MAIN}.acb` (streaming) | event corpus; `20..50_MAIN` lines, `00_TEAM`/`10_PLAYER` name utterances | BUS3_CommentaryON; language pack from `dt20_<lang>_x64.cpk` |
| player voice | `playervoice/PV[_LANG].acb` | `SE_PV_*` / `pv_*` via `SE_PV*` trees | per-language |
| match BGM | `BGM_Match{,_P2P}.acb` (+ awb) | `BGM1_LIC_ENTER_*`, anthem `BGM1_LIC_ANTH_T<NNNN>_01`, `SA_BELL`, time-up `BGM1_TIMEUP_ETC_REQ` | BUS2/BUS5 |
| HUD/UI | `hud/SE_HUD.acb`, `menu/acb/SE_{Mode,System}.acb` | `SE_HUD_{BIG,MID,SML}_IN/OUT`, `SE_WIPE_IN_01` | — |

Decoding for playback: waveforms are HCA (`EncodeType=2`) inside an AFS2 (sibling `.awb` or
embedded at the acb's AFS2 offset — `SE_Movement.acb` embeds at 0x91a0), located per cue via
`CueTable → SynthTable{ReferenceItems} → WaveformTable{MemoryAwbId | StreamAwbId}`. A working HCA
decoder exists in scratch (`<scratch>/AudioRE/clhca/`, built from the prior session; decoded a
48 kHz mono sample to `<scratch>/AudioRE/out48.wav`, OBS artifact).

---

## 7. Open items, and what was tried

1. **Per-node semantics of the condition trees** (which condition binds which cue, and the exact
   meaning of a node child's two pool offsets). Closed at the container/record level (§4.2); the
   remaining work is the `SysFuncBinary`/`SysParamConditionBinary` interpreters (classes at
   `ai_selector@app@sound`). Tried: the raw section walk (done), StrCode64/32 hashing of every cue
   and property name against every u32 in the configs (**0 matches** — the keys are raw offsets and
   ids, not hashes), and the loader functions (0x141452ba0 dispatches to the bin/xml readers; the
   readers are virtual).
2. **The AISAC curve values** are present but not transcribed (they are PES tuning data): the
   graph tables carry f32 control/destination pairs (e.g. `SE_Movement` graph 4 has controls
   64.0/68.0/113.0/250.0/469.0). Read them at runtime from the acb rather than hard-coding.
3. **Cue-id numbering** is per-acb and sparse (e.g. cheer `SE_H_GL_01` = id 380 while
   `region_boowind_big_lp_link` = id 0): always resolve by **name** through `CueNameTable`.
4. **`sq_config`/`sa_config`/`ch_config` deep semantics** (sequence step tables) — container format
   decoded (§4.2), payload semantics not walked.
5. **The `SE_PL_BALLHIT_IRON` tier thresholds**: the three branch constants are at
   0x142588210/0x14259be1c (and a scale at 0x14259ce38); their float values read as
   1.5201516882120804e-23/1.46925918470367e+25 in this dump, which is not the intended
   interpretation — the exe more likely loads these via a relocation-corrected path at runtime.
   Treat the tier *structure* (3 tiers by speed) as OBS and the constants as unread.
6. **Ghidra** (MCP and headless) was unavailable; everything above was derived from objdump on
   image bytes and data reads, per the project rule. Prior-session scratch in
   `<scratch>/AudioRE/` (census.py, dumpacb2.py, utfparse.py, awbdump.py, clhca/) was reused, not
   duplicated.
