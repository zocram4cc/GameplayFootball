"""Tests for turning a stadium's ground the right way up.

PES draws its landscape two-sided; this engine culls back faces. So a ground
sheet wound with its lit side underneath is not dim, it is absent - and what
shows through the hole is the sky fill that postprocess.frag paints below the
horizon. That is why Namek rendered flat green (its sky is `horizon 0.34 0.76
0.11`) while its ground texture is teal, and why st002 rendered blue-purple
against `horizon 0.055 0.055 0.169`. The ground was imported all along: the
Namek landscape is 9984 triangles with sound UVs and an opaque 4096x4096
texture, facing the wrong way.

This is the same trap as the advertising ring (`faces_away_from_pitch`) and the
centre-circle banner (`stadium_staff.wants_winding_flipped`), in the one place it
costs a whole landscape.

The threshold is measured. Area-weighted, st017's meshes separate cleanly:

    Namek            -0.953        the ground sheets, facing straight down
    Namek2 outline   -0.936
    Namek2           -0.935
    ---------------------------- nothing in between
    next mesh        -0.491        stands and props, closed volumes near zero
    ...
    highest          +0.636

Run: python3 -m unittest test_stadium_ground -v
"""

import unittest

import stadium_to_gf


def _quad(z, wound_up):
    """A flat sheet at height z, wound so its normal points up or down."""
    verts = [(-10.0, -10.0, z), (10.0, -10.0, z), (10.0, 10.0, z), (-10.0, 10.0, z)]
    faces = [(0, 1, 2), (0, 2, 3)] if wound_up else [(2, 1, 0), (3, 2, 0)]
    return verts, faces


class WhichWayAGroundFaces(unittest.TestCase):
    def test_a_sheet_wound_downward_is_flagged(self):
        verts, faces = _quad(0.0, wound_up=False)
        self.assertTrue(stadium_to_gf.faces_downward(verts, faces))

    def test_a_sheet_wound_upward_is_left_alone(self):
        verts, faces = _quad(0.0, wound_up=True)
        self.assertFalse(stadium_to_gf.faces_downward(verts, faces))

    def test_height_does_not_matter_only_facing(self):
        # Namek's landscape dips to 16 m below the pitch and rises 45 m above it
        verts, faces = _quad(-16.2, wound_up=False)
        self.assertTrue(stadium_to_gf.faces_downward(verts, faces))

    def test_an_upright_wall_is_not_a_ground(self):
        # a hoarding faces sideways; faces_away_from_pitch is what judges those
        verts = [(-10.0, 0.0, 0.0), (10.0, 0.0, 0.0), (10.0, 0.0, 5.0), (-10.0, 0.0, 5.0)]
        faces = [(0, 1, 2), (0, 2, 3)]
        self.assertFalse(stadium_to_gf.faces_downward(verts, faces))

    def test_a_closed_volume_is_not_a_ground(self):
        """A box's faces cancel out, which is why stands measure near zero."""
        v = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
             (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]
        f = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7),
             (0, 1, 5), (0, 5, 4), (2, 3, 7), (2, 7, 6),
             (1, 2, 6), (1, 6, 5), (0, 4, 7), (0, 7, 3)]
        self.assertFalse(stadium_to_gf.faces_downward(v, f))

    def test_geometry_with_no_area_is_not_flagged(self):
        self.assertFalse(stadium_to_gf.faces_downward([(0, 0, 0)], []))
        degenerate = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 0.0, 0.0)]
        self.assertFalse(stadium_to_gf.faces_downward(degenerate, [(0, 1, 2)]))

    def test_the_threshold_leaves_the_measured_gap_alone(self):
        """-0.8 sits in the gap between -0.935 and -0.491, and nothing is there."""
        self.assertEqual(stadium_to_gf.GROUND_FACING_LIMIT, -0.8)

    def test_a_mesh_in_the_gap_is_not_touched(self):
        """A sheet tilted enough to measure -0.5 is a roof or a ramp, not a ground."""
        verts = [(-10.0, -10.0, 0.0), (10.0, -10.0, 0.0),
                 (10.0, 10.0, 17.3), (-10.0, 10.0, 17.3)]
        faces = [(2, 1, 0), (3, 2, 0)]           # 60 degrees off horizontal
        self.assertFalse(stadium_to_gf.faces_downward(verts, faces))


if __name__ == "__main__":
    unittest.main()
