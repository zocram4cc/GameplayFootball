"""Drops the vertices no face in an ASE uses, and renumbers what is left.

Measured over the 90 imported player models: 912,870 of 2,089,952 vertices - 43.7% -
are never referenced by any face, roughly 580 MB of the 1,328 MB the models take up.
The worst are 83% orphaned (ateam_70214: 19,547 of 23,480). It is the stretched-
triangle drop removing faces and leaving their vertices behind.

Three index spaces have to stay straight:

  MESH_VERTEX  is indexed by MESH_FACE
  MESH_TVERT   is indexed by MESH_TFACE - a separate space; a TFACE does not index
               vertices, so the two are compacted independently
  MESH_NORMALS is indexed by face, not by vertex, so renumbering vertices leaves it
               alone as long as the face order and count do not change. They do not:
               this only ever removes vertices.

  python3 ase_compact.py <file.ase> [more.ase ...] [--dry-run]

Rewrites in place and drops any .geomcache beside the file, which is stamped against
the source's size and hash and would be rejected anyway.
"""

import argparse
import os
import re
import sys

VERTEX = re.compile(r'^(\s*\*MESH_VERTEX\s+)(\d+)(\s.*)$')
FACE = re.compile(r'^(\s*\*MESH_FACE\s+\d+:\s+A:\s+)(\d+)(\s+B:\s+)(\d+)(\s+C:\s+)(\d+)(.*)$')
TVERT = re.compile(r'^(\s*\*MESH_TVERT\s+)(\d+)(\s.*)$')
TFACE = re.compile(r'^(\s*\*MESH_TFACE\s+\d+\s+)(\d+)(\s+)(\d+)(\s+)(\d+)(.*)$')


def _geomobjects(lines):
    starts = [i for i, l in enumerate(lines) if l.lstrip().startswith("*GEOMOBJECT")]
    if not starts:
        return []
    return list(zip(starts, starts[1:] + [len(lines)]))


def _compact_space(chunk, entry_re, face_re, count_token):
    """-> (new lines, how many entries were dropped) for one index space."""
    used = set()
    for line in chunk:
        m = face_re.match(line.rstrip("\n"))
        if m:
            used.update(int(m.group(g)) for g in (2, 4, 6))

    entries = [i for i, l in enumerate(chunk) if entry_re.match(l.rstrip("\n"))]
    if not entries:
        return chunk, 0

    remap = {}
    kept_lines = []
    for i in entries:
        m = entry_re.match(chunk[i].rstrip("\n"))
        index = int(m.group(2))
        if index not in used:
            continue
        remap[index] = len(kept_lines)
        kept_lines.append((i, m))
    dropped = len(entries) - len(kept_lines)
    if not dropped:
        return chunk, 0

    keep_at = {i for i, _ in kept_lines}
    renumber = {i: new for new, (i, _) in enumerate(kept_lines)}
    out = []
    for i, line in enumerate(chunk):
        m_entry = entry_re.match(line.rstrip("\n"))
        if m_entry:
            if i not in keep_at:
                continue
            out.append("%s%d%s\n" % (m_entry.group(1), renumber[i], m_entry.group(3)))
            continue
        m_face = face_re.match(line.rstrip("\n"))
        if m_face:
            groups = list(m_face.groups())
            for slot in (1, 3, 5):
                groups[slot] = str(remap.get(int(groups[slot]), 0))
            out.append("".join(groups) + "\n")
            continue
        if count_token in line:
            out.append(re.sub(r'\d+\s*$', str(len(kept_lines)), line.rstrip()) + "\n")
            continue
        out.append(line)
    return out, dropped


def compact(text):
    """-> (text with the orphans gone, counts)."""
    lines = text.splitlines(keepends=True)
    bounds = _geomobjects(lines)
    stats = {"vertices_dropped": 0, "tverts_dropped": 0, "meshes": 0}
    if not bounds:
        return text, stats
    out = list(lines[:bounds[0][0]])
    for start, end in bounds:
        chunk = lines[start:end]
        chunk, dropped_v = _compact_space(chunk, VERTEX, FACE, "*MESH_NUMVERTEX")
        chunk, dropped_t = _compact_space(chunk, TVERT, TFACE, "*MESH_NUMTVERTEX")
        if dropped_v or dropped_t:
            stats["meshes"] += 1
        stats["vertices_dropped"] += dropped_v
        stats["tverts_dropped"] += dropped_t
        out.extend(chunk)
    return "".join(out), stats


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ase", nargs="+")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    before = after = 0
    for path in args.ase:
        size = os.path.getsize(path)
        text = open(path, errors="surrogateescape").read()
        out, stats = compact(text)
        before += size
        after += len(out.encode("utf-8", "surrogateescape"))
        if stats["vertices_dropped"] or stats["tverts_dropped"]:
            print("%-40s %7.1f -> %7.1f MB  (%d vert(s), %d tvert(s) in %d mesh(es))"
                  % (os.path.basename(path), size / 1e6,
                     len(out.encode("utf-8", "surrogateescape")) / 1e6,
                     stats["vertices_dropped"], stats["tverts_dropped"], stats["meshes"]))
        if args.dry_run or not (stats["vertices_dropped"] or stats["tverts_dropped"]):
            continue
        open(path, "w", errors="surrogateescape").write(out)
        cache = path + ".geomcache"
        if os.path.exists(cache):
            os.remove(cache)
    if len(args.ase) > 1 and before:
        print("total %.1f -> %.1f MB (%.1f%% smaller)"
              % (before / 1e6, after / 1e6, 100.0 * (before - after) / before))
    return 0


if __name__ == "__main__":
    sys.exit(main())
