"""Verbatim contract: convert() keeps every authored face except dedupe
copies and kit-hidden forms. dbg_2009 pole meshes 24/25: 19,808 faces each,
160 genuine 0.79 m shaft triangles were cut at a 0.085 limit (installed
203,765 vs verbatim 203,925)."""
import unittest


class ConvertNeverCutsAuthoredFaces(unittest.TestCase):
    def test_no_max_edge_plumbing_reaches_convert(self):
        """The pole lost 160 faces to a max-edge cut; the flag, the
        parameter, and the call-site pass-through are all deleted, so no
        future caller can re-enable the cut by accident."""
        import fmdl_to_fullbody
        import inspect
        src = inspect.getsource(fmdl_to_fullbody.convert)
        self.assertNotIn("max_edge", src)
        import subprocess
        import os
        here = os.path.dirname(os.path.abspath(__file__))
        out = subprocess.run(
            ["python3", "fmdl_to_fullbody.py", "--help"],
            capture_output=True, text=True, cwd=here).stdout
        self.assertNotIn("max-edge", out)
