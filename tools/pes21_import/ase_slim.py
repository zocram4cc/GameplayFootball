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

FACENORMAL = re.compile(r'^\s*\*MESH_FACENORMAL\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)')
VERTEXNORMAL = re.compile(r'^\s*\*MESH_VERTEXNORMAL\s+\d+\s+(\S+)\s+(\S+)\s+(\S+)')
VERTEX = re.compile(r'^\s*\*MESH_VERTEX\s+(\d+)\s+(\S+)\s+(\S+)\s+(\S+)')
FACE = re.compile(r'^\s*\*MESH_FACE\s+(\d+):\s+A:\s+(\d+)\s+B:\s+(\d+)\s+C:\s+(\d+)')

# How far a stored normal may sit from the winding's and still be reproduced by it.
# Generous on purpose: the question is whether dropping the block changes which way
# the face is lit, not whether the two agree to the last bit.
NORMAL_AGREEMENT = 0.9


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


def _normals_match_winding(chunk, agreement=NORMAL_AGREEMENT):
    """Whether every stored face normal is the one the winding would derive.

    Flat says a mesh carries no smoothing; it says nothing about which way the
    faces point. A sky dome is drawn from the inside and ships one normal pointing
    away from the sun, and the goal net and the debug helpers carry normals
    authored off their winding entirely. Dropping those does not reproduce them.

    A mesh whose positions or faces are not in the chunk cannot be shown
    redundant, so it fails here and keeps its block.
    """
    positions = {}
    faces = {}
    stored = {}
    for line in chunk:
        match = VERTEX.match(line)
        if match:
            positions[int(match.group(1))] = tuple(float(match.group(i)) for i in (2, 3, 4))
            continue
        match = FACE.match(line)
        if match:
            faces[int(match.group(1))] = tuple(int(match.group(i)) for i in (2, 3, 4))
            continue
        match = FACENORMAL.match(line)
        if match:
            stored[int(match.group(1))] = tuple(float(match.group(i)) for i in (2, 3, 4))
    if not positions or not faces or not stored:
        return False
    for index, normal in stored.items():
        triangle = faces.get(index)
        if triangle is None:
            return False
        try:
            a, b, c = (positions[i] for i in triangle)
        except KeyError:
            return False
        u = tuple(b[i] - a[i] for i in range(3))
        v = tuple(c[i] - a[i] for i in range(3))
        cross = (u[1] * v[2] - u[2] * v[1],
                 u[2] * v[0] - u[0] * v[2],
                 u[0] * v[1] - u[1] * v[0])
        length = sum(component ** 2 for component in cross) ** 0.5
        if length <= 1e-9:
            # asenormals.cpp returns a zero normal here too, so there is nothing
            # for the stored one to disagree with.
            continue
        derived = tuple(component / length for component in cross)
        if sum(derived[i] * normal[i] for i in range(3)) < agreement:
            return False
    return True


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
        if _mesh_is_flat(chunk[first:last + 1]) and _normals_match_winding(chunk):
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
