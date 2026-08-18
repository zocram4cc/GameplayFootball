"""Imports a stadium's own pitch art - what PES paints on its grass.

The engine grows its pitch procedurally (proceduralpitch.cpp: grass, a perlin
wobble, mowing bands) and then blends one image over the whole of it by that
image's alpha, sampled across the full pitch including the rim - 60 m by 40 m
either way, pitchFullHalfW/H. Until now that image was one file for every ground,
media/textures/pitch/overlay.png.

PES has a better one for every ground, and it is not a texture but a mesh: the
pack's pitch model is a flat sheet the size of the field with decals over it. In
st002 that is three meshes - the pitch surface carrying pitch_alp (2048 x 4096 of
mowing bands and worn patches), the line mesh tiling a strip texture, and a scuff
pass, pitch_scratch - each with its own UVs.

So this rasterises the mesh into the engine's overlay: every triangle mapped from
metres into overlay pixels and filled with what its decal texture holds at those
UVs, composited in the order PES draws them. The lines are left out by default,
because the engine paints its own and two sets of lines a few centimetres apart
look like a printing error; --lines puts them in.

    pitch_overlay.py <stadium pack dir> --out <stadium dir> [--textures <dir>]

writes pitch_overlay.png beside the stadium's .object, which the engine picks up
in place of the shared one.
"""

import argparse
import glob
import os
import sys

# gametypes.hpp: the overlay covers the pitch and its rim
PITCH_FULL_HALF_W = 60.0
PITCH_FULL_HALF_H = 40.0

# What a mesh of the pitch model is for, by the texture it carries. The line pass
# is PES's own markings; the engine draws those itself.
LINE_TEXTURES = ("line",)


def pitch_to_pixel(x, y, width, height):
    """Metres on the pitch -> pixels in the overlay, the way the engine samples it."""
    return (((x / PITCH_FULL_HALF_W) * 0.5 + 0.5) * width,
            ((y / PITCH_FULL_HALF_H) * 0.5 + 0.5) * height)


def barycentric(point, a, b, c):
    """-> the three weights of `point` in triangle abc, or None if it has no area."""
    area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])
    if abs(area) < 1e-12:
        return None
    wb = ((point[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (point[1] - a[1])) / area
    wc = ((b[0] - a[0]) * (point[1] - a[1]) - (point[0] - a[0]) * (b[1] - a[1])) / area
    return (1.0 - wb - wc, wb, wc)


def wrap(coordinate):
    """UVs outside 0..1 tile, which is how PES's line strip is laid along a line."""
    return coordinate % 1.0


def blank(width, height):
    """A transparent overlay: alpha 0 everywhere leaves the engine's own pitch."""
    import numpy
    return numpy.zeros((height, width, 4), dtype=numpy.uint8)


def _over(dst, src):
    """src composited over dst, both (r, g, b, a) 0..255."""
    src_alpha = src[3] / 255.0
    if src_alpha <= 0.0:
        return dst
    dst_alpha = dst[3] / 255.0
    out_alpha = src_alpha + dst_alpha * (1.0 - src_alpha)
    if out_alpha <= 0.0:
        return (0, 0, 0, 0)
    out = []
    for channel in range(3):
        value = (src[channel] * src_alpha +
                 dst[channel] * dst_alpha * (1.0 - src_alpha)) / out_alpha
        out.append(int(round(max(0.0, min(255.0, value)))))
    out.append(int(round(out_alpha * 255.0)))
    return tuple(out)


def rasterise_triangle(pixels, width, height, positions, uvs, sampler):
    """Fills one pitch triangle into the overlay from its decal texture.

    positions are in metres (the engine's x along the pitch, y across it); uvs are
    that triangle's texture coordinates; sampler(u, v) returns (r, g, b, a).
    """
    corners = [pitch_to_pixel(x, y, width, height) for x, y in positions]
    left = max(0, int(min(c[0] for c in corners)))
    right = min(width - 1, int(max(c[0] for c in corners)) + 1)
    bottom = max(0, int(min(c[1] for c in corners)))
    top = min(height - 1, int(max(c[1] for c in corners)) + 1)
    for py in range(bottom, top + 1):
        for px in range(left, right + 1):
            weights = barycentric((px + 0.5, py + 0.5), *corners)
            if weights is None:
                return
            if min(weights) < 0.0:
                continue
            u = sum(weights[i] * uvs[i][0] for i in range(3))
            v = sum(weights[i] * uvs[i][1] for i in range(3))
            sample = sampler(wrap(u), wrap(v))
            pixels[py][px] = _over(tuple(int(c) for c in pixels[py][px]), sample)


# gametypes.hpp again: where this engine paints its own touchlines and goal lines
PITCH_HALF_W = 55.0
PITCH_HALF_H = 36.0
# A measurement outside this is not a set of pitch markings
PITCH_PLAUSIBLE = (30.0, 80.0)


def fit_scale(line_half_x, line_half_y):
    """-> (sx, sy) stretching PES's marked field onto this engine's.

    PES marks a real 106 x 68 m pitch; this engine's is 110 x 72 and it paints its
    lines at pitchHalfW/H. Unscaled, PES's touchline would fall two metres inside
    ours - the ball would leave the field over open grass, and two sets of lines
    would sit side by side. Under 4% one way and 6% the other fixes that, and no
    crest or mowing band shows the stretch.
    """
    scale = []
    for measured, ours in ((line_half_x, PITCH_HALF_W), (line_half_y, PITCH_HALF_H)):
        if not measured or not (PITCH_PLAUSIBLE[0] <= measured <= PITCH_PLAUSIBLE[1]):
            scale.append(1.0)
        else:
            scale.append(ours / measured)
    return (scale[0], scale[1])


def is_line_pass(texture_name):
    """Whether a pitch mesh carries PES's line markings rather than its grass art."""
    if not texture_name:
        return False
    stem = os.path.basename(str(texture_name)).lower()
    return any(stem.startswith(prefix) for prefix in LINE_TEXTURES)


def worth_writing(pixels):
    """Whether an overlay paints anything at all.

    The engine blends this by its alpha, so an overlay with none is invisible -
    and installing it would replace the shared file, which carries faint markings
    and a little wear belonging to no team, with a blank. A ground whose pitch
    model has only its line pass (Planet Namek) is better off with the shared one.
    """
    return bool((pixels[:, :, 3] > 0).any())


def fit_texture(image, width, height):
    """A decal reduced to the overlay's own resolution, averaging as it goes.

    Sampling a 2048 x 4096 decal point by point into a 2048 x 1024 overlay keeps
    every fourth row and throws the rest away, which combed st002's painted crests
    into horizontal stripes. Averaging first is what a mip level is for.
    """
    from PIL import Image
    target = (min(image.size[0], width), min(image.size[1], height))
    if target == image.size:
        return image
    return image.resize(target, Image.LANCZOS)


def _sampler_for(image):
    """-> sampler(u, v) reading `image` (a PIL image), Fox's v downwards."""
    import numpy
    array = numpy.asarray(image.convert("RGBA"))
    height, width = array.shape[0], array.shape[1]

    def sample(u, v):
        x = min(width - 1, max(0, int(u * width)))
        y = min(height - 1, max(0, int(v * height)))
        return tuple(int(c) for c in array[y][x])

    return sample


def _decode_texture(texture, ftex_index, cache):
    """-> a PIL image for a mesh's texture, or None if the pack does not ship it."""
    import stadium_to_gf
    from PIL import Image
    stem = stadium_to_gf._tex_stem(texture.filename)
    if stem in cache:
        return cache[stem]
    path = ftex_index.get(stem)
    image = None
    if path:
        try:
            if path.lower().endswith(".ftex"):
                import ftex
                image = ftex.to_pil(path) if hasattr(ftex, "to_pil") else None
                if image is None:
                    import tempfile
                    with tempfile.TemporaryDirectory() as tmp:
                        png = os.path.join(tmp, stem + ".png")
                        ftex.convert(path, png)
                        image = Image.open(png).copy()
            else:
                image = Image.open(path).copy()
        except Exception as exc:  # a pack can ship a texture we cannot read
            print("  could not read %s: %s" % (os.path.basename(path), exc))
            image = None
    cache[stem] = image
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("pack", help="a stadium pack directory (it holds pitch/)")
    parser.add_argument("--out", required=True, help="the converted stadium's directory")
    parser.add_argument("--textures", action="append", default=[])
    parser.add_argument("--fmdl-lib", default=None)
    parser.add_argument("--width", type=int, default=2048)
    parser.add_argument("--height", type=int, default=1024)
    parser.add_argument("--lines", action="store_true",
                        help="include PES's line markings; off by default, because the "
                             "engine paints its own and two sets never line up")
    args = parser.parse_args()

    if args.fmdl_lib and args.fmdl_lib not in sys.path:
        sys.path.insert(0, args.fmdl_lib)
    import stadium_to_gf

    models = sorted(glob.glob(os.path.join(args.pack, "pitch", "**", "*.fmdl"), recursive=True))
    if not models:
        print("no pitch model under %s/pitch" % args.pack)
        return 1
    fmdl = stadium_to_gf._load_fmdl(models[0], args.fmdl_lib)
    print("pitch model: %s, %d mesh(es)" % (os.path.basename(models[0]), len(fmdl.meshes)))

    ftex_index = stadium_to_gf.build_ftex_index(
        stadium_to_gf.find_texture_dirs(args.pack, *args.textures))

    # The markings say how big PES thinks this pitch is, so the art can be fitted
    # to the field the engine actually plays on.
    line_half_x = line_half_y = None
    for mesh in fmdl.meshes:
        texture = stadium_to_gf._mesh_base_texture(mesh)
        if not is_line_pass(getattr(texture, "filename", None)):
            continue
        line_half_x = max(abs(v.position.x) for v in mesh.vertices)
        line_half_y = max(abs(v.position.z) for v in mesh.vertices)
        break
    scale_x, scale_y = fit_scale(line_half_x, line_half_y)
    if (scale_x, scale_y) != (1.0, 1.0):
        print("  markings at %.1f x %.1f m -> stretched by %.3f, %.3f onto %.0f x %.0f"
              % (line_half_x, line_half_y, scale_x, scale_y, PITCH_HALF_W, PITCH_HALF_H))

    pixels = blank(args.width, args.height)
    cache = {}
    drawn = 0
    for index, mesh in enumerate(fmdl.meshes):
        texture = stadium_to_gf._mesh_base_texture(mesh)
        name = getattr(texture, "filename", None)
        if is_line_pass(name) and not args.lines:
            print("  mesh %d: %s - the engine paints its own lines, skipped" % (index, name))
            continue
        image = _decode_texture(texture, ftex_index, cache) if texture else None
        if image is not None:
            image = fit_texture(image, args.width, args.height)
        if image is None:
            print("  mesh %d: no texture (%s), skipped" % (index, name))
            continue
        sampler = _sampler_for(image)
        for face in mesh.faces:
            corners = [(v.position.x * scale_x, -v.position.z * scale_y) for v in face.vertices]
            uvs = [(v.uv[0].u, v.uv[0].v) if v.uv else (0.0, 0.0) for v in face.vertices]
            rasterise_triangle(pixels, args.width, args.height, corners, uvs, sampler)
        drawn += 1
        print("  mesh %d: %s over %d triangle(s)" % (index, name, len(mesh.faces)))

    if not drawn:
        print("nothing to paint: leaving the engine's own overlay")
        return 1
    if not worth_writing(pixels):
        print("this ground paints nothing on its grass: leaving the engine's own overlay")
        return 1

    from PIL import Image
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "pitch_overlay.png")
    Image.fromarray(pixels, "RGBA").save(path)
    covered = int((pixels[:, :, 3] > 0).sum())
    print("wrote %s (%d x %d, %.0f%% of it painted)"
          % (path, args.width, args.height, 100.0 * covered / (args.width * args.height)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
