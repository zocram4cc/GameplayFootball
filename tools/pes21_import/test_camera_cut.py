"""Tests for the multi-generation .fdc/.canm cut-table parser.

PES16's camera-cut record is 8 bytes shorter than PES17-21's (276 vs 284
bytes, measured across every foul/goal pack in all three older installs -
see docs/PES_CUTSCENE_ARCHAEOLOGY.md). The fields canm_to_camtrack.py
actually reads - start frame, canm name, near/far - sit at the same offsets
in both, well inside the shorter record; only the trailing per-slot blend
array and frame-rate float that PES17 added are missing from PES16's. Before
this fix a 276-byte tag-0x06 record failed the exact-size gate silently, so
every PES16 camera cut - including `foul_injury_card_n01`, the no-card foul
shot no later version ships - decoded zero cuts and exported no frames.

Run: python3 -m unittest test_camera_cut -v
"""

import struct
import unittest

import camera_cut


def build_container(entries):
    """entries: [(data_bytes, name_bytes)] -> raw FDC-style container bytes."""
    count = len(entries)
    header_size = 4 + 12 * count
    data_blob = b"".join(d for d, _ in entries)
    name_pool_start = header_size + len(data_blob)
    table = b""
    name_pool = b""
    offset = header_size
    name_offset = 0
    for data, name in entries:
        table += struct.pack("<III", offset, len(data), name_pool_start + name_offset)
        name_pool += name + b"\x00"
        offset += len(data)
        name_offset += len(name) + 1
    return struct.pack("<I", count) + table + data_blob + name_pool


def build_camera_cut_record(size, start_frame=10, near=1.0, far=400.0, canm_name=b""):
    """A tag-0x06 record of exactly ``size`` bytes with real fields at PES's offsets."""
    buf = bytearray(size)
    struct.pack_into("<I", buf, 0x00, start_frame)
    struct.pack_into("<H", buf, 0x08, 0xFFFF)
    end = min(0x0C + len(canm_name), size)
    buf[0x0C:end] = canm_name[: end - 0x0C]
    struct.pack_into("<3f", buf, 0x90, near, far, 1.0)
    return bytes(buf)


def build_fdc(cut_records, cpk_path=b"common/demo/fixdemo/foul/cut_data/x.fdc"):
    """One nested cut-table container holding ``cut_records`` (raw bytes each),
    each named by the single tag byte 0x06 - the container format's own
    convention for a record's type."""
    cut_table = build_container([(rec, b"\x06") for rec in cut_records])
    return build_container([(cut_table, cpk_path)])


def build_object_record(slot, position, rotation=(0.0, 0.0, 0.0, 1.0),
                        fpk=b"cpk_dat/common/demo/fixdemoobj/x/x.fpk", gani=b""):
    """A tag-0x05 record with the fields at the offsets read off PES21's ent_obj packs."""
    buf = bytearray(camera_cut.RECORD_SIZES[camera_cut.TAG_OBJECT])
    struct.pack_into("<H", buf, 0x00, slot)
    struct.pack_into("<3f", buf, 0x04, *position)
    struct.pack_into("<4f", buf, 0x10, *rotation)
    buf[0x20:0x20 + len(fpk)] = fpk
    buf[0x120:0x120 + len(gani)] = gani
    return bytes(buf)


def build_object_fdc(records):
    cut_table = build_container([(rec, b"\x05") for rec in records])
    return build_container([(cut_table, b"common/demo/fixdemo/ent/cut_data/ent_obj_x.fdc")])


def build_fdc_with_camera(cut_record, canm_blob, canm_name):
    cut_table = build_container([(cut_record, b"\x06")])
    return build_container([
        (cut_table, b"common/demo/fixdemo/foul/cut_data/x.fdc"),
        (canm_blob, ("foul/canm/" + canm_name).encode()),
    ])


def _build_minimal_canm():
    """The smallest valid CANM: a correct header with zero channels and zero
    frames - enough to prove cut-to-camera name resolution without needing a
    full per-channel keyframe stream."""
    header = struct.pack("<HHIIIII", 1, 0xFF01, 0, 30, 1, 0x18, 0)
    track = struct.pack("<HHI", 0, 0xFF01, 0)  # channelCount=0, magic, pad
    return header + track


class ThePES16RecordSize(unittest.TestCase):
    def test_a_276_byte_camera_cut_decodes(self):
        rec = build_camera_cut_record(276, start_frame=10, near=1.0, far=300.0,
                                       canm_name=b"foul/canm/foul_injury_card_n01_cam1.canm")
        fdc = camera_cut.parse_fdc(build_fdc([rec]))
        self.assertEqual(len(fdc.cuts), 1)
        self.assertEqual(fdc.cuts[0].start_frame, 10)
        self.assertAlmostEqual(fdc.cuts[0].near, 1.0, places=4)
        self.assertAlmostEqual(fdc.cuts[0].far, 300.0, places=4)
        self.assertEqual(fdc.cuts[0].canm_name,
                          "foul/canm/foul_injury_card_n01_cam1.canm")

    def test_a_284_byte_camera_cut_still_decodes(self):
        rec = build_camera_cut_record(284, start_frame=11, near=0.5, far=400.0,
                                       canm_name=b"foul/canm/foul_injury_card_y01.canm")
        fdc = camera_cut.parse_fdc(build_fdc([rec]))
        self.assertEqual(len(fdc.cuts), 1)
        self.assertEqual(fdc.cuts[0].start_frame, 11)

    def test_the_276_byte_record_has_no_trailing_frame_rate_but_does_not_crash(self):
        # PES16 does not carry the frame-rate float PES17+ added at +0xF0; a
        # record this short should report the constant PES shoots at (30 fps)
        # rather than raising or reading past the end of its own buffer.
        rec = build_camera_cut_record(276)
        cut = camera_cut.CameraCut(rec)
        self.assertEqual(cut.frame_rate, 30.0)

    def test_a_size_that_matches_neither_generation_is_not_a_camera_cut(self):
        # Guards against the gate becoming so permissive it accepts noise.
        rec = build_camera_cut_record(200)
        fdc = camera_cut.parse_fdc(build_fdc([rec]))
        self.assertEqual(len(fdc.cuts), 0)


class TheNoCardFoulPackEndToEnd(unittest.TestCase):
    """The actual regression: PES16's no-card foul camera must reach
    canm_to_camtrack's timeline, not silently disappear."""

    def test_a_pes16_shaped_fdc_yields_a_nonempty_timeline(self):
        rec = build_camera_cut_record(
            276, start_frame=10, near=1.0, far=300.0,
            canm_name=b"foul_injury_card_n01_cam1.canm")
        blob = build_fdc_with_camera(rec, _build_minimal_canm(), "foul_injury_card_n01_cam1.canm")
        fdc = camera_cut.parse_fdc(blob)
        self.assertEqual(len(fdc.cuts), 1)
        self.assertEqual(len(fdc.cameras), 1)
        timeline = fdc.timeline()
        self.assertEqual(len(timeline), 1)
        cut, cam = timeline[0]
        self.assertIsNotNone(cam)


class ThePropStagingRecords(unittest.TestCase):
    """Tag 0x05 is where PES stands its walkout furniture; the national flags'
    pack carries two of them, one either side of the halfway line."""

    def test_both_flags_come_out_with_their_marks_and_names(self):
        fdc = camera_cut.parse_fdc(build_object_fdc([
            build_object_record(200, (-7.0, 0.025, 32.0),
                                fpk=b"cpk_dat/common/demo/fixdemoobj/banner_nationalflag_home/"
                                    b"banner_nationalflag_home.fpk"),
            build_object_record(201, (7.0, 0.025, 32.0),
                                fpk=b"cpk_dat/common/demo/fixdemoobj/banner_nationalflag_away/"
                                    b"banner_nationalflag_away.fpk"),
        ]))
        self.assertEqual([o.slot for o in fdc.objects], [200, 201])
        self.assertEqual([o.stem for o in fdc.objects],
                         ["banner_nationalflag_home", "banner_nationalflag_away"])
        for got, want in zip(fdc.objects[0].position, (-7.0, 0.025, 32.0)):
            self.assertAlmostEqual(got, want, places=6)
        self.assertEqual(fdc.objects[0].gani_path, "")
        self.assertAlmostEqual(fdc.objects[0].yaw, 0.0)

    def test_a_back_staged_prop_is_turned_half_a_circle(self):
        fdc = camera_cut.parse_fdc(build_object_fdc([
            build_object_record(85, (0.0, 0.025, -27.5), rotation=(0.0, 1.0, 0.0, 0.0))]))
        self.assertAlmostEqual(abs(fdc.objects[0].yaw), 3.14159, places=4)

    def test_an_animated_prop_names_its_clip(self):
        gani = b"cpk_dat/common/demo/anime/FoxAnim/FixDemo/Animations/dml_prop_circleflag_uefa_cl_01_anm.gani"
        fdc = camera_cut.parse_fdc(build_object_fdc([
            build_object_record(80, (0.0, 0.0, 0.0), gani=gani)]))
        self.assertEqual(fdc.objects[0].gani_path, gani.decode())

    def test_a_record_of_the_wrong_size_is_not_an_object(self):
        short = build_object_record(80, (0.0, 0.0, 0.0))[:-8]
        fdc = camera_cut.parse_fdc(build_object_fdc([short]))
        self.assertEqual(fdc.objects, [])


if __name__ == "__main__":
    unittest.main()
