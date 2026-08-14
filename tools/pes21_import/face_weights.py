"""Exports a face .fmdl's skf_* skin weights for the engine FaceRig.

Output (plain text, one record per line):

  bone,<name>,<px>,<py>,<pz>            face bone bind pivots (GF coords)
  v,<index>,<x>,<y>,<z>,<bone>:<w>[,<bone>:<w>...]   head vertices + weights

Only vertices with at least one skf_* influence are listed; the FaceRig
leaves the rest of the head static. Coordinates are GF space (Z up),
relative to the model origin (the humanoid attaches them at the head node).

  python3 face_weights.py face_high.fmdl faceweights.txt --fmdl-lib <dir>
"""

import argparse
import sys


def export(fmdl_path, out_path, fmdl_lib):
    sys.path.insert(0, fmdl_lib)
    import FmdlFile
    fmdl = FmdlFile.FmdlFile()
    fmdl.readFile(fmdl_path)

    bones = {}
    for bone in fmdl.bones:
        if bone.name.startswith("skf_"):
            g = bone.globalPosition
            bones[bone.name] = (g.x, -g.z, g.y)

    lines = []
    for name, (x, y, z) in sorted(bones.items()):
        lines.append("bone,%s,%.6f,%.6f,%.6f" % (name, x, y, z))

    count = 0
    seen = set()
    for mesh in fmdl.meshes:
        for vertex in mesh.vertices:
            if id(vertex) in seen or not vertex.boneMapping:
                continue
            seen.add(id(vertex))
            weights = [(b.name, w) for b, w in vertex.boneMapping.items()
                       if b.name.startswith("skf_") and w > 0.01]
            if not weights:
                continue
            total = sum(w for _, w in weights)
            p = vertex.position
            entry = ",".join("%s:%.4f" % (b, w / total) for b, w in
                             sorted(weights, key=lambda bw: -bw[1])[:4])
            lines.append("v,%d,%.6f,%.6f,%.6f,%s"
                         % (count, p.x, -p.z, p.y, entry))
            count += 1

    open(out_path, "w").write("\n".join(lines) + "\n")
    return len(bones), count


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("fmdl")
    parser.add_argument("out")
    parser.add_argument("--fmdl-lib", required=True)
    args = parser.parse_args()
    nbones, nverts = export(args.fmdl, args.out, args.fmdl_lib)
    print("wrote %s: %d skf bones, %d weighted vertices" %
          (args.out, nbones, nverts))
