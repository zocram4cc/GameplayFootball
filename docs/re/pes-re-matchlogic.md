# PES 2021 match logic: attributes → behaviour, tactics, ball model, AI map — evidence report

Scope: read-only RE of PES 2021 data (VFS of the user's install) and of the PES2021.exe image. Goal: enough layout and constants to reimplement the attribute, tactics, ball and AI parameter plumbing faithfully. Nothing here is a byte copy of PES data. Tables give layouts, offsets and a few individual field values only.

## 0. Sources, method, tags, state

**Tags.**
- **OBS** = observed, with the method stated.
- **INF** = inferred, with a confidence.
- **EXT** = taken from a third-party write-up. EXT is not verified unless it says "EXT, verified …".

**Sources.**
- **Install VFS** (`pesfmt_dump`). Same mount order as the game.
  - Gameplay constants: `common/match/constant/*.bin`. They come only from `Data/dt18_all.cpk`; a byte-identical `dt18_all - Copia.cpk` is skipped by the mounter.
  - Database: `common/etc/pesdb/*.bin`. The VFS winner is the 4cc `download/4cc_01_base.cpk`, whose `Player.bin` is all placeholders. Real ratings were therefore decoded from the vanilla `Data/dt10_x64.cpk` copy (WESYS zlib container, 5,547,984 B decompressed, 17,782 players). The 4cc file has the same 312 B record layout: 4,830 placeholder records.
- **Image** `<scratch>/pesmem.bin`. The live `.trace`/`.rdata`/`.data` of PES2021.exe were dumped from `/proc/<pid>/mem` (base 0x140000000; see `pes-kit-assembly.md` §7.0). All addresses below are virtual addresses in that image.
  - **Cross-check (OBS):** the third-party addresses in [Aoba, *PES2021 Engine Research*] hit the expected bytes in this dump. Examples:
    - position-compat masks @0x142613D20;
    - BallPlayer duration tables @0x142BEF098 / 0x142BEF0B8;
    - Super-sub table @0x142632F74;
    - stat clamp table @0x142613C60;
    - AI action-name table @0x1434A4D50.
  - So that write-up describes this exact build, and its addresses can be used directly here. Its claims are still tagged EXT unless re-checked below.
- **Disassembly** with capstone, from `<scratch>/MatchLogicRE/cdis.py` and `acc.py`. Ghidra MCP was down for most of the session, so no Ghidra output is quoted.
- **Live process** (PES2021.exe pid <pid>, reads of `/proc/<pid>/mem` only). The state at 20:33 local was a **match in progress**:
  - `gMatchContext` (0x1436F3DC8) was non-null;
  - both team rosters were populated: 23 players each, all 4cc-league edit players whose ability bytes are tier-flat (all 77, 82, 88 or 99);
  - live pitch-dimension struct present (§6).
  - Earlier reads (≈20:15) found `gMatchContext` = 0, i.e. no match. Captured rosters: `<scratch>/MatchLogicRE/rosters.bin`. It is local scratch and contains save-derived names, so its content is not quoted here.
- **Reference ratings for decoding Player.bin:** public pesmaster.com pages for 32 vanilla player ids (`<scratch>/MatchLogicRE/pm/`, `ref.json`; parser `parsepm.py`). They are used only to label bit fields; they are not copied into any table. That database is a slightly later version than dt10, so 0–2 of 32 players differ on some stats. "Fit k/32" below means the decoded field matched k of the 32 players.
- **EXT write-ups:**
  - [Aoba, *PES2021 Engine Research*] and [Aoba, *PES2021 dt18*] on implyingrigged.info. They were supplied as page text in this session; the site itself is Cloudflare-blocked.
  - the4chancup `pes-gameplay-editor`: GPL, mapping files read only, nothing ported. Its `mappings/18/*.txt` are PES18-era. They match PES21 for `support.o`, `defenceMark.o`, `overlap.o`, `pairAnime.o`, `defence.o`, `defenceCover.o` (OBS: decoded values equal the vanilla values Aoba lists), and do NOT match for `basePosition.o` / `diagonalRun.o` (killed, §2.5).
  - the4chancup `4ccEditor` `pes20.cpp` (zlib licence): EDIT save layout. It is NOT the `Player.bin` layout (killed, §1.6).

---

## 1. Player attribute records

### 1.1 `Player.bin` (pesdb) — 312 B per record, no header

Container (OBS): the vanilla copy is a WESYS zlib file (`?? 10 ?? 57 'WESYS'`, zlib from +0x10); the 4cc copy is raw. Records are contiguous, 312 B each.

| Offset | Size | Meaning | Evidence |
|---|---|---|---|
| +0x00 | u32 | team id (club), 0 = none | INF (med-high): the reference RWF's value 108 equals pesmaster's club id for that player; 0 for 8,589 records |
| +0x04 | u32 | second team id (INF: loan/previous club), mostly 0 | INF (low) |
| +0x08 | u32 | **player id** (= face/appearance id) | OBS (equals the pesmaster id for all 32) |
| +0x0C | bits 0..26 | **contract expiry**, decimal `yyyymmdd` (e.g. 20210630) | OBS (32/32 equal pesmaster "Contract Dur.") |
| +0x0C | bits 27..31 | field id 0x76, 5-bit 0..19 (80 % zero) | OBS accessor; meaning unknown |
| +0x10 | bits 0..26 | second date `yyyymmdd`, mostly 0 (INF: loan end) | INF (med) |
| +0x10 | bits 27..31 | **playing style** (id 0x40) | OBS |
| +0x14 | u32 | unknown. Low u16 ∈ {0, 3000, 4000, 8000…} | INF: value/salary-like, unverified |
| +0x18 | bytes 24..26 | almost always 0 | OBS |
| +0x1B | u8 | **height** − 100 | OBS |
| +0x1C … +0x43 | bit-packed | everything in the table in §1.2 | OBS |
| +0x44 | 61 B | name (UTF-8, NUL-padded) | OBS |
| +0x81 | 61 B | shirt/print name | OBS |
| +0xBE | 61 B | third name string (INF: alternate/local-script name) | OBS position only |
| +0xFB | 61 B | fourth name string (INF: national-team print name) | OBS position only |

Nationality and a second nationality are not in the accessor table (§1.2), so they are read elsewhere in the exe:
- bits 233..241 (9-bit) = **nationality** id. OBS: a categorical fit over 32 players is consistent and injective; 166 distinct values, max 312.
- bits 224..232 (9-bit) = second nationality. INF (med): 0 for 88 %.

### 1.2 Bit-field map — recovered from the exe's own accessor table (OBS)

**Accessor table.** The exe holds a field-descriptor table at **0x143529930**: 108 entries of `{u64 fieldId, u64 accessorFn, u64 0}` for field ids 0x0D..0x78. Every accessor `fn(rec, mode)` is a tiny leaf:
- mode 0 returns the value, `(u32@byte >> shift) & mask` + add;
- mode 1 returns the min;
- mode 2 returns the max;
- any other mode returns a default (abilities 80, height 180, weight 75, age 17).

`rec` is the Player.bin record start. Proof: id 0x10 reads `byte[rec+0x1B] + 100`, which matches the empirical height field. Decoder: `<scratch>/MatchLogicRE/acc.py`; raw output `acc_out.txt`.

**Labels.** Every label below was checked against the 32 reference players by brute-force field search (`fieldfind.py`, `bitfind.py`): **fit ≥ 30/32 for every ability, 32/32 for height, age, foot, position, playing style and every COM style**. The remaining single-player misses are database-version drift. The id order 0x12..0x2A is exactly the in-game ability menu order. The skill ids follow the Edit-Mode skill order. Both facts are strong independent confirmations.

Bit numbering: absolute bit index from record start, LSB-first (`(int.from_bytes(rec,'little') >> bit) & ((1<<w)-1)`).

| id | accessor | bit | byte.bit | width | value = raw + | UI min/max (accessor mode 1/2) | meaning |
|---|---|---|---|---|---|---|---|
| 0x0d | 0x141f0fb20 | 408 | 51.0 | 6 | 15 | 15..63 | Age (years) |
| 0x0e | 0x141f0fc30 | 434 | 54.2 | 4 | 0 | 0..12 | Registered position (0 GK,1 CB,2 LB,3 RB,4 DMF,5 CMF,6 LMF,7 RMF,8 AMF,9 LWF,10 RWF,11 SS,12 CF) |
| 0x0f | 0x141f103a0 | 514 | 64.2 | 1 | 0 | 0..1 | Stronger foot (0 right, 1 left) |
| 0x10 | 0x141f0f590 | 216 | 27.0 | 8 | 100 | 100..250 | Height (cm) |
| 0x11 | 0x141f0f620 | 256 | 32.0 | 7 | 30 | 30..150 | Weight (kg) |
| 0x12 | 0x141f0f9e0 | 370 | 46.2 | 6 | 40 | 40..99 | Offensive Awareness |
| 0x13 | 0x141f0f6e0 | 281 | 35.1 | 6 | 40 | 40..99 | Ball Control |
| 0x14 | 0x141f0f950 | 352 | 44.0 | 6 | 40 | 40..99 | Dribbling |
| 0x15 | 0x141f0fb70 | 416 | 52.0 | 6 | 40 | 40..99 | Tight Possession |
| 0x16 | 0x141f0f650 | 263 | 32.7 | 6 | 40 | 40..99 | Low Pass |
| 0x17 | 0x141f0faf0 | 402 | 50.2 | 6 | 40 | 40..99 | Lofted Pass |
| 0x18 | 0x141f0fac0 | 396 | 49.4 | 6 | 40 | 40..99 | Finishing |
| 0x19 | 0x141f0f730 | 288 | 36.0 | 6 | 40 | 40..99 | Heading |
| 0x1a | 0x141f0f5f0 | 250 | 31.2 | 6 | 40 | 40..99 | Place Kicking |
| 0x1b | 0x141f0f8a0 | 332 | 41.4 | 6 | 40 | 40..99 | Curl |
| 0x1c | 0x141f0f7c0 | 306 | 38.2 | 6 | 40 | 40..99 | Speed |
| 0x1d | 0x141f0f900 | 344 | 43.0 | 6 | 40 | 40..99 | Acceleration |
| 0x1e | 0x141f0f980 | 358 | 44.6 | 6 | 40 | 40..99 | Kicking Power |
| 0x1f | 0x141f0f760 | 294 | 36.6 | 6 | 40 | 40..99 | Jump |
| 0x20 | 0x141f0fa90 | 390 | 48.6 | 6 | 40 | 40..99 | Physical Contact |
| 0x21 | 0x141f0fa10 | 376 | 47.0 | 6 | 40 | 40..99 | Balance |
| 0x22 | 0x141f0f8d0 | 338 | 42.2 | 6 | 40 | 40..99 | Stamina |
| 0x23 | 0x141f0f6b0 | 275 | 34.3 | 6 | 40 | 40..99 | Defensive Awareness |
| 0x24 | 0x141f0f7f0 | 312 | 39.0 | 6 | 40 | 40..99 | Ball Winning |
| 0x25 | 0x141f0fa60 | 384 | 48.0 | 6 | 40 | 40..99 | Aggression |
| 0x26 | 0x141f0f870 | 326 | 40.6 | 6 | 40 | 40..99 | GK Awareness |
| 0x27 | 0x141f0f9b0 | 364 | 45.4 | 6 | 40 | 40..99 | GK Catching |
| 0x28 | 0x141f0f680 | 269 | 33.5 | 6 | 40 | 40..99 | GK Clearing |
| 0x29 | 0x141f0f840 | 320 | 40.0 | 6 | 40 | 40..99 | GK Reflexes |
| 0x2a | 0x141f0f790 | 300 | 37.4 | 6 | 40 | 40..99 | GK Reach |
| 0x2b | 0x141f0fd70 | 454 | 56.6 | 2 | 0 | 0..3 | Weak Foot Usage (UI = raw+1) |
| 0x2c | 0x141f0fe20 | 462 | 57.6 | 2 | 0 | 0..3 | Weak Foot Accuracy (UI = raw+1) |
| 0x2d | 0x141f0fc60 | 438 | 54.6 | 3 | 0 | 0..7 | Form (UI 1..8 = raw+1) |
| 0x2e | 0x141f0fdd0 | 458 | 57.2 | 2 | 0 | 0..2 | Injury Resistance (UI = raw+1; 0..2 = C,B,A) |
| 0x2f | 0x141f0fd40 | 451 | 56.3 | 3 | 0 | 0..7 | unknown 3-bit (0..7) |
| 0x30 | 0x141f0fa40 | 382 | 47.6 | 2 | 0 | 0..2 | unknown 2-bit (0..2) |
| 0x31 | 0x141f10300 | 509 | 63.5 | 1 | 0 | 0..1 | unknown 1-bit (INF: stronger hand) |
| 0x32 | 0x141f0ffc0 | 480 | 60.0 | 2 | 0 | 0..2 | unknown 2-bit (0..2) |
| 0x33 | 0x141f0f930 | 350 | 43.6 | 2 | 0 | 0..2 | Playable position GK (0 C,1 B,2 A) |
| 0x34 | 0x141f0feb0 | 468 | 58.4 | 2 | 0 | 0..2 | Playable CB |
| 0x35 | 0x141f0f820 | 318 | 39.6 | 2 | 0 | 0..2 | Playable LB |
| 0x36 | 0x141f0ff40 | 474 | 59.2 | 2 | 0 | 0..2 | Playable RB |
| 0x37 | 0x141f0fb50 | 414 | 51.6 | 2 | 0 | 0..2 | Playable DMF |
| 0x38 | 0x141f0fda0 | 456 | 57.0 | 2 | 0 | 0..2 | Playable CMF |
| 0x39 | 0x141f0fe80 | 466 | 58.2 | 2 | 0 | 0..2 | Playable LMF |
| 0x3a | 0x141f0fdf0 | 460 | 57.4 | 2 | 0 | 0..2 | Playable RMF |
| 0x3b | 0x141f0fe50 | 464 | 58.0 | 2 | 0 | 0..2 | Playable AMF |
| 0x3c | 0x141f0ff10 | 472 | 59.0 | 2 | 0 | 0..2 | Playable LWF |
| 0x3d | 0x141f0ff70 | 476 | 59.4 | 2 | 0 | 0..2 | Playable RWF |
| 0x3e | 0x141f0ffa0 | 478 | 59.6 | 2 | 0 | 0..2 | Playable SS |
| 0x3f | 0x141f0fee0 | 470 | 58.6 | 2 | 0 | 0..2 | Playable CF |
| 0x40 | 0x141f0f560 | 155 | 19.3 | 5 | 0 | 0..21 | Playing style enum (see §1.4) |
| 0x41 | 0x141f100c0 | 489 | 61.1 | 1 | 0 | 0..1 | COM: Trickster |
| 0x42 | 0x141f105c0 | 531 | 66.3 | 1 | 0 | 0..1 | COM: Mazing Run |
| 0x43 | 0x141f10520 | 526 | 65.6 | 1 | 0 | 0..1 | COM: Speeding Bullet |
| 0x44 | 0x141f10320 | 510 | 63.6 | 1 | 0 | 0..1 | COM: Incisive Run |
| 0x45 | 0x141f10580 | 529 | 66.1 | 1 | 0 | 0..1 | COM: Long Ball Expert |
| 0x46 | 0x141f0fcf0 | 447 | 55.7 | 1 | 0 | 0..1 | COM: Early Cross |
| 0x47 | 0x141f10460 | 520 | 65.0 | 1 | 0 | 0..1 | COM: Long Ranger |
| 0x48 | 0x141f10440 | 519 | 64.7 | 1 | 0 | 0..1 | skill: Scissors Feint |
| 0x49 | 0x141f104c0 | 523 | 65.3 | 1 | 0 | 0..1 | skill: Double Touch |
| 0x4a | 0x141f10040 | 485 | 60.5 | 1 | 0 | 0..1 | skill: Flip Flap |
| 0x4b | 0x141f101c0 | 497 | 62.1 | 1 | 0 | 0..1 | skill: Marseille Turn |
| 0x4c | 0x141f0ffe0 | 482 | 60.2 | 1 | 0 | 0..1 | skill: Sombrero |
| 0x4d | 0x141f0f710 | 287 | 35.7 | 1 | 0 | 0..1 | skill: Cross Over Turn |
| 0x4e | 0x141f10560 | 528 | 66.0 | 1 | 0 | 0..1 | skill: Cut Behind & Turn |
| 0x4f | 0x141f10500 | 525 | 65.5 | 1 | 0 | 0..1 | skill: Scotch Move |
| 0x50 | 0x141f10200 | 500 | 62.4 | 1 | 0 | 0..1 | skill: Step On Skill Control |
| 0x51 | 0x141f10180 | 495 | 61.7 | 1 | 0 | 0..1 | skill: Heading |
| 0x52 | 0x141f10540 | 527 | 65.7 | 1 | 0 | 0..1 | skill: Long Range Drive |
| 0x53 | 0x141f102c0 | 506 | 63.2 | 1 | 0 | 0..1 | skill: Chip Shot Control |
| 0x54 | 0x141f10420 | 518 | 64.6 | 1 | 0 | 0..1 | skill: Long Range Shooting |
| 0x55 | 0x141f10380 | 513 | 64.1 | 1 | 0 | 0..1 | skill: Knuckle Shot |
| 0x56 | 0x141f10160 | 494 | 61.6 | 1 | 0 | 0..1 | skill: Dipping Shot |
| 0x57 | 0x141f101e0 | 499 | 62.3 | 1 | 0 | 0..1 | skill: Rising Shots |
| 0x58 | 0x141f104e0 | 524 | 65.4 | 1 | 0 | 0..1 | skill: Acrobatic Finishing |
| 0x59 | 0x141f102a0 | 505 | 63.1 | 1 | 0 | 0..1 | skill: Heel Trick |
| 0x5a | 0x141f10340 | 511 | 63.7 | 1 | 0 | 0..1 | skill: First-time Shot |
| 0x5b | 0x141f102e0 | 507 | 63.3 | 1 | 0 | 0..1 | skill: One-touch Pass |
| 0x5c | 0x141f10080 | 487 | 60.7 | 1 | 0 | 0..1 | skill: Through Passing |
| 0x5d | 0x141f10020 | 484 | 60.4 | 1 | 0 | 0..1 | skill: Weighted Pass |
| 0x5e | 0x141f10000 | 483 | 60.3 | 1 | 0 | 0..1 | skill: Pinpoint Crossing |
| 0x5f | 0x141f10140 | 493 | 61.5 | 1 | 0 | 0..1 | skill: Outside Curler |
| 0x60 | 0x141f103c0 | 515 | 64.3 | 1 | 0 | 0..1 | skill: Rabona |
| 0x61 | 0x141f10360 | 512 | 64.0 | 1 | 0 | 0..1 | skill: No Look Pass |
| 0x62 | 0x141f100a0 | 488 | 61.0 | 1 | 0 | 0..1 | skill: Low Lofted Pass |
| 0x63 | 0x141f100e0 | 490 | 61.2 | 1 | 0 | 0..1 | skill: GK Low Punt |
| 0x64 | 0x141f101a0 | 496 | 62.0 | 1 | 0 | 0..1 | skill: GK High Punt |
| 0x65 | 0x141f10480 | 521 | 65.1 | 1 | 0 | 0..1 | skill: Long Throw |
| 0x66 | 0x141f104a0 | 522 | 65.2 | 1 | 0 | 0..1 | skill: GK Long Throw |
| 0x67 | 0x141f10220 | 501 | 62.5 | 1 | 0 | 0..1 | skill: Penalty Specialist |
| 0x68 | 0x141f10240 | 502 | 62.6 | 1 | 0 | 0..1 | skill: GK Penalty Saver |
| 0x69 | 0x141f10100 | 491 | 61.3 | 1 | 0 | 0..1 | skill: Gamesmanship |
| 0x6a | 0x141f10280 | 504 | 63.0 | 1 | 0 | 0..1 | skill: Man Marking |
| 0x6b | 0x141f10400 | 517 | 64.5 | 1 | 0 | 0..1 | skill: Track Back |
| 0x6c | 0x141f10260 | 503 | 62.7 | 1 | 0 | 0..1 | skill: Interception |
| 0x6d | 0x141f105a0 | 530 | 66.2 | 1 | 0 | 0..1 | skill: Acrobatic Clear |
| 0x6e | 0x141f10120 | 492 | 61.4 | 1 | 0 | 0..1 | skill: Captaincy |
| 0x6f | 0x141f103e0 | 516 | 64.4 | 1 | 0 | 0..1 | skill: Super-sub |
| 0x70 | 0x141f10060 | 486 | 60.6 | 1 | 0 | 0..1 | skill: Fighting Spirit |
| 0x71 | 0x141f0fc90 | 441 | 55.1 | 3 | 0 | 0..4 | unknown 3-bit 0..4 (INF motion) |
| 0x72 | 0x141f0fbd0 | 426 | 53.2 | 4 | 0 | 0..9 | unknown 4-bit 0..9 (INF motion) |
| 0x73 | 0x141f0fd10 | 448 | 56.0 | 3 | 0 | 0..4 | unknown 3-bit 0..4 (INF motion) |
| 0x74 | 0x141f0fc00 | 430 | 53.6 | 4 | 0 | 0..9 | unknown 4-bit 0..9 (INF motion) |
| 0x75 | 0x141f0fba0 | 422 | 52.6 | 4 | 0 | 0..9 | unknown 4-bit 0..9 (INF motion) |
| 0x76 | 0x141f0f530 | 123 | 15.3 | 5 | 0 | 0..19 | unknown 5-bit 0..19 |
| 0x77 | 0x141f0fcc0 | 444 | 55.4 | 3 | 0 | 0..6 | unknown 3-bit 0..6 |
| 0x78 | 0x141f0f5c0 | 242 | 30.2 | 8 | 0 | 0..162 | unknown 8-bit 0..162 |

**Notes on the table.**
- **Abilities** are 6-bit, stored as value − 40. This gives a hard 40..99 range; the accessor's mode-2 max is 99, an OBS literal `0x63` in every ability accessor.
- **Playable-position labels** (0x33..0x3F). OBS over all 17,782 vanilla players: for players whose registered position is P, the matching field reads 2 ("A") in **100 %** of cases. For example, field 0x33 is 2 for every GK and for no outfielder. 0..2 = C/B/A. This matches the engine familiarity levels Aoba reports (EXT).
- **Weak foot.** 0x2B/0x2C fit 31/32 with UI = raw + 1.
- **Injury resistance.** 0x2E fits 31/32 with UI = raw + 1. The value spread (11k/4.5k/1.9k players at raw 1/2/0) matches the B-heavy C/B/A distribution.
- **Form.** 0x2D fits 32/32 with UI = raw + 1.
- **Unknowns.**
  - The five 0..4/0..9 fields 0x71..0x75 plus 0x2F (0..7) match, one for one, the six motion-style bytes of the in-match block. That block's clamp ranges are 0x37 0..7, 0x38 0..4, 0x39 0..9, 0x3A 0..4, 0x3B 0..9 (§1.3). INF (med): these are the dribble/running/arm, corner-kick, free-kick and penalty motion selectors.
  - 0x30 and 0x32 (2-bit 0..2) and 0x31 (1-bit): unlabelled.
  - 0x76 (0..19) and 0x77 (0..6) line up with in-match clamp indices 0x0E (0..19) and 0x0F (0..6). INF (low): celebration/edit-related.
- **Modding:** see §7.

### 1.3 In-match player block (live) — 0x128 B per player

**Getters (OBS, disassembly).**
- `GetTeamRosterData(ctx, t)` = `*(ctx + (t + 0xA3) * 40)` @0x140A489D0, for t ∈ {0, 1}.
- `GetPlayerData(ctx, i)` = `*(ctx + (i + 0xBF) * 40)` @0x140A48A10, for i < 26.
- `GetPlayerDataBlock(roster, slot)` @0x140A3B110 (team-checked variant @0x140A3B140):
  - `idx = *(int*)(roster + 4 + 4*slot)`;
  - result = `roster + idx * 0x128 + 0xE0`.
  - Roster header: `+0` int team index (0/1); `+4..+0xA3` the 40-int slot→block index map.
- `ctx` = `*(u64*)0x1436F3DC8` (`gMatchContext`, Aoba's name; non-null only while a match is loaded).

**Block layout.**
- **Bytes +0x00..+0x3B: one byte per stat index**, typed by the clamp table `GetStatClampBounds` @0x1408CAF60 → table **@0x142613C60** (60 × {u8 min, u8 max}, OBS):

  | idx | range | meaning | evidence |
  |---|---|---|---|
  | 0x00 | 0..12 | registered position | OBS live (equals the familiarity A slot) |
  | 0x01..0x0D | 0..2 | familiarity per position, same order as 0x00 (GK,CB,LB,RB,DMF,CMF,LMF,RMF,AMF,LWF,RWF,SS,CF) | OBS live: block +1+pos = 2 for the registered position |
  | 0x0E | 0..19 | unknown | OBS range |
  | 0x0F | 0..6 | unknown | OBS range |
  | 0x10 | 0..2 | unknown | OBS range |
  | 0x11 | 0..9 | unknown | OBS range |
  | 0x12, 0x13 | 0..255 | unknown | OBS range |
  | 0x14..0x25 | 40..99 | 18 abilities | OBS range |
  | 0x26 | 0..3 | Aoba: "conditioning" | EXT. Live: 1 for 77/82-tier players, 3 for 88/99-tier |
  | 0x27..0x2D | 40..99 | 7 abilities | OBS range |
  | 0x2E | 0..7 | form | INF (live 3 or 7) |
  | 0x2F | 0..2 | injury resistance | INF |
  | 0x30 | 15..63 | age | OBS (live values plausible; same range as accessor 0x0D) |
  | 0x31 | 132..220 | height | OBS |
  | 0x32 | 30..129 | weight | OBS |
  | 0x33, 0x34 | 0..1 | foot/hand | INF |
  | 0x35, 0x36 | 0..3 | weak-foot usage / accuracy | INF |
  | 0x37..0x3B | 0..7, 0..4, 0..9, 0..4, 0..9 | motions | INF |

- **+0x40 u64 = skill bitmask; +0x48 u32 = playing-style bitmask.** Both use the *engine* bit order, which differs from the Player.bin id order; the full 41/21-bit tables are in Aoba. Spot check (OBS live): the first GK block has style bit 19 = Offensive GK and skill bits 30 and 35 = GK Penalty Saver and GK High Punt.
- **+0xE0 u32** = player id (OBS).
- +0x80.. holds strings: names from the save; not decoded here.

**Which ability is which in-match index.** This is only partially closed. Three OBS tables in .rdata assign every index to a modifier tier (§3.2):
- the Super-sub table @0x142632F74;
- the familiarity tier map @0x142632EB0;
- the condition tier map @0x142632CC0.

Combining their group memberships with Aoba's labels (OA = 0x14, Dribbling = 0x17, Speed = 0x27, PC = 0x28, Balance = 0x29, Acceleration = 0x2B, TP = 0x19, Aggression = 0x1F):

| group (all three tables agree) | indices |
|---|---|
| awareness + GK + KP + Acc | 0x14, 0x15, 0x16, 0x22, 0x23, 0x24, 0x25, 0x2A, 0x2B |
| technical | 0x17, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x20, 0x21 |
| speed + jump | 0x27, 0x2C |
| physique | 0x28, 0x29, 0x2D |
| not in the Super-sub table | 0x19, 0x1F (TP and Aggression, both added in PES2020) |

INF (high, from Aoba's listing order matching table order):
- 0x14 OA, 0x15 DA, 0x16 GK Aw;
- 0x17 Drib, 0x18 BC, 0x1A Fin, 0x1B LP, 0x1C LoP, 0x1D Head, 0x1E BW, 0x20 PK, 0x21 Curl;
- 0x22..0x25 GK Catch/Clear/Reflex/Reach;
- 0x27 Speed, 0x28 PC, 0x29 Bal, 0x2A KP, 0x2B Acc, 0x2C Jump, 0x2D Stamina.

Pairs that the tables can never separate: Speed↔Jump (0x27/0x2C) and KP↔Acc (0x2A/0x2B). Aoba's live labels are the only evidence for them.

A tried shortcut failed: the 4cc rosters in the live match have flat stats (all 25 ability bytes equal per player), so they cannot pin individual indices.

**Packed → block setter (OBS).** `@0x1412E4C60` copies 25 abilities from a 5-per-u32 packed struct into the block by calling a field setter `0x1414C6E90(obj, id, value)` with ids 0x0E..0x27 (0x20 skipped) = block index − 6. TP sits in an extra byte at +0x13 and Aggression in the low 6 bits at +0x14, consistent with their late addition. This packed struct is *not* `Player.bin`; it was not traced to its source.

### 1.4 Playing style enum (Player.bin id 0x40)

OBS:
- 32/32 fit for the values held by the reference players: 0, 1, 6, 9, 10, 11, 12, 13, 14, 15, 16, 20.
- Over all 17,782 players, the registered-position histogram of each value makes every slot unambiguous. Example: value 17 → 809 players, all GK. Value 21 → 113/117 fullbacks.

| value | style | value | style |
|---|---|---|---|
| 0 | none | 11 | Offensive Full-back |
| 1 | Goal Poacher | 12 | Defensive Full-back |
| 2 | Dummy Runner | 13 | Target Man |
| 3 | Fox in the Box | 14 | Creative Playmaker |
| 4 | Prolific Winger | 15 | Build Up |
| 5 | Classic No. 10 | 16 | Offensive Goalkeeper |
| 6 | Hole Player | 17 | Defensive Goalkeeper |
| 7 | Box-to-Box | 18 | Roaming Flank |
| 8 | Anchor Man | 19 | Cross Specialist |
| 9 | The Destroyer | 20 | Orchestrator |
| 10 | Extra Frontman | 21 | Full-back Finisher |

The engine uses a different bit order for its 21-bit style mask; see Aoba §"Playing Style Bitmask". Its position-compatibility masks @0x142613D20 are OBS present: `{0x180000, 0xD000, 0x70000, 0x3C00, 0x3700, 0x6D0, 0x712, 0x113, 0xF0, 0x0F}`.

### 1.5 Live EDIT player table

`pes-kit-assembly.md` §7.1 found a stride-0x17C table holding 4,830 records.
- The live table at 0x45770040, stride 0x17C, holds 4,830 records. **This is the EDIT-save player data, not Player.bin.**
- `+0` u32 id; `+4` u32 id copy; `+8` name (UTF-8, 61 B); `+0x45` and `+0x82` further 61/64-B name fields.
- From about +0xC0 it holds slot/number u16 pairs (0xFFFF = unused), and from about +0x14C packed ability bits.
- Tried and failed: neither the Player.bin bit layout nor the 4ccEditor EDIT bitstream appears at any bit shift of this record (§1.6), so the packed region was not decoded.
- Live roster stats did not need it: the in-match block of §1.3 already gives them.

### 1.6 Killed hypotheses (player records)

- **"Player.bin = 8 B prefix + 4ccEditor EDIT entry"** (as pes-db-generator's `player.py` implies). Killed: the 4cc field order at any bit shift gives at most 49 % of 7-bit fields in 40..99 (`fit.py`). Real abilities are 6-bit offset by 40, in a different order.
- **"Abilities are plain 7-bit values."** Killed: 7-bit windows with 97 % of values in 40..99 occur only at 9 scattered bit positions (`scan.py`).
- **"Skills are a contiguous 41-bit mask in engine order or Edit order."** Killed: no contiguous window matched 4 players with ≤ 3 bit errors. The skill bits are scattered over bits 287..531; the per-skill map is in §1.2.
- **"In-match index order = Player.bin id order with a gap at 0x26."** Killed: it would put BC and Drib (technical) at 0x15/0x16, but those indices are in the awareness tier in all three .rdata modifier tables.
- **"Live 0x17C record contains the Player.bin layout."** Killed: no shift reproduced the 46 live in-match blocks (abilities + height + age); `<scratch>/MatchLogicRE` session log.

---

## 2. Team, tactics, formation

### 2.1 `Team.bin` — 1532 B records (vanilla 663 teams)

OBS: 1,015,716 / 1532 = 663. Names sit at fixed in-record offsets.

| offset | meaning | evidence |
|---|---|---|
| +0x08 | u32 team id | OBS: 108 for the record whose English name field is a well-known club that pesmaster lists as team 108 |
| +0x00, +0x14, +0x1C, +0x30 | u32 ids | INF: manager/stadium/rival ids |
| +0x34..+0x4F | packed colours/flags | not decoded |
| +0x50 | long name, local script | OBS |
| +0x170 | long name, English (UTF-8) | OBS |
| +0x372 | short name (3 chars) | OBS |
| +0x3C2 | name, CJK script | OBS |
| +0x566 | second short name | OBS |

Team.bin holds **no** tactics.

### 2.2 `Tactics.bin` — 12 B records, 2 per team

Record: `{u32 tacticsId, u32 teamId, u32 packed}`. OBS: 1,326 records = 663 teams × 2. tacticsIds are consecutive from 6902; every team has exactly 2. INF: game plans/strategies A and B.

Packed word, bit 31 always 0 (`<scratch>/MatchLogicRE` session analysis: value ranges plus pairwise mutual information of bits):

| bits | range seen | INF meaning |
|---|---|---|
| 0..3 | 0..9 | slider 1..10 (support range / defensive line / compactness, order unknown) |
| 4..7 | 0..9 | slider 1..10 |
| 8..11 | 0..9 | slider 1..10 |
| 12..15 | {0..7, 9} | enum (INF: advanced instruction) |
| 16..18 | correlated block | enum |
| 19..20 | 0..2 | enum |
| 21..22 | 0..2 | enum |
| 23..30 | mostly independent single bits, some correlated with 20 and 25 | flags (INF: attacking style, build-up, attacking area, positioning, defensive style, containment, pressuring) |

- **Status:** field boundaries are OBS (statistics); semantic labels are INF (low). Unlike Player.bin there is **no** accessor table for this file: a scan of .rdata for any other `{id, fn, 0}` descriptor table found only the Player.bin one (OBS). Closing the labels needs the tactics loader decompiled, or one labelled sample (e.g. a team whose in-game tactics screen is known).
- **Modding:** sliders are 4-bit fields, so they can be edited in place.

### 2.3 `TacticsFormation.bin` — 12 B records, 33 per tactics id

OBS: 43,758 records = 1,326 × 33. Record: `{u32 tacticsId, u32 role, u32 packed}`.
- `role` uses the 0..12 position enum (GK, CB, LB, RB, DMF, CMF, LMF, RMF, AMF, LWF, RWF, SS, CF).
- `packed` byte 0 = **X** (depth from own goal line; GK 3, CB 11, FB 13, MF 25, FW 43; range 3..43).
- byte 1 = **Z** (lateral, 8..92; LB 16, RB 88, centre 52, so Z grows to the right).
- byte 2 = `set << 4 | slot` (slot 0..10, set 0..2).
- byte 3 = 0.
- The 33 records are 3 sets × 11 players. Only 123/14,586 player entries differ between sets. INF (med): set 0 = base shape, sets 1/2 = in-possession/out-of-possession ("fluid formation") variants.
- Grid → metres: not resolved. INF: grid units of roughly pitch-half-length/50 in X and pitch-width/100 in Z. Needs the in-match consumer (`teamData + 0x4C00` per Aoba, formation position indices).

### 2.4 Tactics in the match (EXT, addresses usable in this build)

| Aoba name | address | role |
|---|---|---|
| `teamData+308` | — | tactical mentality 0..4 |
| `teamData+596` | — | attack direction ±1 |
| `teamData+34652` | — | defensive line |
| `teamData+0x4C00` | — | per-slot formation position index (10-way merged L/R) used for style checks |
| TacticalEvaluator | `sub_1406006D0` | 9 tactical options; tier ≥ 5 only |
| DefensiveLineDepth / Width | `sub_1405F9EB0` / `sub_140688300` | difficulty-stepped offsets |
| PlayerRoleOverride | `sub_14062BF40` | role data from `TeamData + slot*64 + 620` |

### 2.5 Team AI tuning: `constant_team.bin` (format in §4.1)

**OBS decoded values** (alphabetical-layout rule, §4.1). They equal Aoba's "vanilla" values, so this install's dt18 is vanilla.

| file | values |
|---|---|
| `support.o` (240 B) | angleMax 45, angleMaxFront 90, distDash 12, distJog 2, distMax 20, distMaxAdjust 15, distRequestPass 3, distRun 4, lowerConnectionParam 50, spaceRectLength 6, supportDist 6, supportDistSituationBack 8, …Side 12, thicknessPillar 16, upperConnectionParam 70, waitTimeMax 1; `style` = offset → 20 zeros |
| `defenceMark.o` | normal / Box-to-Box pairs: markContinueDist 15/20, remainMarkX/ZLimit 10/13 and 12/18, zoneMarkX/ZLimit 15/18, checkBackFollowRemainDist 15/18, checkBackFollowZoneDist 1/2, checkZoneAreaRate 0.1/0.3, adjustDistBoxToBox 3, predictionFrame 20, addAngleKickOff 22.5 |
| `overlap.o` | addCount 1 / adverse 0; checkMiddleLineX −1 / −5; checkTopLineX −18 / −25 |
| `pairAnime.o` | pes15TestDecideTiming enable 1, maxSec 0.8; firstAttackEnd enable 0, paraDiff 10; stamina effect 0.5, max 0.8, min 0.2 |
| `defence.o` | predictionFrame 40 |
| `defenceCover.o` | futureFrame 60 |

- **`basePosition.o` (864 B), `diagonalRun.o`:** the PES18 mapping offsets are wrong for PES21 (killed). Example: it decodes transitionSec = 0.3 and marginSide = 40, while Aoba's vanilla values are 5 and 1. Not re-mapped here. Field names are in the exe (§4.1), so a re-map is mechanical.
- **How sliders reach these files (EXT).** Aoba: basePosition `pressRate`, `dfLine*`, `transitionSec`, `adjustXFromAttackAwareness3/4` (OA-dependent forward push 2.5 / 5 m); defence.o matchup limits by Defensive Awareness band (5 m / 8 m / +6 m / +12 m). The mapping from the user sliders (§2.2) to these fields is not traced.

---

## 3. How attributes enter gameplay

### 3.1 Per-frame live-stat pipeline (EXT, tables verified)

Live stat = `clamp(base × A1 × A2 × A3 × A4 × A5, min, max)`. Source: Aoba, `sub_14092AF10`, arrays at `+0xC798`, `+0xC888`, `+0xC978`, `+0xCA68`, `+0xCB58`, 60 floats each; clamp via `0x1408CAF60` / `0x142613C60` (OBS). The arrays:
- A1 = unknown, always 1.0;
- A2 = condition arrow;
- A3 = position familiarity;
- A4 = Super-sub;
- A5 = live stamina drain (gradual, physical stats first).

### 3.2 Modifier tables (OBS in .rdata — each is a mod target)

- **Condition arrows** @**0x142632D70**: 3 tiers × 5 arrows {Top, Great, Normal, Poor, Terrible}:
  - tier 0: 1.12, 1.06, 1.00, 0.92, 0.84;
  - tier 1: 1.09, 1.05, 1.00, 0.94, 0.88;
  - tier 2: 1.06, 1.03, 1.00, 0.94, 0.88.
  - Tier map `{u32 idx, u32 tier}` @**0x142632CC0**:
    - tier 0 = 0x14, 0x15, 0x16, 0x22, 0x23, 0x24, 0x25, 0x2A, 0x2B;
    - tier 1 = 0x17, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x20, 0x21, 0x27, 0x2C;
    - tier 2 = 0x28, 0x29 (+0x2D after the dump window).
  - **Discrepancy:** Aoba's prose puts Acceleration in the middle tier; the table puts 0x2B in tier 0. Either 0x2B is not Acceleration, or the prose is wrong.
- **Position familiarity** @**0x142632E68**: rows {C, B, A}:
  - tier 0: 0.80, 0.92, 1.00;
  - tier 1: 0.82, 0.94, 1.00;
  - tier 2: 0.84, 0.96, 1.00.
  - Tier map @**0x142632EB0**: tier 1 = 0x14, 0x15, 0x16, 0x22..0x25, 0x2A, 0x2B; tier 2 = 0x28, 0x29, 0x2D; tier 0 = the rest except 0x19 and 0x1F.
- **Super-sub** @**0x142632F74**: `{idx, f32}` × 23:
  - ×1.064 (0x3F883127) for 0x14, 0x15, 0x16, 0x22..0x25, 0x2A, 0x2B, 0x27, 0x2C;
  - ×1.048 for 0x17, 0x18, 0x1A..0x1E, 0x20, 0x21;
  - ×1.03 for 0x28, 0x29, 0x2D;
  - 0x19 and 0x1F absent.
  - Active when the player's match-period value is 1..4 (EXT: second-half sub or later).
- **Clamp** @**0x142613C60** (§1.3): abilities 40..99 after modifiers.
- Patching any of these floats changes the whole game's balance. They live in .rdata, so an exe patch (or a sider memory patch) is needed; none can be edited in dt18.

### 3.3 Skills and fatigue (EXT, `sub_140512CD0`)

- Fatigue threshold, as a percentage of stamina depleted:
  - 60 base;
  - 70 if the team captain has Captaincy (bit 25 of the engine skill mask);
  - +15 with Fighting Spirit (bit 27);
  - cap 100.
- Above the threshold the action-quality tier falls 3 → 2 (→ 1 when severely over). Tier floats 5 / 11 / 16 / 28 at **0x1425E4D64** (OBS present).
- The teammate-morale loop is dead code: it reads the wrong struct.

### 3.4 Duels (EXT; `sub_1405027A0` and the PES15 "Jostle" pipeline)

- Stat term: `(99 − clamp(stat, 50, 99)) / (49 / rand)`. Attacker stat at ability +20, defender at +21.
- Stacked multipliers: defending ×0.5; DELAY ×0.4/0.5; near the penalty area attacker ×0.2 / defender ×0.8; form ≥ 4 ×0.5; tier ≥ 4 ×0.5; …
- `pairAnime.o` stamina weighting 0.5 / 0.8 / 0.2 (OBS values above).

### 3.5 Stamina drain: `constant_match.bin` `pesSmart.o` (OBS)

`pesSmart.o` holds nested `{custom, value}` sub-objects reached through a list of 9 offsets at +0x270. Values in key order:

| key | value |
|---|---|
| contactTired | 0 |
| dashTired | 3 |
| defenceTired | 3 / 3 (own side) |
| dribbleTired | 2 |
| fallDownTired | 10 |
| jostleTired | 2 |
| metabolism (recovery) | 8 |
| speedTired | 1 |
| staggerTired | 3 |

Further `pesSmart.o` values (OBS, equal to Aoba's vanilla):
- GK advantage rates 1.7 / 1.5 / 1.3 (Defensive / Normal / Offensive GK);
- catchable speeds 85 / 70 / 60 and frontAdjustRate 1.1;
- punchable 110 / 80;
- FK adjust rates 0.8 / 0.4 with para 80 / 50 and specialist 80.

The drain formula itself, i.e. how the A5 multiplier follows from these, is not traced.

### 3.6 Other stat consumers with recovered numbers

- **trap.o** (EXT; OBS-present names in exe strings):
  - first-touch reaction 0.10 s at high stat vs 0.15 s at low stat (ground), 0.2 vs 0.3 (air);
  - stat range 50..95;
  - `ballControlWeekFootDownLimit` 20.
- **pesSmart freekick**: accuracy lerps 0.4 → 0.8 as the stat goes 50 → 80.
- **motivation.o** (EXT): effect_up 1.05, effect_down 0.95, driven by `teamEmotion.o` events (gauge −50..+50).
- **Units / time base (OBS).**
  - `GetGameSpeed()` @0x141E532D0 returns the float at **0x14351AE54 = 54.0**.
  - `@0x141E532F0` is the only writer; it also stores int **54** (0x36) at 0x14351AE50 (int getter @0x141E532E0).
  - The converter `sub_1404C8DC0` computes `x * c1 / (gameSpeed * c2)`. EXT names the constants 1000 / 3600, which makes it km/h → m per game frame.
  - Consequence: **one match-logic frame = 1/54 s of game time**. INF (med): the logic steps once per rendered frame (60 fps), so the simulation runs 60/54 ≈ 1.11× real time. Not timed live.
- **Kick/shot speeds** (`shoot.o`, EXT): normal-shot gauge 6 levels, max 120 (km/h); controlled max 98.

### 3.7 Difficulty → AI capability (EXT, tables verified)

BallPlayer tier = difficulty 0..6 (human teams forced to 5).
- Duration tables @**0x142BEF098** = 60, 40, 30, 20, 10, 0, 0 and @**0x142BEF0B8** = 40, 30, 20, 10, 5, 0, 0. Both OBS. The values are "think" frames scaled by `gameSpeed/60`.
- Gates at tier > 1 / ≥ 2 / > 2 / ≥ 3 / ≥ 4 / ≥ 5 / == 6; the full list is in Aoba.
- Mod targets: the two tables; the `sub_141FC09D0` missing-case-6 bug (Legend → Regular on tactical change, EXT).

---

## 4. Ball physics and the dt18 constant files

### 4.1 `common/match/constant/*.bin` format (OBS) — the main gameplay tuning surface

**Container.** WESYS zlib (`ff 10 51 57 'WESYS'`, u32 compressed / uncompressed sizes, zlib from +0x10).

**Table.** `u32 count, u32 tableOff(=8)`, then `count × {u32 dataOff, u32 size, u32 nameOff}`, then NUL-terminated names `xxx.o`, then 16-aligned blobs. This is the same record shape as `UniformParameter.bin`. Parser: `<scratch>/MatchLogicRE/tbl.py`.

**Files and object counts.**

| file | objects |
|---|---|
| `constant_match` | 27 (ball, cameraInplay/Setplay, command, cpuLevel, cursor, feintCommand, injury, inplayDemo, mlScreenShot, modeMatchup, pes15Test, pesSmart, rating, setplayGuide×6, teamEmotion, throwin, userPlayTendencyTest, animeAging×4) |
| `constant_player` | 36 (avoid, ballplayer*, block, centering, contact, dribble, feint, flypass, freekick, freemove, gk, goalKick, grounderpass, matchup, motivation, moveOnPass, passget, penaltykick, playStyle, press, reaction, shoot, sliding, tackle, throughpass, trap) |
| `constant_team` | 16 (basePosition, centeringGet, combination, defence, defenceCover, defenceMark, diagonalRun, lineBreak, overlap, pairAnime, pullAway, selectorVision, spaceRun, subConcept, support, teamId) |
| `constant_positionCK` | 10 corner-kick layouts (368 B each) |
| `constant_positionPK` | 5 penalty layouts (368 B each) |
| `constant_shootAging` | 21 `blockDebug*` |
| `constant_stadium` | demo areas |
| `constant_tutorial` | tutorial data |

**Blob layout rule.** Each `.o` is the compiled form of a dev-time JSON. The exe still contains the JSON paths: string region **0x142B49B40..** `DevelopData/common/match/constant/{match|player|team|positionCK|positionPK|shootAging}/<name>.json`. The key names follow in the same .rdata region, grouped per object, sorted alphabetically. Rules:
- each top-level key occupies 4 bytes, in **alphabetical key order** (int32, float32 or bool32);
- list/object values are stored as a **u32 offset** (from blob start) to a 16-aligned sub-array/sub-object placed after the scalar block.

Validation (OBS):
- `support.o`: 39 keys → values at the expected offsets, equal to Aoba's vanilla table; the list key `style` holds offset 0xA0 → 20 zeros;
- `defenceMark.o` and `pesSmart.o`: the same;
- `ball.o`: 66 keys × 4 B = 0x108, and the first list offset is exactly 0x110 (next 16-aligned).

To mod any field, take its key from the string region, compute the offset by this rule and patch the dt18 cpk. A key-list dumper: `<scratch>/MatchLogicRE/cstrings.txt`, strings 0x142B49000–0x142B60000.

### 4.2 `ball.o` (1040 B) — complete decode (OBS)

Full list: `<scratch>/MatchLogicRE/ball_o.txt`. Lists have 6 entries, plus 2 pad floats per 32-B block.

| key | value | key | value |
|---|---|---|---|
| airRegistNormal | 1.0 | grounderBoundYAdd | 0.09 |
| backSpinBuoyancyRate | 1.0 | grounderCTan | 8.0 |
| backSpinLogRate | 7.0 | grounderFrictionRate | 0.985 |
| boundBoundSpeedMax / Min | 12.0 / 2.0 | grounderSpeed | 20.0 |
| boundCheckBoundAddRate | [10, 5, 0, 0, 5, 5] | **magnusRate** | **0.035** |
| boundCheckFrictionAddRate | [0, 0, 0, 0, 7.5, 0] | natualBoundA | [0.055 ×6] |
| boundCheckRotationDecRate | 0 | natualBoundB | 0.4 |
| boundFrictionSpeedMax / Min | 25.0 / 10.0 | natunalRollBackSpinRate | [0.35, 0.4, 0.45, 0.2, 0.5, 0.4] |
| **boundRate** (restitution) | **[0.7, 0.7, 0.7, 0.5, 0.7, 0.7]** | naturalRoll | 2.0 |
| boundRollMax | 7.5 | nonSpinMax / Min | 105 / 45 |
| boundRotationSpeedMax / Min | 20 / 5 | nonSpinRate, TopDown/TopRiseDecRate | 1.0, 1.0, 1.0 |
| boundToRotationAddRateXZ | [0.62, 0.64, 0.66, 0.14, 0.62, 0.62] | overRotMax / Min | [1 ×6] / [−2 ×6] |
| boundToRotationAddRateY | [0.07 ×5, 0.03] | pitchCondition | 1 (bool) |
| boundToRotationBaseXZ | [0.13, 0.17, 0.21, 0.11, 0.2, 0.2] | roll | −2.0 |
| boundToRotationBaseY | [0.03 ×5, 0.02] | rollToBoundAdd | 0.07 |
| boundToRotationSpeedMaxXZ | [20, 20, 20, 20, 0.7, 25] | rollToBoundRollMax / Min | 7 / 2 |
| boundToRotationSpeedMaxY | [0.7 ×5, 30] | rollToSpeedMaxKPH | [18, 18, 18, 8.5, 18, 16] |
| boundToRotationSpeedMinXZ | [5, 5, 5, 5, 0.1, 3] | rollToSpeedMaxRoll | 8.5 |
| boundToRotationSpeedMinY | [0.1 ×5, 1.0] | rollToSpeedSpeedYMax / Min | 0.5 / 0.1 |
| curve | 0 (bool) | rollToSpeedSpeedYRateBase | [0.8, 0.8, 0.8, 0.4, 0.2, 0.8] |
| debugKickAngle / Speed | 3 / 35 | rotationCustom | 0 (bool) |
| **dragSpeedMax / Min** | **96.995 / 56.996** | speedCustom | 0.175 |
| frictionRollRateMax | [0.975, 0.972, 0.969, 0.99, 0.955, 0.983] | stopSpeed | 10.0 |
| frictionRollRateMin | [0.985, 0.982, 0.979, 0.98, 0.987, 0.983] | topSpinChangeSpeed | 1.0 |
| frictionRollRateStop | [0.975, 0.972, 0.969, 0.97, 0.987, 0.983] | topSpinDown / RiseDecRate | 1.0 / 0 |
| frictionRollSpeedMax | [25, 25, 25, 50, 8, 30] | gounnderprob (sic) | [0 ×6] |
| frictionRollSpeedMin | [8, 8, 8, 8, 8, 15] | | |
| frictionRollSpeedStop | [0, 0, 0, 0, 8, 0] | | |

**Interpretation.**
- The **6-entry lists are per pitch/weather condition** (INF high). Index 3 behaves like a wet pitch: lower bounce 0.5, faster skid (roll keep 0.99, roll-speed cap 50), lower spin pickup 0.14. Index 4 behaves like snow/heavy ground: friction 0.955, stop speed 8, extra friction on bounce 7.5. The order of conditions is unresolved (candidates: `BallCondition.bin` or the weather enum).
- Speeds are km/h (`rollToSpeedMaxKPH`, `dragSpeedMax ≈ 97`). Roll friction is a per-frame keep factor, lerped between the Min/Max rates by speed between `frictionRollSpeedMin/Max`. INF: the frame is the 1/54 s logic frame.
- **Gravity (OBS):** literal 9.8 @**0x1425ECC24** and 9.80665 @**0x1425A9678**.
  - Example consumer around `0x14080F2FD`: `g * 0.5 / gameSpeed / gameSpeed * n * n`, with 0.5 @0x142565600. That is s = ½ g t² with t = n / 54.
  - 9.8 has 9 code refs (0x1407E7CEF, 0x14080F302…, 0x14210B046 in BallPlayer); 9.80665 has 9 refs (0x1404AADF9, 0x140839D54, …).
- **No mass or density parameter** exists for the match ball (OBS: no `mass` key in ball.json; searched the ball key list and the physics `Ph` keys). The `Ph friction / restitution / density` strings near 0x142A50E00 belong to the generic Fox physics (ragdoll/props).
- **Spin/curl:** `magnusRate 0.035`, back-spin buoyancy and log rate; the Magnus force formula is in code and not traced.
- `pesdb/Ball.bin` (49 × 140 B `{u16 idx, u16 ballId, name[136]}`) and `BallCondition.bin` (8-B `{u16, u16, u32}` mapping records) contain **no physics** (OBS).

### 4.3 Killed hypotheses (constants)

- "pes-gameplay-editor `mappings/18` apply to PES21 unchanged." Killed for basePosition and diagonalRun; true for support, defenceMark, overlap, pairAnime, defence, defenceCover.
- "Ball physics comes from `Ball.bin` / `BallCondition.bin`." Killed: both hold names/ids only.

---

## 5. AI architecture map

Source: RTTI type names in the image (OBS; list `<scratch>/MatchLogicRE/rtti_match.txt`, 1,197 `match::` classes).

| Layer | Classes (RTTI) | Role |
|---|---|---|
| Team brain | `match::ai::Team`, `TeamMemberChange`, `ZoneDefenceManager`, `ai::Judge`, `registry::TeamAIInfoRef` | Per-team state, substitution logic, zonal defence assignment, situation judging |
| Team concepts | 47 × `match::ai::SubConcept*` (e.g. `SubConceptFastCounter`, `…ForeCheckPress`, `…RetreatBlockCreate`, `…SideOverlapAndSpaceRun`, `…TransitionChase`, `…DoublePress`) | Named team-level plans (attack, press, retreat patterns); tuned by `subConcept.o`, `combination.o` |
| Off-ball run choice | `match::ai::ActionSelectorManager` + 15 × `ActionSelector*` (ChanceSpaceRun, CounterSpaceRun, SecondLineSpaceRun, DiagonalRun, LineBreak, Overlap, OverlapCB, PostPlay, PullAway, GoalGet, CenteringGet, Score) | One selector per run type; each maps to a constant_team `.o` (spaceRun, diagonalRun, lineBreak, overlap, pullAway, centeringGet) and to AI action ids 29–47 |
| Player brain | `match::ai::PlayerAIBase` → `PlayerOffence`, `PlayerDefence`, `PlayerGKAuto`; `ai::Avoid`, `AvoidArea`, `AvoidPlayer`; `ActionMove`, `ActionBall`, `ActionInterface` | Off-ball decision per player; collision avoidance (`avoid.o`) |
| Ball carrier | `match::ai::bp::BallPlayer` + 26 × `bp::ThinkUnit*` (Shoot, Dribble, Feint, Clear, GKClear, PassForward/Long/Cross/EarlyCross/OneTwo/SideChange/Safety/Target/Special/BackSpace/GoalFront/DirectHighBall/RespondRequest, SetPlay, OnePattern…) + 11 × `bp::ImageUnit*` (Basic, Breakthrough, Cross, CurveShot, Long, MiddleShot, PassThrough, ShotPassFromGoal, SideChange, BackDFLine) + `bp::Intention` | Think units score candidate actions; image units are "play images" (intent templates). Answer adopted per Aoba's two-branch MainEval (`sub_142110D30`) |
| Action execution | 112 × `match::player::Action*` (e.g. `ActionDribble`, `ActionKeeperCatching`, `ActionFreeKick*`, `ActionCover`, `ActionDelay`, …) | Executes the chosen action id from the 126-entry table @**0x1434A4D50** (OBS: `NOTHING`, `BASE_POSITION`, `FREE_RUN`, `DRIBBLE`, `TRAP` … `TRAINING_MOVE_ROUTE`, then `PUSH`) with properties table @**0x1426484F0** (OBS first entries 0x1, 0x0, 0x1, 0x5, 0x4008, 0x400C) |
| Animation/motion | 137 × `match::anime::*` | Motion selection (AnimeTable/Mbinfo data, see the anim reports) |
| Referee | `match::referee::Referee`, `RefereeChief`, `RefereeLinesman`, `RefereeActionBasePosition`, `RefereeActionOutofPlay` | Officials' movement; card decisions run in the judge code (`sub_1405B7470`, EXT) |
| Data plumbing | 186 × `match::registry::*Ref` (`BallInfoRef`, `BallExternalForceRef`, `MatchInfoRef`, `MatchEnvRef`, `SituationInfoRef`, `RandomInfoRef`, `TeamAIInfoRef`, …), `match::situation::Loader`, `match::ai_selector::File / ArchiveFile` | Blackboard-style shared state between subsystems; `ai_selector` loads selector data files |

**Tick structure** (EXT + OBS constants):
- Logic frame = 1/54 game-second (OBS gameSpeed).
- Every frame: stat pipeline, duel processor (all 22 players), action-state updates.
- BallPlayer: branch A (kick gate, action-property bits 16/17) waits `table[tier] × gameSpeed/60` frames. Branch B re-evaluates at most every ≈ gameSpeed × 0.1 ≈ 5.4 frames.
- TacticalEvaluator is throttled by a timestamp at `a1+112`. TacticalPressingTrigger uses a 32 × gameSpeed base timer.

Not measured live: per-class tick rates. Doing so would need breakpoints, which are forbidden here.

---

## 6. Match rules and flow

- **Pitch (OBS, live, 20:33 match)** `*(gMatchContext + 10920)`: 8 floats = 52.56 (half length), 34.06 (half width), 3.66 (goal half width), 2.44 (goal height), 47.06 (goal-area line), 9.16 (goal-area half width), 36.14 (penalty-area line), 20.12 (penalty-area half width). Equal to Aoba.
- **Phase / situation enums (EXT):**
  - `MODE` 0..7 (normal, kick-off, throw-in, goal kick, corner, FK, PK, endless);
  - `HALF_KIND` 0..10 (pre, 1st, HT, 2nd, ET, ET1, ET-int, ET2, —, PK shoot-out, end);
  - `PLAY_KIND` 0..6;
  - `min` u8 and `lossTime` u8 in SituationData.
  - Registration code at `0x140465F40` (OBS: reflection registrations of offsets 0x142..0x146 around 0x140466453).
  - Wall distances: 9.15 m; 10.15 m for kick-off/PK; 6 m for throw-ins.
- **Match length setting.**
  - OBS: a match-settings serializer (NETCLIENT/MENU message, around 0x1410AE0C7–0x1410AEA53) writes `match_time` as a **u16** read from `settings+0xD5A`.
  - Neighbours: `win_lose` u8 +0xD58, `stadium_id` u8 +0xD5C, `random_stadium` +0xD60, `time_zone` u8 +0xD64, `weather` u8 +0xD6C, `random_weather` +0xD6D, `match_skip` +0xD70, `season_num` u16 +0xD74, `competition_id` +0xD76, `fixture` +0xD77; earlier `gamemode` u8 +0x1A4, `coarch_mode` (sic) u8 +0x1A5, `vs_com` u32 +0x1A8.
  - **Open:** how the in-match clock maps `match_time` (minutes of real time per match) to the 90 game-minutes. Tried: string xrefs (`match_time` 1 ref, `lossTime` 1 ref = the reflection registration), and scanning the live context for a SituationData-shaped struct. No candidate matched; the struct base is unknown. Next step: decompile the callers of the `lossTime` registration owner, or diff two live snapshots one game-minute apart (read-only).
- **Stoppage time.** Only the `lossTime` u8 and the HUD strings (`losstime_min_`, `losstime_sec_`) are confirmed. The computation is not traced.
- **Referee strictness (EXT, Aoba).** No per-referee strictness: the foul thresholds are hardcoded (outside the box T1/T2/T3 = 40/80/110; inside 60/80/110). The DOGSO → red path is capped to yellow, so every red is a second yellow. Card suppression flag `*(gMatchContext+0x2C0)+118`: OBS live = 0 in this match.
- **Injuries:** `injury.o` (48 B) exists (mapped by pes-gameplay-editor); not decoded here.

---

## 7. Modding reference (exe and data locations)

| What | Where | Notes |
|---|---|---|
| Player.bin field accessors | table **0x143529930** (108 × 24 B), functions 0x141F0F530–0x141F105C0 | Bit offset, width and min/max literals for every player field; widening a field or changing a max starts here |
| Packed → in-match ability setter | **0x1412E4C60** → field setter **0x1414C6E90** | id = block index − 6 |
| Stat clamp | **0x142613C60** (60 × {min, max}), reader 0x1408CAF60 | e.g. raising the 99 cap |
| Condition arrows | **0x142632D70** values, **0x142632CC0** tier map | |
| Position familiarity | **0x142632E68** values, **0x142632EB0** tier map | |
| Super-sub | **0x142632F74** (23 × {idx, f32}) | add 0x19 / 0x1F to make TP / Aggression benefit |
| Style position compatibility | **0x142613D20** (10 × u32) | EXT patch: SS mask 0x113 → 0x313 enables Hole Player at SS |
| BallPlayer deliberation | **0x142BEF098**, **0x142BEF0B8** (7 floats each) | |
| Game-speed constant | init **0x141E532F0** (float from 0x14259D624 → 0x14351AE54, int 54 → 0x14351AE50) | Global time base of every speed/frame conversion. INF: changing it rescales the whole simulation |
| Gravity | **0x1425ECC24** (9.8), **0x1425A9678** (9.80665) | shared literals; check every xref before patching |
| AI action names / properties | **0x1434A4D50** / **0x1426484F0** | |
| gMatchContext | **0x1436F3DC8** | roster getters 0x140A489D0 / 0x140A48A10 / 0x140A3B110 |
| All dt18 tuning | `Data/dt18_all.cpk` → `common/match/constant/*.bin` | edit via the §4.1 rules; names come from the exe string region 0x142B49B40.. |
| Ratings, positions, skills | `pesdb/Player.bin` per §1.2 (4cc installs override via `download/4cc_01_base.cpk` and the EDIT save) | |

---

## 8. Status

| Question | Status |
|---|---|
| 1 Player attributes | **Closed for Player.bin.** Every field labelled except 0x2F, 0x30, 0x31, 0x32, 0x76, 0x77, 0x78, the motion selectors (INF) and the header ids at +0x00/+0x04/+0x14. In-match block: structure and ranges closed; ability index identities only group-closed (Speed/Jump and KP/Acc pairs rest on EXT). Live EDIT table: located, packed region not decoded |
| 2 Team tactics | **Partly closed.** Formation file fully laid out (grid → metres open). Tactics word: field boundaries only, semantics INF. Team AI constants: closed for 6 objects, basePosition/diagonalRun need re-mapping (mechanical) |
| 3 Attributes → gameplay | **Closed for the modifier pipeline** (tables OBS) and stamina/GK/FK constants (OBS). Per-action formulas (pass error, shot error vs Finishing/KP, speed → m/s curve) are not traced; the speed unit chain (km/h → m/frame, 54 fps) is closed |
| 4 Ball physics | **Closed for parameters** (ball.o fully decoded, gravity, time base). Condition-index order and integrator formulas (drag, Magnus, bounce) open |
| 5 AI architecture | **Map closed** (RTTI layers + Aoba call graph). Per-subsystem tick rates: EXT only |
| 6 Rules / time | Pitch and enums closed; referee closed (EXT). **Match-length → clock scaling and stoppage computation open** (tries listed in §6) |

Scripts and intermediate files: `<scratch>/MatchLogicRE/` (`tbl.py`, `acc.py`, `acc_out.txt`, `fieldfind.py`, `bitfind.py`, `parsepm.py`, `cdis.py`, `ball_o.txt`, `cstrings.txt`, `rtti_match.txt`, `pbin_table.md`).


> **Confirmed (ContactRE):** in-match stat indices 0x1a–0x1d are Finishing, Low Pass, Lofted Pass, Heading, by their use in code.
