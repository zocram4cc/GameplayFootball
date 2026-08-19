"""Renders an .ase to a PNG, offline and deterministically.

For looking at an imported model without a running engine. The engine's own viewer
(gfviewer) shows what the game sees and is the right tool once it runs; this shows what
the file contains, which is what you want when the question is whether the import kept
the geometry.

Solid, z-buffered, flat-shaded from a single headlamp, framed from the model's own
bounds by the same rules as src/utils/viewercamera.cpp.

  python3 ase_render.py <model.ase> <out.png> [--size 700] [--yaw 30] [--pitch 12]
                        [--mesh NAME] [--wire]
"""

import argparse
import math
import os
import re
import struct
import sys
import zlib

VERTEX = re.compile(r'\*MESH_VERTEX\s+(\d+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)')
FACE = re.compile(r'\*MESH_FACE\s+(\d+):\s+A:\s+(\d+)\s+B:\s+(\d+)\s+C:\s+(\d+)')
NAME = re.compile(r'\*NODE_NAME "([^"]+)"')


def read(path, only=None):
    """-> [(name, vertices, faces)] for each GEOMOBJECT."""
    out = []
    for chunk in open(path, errors="ignore").read().split("*GEOMOBJECT")[1:]:
        match = NAME.search(chunk)
        name = match.group(1) if match else "?"
        if only and only not in name:
            continue
        verts = {}
        for m in VERTEX.finditer(chunk):
            verts[int(m.group(1))] = tuple(float(m.group(i)) for i in (2, 3, 4))
        faces = []
        for m in FACE.finditer(chunk):
            tri = tuple(int(m.group(i)) for i in (2, 3, 4))
            if all(i in verts for i in tri):
                faces.append(tri)
        if verts:
            out.append((name, verts, faces))
    return out


def png(path, width, height, pixels):
    """A PNG with no dependencies: one filter-0 scanline per row."""
    raw = b"".join(b"\x00" + bytes(pixels[y * width * 3:(y + 1) * width * 3])
                   for y in range(height))
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def render(meshes, size, yaw, pitch, wire=False):
    points = [p for _, verts, _ in meshes for p in verts.values()]
    if not points:
        return None
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    target = [(lo[i] + hi[i]) * 0.5 for i in range(3)]
    radius = max(max(hi[i] - lo[i] for i in range(3)), 0.2) * 0.5
    fov = 35.0
    distance = radius / math.tan(math.radians(fov) * 0.5) * 1.35

    # the same orbit as ViewerCamera: yaw 0 puts the eye on -y
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    eye = [target[0] + distance * cp * sy,
           target[1] - distance * cp * cy,
           target[2] + distance * sp]
    forward = [target[i] - eye[i] for i in range(3)]
    flen = math.sqrt(sum(c * c for c in forward)) or 1.0
    forward = [c / flen for c in forward]
    right = [forward[1], -forward[0], 0.0]
    rlen = math.sqrt(sum(c * c for c in right)) or 1.0
    right = [c / rlen for c in right]
    up = [right[1] * forward[2] - right[2] * forward[1],
          right[2] * forward[0] - right[0] * forward[2],
          right[0] * forward[1] - right[1] * forward[0]]

    scale = size * 0.5 / math.tan(math.radians(fov) * 0.5)
    depth = [1e30] * (size * size)
    pixels = bytearray()
    for _ in range(size * size):
        pixels += b"\x1a\x1c\x20"

    def project(p):
        d = [p[i] - eye[i] for i in range(3)]
        z = sum(d[i] * forward[i] for i in range(3))
        if z <= 0.02:
            return None
        x = sum(d[i] * right[i] for i in range(3))
        y = sum(d[i] * up[i] for i in range(3))
        return (size * 0.5 + x * scale / z, size * 0.5 - y * scale / z, z)

    for name, verts, faces in meshes:
        for tri in faces:
            world = [verts[i] for i in tri]
            screen = [project(p) for p in world]
            if any(s is None for s in screen):
                continue
            e1 = [world[1][i] - world[0][i] for i in range(3)]
            e2 = [world[2][i] - world[0][i] for i in range(3)]
            n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                 e1[0] * e2[1] - e1[1] * e2[0]]
            nlen = math.sqrt(sum(c * c for c in n))
            if nlen < 1e-12:
                continue
            n = [c / nlen for c in n]
            # a headlamp, and both faces lit so a one-sided mesh is not black
            lambert = abs(sum(-forward[i] * n[i] for i in range(3)))
            shade = 0.22 + 0.78 * lambert
            colour = (int(215 * shade), int(219 * shade), int(226 * shade))

            xs = [s[0] for s in screen]
            ys = [s[1] for s in screen]
            x0, x1 = max(0, int(min(xs))), min(size - 1, int(max(xs)) + 1)
            y0, y1 = max(0, int(min(ys))), min(size - 1, int(max(ys)) + 1)
            if x1 < x0 or y1 < y0:
                continue
            ax, ay = screen[0][0], screen[0][1]
            bx, by = screen[1][0], screen[1][1]
            cx, cxy = screen[2][0], screen[2][1]
            area = (bx - ax) * (cxy - ay) - (by - ay) * (cx - ax)
            if abs(area) < 1e-9:
                continue
            for py in range(y0, y1 + 1):
                for px in range(x0, x1 + 1):
                    w0 = ((bx - ax) * (py + 0.5 - ay) - (by - ay) * (px + 0.5 - ax)) / area
                    w1 = ((px + 0.5 - ax) * (cxy - ay) - (py + 0.5 - ay) * (cx - ax)) / area
                    if w0 < 0 or w1 < 0 or w0 + w1 > 1:
                        continue
                    z = screen[0][2] * (1 - w0 - w1) + screen[1][2] * w1 + screen[2][2] * w0
                    at = py * size + px
                    if z >= depth[at]:
                        continue
                    depth[at] = z
                    pixels[at * 3:at * 3 + 3] = bytes(colour)
    return pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("ase")
    parser.add_argument("out")
    parser.add_argument("--size", type=int, default=700)
    parser.add_argument("--yaw", type=float, default=30.0)
    parser.add_argument("--pitch", type=float, default=12.0)
    parser.add_argument("--mesh", default=None)
    args = parser.parse_args()

    meshes = read(args.ase, args.mesh)
    if not meshes:
        print("nothing to draw in", args.ase)
        return 1
    pixels = render(meshes, args.size, math.radians(args.yaw), math.radians(args.pitch))
    if pixels is None:
        print("no geometry in", args.ase)
        return 1
    png(args.out, args.size, args.size, pixels)
    faces = sum(len(f) for _, _, f in meshes)
    print("%s: %d mesh(es), %d face(s) -> %s"
          % (os.path.basename(args.ase), len(meshes), faces, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
