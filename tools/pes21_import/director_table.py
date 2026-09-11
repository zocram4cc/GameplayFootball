"""PES's cutscene director tables: `common/demo/fixdemo/<cat>/table_<cat>.bin`.

Each `.fdc` under `cut_data/` is ONE state of a scene - its cameras, actor
placements and props. What assembles a scene - which states exist, which
shot each may use, under which conditions - is this table, one per category
(goal, ent, foul, change, end, timeup, pk, result, mode). Nothing in the
import read it, which is why every imported clip could be faithful and the
goal still look nothing like PES: we had the shots and not the director.

Layout (PES21; foul is 12 KB and was the Rosetta, the others share it):

    0x00  u32[15]  header. [6] pool length, [7] per-row weights, [8] = 0x3c
                   start of the string index, [10] row flags, [11] the
                   state -> rows table, [12] per-row bytes, [13] string pool.
    0x3c  u32[nStates]  END offset of each state name in the pool (the .fdc
                   names and the transition-target names that follow them in
                   the pool are not indexed; they are found by shape).
    ...   u32[rows*10]  the shot rows: 10 pool offsets each - the .fdc for
                   actor slots 0-8 and, in slot 9, the camera .fdc. Most
                   rows name one .fdc ten times; an injury row names the hurt
                   player's own .fdc in one slot.
    [10]  (u32 flags, u32 bool)[]  per row: a condition mask and a flag.
    [11]  u16 version(1), u16 nStates, then (u16 firstRow, u16 count) per
                   state - which rows a state may draw from, firstRow counted
                   from 1 - then u16 rowTotal and a 20-byte record per row.
    [12]  small per-row bytes.
    [13]  the string pool, unterminated strings back to back.

    director_table.py <table.bin>              summary
    director_table.py <table.bin> --states     every state with its rows
    director_table.py <table.bin> --graph      states -> the .fdc bases they use
"""

import struct
import sys

SLOTS_PER_ROW = 10  # actor slots 0-8, camera in slot 9
ROW_RECORD_BYTES = 20


class Table:
    def __init__(self, blob, path=""):
        self.path = path
        self.blob = blob
        h = struct.unpack_from("<15I", blob, 0)  # 0x3c bytes; the index starts right after
        self.header = h
        pool0 = h[13]
        self.pool = blob[pool0:pool0 + h[6]].decode("ascii", "replace")

        # State -> rows comes first: its state count sizes the string index.
        a = h[11]
        version, n_states = struct.unpack_from("<HH", blob, a)
        # String index: the END offset of each state name, so the pool splits exactly.
        ends = [struct.unpack_from("<I", blob, h[8] + 4 * k)[0] for k in range(n_states)]
        starts = [0] + ends[:-1]
        self.strings = [(s, self.pool[s:e]) for s, e in zip(starts, ends)]
        self.string_at = dict(self.strings)
        # The index stops after the state names. The .fdc names that follow are
        # back to back with no separator, so each one starts where the previous
        # ended and runs to its own ".fdc" (a regex from the left would swallow
        # the tail of the name before it).
        pos = ends[-1] if ends else 0
        while True:
            end = self.pool.find(".fdc", pos)
            if end < 0:
                break
            self.string_at[pos] = self.pool[pos:end + 4]
            pos = end + 4
        self.fdc_names = sorted(set(v for v in self.string_at.values() if v.endswith(".fdc")))

        self.version = version
        self.state_rows = [struct.unpack_from("<HH", blob, a + 4 + 4 * k) for k in range(n_states)]
        self.states = [name for _, name in self.strings[:n_states]]
        after = a + 4 + 4 * n_states
        self.row_total = struct.unpack_from("<H", blob, after)[0]
        self.row_records = [struct.unpack_from("<5I", blob, after + 2 + ROW_RECORD_BYTES * k)
                            for k in range(self.row_total)]

        # The shot rows themselves, straight after the string index. A row is a
        # STACK of up to ten .fdc layers, left to right: a camera-only file
        # (goal_cmnCam_outM00, ..._Z_fromL), an actor variant, an effect
        # (goal_Effect_h), then the state's _base padding the rest. Playing a
        # row means loading every distinct layer together.
        self.rows_offset = h[8] + 4 * n_states
        self.rows = []
        for k in range(self.row_total):
            vals = struct.unpack_from("<%dI" % SLOTS_PER_ROW, blob,
                                      self.rows_offset + 4 * SLOTS_PER_ROW * k)
            names = [self.string_at.get(v, "?%x" % v) for v in vals]
            layers = []
            for name in names:
                if name not in layers:
                    layers.append(name)
            self.rows.append({"layers": layers, "actors": names[:9], "camera": names[9]})

        # Per-row weight (h7, f32; the run/celebrate rows carry clip lengths in
        # frames at 30 fps, e.g. 1366 for a 45 s run), condition flags (h10:
        # situation mask + small enum), and the 5-u32 record after the CSR
        # whose first word packs zone / shot index (0x22 01 0e 00: byte 2 is
        # the pitch quadrant the cmnCam_S_{L,R}{B,M} names spell out).
        self.row_weight = list(struct.unpack_from("<%df" % self.row_total, blob, h[7]))
        n_flags = (h[11] - h[10]) // 8
        self.row_flags = [struct.unpack_from("<II", blob, h[10] + 8 * k) for k in range(n_flags)]

    def rows_of(self, state_index):
        # The CSR counts rows from 1: with 0 the last state of every table
        # landed on the sentinel row that points at the label list, and nine
        # of thirty foul states named the wrong shot.
        first, count = self.state_rows[state_index]
        return self.rows[first - 1:first - 1 + count]

    def bases_of(self, state_index):
        """The distinct .fdc bases (no extension) a state draws from."""
        out = []
        for row in self.rows_of(state_index):
            for name in row["layers"]:
                base = name[:-4] if name.endswith(".fdc") else name
                if base not in out:
                    out.append(base)
        return out

    def phase_of(self, state_name):
        """Where a state sits in the fixed director walk, from its name.

        The tables hold no edges: the order of phases is the engine's, and the
        choice within a phase is the data. The goal walk PES plays is
        START -> RUN -> CELEBRATE -> HUG -> A (fence/flag/camera) -> C (finish)
        -> END, with the S_ states as situation overrides (own goal, offside,
        golazo, assist).
        """
        stem = state_name.split("_", 1)[1] if "_" in state_name else state_name
        for prefix, phase in (("START", "start"), ("RUN_", "run"), ("CELEBRATE_", "celebrate"),
                              ("HUG", "hug"), ("A_", "approach"), ("AC_", "approach"),
                              ("C_", "finish"), ("END", "end"), ("S_", "situation"),
                              ("I_", "insert"), ("F_", "finish")):
            if stem.startswith(prefix):
                return phase
        return "other"


def load(path):
    with open(path, "rb") as f:
        return Table(f.read(), path)


def summary(table):
    h = table.header
    print("%s: %d bytes, %d strings, %d states, %d rows (version %d)" % (
        table.path, len(table.blob), len(table.strings), len(table.states), table.row_total,
        table.version))
    print("  %d .fdc names" % len(table.fdc_names))
    bad = sum(1 for r in table.rows for n in r["actors"] + [r["camera"]] if n.startswith("?"))
    print("  unresolved row entries: %d" % bad)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    table = load(argv[1])
    summary(table)
    if "--states" in argv:
        for i, name in enumerate(table.states):
            first, count = table.state_rows[i]
            print("%-40s rows %d..%d" % (name, first - 1, first + count - 2))
            for k, row in enumerate(table.rows_of(i)):
                r = first - 1 + k
                flags, on = table.row_flags[r] if r < len(table.row_flags) else (0, 0)
                print("    %-8.1f flags %08x %-6d rec %-10x  %s" % (
                    table.row_weight[r], flags, on, table.row_records[r][0],
                    " + ".join(row["layers"])))
    if "--graph" in argv:
        for i, name in enumerate(table.states):
            print("%-10s %s: %s" % (table.phase_of(name), name, " ".join(table.bases_of(i))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
