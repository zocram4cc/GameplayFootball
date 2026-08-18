"""Tests for the ftex decoder, and for the pair of fields it had backwards.

FTEX stores its dimensions width first and height second. Reading them the other
way round is invisible on a square texture - which most of PES's are - and shears
every other one into what looks like interlacing: st002's pitch crest came out as
two hatched half-crests side by side, and the ad boards had the same look. The
block data is fine; only the shape it is poured into was wrong.

A file to check it against: st002's pitch_scratch_bsm_alp.ftex is 2048 x 1024
(landscape, two crests painted at the goalmouths) and 2,097,152 bytes of BC3, one
byte a pixel.

Run: python3 -m unittest test_ftex -v
"""

import struct
import unittest
import zlib

import ftex


def _ftex(width, height, pixel_format=4, payload=b"\0" * 64):
    """A minimal one-mip FTEX around `payload`, laid out as the format has it."""
    header = struct.pack("<4sfHHHHBBH", b"FTEX", 2.03, pixel_format, width, height, 1, 1, 2, 0x11)
    header += struct.pack("<I", 0)          # 0x10: unused
    header += struct.pack("<I", 1)          # 0x14: texture type
    header += struct.pack("<I", 1)          # 0x18: (parse reads type here too)
    header += b"\0" * (0x20 - len(header))
    header += bytes([0])                    # 0x20: ftexs count
    header += b"\0" * (64 - len(header))
    compressed = zlib.compress(payload)
    # the data follows the 64-byte header and this 16-byte mip info
    mip = struct.pack("<IIIBBH", 80, len(payload), len(compressed), 0, 0, 0)
    mip += b"\0" * (16 - len(mip))
    return header + mip + compressed


class Dimensions(unittest.TestCase):
    def test_width_comes_first_in_the_header(self):
        _pf, width, height, _depth, _type, _frames = ftex.parse(_ftex(2048, 1024))
        self.assertEqual((width, height), (2048, 1024))

    def test_a_portrait_texture_is_not_turned_on_its_side(self):
        _pf, width, height, _depth, _type, _frames = ftex.parse(_ftex(512, 4096))
        self.assertEqual((width, height), (512, 4096))

    def test_a_square_one_reads_the_same_either_way(self):
        # which is why this went unnoticed: most of PES's textures are square
        _pf, width, height, _depth, _type, _frames = ftex.parse(_ftex(1024, 1024))
        self.assertEqual((width, height), (1024, 1024))

    def test_the_dds_it_writes_says_the_same(self):
        dds = ftex.to_dds(_ftex(2048, 1024, payload=b"\0" * (2048 * 1024)))
        dds_height, dds_width = struct.unpack_from("<II", dds, 12)
        self.assertEqual((dds_width, dds_height), (2048, 1024))

    def test_something_that_is_not_an_ftex_is_refused(self):
        with self.assertRaises(ValueError):
            ftex.parse(b"NOPE" + b"\0" * 100)


if __name__ == "__main__":
    unittest.main()
