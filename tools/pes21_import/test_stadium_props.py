"""Tests for the furniture PES stands around a pitch.

A stadium pack ships none of it. PES keeps one set in its own packages and hands
it to every ground - which is why every 4cc pack's audi, cheer, standsFlag,
scarecrow and tv sub-packs are 48-byte stubs - so the corner flags, the fourth
official's board, the television cameras, the barrier at the tunnel mouth and the
paramedics all live in base PES21 (dt12_g4.cpk: common/demo/prop and
common/demo/fixdemoobj). This engine has never had any of it: there is not even a
corner flag in media/objects.

What can be imported is decided by whether the skins are installed. The objects
are complete - gadget_cornerflag is 263 vertices and fully textured - while the
stock-kit humans (stewards, press, the television crew) ask for clothing textures
no archive here carries, the same gap as the staff coach kit, so they are left out
rather than stood there white.

Placement is this module's job: PES positions them from its demo data, which is
authored per stadium, and what carries over is where they belong on a football
pitch. Corner flags go on the corners, cameras outside the perimeter looking in,
the board and the medics in the technical areas, the barrier along the tunnel
side - which for this engine is -y, the touchline the walk-on comes in over.

Run: python3 -m unittest test_stadium_props -v
"""

import math
import unittest

import stadium_staff
import stadium_props


HALF_X = 55.0  # gametypes.hpp
HALF_Y = 36.0


def _faces(mark, target=(0.0, 0.0)):
    """Whether a mark's yaw points it at `target` (a figure at yaw a faces (sin a, -cos a))."""
    x, y, yaw = mark
    facing = (math.sin(yaw), -math.cos(yaw))
    towards = (target[0] - x, target[1] - y)
    length = math.hypot(*towards) or 1.0
    towards = (towards[0] / length, towards[1] / length)
    return facing[0] * towards[0] + facing[1] * towards[1] > 0.9


class CornerFlags(unittest.TestCase):
    def test_there_are_four_of_them(self):
        self.assertEqual(len(stadium_props.corner_flag_marks(HALF_X, HALF_Y)), 4)

    def test_they_stand_on_the_corners(self):
        for x, y, _yaw in stadium_props.corner_flag_marks(HALF_X, HALF_Y):
            self.assertAlmostEqual(abs(x), HALF_X, places=6)
            self.assertAlmostEqual(abs(y), HALF_Y, places=6)

    def test_every_corner_gets_one(self):
        corners = {(x > 0, y > 0) for x, y, _ in stadium_props.corner_flag_marks(HALF_X, HALF_Y)}
        self.assertEqual(len(corners), 4)


class Cameras(unittest.TestCase):
    def setUp(self):
        self.marks = stadium_props.camera_marks(HALF_X, HALF_Y)

    def test_no_camera_stands_on_the_pitch(self):
        for x, y, _yaw in self.marks:
            self.assertTrue(abs(x) > HALF_X or abs(y) > HALF_Y,
                            "camera at %.1f, %.1f is on the field" % (x, y))

    def test_they_all_look_at_the_middle(self):
        for mark in self.marks:
            self.assertTrue(_faces(mark), "camera at %.1f, %.1f faces away" % mark[:2])

    def test_there_is_one_behind_each_goal(self):
        behind = [m for m in self.marks if abs(m[0]) > HALF_X]
        self.assertGreaterEqual(len(behind), 2)
        self.assertEqual(len({m[0] > 0 for m in behind}), 2)

    def test_and_one_on_the_far_touchline(self):
        # the main broadcast camera side, away from the tunnel
        far = [m for m in self.marks if m[1] > HALF_Y]
        self.assertTrue(far)


class TechnicalAreas(unittest.TestCase):
    def setUp(self):
        self.marks = stadium_props.technical_area_marks(HALF_X, HALF_Y)

    def test_there_is_one_either_side_of_the_halfway_line(self):
        self.assertEqual(len(self.marks), 2)
        self.assertEqual(len({m[0] > 0 for m in self.marks}), 2)

    def test_they_are_off_the_pitch_on_the_tunnel_side(self):
        for x, y, _yaw in self.marks:
            self.assertLess(y, -HALF_Y)
            self.assertLess(abs(x), 25.0)

    def test_they_face_the_pitch(self):
        for mark in self.marks:
            self.assertTrue(_faces(mark))


class Barrier(unittest.TestCase):
    def test_it_lies_along_the_tunnel_touchline(self):
        x, y, yaw = stadium_props.barrier_mark(HALF_X, HALF_Y)
        self.assertLess(y, -HALF_Y)
        # a 25 m barrier runs along the line, not across it: it faces the pitch
        self.assertTrue(_faces((x, y, yaw)))


class ClearanceOfEverything(unittest.TestCase):
    """Nothing but the corner flags may touch the field of play."""

    def test_no_mark_of_any_kind_lands_inside_the_lines(self):
        marks = (stadium_props.camera_marks(HALF_X, HALF_Y)
                 + stadium_props.technical_area_marks(HALF_X, HALF_Y)
                 + [stadium_props.barrier_mark(HALF_X, HALF_Y)])
        for x, y, _yaw in marks:
            self.assertTrue(abs(x) > HALF_X or abs(y) > HALF_Y, "%.1f, %.1f is on the field" % (x, y))


class WhichPropsToTake(unittest.TestCase):
    """The set is named rather than swept up, so a cup ceremony does not arrive."""

    def test_the_touchline_set_is_what_a_match_has(self):
        wanted = stadium_props.WANTED
        self.assertIn("gadget_cornerflag", wanted)
        self.assertIn("substitute_board_cmn", wanted)
        self.assertIn("doh_beltpole", wanted)

    def test_no_trophies_or_gadgets_from_the_cutscenes(self):
        for name in stadium_props.WANTED:
            self.assertFalse(name.startswith("cup_"), name)
            self.assertFalse(name.startswith("trophy_"), name)
            self.assertFalse(name.startswith("news_"), name)

    def test_a_prop_is_matched_by_its_own_file_name(self):
        self.assertEqual(stadium_props.prop_role("/x/y/gadget_cornerflag.fmdl"), "cornerflag")
        self.assertEqual(stadium_props.prop_role("/x/y/mob_prop_tvcamera01.fmdl"), "camera")
        self.assertEqual(stadium_props.prop_role("/x/y/substitute_board_cmn.fmdl"), "bench")
        self.assertEqual(stadium_props.prop_role("/x/y/dm_medicalstaff_01.fmdl"), "bench")
        self.assertEqual(stadium_props.prop_role("/x/y/doh_beltpole.fmdl"), "barrier")
        self.assertIsNone(stadium_props.prop_role("/x/y/cup_uefa_euro_hi.fmdl"))


class OnePropPerMark(unittest.TestCase):
    """Three camera models and three camera marks make three cameras, not nine.

    Taking every model to every mark stood a broadcast camera, a hand camera and a
    video camera on the same spot behind each goal.
    """

    def test_the_models_are_shared_out_over_the_marks(self):
        pairs = stadium_props.assign(["a", "b", "c"], [(0, 0, 0), (1, 0, 0), (2, 0, 0)])
        self.assertEqual([p[0] for p in pairs], ["a", "b", "c"])

    def test_one_model_fills_every_mark(self):
        pairs = stadium_props.assign(["flag"], [(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0)])
        self.assertEqual(len(pairs), 4)
        self.assertEqual({p[0] for p in pairs}, {"flag"})

    def test_more_models_than_marks_leaves_the_extras_out(self):
        pairs = stadium_props.assign(["a", "b", "c"], [(0, 0, 0)])
        self.assertEqual(len(pairs), 1)

    def test_no_marks_places_nothing(self):
        self.assertEqual(stadium_props.assign(["a"], []), [])


class SittingOnTheGround(unittest.TestCase):
    """PES hangs some props off their attach point rather than standing them up.

    Measured: gadget_cornerflag runs z -0.07..1.57 and the sub board -0.03..0.39,
    both effectively on the ground, but mob_prop_tvcamera01 runs -0.42..0.82 and
    mob_prop_camera00 -1.63..0.09 - a camera that would be half buried and one
    that would be entirely underground. Every piece is set down on the grass.
    """

    class _Point(object):
        def __init__(self, x, y, z):
            self.x, self.y, self.z = x, y, z

    class _UV(object):
        u = 0.0
        v = 0.0

    class _Vertex(object):
        def __init__(self, x, y, z):
            self.position = SittingOnTheGround._Point(x, y, z)
            self.uv = [SittingOnTheGround._UV()]

    class _Face(object):
        def __init__(self, vertices):
            self.vertices = vertices

    def _mesh(self, low, high):
        # Fox axes: y is up
        verts = [self._Vertex(x, y, z) for x in (-0.2, 0.2) for y in (low, high) for z in (-0.2, 0.2)]

        class Mesh(object):
            pass
        mesh = Mesh()
        mesh.vertices = verts
        mesh.faces = [self._Face(verts[:3])]
        return mesh

    def _written_z(self, low, high):
        import io
        out = io.StringIO()
        stadium_props.write_prop(out, "prop", 0, self._mesh(low, high), (0.0, -40.0), 0.0)
        zs = [float(line.split()[4]) for line in out.getvalue().splitlines()
              if "*MESH_VERTEX " in line]
        return (min(zs), max(zs))

    def test_a_prop_hanging_below_its_origin_is_stood_up(self):
        low, _high = self._written_z(-1.63, 0.09)
        self.assertAlmostEqual(low, 0.0, places=3)

    def test_its_height_is_not_changed(self):
        low, high = self._written_z(-1.63, 0.09)
        self.assertAlmostEqual(high - low, 1.72, places=3)

    def test_one_already_standing_is_left_where_it_is(self):
        low, _high = self._written_z(0.0, 1.5)
        self.assertAlmostEqual(low, 0.0, places=3)

    def test_a_hair_below_the_grass_is_lifted_onto_it(self):
        low, _high = self._written_z(-0.07, 1.57)
        self.assertAlmostEqual(low, 0.0, places=3)


class PlacedWhereTheMarkIs(unittest.TestCase):
    """A prop stands on its mark, not pushed off the pitch like a coach.

    The staff writer sets every figure out past the touchline by its own depth,
    which is right for people on a touchline and wrong for everything else: it
    would take the corner flags off the corners, and a camera meant to sit six
    metres behind a goal at (61, 0) would land at (61, 38), out by the halfway
    line.
    """

    class _Point(object):
        def __init__(self, x, y, z):
            self.x, self.y, self.z = x, y, z

    class _Vertex(object):
        def __init__(self, x, y, z):
            self.position = PlacedWhereTheMarkIs._Point(x, y, z)
            self.uv = [PlacedWhereTheMarkIs._UV()]

    class _UV(object):
        u = 0.0
        v = 0.0

    class _Face(object):
        def __init__(self, vertices):
            self.vertices = vertices

    class _Mesh(object):
        def __init__(self):
            # a half-metre post standing on its own origin, in Fox axes (y up)
            self.vertices = [PlacedWhereTheMarkIs._Vertex(x, z, y)
                             for x in (-0.25, 0.25) for y in (-0.25, 0.25) for z in (0.0, 1.5)]
            self.faces = [PlacedWhereTheMarkIs._Face(self.vertices[:3])]

    def _written_centre(self, mark):
        import io
        import stadium_staff
        out = io.StringIO()
        stadium_props.write_prop(out, "prop", 0, self._Mesh(), (mark[0], mark[1]), mark[2])
        xs, ys = [], []
        for line in out.getvalue().splitlines():
            if "*MESH_VERTEX " in line:
                parts = line.split()
                xs.append(float(parts[2]))
                ys.append(float(parts[3]))
        return ((min(xs) + max(xs)) / 2.0, (min(ys) + max(ys)) / 2.0)

    def test_a_corner_flag_lands_on_its_corner(self):
        x, y = self._written_centre((HALF_X, -HALF_Y, 0.0))
        self.assertAlmostEqual(x, HALF_X, places=3)
        self.assertAlmostEqual(y, -HALF_Y, places=3)

    def test_a_camera_behind_the_goal_stays_behind_the_goal(self):
        x, y = self._written_centre((61.0, 0.0, 0.0))
        self.assertAlmostEqual(x, 61.0, places=3)
        self.assertAlmostEqual(y, 0.0, places=3)

    def test_the_tunnel_side_marks_are_kept_as_given(self):
        x, y = self._written_centre((12.0, -38.5, 0.0))
        self.assertAlmostEqual(x, 12.0, places=3)
        self.assertAlmostEqual(y, -38.5, places=3)


class TheEntranceSet(unittest.TestCase):
    """What PES carries out for the walkout and takes away again.

    doh_fb_home/away are the flag bearers (10.9k vertices, dressed),
    banner_nationalflag_home/away and banner_euro_competition the banners they hold,
    tunnelarch_uefa_euro the arch over the tunnel mouth, circleflag_afc_cl_01 the
    pennant display on the centre circle, and passage_01..99 the tunnel itself.
    None of it belongs on the pitch once the match starts, so it is a separate set
    the engine drops at kickoff.
    """

    def test_the_flag_bearers_and_their_banners_are_in_it(self):
        self.assertIn("doh_fb_home", stadium_props.ENTRANCE_WANTED)
        self.assertIn("banner_nationalflag_home", stadium_props.ENTRANCE_WANTED)

    def test_so_is_the_arch(self):
        self.assertIn("tunnelarch_uefa_euro", stadium_props.ENTRANCE_WANTED)

    def test_the_pennant_ring_is_a_set_of_its_own(self):
        # the broadcast does have it, but which emblem it carries depends on the
        # competition, so it is written once per emblem and loaded separately
        self.assertNotIn("circleflag_afc_cl_01", stadium_props.ENTRANCE_WANTED)
        self.assertIn("circleflag_afc_cl_01", stadium_props.PENNANT_WANTED)

    def test_the_touchline_furniture_is_not(self):
        # it stays out for the whole match
        self.assertNotIn("gadget_cornerflag", stadium_props.ENTRANCE_WANTED)
        self.assertNotIn("doh_beltpole", stadium_props.ENTRANCE_WANTED)

    def test_each_piece_knows_where_it_goes(self):
        self.assertEqual(stadium_props.entrance_role("/x/doh_fb_home.fmdl"), "flagbearer")
        self.assertEqual(stadium_props.entrance_role("/x/banner_nationalflag_away.fmdl"), "banner")
        self.assertEqual(stadium_props.entrance_role("/x/tunnelarch_uefa_euro.fmdl"), "arch")
        self.assertEqual(stadium_props.entrance_role("/x/circleflag_afc_cl_01.fmdl"), "pennant")
        self.assertEqual(stadium_props.entrance_role("/x/passage_01.fmdl"), "tunnel")
        self.assertIsNone(stadium_props.entrance_role("/x/gadget_cornerflag.fmdl"))

    def test_the_pennant_display_stands_on_the_centre_circle(self):
        marks = stadium_props.marks_for_entrance("pennant", HALF_X, HALF_Y)
        self.assertTrue(marks)
        for x, y, _yaw in marks:
            self.assertLess(math.hypot(x, y), 10.0)

    def test_the_flag_bearers_wait_at_the_tunnel_mouth(self):
        marks = stadium_props.marks_for_entrance("flagbearer", HALF_X, HALF_Y)
        self.assertTrue(marks)
        for x, y, _yaw in marks:
            self.assertLess(y, -HALF_Y)          # outside the near touchline
            self.assertLess(abs(x), 20.0)        # by the halfway line, where the walk comes in

    def test_the_arch_is_over_that_mouth_too(self):
        arch = stadium_props.marks_for_entrance("arch", HALF_X, HALF_Y)
        bearers = stadium_props.marks_for_entrance("flagbearer", HALF_X, HALF_Y)
        self.assertTrue(arch)
        self.assertLess(abs(arch[0][1] - bearers[0][1]), 12.0)

    def test_the_tunnel_is_not_in_the_default_set(self):
        # passage_01 is 48 x 61 m, and placed at the mouth its wall filled the
        # entrance camera's frame for the whole walk-on
        self.assertNotIn("passage_01", stadium_props.ENTRANCE_WANTED)
        self.assertIn("passage_01", stadium_props.TUNNEL_EXTRAS)

    def test_the_tunnel_sits_behind_the_mouth_not_on_the_pitch(self):
        marks = stadium_props.marks_for_entrance("tunnel", HALF_X, HALF_Y)
        self.assertTrue(marks)
        for _x, y, _yaw in marks:
            self.assertLess(y, -HALF_Y - 20.0)

    def test_something_with_no_place_in_the_walkout_gets_no_marks(self):
        self.assertEqual(stadium_props.marks_for_entrance("cornerflag", HALF_X, HALF_Y), [])


class ThePennantCarriesTheCompetition(unittest.TestCase):
    """The ring on the centre circle is the competition's, and says which one.

    PES's circleflag_afc_cl_01 is eight meshes: four flag faces on
    acl_circlef_prop000_nomip_bsm and four bearers on acl_circlef_prop001_bsm. The
    face is where the competition's own emblem goes - the 4cc mod does the same
    thing to its UEFA slot, where circleflag_uefa_cl_0_bsm carries a Winter Cup
    badge - so a /a/ v /b/ tie wants the 4chan Stupor Cup clover
    (emblemLc/emb_0004) and an LCG or 2HUG tie the /vg/ Football League crest
    (emb_0008).
    """

    def test_the_flag_face_is_the_one_that_takes_the_emblem(self):
        self.assertTrue(stadium_props.is_pennant_face("acl_circlef_prop000_nomip_bsm.tga"))
        self.assertTrue(stadium_props.is_pennant_face("circleflag_uefa_cl_0_bsm.tga"))
        self.assertTrue(stadium_props.is_pennant_face("demo_circleflag_nomip_bsm.tga"))

    def test_the_bearers_are_left_in_their_own_kit(self):
        self.assertFalse(stadium_props.is_pennant_face("acl_circlef_prop001_bsm.tga"))
        self.assertFalse(stadium_props.is_pennant_face("cl_staff_bsm.tga"))
        self.assertFalse(stadium_props.is_pennant_face(None))

    def test_the_pennant_has_its_own_set(self):
        self.assertEqual(stadium_props.PENNANT_WANTED, ("circleflag_afc_cl_01",))


class ComposingTheBanner(unittest.TestCase):
    """The competition's emblem goes onto PES's banner, not in place of it.

    PES's flag face is a 1024 x 1024 disc - a dark navy field with a hex pattern and
    the AFC Champions League badge in the middle. Dropping a UI emblem in whole
    gives a mostly transparent PNG where a banner should be, and the engine's ASE
    materials have no alpha blending to save it: the disc came out a dark blob on
    the centre circle. So the middle is wiped to the banner's own colour and the
    emblem painted on it, and the outer ring - pattern, edge and all - is PES's.
    """

    def _base(self, size=64):
        from PIL import Image, ImageDraw
        base = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(base)
        draw.ellipse((0, 0, size - 1, size - 1), fill=(20, 30, 70, 255))
        draw.ellipse((size // 4, size // 4, 3 * size // 4, 3 * size // 4),
                     fill=(255, 128, 0, 255))          # the old badge
        return base

    def _emblem(self, size=32):
        from PIL import Image, ImageDraw
        emblem = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        ImageDraw.Draw(emblem).ellipse((2, 2, size - 3, size - 3), fill=(0, 200, 0, 255))
        return emblem

    def test_the_banner_keeps_its_size(self):
        out = stadium_props.compose_flag_face(self._base(), self._emblem())
        self.assertEqual(out.size, (64, 64))

    def test_the_old_badge_is_gone_from_the_middle(self):
        base = self._base()
        out = stadium_props.compose_flag_face(base, self._emblem())
        # dead centre is the new emblem, not the orange badge
        self.assertNotEqual(out.getpixel((32, 32))[:3], base.getpixel((32, 32))[:3])
        self.assertGreater(out.getpixel((32, 32))[1], out.getpixel((32, 32))[0])

    def test_the_edge_of_the_disc_is_still_pes(self):
        base = self._base()
        out = stadium_props.compose_flag_face(base, self._emblem())
        self.assertEqual(out.getpixel((32, 1))[:3], base.getpixel((32, 1))[:3])
        self.assertEqual(out.getpixel((1, 32))[:3], base.getpixel((1, 32))[:3])

    def test_what_was_transparent_stays_transparent(self):
        # the corners are outside the disc
        out = stadium_props.compose_flag_face(self._base(), self._emblem())
        self.assertEqual(out.getpixel((0, 0))[3], 0)

    def test_no_emblem_leaves_the_banner_alone(self):
        base = self._base()
        out = stadium_props.compose_flag_face(base, None)
        self.assertEqual(out.tobytes(), base.tobytes())


class TheWalkoutFlags(unittest.TestCase):
    """The flags the walkout cast carries, and the arch they walk under.

    Same trap as the crowd's stand flags: PES's doh_fb_home and tunnelarch models
    both reference sys_zero_bsm, a placeholder it swaps at run time, and the picture
    left in the file is the flag of the United States for the bearers and the FC
    Barcelona crest for the arch. Since textures are keyed by bare filename, one
    sys_zero_bsm.png per stadium was shared between all of them, so whichever was
    converted last decided what the whole walkout carried.
    """

    def test_a_bearers_flag_becomes_the_engines_own_cloth(self):
        self.assertEqual(stadium_props.placeholder_bitmap("sys_zero_bsm", "doh_fb_home"),
                         "media/textures/stadium/teamflag_home.png")
        self.assertEqual(stadium_props.placeholder_bitmap("sys_zero_bsm", "doh_fb_away"),
                         "media/textures/stadium/teamflag_away.png")

    def test_the_arch_takes_the_home_sides_cloth(self):
        # PES puts the home club's crest on it
        self.assertEqual(stadium_props.placeholder_bitmap("sys_zero_bsm", "tunnelarch_uefa_euro"),
                         "media/textures/stadium/teamflag_home.png")

    def test_real_artwork_is_converted_as_usual(self):
        self.assertIsNone(stadium_props.placeholder_bitmap("staff_fb00a_bsm", "doh_fb_home"))
        self.assertIsNone(stadium_props.placeholder_bitmap("tunnelarch_euro000_bsm",
                                                          "tunnelarch_uefa_euro"))

    def test_nothing_is_not_a_placeholder(self):
        self.assertIsNone(stadium_props.placeholder_bitmap(None, "doh_fb_home"))


class DressedMeshByMesh(unittest.TestCase):
    """A prop with one placeholder panel is still worth having.

    tunnelarch_uefa_euro carries four meshes: two textured and two on
    dummy_embA/embH, which are the placeholders PES swaps for the two teams'
    emblems. Judged all-or-nothing the whole arch was left in the pack; judged mesh
    by mesh the arch comes and the two blank panels do not.
    """

    def test_a_model_with_some_dressed_meshes_is_kept(self):
        keep = stadium_props.dressed_meshes([True, True, False, False])
        self.assertEqual(keep, [0, 1])

    def test_one_with_none_dressed_is_left_out_entirely(self):
        self.assertEqual(stadium_props.dressed_meshes([False, False]), [])

    def test_a_fully_dressed_model_keeps_everything(self):
        self.assertEqual(stadium_props.dressed_meshes([True, True, True]), [0, 1, 2])

    def test_no_meshes_at_all_is_nothing_to_keep(self):
        self.assertEqual(stadium_props.dressed_meshes([]), [])




class APropIsOnePieceOfFurniture(unittest.TestCase):
    """A prop's meshes have to be placed together, not one at a time.

    The corner flag rendered with its flag lying flat on the grass beside the pole,
    and the cause was the code meant to stop props being buried. gadget_cornerflag is
    four meshes - pole, ground disc, and the cloth in two pieces - and each was set
    down on the grass on its own, so the cloth, authored at z 1.28..1.62 where it hangs
    off the top of a 1.62 m pole, was lifted by -1.28 and came out at z 0..0.343. The
    footprint centring did the same sideways: the cloth hangs to one side of the pole
    and was pushed back onto its axis.

    Measured off the imported props.ase before the fix, all four pieces of every flag
    started at exactly z 0.000, which is the signature.

    So the ground lift and the footprint offset are computed once for the whole prop,
    over every vertex it has, and every mesh is placed with that.
    """

    # a pole from the ground to 1.62, and a flag hanging from its top
    POLE = [(0.0, 0.0, 0.0), (0.02, 0.0, 0.0), (0.0, 0.0, 1.62)]
    CLOTH = [(0.0, 0.0, 1.28), (0.3, 0.0, 1.28), (0.3, 0.0, 1.62)]

    def test_the_lift_comes_from_the_whole_prop(self):
        lift = stadium_props.prop_placement([self.POLE, self.CLOTH])["lift"]
        self.assertAlmostEqual(lift, 0.0)   # the pole already stands on the ground

    def test_a_prop_hung_off_an_attach_point_is_still_brought_down(self):
        # mob_prop_camera00 runs from 1.63 below its origin to 0.09 above
        below = [(0.0, 0.0, -1.63), (0.1, 0.0, -1.63), (0.1, 0.0, 0.09)]
        self.assertAlmostEqual(stadium_props.prop_placement([below])["lift"], 1.63)

    def test_the_flag_keeps_its_height_above_the_ground(self):
        place = stadium_props.prop_placement([self.POLE, self.CLOTH])
        cloth = stadium_props.place_prop_mesh(self.CLOTH, place)
        self.assertAlmostEqual(min(v[2] for v in cloth), 1.28)
        self.assertAlmostEqual(max(v[2] for v in cloth), 1.62)

    def test_the_pole_still_stands_on_the_grass(self):
        place = stadium_props.prop_placement([self.POLE, self.CLOTH])
        pole = stadium_props.place_prop_mesh(self.POLE, place)
        self.assertAlmostEqual(min(v[2] for v in pole), 0.0)

    def test_the_flag_stays_out_to_the_side_of_the_pole(self):
        place = stadium_props.prop_placement([self.POLE, self.CLOTH])
        pole = stadium_props.place_prop_mesh(self.POLE, place)
        cloth = stadium_props.place_prop_mesh(self.CLOTH, place)
        # the cloth's own x range is 0.0..0.3 against the pole's 0.0..0.02, and that
        # difference has to survive the centring
        self.assertGreater(max(v[0] for v in cloth), max(v[0] for v in pole))

    def test_placing_one_mesh_alone_is_what_it_always_was(self):
        # a single-mesh prop is centred and grounded exactly as before
        place = stadium_props.prop_placement([self.POLE])
        pole = stadium_props.place_prop_mesh(self.POLE, place)
        self.assertAlmostEqual(min(v[2] for v in pole), 0.0)
        centre_x = (min(v[0] for v in pole) + max(v[0] for v in pole)) / 2.0
        self.assertAlmostEqual(centre_x, 0.0)

    def test_a_prop_with_no_vertices_does_not_divide_by_nothing(self):
        place = stadium_props.prop_placement([[]])
        self.assertAlmostEqual(place["lift"], 0.0)
        self.assertEqual(stadium_props.place_prop_mesh([], place), [])


class GroundFootprint(unittest.TestCase):
    """Where a prop stands: on what touches the grass, not on its own average.

    PES's corner flag is a pole with 0.61 m of cloth hanging off one side of it.
    Centring the whole prop put the pole 0.30 m off the corner - measured on the
    installed stadiums, 0.17 m outside the goal line and 0.25 m inside the touchline.
    """

    def test_pole_with_an_overhang_stands_on_its_pole(self):
        # A pole on the origin, with a flag hanging out to -x near the top.
        pole = [(0.0, 0.0, 0.0), (0.02, 0.0, 0.0), (0.0, 0.0, 1.6)]
        cloth = [(-0.6, 0.0, 1.3), (0.0, 0.0, 1.6), (-0.6, 0.0, 1.6)]
        dx, dy = stadium_props.ground_footprint_offset(pole + cloth)
        self.assertAlmostEqual(dx, -0.01, places=3)
        self.assertAlmostEqual(dy, 0.0, places=3)

    def test_whole_prop_centring_is_what_moved_it(self):
        pole = [(0.0, 0.0, 0.0), (0.02, 0.0, 0.0), (0.0, 0.0, 1.6)]
        cloth = [(-0.6, 0.0, 1.3), (0.0, 0.0, 1.6), (-0.6, 0.0, 1.6)]
        dx, _ = stadium_staff.footprint_offset(pole + cloth)
        self.assertAlmostEqual(dx, 0.29, places=3)

    def test_a_prop_that_all_touches_the_ground_is_unchanged(self):
        flat = [(1.0, 2.0, 0.0), (3.0, 2.0, 0.0), (3.0, 4.0, 0.0)]
        self.assertEqual(stadium_props.ground_footprint_offset(flat),
                         stadium_staff.footprint_offset(flat))

    def test_one_point_of_contact_lands_on_the_mark(self):
        # A prop balanced on a single vertex stands on that vertex, not on its bulk.
        spike = [(0.0, 0.0, 0.0), (4.0, 0.0, 1.0), (4.0, 4.0, 1.0)]
        self.assertEqual(stadium_props.ground_footprint_offset(spike), (0.0, 0.0))

    def test_the_band_only_takes_the_bottom(self):
        # A tripod's feet, with a metre of lens above them, stand on the feet.
        feet = [(-0.2, 0.0, 0.0), (0.2, 0.0, 0.0), (0.0, 0.2, 0.02)]
        lens = [(3.0, 0.0, 1.2), (3.4, 0.0, 1.2), (3.2, 0.4, 1.2)]
        dx, dy = stadium_props.ground_footprint_offset(feet + lens)
        self.assertAlmostEqual(dx, 0.0, places=3)
        self.assertAlmostEqual(dy, -0.1, places=3)

    def test_the_flag_lands_on_the_corner(self):
        pole = [(0.0, 0.0, 0.0), (0.02, 0.0, 0.0), (0.0, 0.0, 1.6)]
        cloth = [(-0.6, 0.0, 1.3), (0.0, 0.0, 1.6), (-0.6, 0.0, 1.6)]
        placement = stadium_props.prop_placement([pole, cloth])
        placed = stadium_props.place_prop_mesh(pole, placement)
        # The pole's own axis, once placed, is on the mark.
        xs = [v[0] for v in placed]
        self.assertAlmostEqual(0.5 * (min(xs) + max(xs)), 0.0, places=3)
        self.assertAlmostEqual(min(v[2] for v in placed), 0.0, places=3)


if __name__ == "__main__":
    unittest.main()


class ThePennantIsHeldNotDropped(unittest.TestCase):
    """PES drives the circle flag as cloth and ships it in its rest pose: flat,
    its rim at 0.10 m, a metre under the ring of hands holding it. Imported
    static it renders as a dark disc lying on the centre circle inside a ring of
    men holding nothing, which is how it shipped."""

    def bearer_ring(self, hand_z=1.10, radius=8.27, count=24):
        """A ring of men: feet on the grass, hands out at hand_z, heads above."""
        import math
        out = []
        for i in range(count):
            a = 2 * math.pi * i / count
            for z in (0.0, 0.05, 0.5, hand_z, hand_z, hand_z, 1.7, 1.8):
                out.append((radius * math.cos(a), radius * math.sin(a), z))
        return out

    def flat_flag(self, rim_z=0.10, radius=8.27, count=40):
        import math
        out = []
        for i in range(count):
            a = 2 * math.pi * i / count
            out.append((radius * math.cos(a), radius * math.sin(a), rim_z))
            out.append((radius * 0.5 * math.cos(a), radius * 0.5 * math.sin(a), rim_z))
        return out

    def test_the_hands_are_found_between_the_feet_and_the_heads(self):
        self.assertAlmostEqual(
            stadium_props.hand_height(self.bearer_ring(), 8.27), 1.10, places=2)

    def test_a_shorter_set_is_measured_not_assumed(self):
        self.assertAlmostEqual(
            stadium_props.hand_height(self.bearer_ring(hand_z=0.90), 8.27), 0.90,
            places=2)

    def test_the_flag_is_raised_to_the_hands(self):
        lift = stadium_props.pennant_face_lift(
            [self.flat_flag()], [self.bearer_ring()])
        self.assertAlmostEqual(lift, 1.00, places=2)

    def test_a_flag_already_in_the_hands_is_left_alone(self):
        lift = stadium_props.pennant_face_lift(
            [self.flat_flag(rim_z=1.10)], [self.bearer_ring()])
        self.assertEqual(lift, 0.0)

    def test_a_flag_above_the_hands_is_never_pulled_down(self):
        lift = stadium_props.pennant_face_lift(
            [self.flat_flag(rim_z=1.60)], [self.bearer_ring()])
        self.assertEqual(lift, 0.0)

    def test_bearers_with_no_hand_band_leave_it_where_it_is(self):
        """Nothing between knee and shoulder is not a set of bearers, and
        guessing a height for it would drop the flag somewhere arbitrary."""
        posts = [(8.27, 0.0, z) for z in (0.0, 0.1, 0.2, 1.9, 2.0)]
        self.assertEqual(
            stadium_props.pennant_face_lift([self.flat_flag()], [posts]), 0.0)

    def test_nothing_to_measure_is_not_an_error(self):
        self.assertEqual(stadium_props.pennant_face_lift([], []), 0.0)
        self.assertEqual(stadium_props.pennant_face_lift([self.flat_flag()], []), 0.0)
