"""Drops the normals an ASE does not need, keeping the ones it does.

Normals are 45% of the bytes in an imported stadium. Of pes_st002.ase's 1,132 MB,
MESH_VERTEXNORMAL accounts for 386.6 MB and MESH_FACENORMAL for 121.2 MB - and they
are redundant, because over 200,000 sampled faces of pes_st011.ase every single one
has its three vertex normals identical. The file writes a flat normal three times and
carries no smoothing at all, which is exactly what src/loaders/asenormals.cpp derives
from the winding when a mesh ships none.

Redundant is not the same as everywhere. props.ase measures 7.0% flat and
entrance.ase 1.8%, because the paramedics and the flag bearers really are
smooth-shaded and dropping their normals would flatten them. So the decision is per
mesh: a mesh whose every face is flat loses its MESH_NORMALS block, and a mesh with
any smoothing in it keeps the block whole.

  python3 ase_slim.py <file.ase> [more.ase ...] [--dry-run]

Rewrites in place, and drops any .geomcache beside the file - the cache is stamped
against the source's size and hash, so a stale one would be rejected anyway.
"""

import argparse
import os
import re
import sys

FACENORMAL = re.compile(r'^\s*\*MESH_FACENORMAL\s')
VERTEXNORMAL = re.compile(r'^\s*\*MESH_VERTEXNORMAL\s+\d+\s+(\S+)\s+(\S+)\s+(\S+)')


def _mesh_is_flat(lines):
    """Every face's three vertex normals identical, and there is at least one face."""
    faces = 0
    current = []
    for line in lines:
        if FACENORMAL.match(line):
            if current:
                if len(current) != 3 or len(set(current)) != 1:
                    return False
                faces += 1
            current = []
            continue
        match = VERTEXNORMAL.match(line)
        if match:
            current.append(match.groups())
    if current:
        if len(current) != 3 or len(set(current)) != 1:
            return False
        faces += 1
    return faces > 0


def _normals_span(lines):
    """(start, end) of the MESH_NORMALS block within these lines, or None.

    Found by brace depth rather than by indentation, so a file written by another
    tool with different whitespace is still handled.
    """
    for i, line in enumerate(lines):
        if "*MESH_NORMALS" not in line:
            continue
        depth = line.count("{") - line.count("}")
        j = i
        while depth > 0 and j + 1 < len(lines):
            j += 1
            depth += lines[j].count("{") - lines[j].count("}")
        return i, j
    return None


def slim(text):
    """-> (text without the derivable normals, counts)."""
    lines = text.splitlines(keepends=True)
    starts = [i for i, l in enumerate(lines) if l.lstrip().startswith("*GEOMOBJECT")]
    stats = {"slimmed": 0, "kept": 0, "saved": 0}
    if not starts:
        return text, stats
    out = list(lines[:starts[0]])
    bounds = list(zip(starts, starts[1:] + [len(lines)]))
    for start, end in bounds:
        chunk = lines[start:end]
        span = _normals_span(chunk)
        if span is None:
            out.extend(chunk)
            continue
        first, last = span
        if _mesh_is_flat(chunk[first:last + 1]):
            stats["slimmed"] += 1
            out.extend(chunk[:first])
            out.extend(chunk[last + 1:])
        else:
            stats["kept"] += 1
            out.extend(chunk)
    slimmed = "".join(out)
    stats["saved"] = len(text) - len(slimmed)
    return slimmed, stats


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ase", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    total_before = total_after = 0
    for path in args.ase:
        before = os.path.getsize(path)
        text = open(path, errors="surrogateescape").read()
        out, stats = slim(text)
        total_before += before
        total_after += before - stats["saved"]
        print("%-56s %8.1f -> %8.1f MB  (%d mesh(es) slimmed, %d kept)"
              % (os.path.basename(path), before / 1e6, (before - stats["saved"]) / 1e6,
                 stats["slimmed"], stats["kept"]))
        if args.dry_run or not stats["slimmed"]:
            continue
        open(path, "w", errors="surrogateescape").write(out)
        cache = path + ".geomcache"
        if os.path.exists(cache):
            os.remove(cache)
    if len(args.ase) > 1:
        print("total %.1f -> %.1f MB (%.1f%% smaller)"
              % (total_before / 1e6, total_after / 1e6,
                 100.0 * (total_before - total_after) / total_before if total_before else 0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
