"""Verbatim contract: convert() keeps every authored face except dedupe
copies and kit-hidden forms. dbg_2009 pole meshes 24/25: 19,808 faces each,
160 genuine 0.79 m shaft triangles were cut at a 0.085 limit (installed
203,765 vs verbatim 203,925)."""
import os
import shutil
import tempfile
import unittest

from PIL import Image

import fmdl_to_fullbody
from test_mesh_selection import fake_mesh


def _pack_with_texel(mesh, opaque):
    """A fake pack dir where mesh's texture resolves: opaque or blank."""
    pack = tempfile.mkdtemp()
    alpha = 255 if opaque else 0
    Image.new("RGBA", (4, 4), (200, 100, 50, alpha)).save(
        os.path.join(pack, "dummy_kit.png"))
    return pack


class ConvertNeverCutsAuthoredFaces(unittest.TestCase):
    def test_no_max_edge_plumbing_reaches_convert(self):
        """The pole lost 160 faces to a max-edge cut; the flag, the
        parameter, and the call-site pass-through are all deleted, so no
        future caller can re-enable the cut by accident."""
        import inspect
        src = inspect.getsource(fmdl_to_fullbody.convert)
        self.assertNotIn("max_edge", src)
        import subprocess
        here = os.path.dirname(os.path.abspath(__file__))
        out = subprocess.run(
            ["python3", "fmdl_to_fullbody.py", "--help"],
            capture_output=True, text=True, cwd=here).stdout
        self.assertNotIn("max-edge", out)


class DummyTorsoIsNotAPlaceholder(unittest.TestCase):
    """Bowser k2593: the dropped dummy_kit mesh is a mannequin torso
    (1,293 verts, y 0.18..1.81, 875/1,907 faces sampling opaque texels),
    not a blank swap-stand-in. Dropping it by name deleted his torso."""

    def test_opaque_dummy_mesh_survives_select(self):
        mesh = fake_mesh(100, texture="dummy_kit", seed=1.0)
        pack = _pack_with_texel(mesh, opaque=True)
        self.addCleanup(shutil.rmtree, pack)
        kept = fmdl_to_fullbody.select_meshes([mesh], 100000,
                                              source_dir=pack)
        self.assertEqual(kept, [mesh])

    def test_blank_dummy_mesh_is_still_dropped(self):
        """WAHluigi k2588: a fully transparent dummy shell is the swap
        stand-in the rule was written for — that drop stays."""
        mesh = fake_mesh(100, texture="dummy_kit", seed=1.0)
        pack = _pack_with_texel(mesh, opaque=False)
        self.addCleanup(shutil.rmtree, pack)
        kept = fmdl_to_fullbody.select_meshes([mesh], 100000,
                                              source_dir=pack)
        self.assertEqual(kept, [])
