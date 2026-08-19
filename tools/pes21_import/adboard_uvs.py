"""Makes an advertising ring read the right way round, in place, in its ASE.

Turning a hoarding to face the pitch is not enough. Rendered and read off a capture,
the imported ring shows a run of boards where some ads read normally and others are
mirrored ("LESBIANS" as "SNAIBSEL"). Measured over adboards.ase: of the 89 meshes
carrying board faces, 70 read correctly throughout and one is mirrored throughout, but
18 - the big merged runs - hold both, and those carry about 900 of the 916 mirrored
faces. Inside one of them the same UV rectangle turns up twice with opposite
handedness (nine faces at U 0.668..0.854 mirrored against nine at 0.670..0.841
readable), which is what mirroring a duplicated segment leaves behind.

PES gets away with it because it assigns the advertising faces at runtime through
bill_anime.json; this engine uses the model's own UVs, so the mirrored copies show
mirrored ads.

The fix cannot be per-vertex. An ASE shares one UV per vertex across every face that
uses it, and here mirrored faces sit in the same mesh as readable ones, sharing
vertices with them. So the UV list is rebuilt per face corner - identical corners
deduplicated - and only the corners of mirrored faces move.

  python3 adboard_uvs.py <in.ase> [-o <out.ase>]

With no -o the file is rewritten in place, which is what a pack imported before this
existed needs.
"""

import argparse
import re
import sys

import stadium_to_gf

VERTEX = re.compile(r'^\s*\*MESH_VERTEX\s+(\d+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)')
FACE = re.compile(r'^\s*\*MESH_FACE\s+(\d+):\s+A:\s+(\d+)\s+B:\s+(\d+)\s+C:\s+(\d+)')
TVERT = re.compile(r'^\s*\*MESH_TVERT\s+(\d+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)')
TFACE = re.compile(r'^\s*\*MESH_TFACE\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)')


def _read(lines):
    """-> (vertices, faces, uvs, tfaces) out of one mesh's lines."""
    vertices, faces, uvs, tfaces = {}, [], {}, []
    for line in lines:
        m = VERTEX.match(line)
        if m:
            vertices[int(m.group(1))] = tuple(float(m.group(i)) for i in (2, 3, 4))
            continue
        m = FACE.match(line)
        if m:
            faces.append(tuple(int(m.group(i)) for i in (2, 3, 4)))
            continue
        m = TVERT.match(line)
        if m:
            uvs[int(m.group(1))] = (float(m.group(2)), float(m.group(3)))
            continue
        m = TFACE.match(line)
        if m:
            tfaces.append(tuple(int(m.group(i)) for i in (2, 3, 4)))
    return vertices, faces, uvs, tfaces


def _rebuild(vertices, faces, uvs, tfaces):
    """-> (new uv list, new tfaces, readable count, mirrored count).

    Corners are deduplicated on their rounded coordinates, so a ring whose UVs were
    already right comes out byte-for-byte as it went in.
    """
    table, order = {}, []

    def index_of(uv):
        key = (round(uv[0], 5), round(uv[1], 5))
        if key not in table:
            table[key] = len(order)
            order.append(uv)
        return table[key]

    readable = mirrored = 0
    out = []
    for face, tface in zip(faces, tfaces):
        if any(i not in vertices for i in face) or any(i not in uvs for i in tface):
            out.append(tface)
            for i in tface:
                if i in uvs:
                    index_of(uvs[i])
            continue
        corners = [vertices[i] for i in face]
        corner_uvs = [uvs[i] for i in tface]
        verdict = stadium_to_gf.face_reads_mirrored(corners, corner_uvs)
        if verdict:
            mirrored += 1
            corner_uvs = stadium_to_gf.mirror_face_u(corner_uvs)
        elif verdict is False:
            readable += 1
        out.append(tuple(index_of(uv) for uv in corner_uvs))
    return order, out, readable, mirrored


def _rewrite(lines, uvs, tfaces):
    """The mesh's lines with its UV list and TFACE list replaced."""
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if "*MESH_NUMTVERTEX" in line:
            out.append(re.sub(r'\d+\s*$', str(len(uvs)), line.rstrip()) + "\n")
        elif "*MESH_TVERTLIST" in line:
            out.append(line)
            i += 1
            indent = "\t\t\t"
            while i < len(lines) and "}" not in lines[i]:
                i += 1
            for n, uv in enumerate(uvs):
                out.append("%s*MESH_TVERT %d\t%.5f\t%.5f\t0.0\n" % (indent, n, uv[0], uv[1]))
            out.append(lines[i] if i < len(lines) else "\t\t}\n")
        elif "*MESH_NUMTVFACES" in line:
            out.append(re.sub(r'\d+\s*$', str(len(tfaces)), line.rstrip()) + "\n")
        elif "*MESH_TFACELIST" in line:
            out.append(line)
            i += 1
            while i < len(lines) and "}" not in lines[i]:
                i += 1
            for n, tf in enumerate(tfaces):
                out.append("\t\t\t*MESH_TFACE %d\t%d\t%d\t%d\n" % (n, tf[0], tf[1], tf[2]))
            out.append(lines[i] if i < len(lines) else "\t\t}\n")
        else:
            out.append(line)
        i += 1
    return out


def normalise(text):
    """-> (text with every board face reading from the pitch, counts)."""
    lines = text.splitlines(keepends=True)
    # mesh boundaries: a GEOMOBJECT opens each one
    starts = [i for i, l in enumerate(lines) if l.lstrip().startswith("*GEOMOBJECT")]
    if not starts:
        return text, {"readable": 0, "mirrored": 0, "meshes": 0}
    bounds = list(zip(starts, starts[1:] + [len(lines)]))
    stats = {"readable": 0, "mirrored": 0, "meshes": 0}
    rebuilt = []
    last = 0
    for start, end in bounds:
        rebuilt.extend(lines[last:start])
        chunk = lines[start:end]
        vertices, faces, uvs, tfaces = _read(chunk)
        if not uvs or len(faces) != len(tfaces):
            rebuilt.extend(chunk)
            last = end
            continue
        new_uvs, new_tfaces, readable, mirrored = _rebuild(vertices, faces, uvs, tfaces)
        stats["readable"] += readable
        stats["mirrored"] += mirrored
        if mirrored:
            stats["meshes"] += 1
            rebuilt.extend(_rewrite(chunk, new_uvs, new_tfaces))
        else:
            rebuilt.extend(chunk)
        last = end
    rebuilt.extend(lines[last:])
    return "".join(rebuilt), stats


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ase")
    parser.add_argument("-o", "--out", default=None)
    args = parser.parse_args()
    fixed, stats = normalise(open(args.ase).read())
    open(args.out or args.ase, "w").write(fixed)
    print("%d board face(s) read from the pitch, %d turned back (in %d mesh(es)) -> %s"
          % (stats["readable"], stats["mirrored"], stats["meshes"], args.out or args.ase))
    return 0


if __name__ == "__main__":
    sys.exit(main())
