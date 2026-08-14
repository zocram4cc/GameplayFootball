"""Shared ASE-writing helpers for the pes21 import tools.

The engine's ASELoader hard-requires a MESH_NORMALS block per mesh
(FatalError without one), so every generated GEOMOBJECT must emit face and
vertex normals.
"""

import math


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _normalize(v):
    n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return (v[0] / n, v[1] / n, v[2] / n) if n > 1e-12 else (0.0, 0.0, 1.0)


def write_mesh_normals(out, vertices, faces, smooth=True):
    """Emits *MESH_NORMALS for faces=[(a,b,c)] over vertices=[(x,y,z)].

    smooth=True averages face normals per vertex (organic meshes);
    smooth=False writes flat face normals (stadium/crowd billboards).
    """
    face_normals = []
    for a, b, c in faces:
        n = _normalize(_cross(_sub(vertices[b], vertices[a]),
                              _sub(vertices[c], vertices[a])))
        face_normals.append(n)

    vertex_normals = None
    if smooth:
        acc = [[0.0, 0.0, 0.0] for _ in vertices]
        for (a, b, c), n in zip(faces, face_normals):
            for idx in (a, b, c):
                acc[idx][0] += n[0]
                acc[idx][1] += n[1]
                acc[idx][2] += n[2]
        vertex_normals = [_normalize(tuple(v)) for v in acc]

    out.write("\t\t*MESH_NORMALS {\n")
    for i, ((a, b, c), fn) in enumerate(zip(faces, face_normals)):
        out.write("\t\t\t*MESH_FACENORMAL %d\t%.4f\t%.4f\t%.4f\n"
                  % (i, fn[0], fn[1], fn[2]))
        for idx in (a, b, c):
            vn = vertex_normals[idx] if smooth else fn
            out.write("\t\t\t\t*MESH_VERTEXNORMAL %d\t%.4f\t%.4f\t%.4f\n"
                      % (idx, vn[0], vn[1], vn[2]))
    out.write("\t\t}\n")
