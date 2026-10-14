# Cutscene slot → player mapping (FixDemoMatch creation path)

Tags: **OBS(address)** = read in decompile/disasm at that VA. **INF(level)** = inferred.
Image base 0x140000000 (`pes_full.bin`). Decompiles in the CutsceneSlotRE scratch dir
(`cutslot_*.c` / `cutslot_*_ann.c`, annot.py-folded). Every branch below was
cross-checked against raw disassembly (`pdis.py` on PES2021.exe).

## 0. Correction to the ticket premise

**KILLED:** "`FUN_141f5e630` is the FixDemoHuman creation path." OBS
(`cutslot_5e630.c`, vtable 0x142ba6bc0 index 4): it is a small
`(mgr, int, int, int*)` step-state function (param_2 switch on 2,3,4,5,6,7 writing
`*param_5`/`param_5[4]` constants). It never allocates, never touches the
0x78/0xa8 slot lists, never calls the Human factory. The real creation path is:

```
FixDemoMatch vtbl idx14  FUN_141f5f330  (slot-list build, per-category dispatch)
FixDemoMatch vtbl idx16  FUN_141f5e930  (tail calls FUN_141f57840)
  FUN_141f57840  iterates mgr+0x78 8-byte slot entries, one FixDemoHuman each
    FUN_141f577b0  (kind switch → FUN_141f57a00 / FUN_141f57e60 /
                     FUN_141f58160 / player path; factory FUN_141f7d010 +
                     init FUN_141f7de00; node stored in mgr+0x60 map)
FixDemoMatch vtbl idx15  FUN_141f5f200  (0xa8 actor-id list → slot remap)
  FUN_141f563e0 / FUN_141f56a30 → FUN_141f79f10 (slot swap between map nodes)
```

## 1. Slot-entry layout (consumer-anchored, OBS)

Each mgr+0x78 entry is 8 bytes: `[u32 actorId][u8 kind][u8 side][u8 slot][u8 flag]`.
Field roles are fixed by the consumers, not by guesswork:

- `entry[4]` = kind. OBS `FUN_141f577b0` (raw 0x141f577b7–0x141f577d3:
  `movzx edx,[rdx+4]; cmp dl,2; je player` / `cmp al,9; je 57e60` /
  `sub al,0xa; cmp al,6; ja 58160` / else `57a00` with `[r9+6],[r9+5]` loaded).
- `entry[5]` = side (0/1). OBS: `FUN_141f56bb0`/`FUN_141f56c70` filter
  `pcVar2[5] == side`, then use `pcVar2[6]` as the ctx P-index:
  `*(ctx+0xf8+side*0x2830+slot*0x100)`; `FUN_141f56900` same with
  `(slot+1)*0x100`; `FUN_141f57a00` normal path reads
  `P(param_4, param_5)` exactly this way (raw 0x141f57abb–0x141f57ad4:
  `imul rax,0x2830; add rax,0xf8; add rax,rdi(ctx)`).
- `entry[6]` = ctx player slot. OBS: `FUN_141f5f610` 0x70026/27 case writes
  `entry[6] = ctx+0x5218` (event player slot); `FUN_141f577b0` player path passes
  it as the P-index for the Human factory input.

## 2. Kind → player rule (OBS `FUN_141f577b0`)

| kind (`entry[4]`) | player bound | addresses |
|---|---|---|
| 2 | ctx `P(side,slot)` = the match player record at `ctx+0xf8+side*0x2830+slot*0x100` (slot 0 = starting GK; 0–10 starters; 11+ subs/reserves). Human built from that record (name/number/appearance copied in `FUN_141f57a00`-style init) | 0x141f577b0 player branch; P-addr raw 0x141f57abb–d4 |
| 9 | preset/official path `FUN_141f57e60(mgr, actorId, 9, side)` | 0x141f577cd `je 0x141f5780e`; 0x141f57e60 |
| 10–16 (0xa–0x10) | `FUN_141f57a00(mgr, actorId, kind, side, slot)`: slot 100 (0x64) or 101–104 (0x65–0x68) read dedicated ctx records (`ctx+0x5184/0x5198/0x51a8/0x51b8/0x51c8` + `FUN_14195c580`); slot 0x68 variant; any other slot < 0x28 = normal `P(side,slot)` with role bytes derived from `P+0x41`, `P+8`, `P+0x8e/0xa4` | 0x141f57a00; raw special-id chain 0x141f57a8a–0x141f57ab1 (`sub eax,0x64; je …; sub eax,1; je …` ×3) |
| anything else (0,1,3–8) | `FUN_141f58160`: side-swap remap — requested side 0/1/3/4/5/6/7/8 is flipped to 1/0/4/3/6/5/8/7 when `param_4` (mgr side context) mismatches, with remap flags; then normal `P(side,slot)` bind | 0x141f58160 switch; raw `ja 0x141f577ed` fallthrough |

INF(med): kind 2 = outfield/XI players, 9 = referee/preset official, 10–16 =
role slots (keeper/coach/special ctx records), others = mirrored/generic extras.
The exact role of ctx records 0x5184+ is NOT decoded (open, §4).

## 3. Per-scene-kind producers (OBS `FUN_141f5f330` + raw)

`FUN_141f5f330` reads byte `request+2` and dispatches (raw 0x141f5f3d1–0x141f5f44f:
`cmp r11d,5 → 5f480`; `cmp 2 → 5fa90`; `cmp 3 → 5fc00`; `cmp 4 → 60140`;
`cmp 6 → 60590`; `cmp 7 → 5f610`; else `561a0`). INF(hi): `request+2` is the
**table category** (1 ent … 9 mode, cf. cutscenes §2): builders exist exactly for
cats 2,3,4,5,6,7; cats 1/8/9 fall through to the default `FUN_141f561a0`
(plain XI fill: side0 slots 0–10 then side1 slots 0–10). The bitmask each builder
receives is `OR over play-steps of *(step+0x3c)` (raw 0x141f5f3b0–0x141f5f3bd:
`imul rax,r8,0x1e0; or r9d,[rax+rsi+0x3c]`), stride 0x1e0 = expanded-step size —
INF(med-hi): bit *i* = fdc Human slot *i* present in this step, so only cast
slots get bindings.

- **cat 5 (change/substitution) → `FUN_141f5f480`**: emits 0x78 entries with
  actorIds `0x1000+bit` for bits {1–6, 9, 10, 11–13, 0x16, 0x17, 0x19} of the mask
  (OBS switch; bits 0x1000/0x1007/0x1008/0x1014/0x1015/0x1018/0x101a skipped).
  Per-bit (side,slot) pairs OBS: `0x1001–0x1006:(0,1–6)`, `0x1009:(0,7)`,
  `0x100a:(0,8)`, `0x100b:(1,3)`, `0x100c:(1,1)`, `0x100d:(1,2)`;
  bits 0x16/0x17 add `(0x16,side,100)`-style official entries via the
  `FUN_141f56800` 7-arg variant (thunk to 0x15b59a660 — Denuvo area, treat as
  opaque). Note the asymmetry (home 1–8, away 3/1/2) is OBS, not a typo.
- **cat 2 (foul/card) → `FUN_141f5fa90`**: actorIds `0x1100+bit`, bits
  {1–5, 0xb–0xe}; pairs OBS: `0x1101:(0,1)`, `0x1102:(0,0)`, `0x1103:(0,2)`,
  `0x1104:(0,3)`, `0x1105:(0,4)`, `0x110b:(1,1)`, `0x110c:(1,2)`, `0x110d:(1,3)`,
  `0x110e:(1,4)`; then `0x1116`-family official entries. The 0xa8 remap
  (`FUN_141f5f200`) converts these to fdc slots: kind-2 byte → `slot+0x16`;
  kind<2 → `side*0xb+slot` if slot<0xb else `slot+0xd` (OBS 0x141f5f200).
- **cat 3 (goal) → `FUN_141f5fc00`**: actorIds `0x1300+bit`; bit 0 → `(side,10)`,
  bit 7 → `(side,0xb)`, bit 8 → `(side,0xc)` (OBS); bits 1–6,9–11 map to
  descending slots 9,8,7,6,5,4,2,1,0 (OBS switch); bits 0xc–0x12,0x15 use
  `side^1`-when-`<2` flip (OBS `*(mgr+0x38)<2 ? …==0` raw pattern at
  0x141f5f68f–0x141f5f6a0); bit 0x18 sets the 0x1318/goal-context entries
  (scorer/keeper check `(*P+8>>0x20 & 0x800fffff)==0x9da0 ? 7 : 3`, OBS).
- **cat 4 (pk/penalty) → `FUN_141f60140`**: actorIds `0x1500+bit`, straight
  `(0,0–10)` then `(1,0–10)` for bits 0–0x15, officials 0x16–0x18 (OBS).
- **cat 6 (timeup) → `FUN_141f60590`**: actorIds `0x1600+bit` with a permuted
  slot order OBS: home `10,3,4,5,6,7,8,9,0,1` then GK-slot 2; away same;
  bit 0x19 additionally binds `(eventSide+3, eventSide, eventSlot)` on 0x78 and
  `(3,0,0x19)` on 0xa8 (OBS; `ctx+0x5214/0x5218` read).
- **cat 7 (end) → `FUN_141f5f610`**: keyed by scene id, not mask bits (OBS
  switch on `param_2`): `0x70012/0x70013/0x7001b`, `0x7001d/0x70021–0x70025`,
  `0x7001e/0x7001f/0x70020/0x70024`, `0x70026/0x70027` (event slot written into
  entry[6], OBS), default. Common shape: `(0,side,0–10)` ×11 plus
  `(0x10,side==0,5–10)` ×6, then referee/keeper tail `(0xb–0xe…)` gated on
  `ctx S(s)+0x2819 >= 0x10` (OBS; raw `mov r8d,[rsi+0x38]` side loads at
  0x141f5f673/0x141f5f68f confirmed).
- **Side source**: every builder reads `mgr+0x38` as the default side
  (OBS ~20 reads; raw e.g. 0x141f5f673 `mov r8d,[rsi+0x38]`).
  `FUN_141f5be50` copies `mgr+0x38` into `DemoPlayerInfo+0x1c` (OBS).
  **The writer of `mgr+0x38` was NOT found** (open, §4).

## 4. What is still open (narrow)

1. **`mgr+0x38` writer** (who sets the subject side). Searched every decompiled
   FixDemoMatch body (ctor 0x141f5e360, base ctor 0x141f55cb0, 0x141f59050,
   0x141f571d0, 0x141f5a920, 0x141f5b9e0, 0x141f59b90, 0x141f5a0b0, 0x141f5c720,
   0x141f5e0a0, 0x141f57910): reads only, no store. **Next: decode vtable
   idx1/idx2 (`FUN_141f61210`, `FUN_141f61290`)** — the only undecoded
   FixDemoMatch methods that run before idx14 — or the event builders in
   0x14047xxxx that fill the DemoInfo.
2. **Exact `(A,B)→bytes` packing of `FUN_141f56600`** (6-arg) vs the thunk
   `FUN_141f56800` (7-arg, count=param_6/flag=param_7/kind=param_3/side=param_4/
   slotbase=param_5 — OBS from its loop). §3 tables quote the call args verbatim
   so they survive regardless; the byte mapping needs one careful pass over the
   stores at 0x141f56650–0x141f566e7 (already dumped, `lea eax,[rdi+r15]` id /
   `lea eax,[r14+rdi]` sequences). Per-bit (side,slot) pairs above are OBS call
   args, INF(med) as final entry bytes.
3. **0x78-entry bit for referee/keeper ids** (`0x16/0x19` 56800 calls) and the
   `FUN_141f56a30` triple layout (`[kind,u8,?]` at `info+0x1d`, 9 entries —
   arity mismatch in decompile, INF(lo)).
4. **ctx records behind 57a00 special slots** (`ctx+0x5184/0x5198/0x51a8/0x51b8/
   0x51c8` + `FUN_14195c580`) and the `(*P+8>>0x20 & 0x800fffff)==0x9da0`
   goal-keeper test in 5fc00 (OBS value, meaning unknown).

## 5. Killed hypotheses

- `FUN_141f5e630` = creation path — killed (§0; it is idx4 step-state).
- "0x78 entries are keyed identically to fdc slots" — killed: actorIds are
  per-category (`0x10xx/0x11xx/0x13xx/0x15xx/0x16xx/0x700xx`), remapped to fdc
  slots by `FUN_141f5f200`/`FUN_141f79f10` (slot-swap between map nodes, OBS).
- "request+2 is the T2 step kind" — killed: T2 kinds are {0,1,3,4} (cutscenes
  §2.3) but the dispatch tests {2,3,4,5,6,7}; INF(hi) it is the table category.
