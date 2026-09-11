"""The director table's layout, pinned on a synthetic table built the way PES21
writes them (director_table.py's docstring): the state index sizes everything
after it, rows are ten pool offsets, the CSR after the flags says which rows a
state owns, and the per-row weight/flags/record arrays run parallel to the rows.

A wrong offset here does not fail loudly - it reads plausible garbage - which
is exactly how the tables went unread for the whole import.
"""

import struct
import unittest

import director_table


def build(states, rows, weights, flags, records):
    """-> bytes of a table with the given states (name -> [row indices]),
    rows ([layer names]), and parallel per-row arrays."""
    pool = ""
    ends = []
    name_at = {}
    for name in states:
        name_at[name] = len(pool)
        pool += name
        ends.append(len(pool))
    fdcs = sorted({layer for row in rows for layer in row})
    for f in fdcs:
        name_at[f] = len(pool)
        pool += f
    pool_bytes = pool.encode()

    body = b""
    index_off = 0x3c
    body += b"".join(struct.pack("<I", e) for e in ends)
    rows_off = index_off + len(body)
    for row in rows:
        layers = list(row) + [row[-1]] * (10 - len(row))
        body += b"".join(struct.pack("<I", name_at[l]) for l in layers)
    weights_off = index_off + len(body)
    body += b"".join(struct.pack("<f", w) for w in weights)
    flags_off = index_off + len(body)
    body += b"".join(struct.pack("<II", f, o) for f, o in flags)
    csr_off = index_off + len(body)
    body += struct.pack("<HH", 1, len(states))
    first = 1  # the CSR counts rows from 1
    for name in states:
        n = len(states[name])
        body += struct.pack("<HH", first, n)
        first += n
    body += struct.pack("<H", len(rows))
    body += b"".join(struct.pack("<5I", *r) for r in records)
    small_off = index_off + len(body)
    body += bytes([10, 10]) + b"\x03\x00\x00\x00" * len(rows)
    pool_off = index_off + len(body)

    header = [0] * 15
    header[6] = len(pool_bytes)
    header[7] = weights_off
    header[8] = index_off
    header[10] = flags_off
    header[11] = csr_off
    header[12] = small_off
    header[13] = pool_off
    return struct.pack("<15I", *header) + body + pool_bytes, rows_off


class DirectorTable(unittest.TestCase):
    def setUp(self):
        self.states = {
            "GOAL_CELEBRATE_0006": [0, 1],
            "GOAL_RUN_30_BANZAI": [2],
            "GOAL_HUGA_LV2": [],
        }
        self.rows = [
            ["goal_cmnCam_outM00.fdc", "goal_celebrate_0006_base.fdc"],
            ["goal_Effect_h.fdc", "goal_celebrate_0006_outH10.fdc", "goal_celebrate_0006_base.fdc"],
            ["goal_2018_run_30_banzai_Z_fromL.fdc", "goal_2018_run_30_banzai.fdc"],
        ]
        blob, self.rows_off = build(self.states, self.rows, [1586.4, 1390.6, 1366.2],
                                    [(0x3f, 1), (0x0, 3), (0x1, 1)],
                                    [(0x3000f01, 0x100, 0, 0, 0)] * 3)
        self.table = director_table.Table(blob, "synthetic")

    def test_states_and_rows_resolve(self):
        self.assertEqual(self.table.states, list(self.states))
        self.assertEqual(self.table.rows_offset, self.rows_off)
        self.assertEqual(self.table.row_total, 3)
        self.assertEqual([r["layers"] for r in self.table.rows], self.rows)

    def test_state_owns_its_rows_and_bases(self):
        self.assertEqual([r["layers"] for r in self.table.rows_of(0)], self.rows[:2])
        self.assertEqual(self.table.bases_of(1),
                         ["goal_2018_run_30_banzai_Z_fromL", "goal_2018_run_30_banzai"])
        self.assertEqual(self.table.rows_of(2), [])

    def test_parallel_arrays(self):
        self.assertAlmostEqual(self.table.row_weight[2], 1366.2, places=1)
        self.assertEqual(self.table.row_flags[1], (0, 3))
        self.assertEqual(self.table.row_records[0][0], 0x3000f01)

    def test_phase_from_name(self):
        phase = self.table.phase_of
        self.assertEqual([phase(n) for n in ("GOAL_START", "GOAL_RUN_40_PLANE", "GOAL_CELEBRATE_0032",
                                             "GOAL_HUGB_LV3", "GOAL_A_BEHINDGOAL01",
                                             "GOAL_C_FINISH_SUCCESS", "GOAL_END_", "GOAL_S_OWNGOAL_GK")],
                         ["start", "run", "celebrate", "hug", "approach", "finish", "end", "situation"])


if __name__ == "__main__":
    unittest.main()
