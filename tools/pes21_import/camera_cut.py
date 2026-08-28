"""PES 2021 cutscene camera reader — .fdc cut tables and their embedded .canm streams.

Source data (all of it lives in ONE cpk):

  PES21/download/dt12_g4.cpk
    common/demo/fixdemo/<category>/cut_data/*.fdc      4804 files
    common/demo/fixdemo/<category>/table_<category>.bin   9 files
    where <category> in {change, end, ent, foul, goal, mode, pk, result, timeup}

1726 of the 4804 .fdc carry camera animation; between them they hold 5104
embedded .canm streams (1 273 980 camera frames at 30 fps ~= 11.8 hours of
authored camerawork). Konami's naming: "cut" = one camera shot, .fdc = "fix
demo cut" table, .canm = "camera animation".

--------------------------------------------------------------------------
FDC container
--------------------------------------------------------------------------
A generic Fox-style serialised container, nestable, with a name pool at the
end of each container's own slice:

  u32 entryCount
  entryCount x { u32 dataOffset, u32 dataSize, u32 nameOffset }   # 12 bytes
  <entry data, strictly contiguous, first entry starts right after the table>
  <name pool: NUL-terminated names, dataOffset/nameOffset are container-relative>

Every .fdc root has this shape. Root entries are:
  * exactly one nested container whose name is the file's own cpk path
    ("cpk_dat/common/demo/fixdemo/goal/cut_data/foo.fdc") -> the CUT TABLE
  * zero or more CANM blobs (magic 01 00 01 ff), named
    "<category>/canm/<clip>NNNN.canm"
  * leaf blobs named *.gani / *.seq / *.ask (actor animation payloads, often
    4-byte stubs pointing at common/demo/anime/FoxAnim/FixDemo/Animations/)

--------------------------------------------------------------------------
Cut table records
--------------------------------------------------------------------------
Inside the cut table, each entry's "name" is a single binary byte = the record
type, and the type fixes the record size exactly (verified over all 4804 files,
no exceptions):

  tag  size    count  meaning (inferred from the name field at +0x0c)
  0x00 0x040    2389  sequence header (one per table)
  0x01 0x09c    1399  scene object ref (*.xml under common/demo/fixdemoobj/)
  0x02 0x05c    3447  ?
  0x03 0x0bc    5850  ?
  0x04 0x16c    6274  actor animation cut (*.gani)
  0x05 0x1bc    4973  model/skeleton ref (*.fpk, *.skl)
  0x06 0x11c    9031  CAMERA CUT (*.canm)   <-- what this module reads
  0x07 0x144     626  ?
  0x08 0x060       3  ?

Actor cut record (tag 0x04, 364 bytes) — the per-actor choreography of the
_pl (player) and _mob (crowd/staff) packs, one record per actor slot:
  +0x00 u16    actor slot. In ent _pl packs: 0-10 home XI, 11-21 away XI,
               22-24 the officials (referee + assistants)
  +0x02 u16    1 in every observed record
  +0x04 f32[3] spawn position (x, y, z), Fox metres, Y up, pitch-centre origin
  +0x10 f32    spawn yaw, DEGREES about +Y; 0 faces +Z, 90 faces +X
               (verified: the GK at (-40, 0, 0) yaw 90 faces the centre spot)
  +0x14 char[0x80] gani path ("cpk_dat/common/demo/anime/FoxAnim/FixDemo/
               Animations/dml_ent_kickoff02_idle08.gani"); the fdc carries the
               same path as a 4-byte stub entry — the real gani ships loose in
               dt12 under that path
  +0x94 char[0x80] seq path (same stem, .seq: a small event/marker table -
               u16 frame windows, sync points; NOT motion data)
  +0x114 f32   phase offset into the clip, gani ticks (1/59.94 s). The clip
               loops over the demo; offsets beyond the clip length wrap, and
               -60 appears once (start 60 ticks before the demo clock)
  +0x118 u32   flags (0 / 1 / 256 observed)
  +0x120 u8[]  small flag bytes (01 04 0a 00 ... / 01 04 00 00 ...)
The walk/warmup motion itself is bind-relative root motion inside the gani
(RIG_ROOT XZ+yaw, max ~3 m over any ent clip): entrances are staged as
placements + near-in-place clips, with the camera cuts hiding repositions
between the packs of a family.

Camera cut record (tag 0x06, 284 bytes):
  +0x00 u32    startFrame        (frame in the demo timeline where this shot begins;
                                 records ascend, typically in 0/10/100/110/200/... pairs)
  +0x04 f32    unknown, 0.0 in 5430 of 9031 records; elsewhere values like 120,
               150, 180 and multiples of 840 that step up across consecutive cuts
  +0x08 u16    0xffff            (constant in every observed record)
  +0x0a u16    unknown, 0 in 8415 records
  +0x0c char[] canm name, NUL-terminated, zero-filled to +0x90
               ("goal/canm/goal_2018_run_30_cam_Z_fromL.canm"); empty = no clip
               (6620 of 9031 records name a clip)
  +0x90 f32    near clip, metres — OVERRIDES the clip's channel 3 (differs in
               5960 of 6620 clip-bearing records, e.g. near 5 / far 300 over the
               clip's own 1 / 100)
  +0x94 f32    far clip, metres — overrides channel 4
  +0x98 f32    1.0 (8711), else 0.5 / 0.75 / 0.25 — looks like a weight
  +0x9c..0xb0  unknown
  +0xb0 u8     small enum: 4 (7900), 6 (609), 5 (518)
  +0xb4 u32    unknown, 50 (6718), 90 (1519), 95, 80, 60... only equals the
               clip's frame count in 17 records, so it is NOT the shot duration
  +0xb8..0xd8  0xffffffff-filled slots (unused indices)
  +0xd8..0xf0  24 x u8 in {0, 50, 100} — per-slot blend/visibility percentages
  +0xf0 f32[11] {0,0,0, 2.8, 2.8, aspect..., 0.0, 30.0}; last is the frame rate

--------------------------------------------------------------------------
CANM — per-frame camera stream
--------------------------------------------------------------------------
  0x00 u16  version    always 1
  0x02 u16  magic      0xff01
  0x04 u32  frameCount (last frame index; keyCount == frameCount + 1)
  0x08 u32  frameRate  always 30
  0x0c u32  trackCount always 1
  0x10 u32  trackOffset (always 0x18)
  0x14 u32  pad

  track @ trackOffset:
    u16 channelCount (always 6)
    u16 0xff01
    u32 pad
    u32 channelOffsets[channelCount]   # relative to trackOffset

  channel:
    +0x00 u32 type        1 = quaternion, 4 = vector4, 8 = float
    +0x04 u16 keyCount
    +0x06 u16 channelIndex
    +0x08 u32 timesOffset  (channel-relative) -> u16 frame index per key
    +0x0c u32 valuesOffset (channel-relative) -> 4 floats (type 1/4) or 1 float (type 8)
    (values of the last channel are padded to a 16-byte boundary)

The 6 channels are identical in all 5104 streams — signature
"0:1/16|1:4/16|2:8/4|3:8/4|4:8/4|5:8/16":

  0  quat   rotation, raw float32 x,y,z,w — always exactly unit length
  1  vec4   position x,y,z in METRES, w == 1.0
  2  float  vertical field of view in DEGREES
  3  float  near clip plane, metres  (one key)
  4  float  far clip plane, metres   (one key)
  5  float  aspect ratio, 1.49995 or 1.5 (one key)

Coordinate system (Fox / PES): right-handed, Y up, ORIGIN AT THE PITCH CENTRE
SPOT, units metres. X runs along the pitch length — goal lines at X = +-52.5,
which is how the units were pinned down: pk_01_intro_setBall00_cam01.fdc has a
camera parked at exactly (52.500, 1.000, 7.000) and another at (-55.550, 1.000,
0.600), i.e. 3.05 m behind the other goal line. Z runs along the pitch width
(+-34 at the sidelines). The camera looks along its local -Z with +Y up, the
usual OpenGL/Maya eye basis.

FOV cross-check: the hot values are exactly Maya vertical FOVs for a 24 mm
(0.945") vertical film aperture, FOV = 2*atan(12 / focal_mm):
  46.40 deg = 28 mm    41.11 deg = 32 mm    43.60 deg = 30 mm
  53.13 deg = 24 mm    51.28 deg = 25 mm    26.99 deg = 50 mm
  22.62 deg = 60 mm    37.85 deg = 35 mm
and aspect 1.5 = 36 mm / 24 mm. The streams are Maya camera bakes; several cut
files are even named *_mayaL0x / *_mayaL1x.

--------------------------------------------------------------------------
Mapping onto GameplayFootball
--------------------------------------------------------------------------
GF is right-handed Z up, metres, pitch centred at the origin, and its camera
also looks down local -Z with +Y up (verified against the pre-kickoff orbit in
Match::UpdateIngameCamera, src/onthepitch/match.cpp). So only the world basis
differs, by a +90 deg rotation about X:

  X_gf = X_fox      Y_gf = -Z_fox      Z_gf = Y_fox

which is exactly q = (sin45, 0, 0, cos45) applied on the left to both the
position and the rotation. See to_gf_position() / to_gf_quaternion().
GF's cameraFOV has the same meaning as channel 2 (full vertical angle in
degrees, Matrix4::ConstructProjection uses tan(fov * pi/360)), so it copies
straight across; see gf_fov() for the 3:2 -> screen-aspect correction.
"""

import math
import os
import struct
import sys

CANM_MAGIC = b"\x01\x00\x01\xff"

# cut-table record type tags -> exact record size
RECORD_SIZES = {
    0x00: 0x040,   # sequence header
    0x01: 0x09C,   # scene object (*.xml)
    0x02: 0x05C,
    0x03: 0x0BC,
    0x04: 0x16C,   # actor animation cut (*.gani)
    0x05: 0x1BC,   # model / skeleton (*.fpk, *.skl)
    0x06: 0x11C,   # camera cut (*.canm) - PES17-21's size; see CAMERA_CUT_SIZES
    0x07: 0x144,
    0x08: 0x060,
}
# PES16's camera-cut record is 8 bytes shorter (0x114, 276 bytes) than every
# later generation's (0x11C, 284): measured across every foul/goal pack in
# PES16/17/19. It lacks the trailing per-slot blend array and frame-rate float
# PES17 added; the fields actually used here (start frame, canm name,
# near/far) sit at identical offsets in both, well inside the shorter one.
CAMERA_CUT_SIZES = (0x114, 0x11C)
TAG_ACTOR_CUT = 0x04
TAG_CAMERA_CUT = 0x06

# canm channel indices
CH_ROTATION = 0
CH_POSITION = 1
CH_FOV = 2
CH_NEAR = 3
CH_FAR = 4
CH_ASPECT = 5

# Fox pitch reference (metres) used for the optional pitch rescale
FOX_PITCH_HALF_LENGTH = 52.5
FOX_PITCH_HALF_WIDTH = 34.0

_SQRT_HALF = math.sqrt(0.5)


# --------------------------------------------------------------------------
# generic container
# --------------------------------------------------------------------------

class Entry:
    """One entry of an FDC container."""

    def __init__(self, buf, offset, size, name):
        self.buf = buf          # the whole container slice
        self.offset = offset    # container-relative
        self.size = size
        self.name = name        # decoded name-pool string (may be binary-ish)

    @property
    def data(self):
        return self.buf[self.offset:self.offset + self.size]

    @property
    def tag(self):
        """First byte of the name, i.e. the record type in a cut table.

        Type 0x00's name is the empty string, so an empty name means tag 0.
        """
        return self.name_raw[0] if self.name_raw else 0x00

    def __repr__(self):
        return "Entry(off=%#x size=%#x name=%r)" % (self.offset, self.size, self.name)


def read_container(buf):
    """Parse an FDC container out of ``buf``. Returns a list of Entry, or None.

    Validation is strict — entry data must tile the buffer contiguously starting
    right after the offset table — which is what makes container-vs-leaf sniffing
    reliable.
    """
    if len(buf) < 4:
        return None
    count = struct.unpack_from("<I", buf, 0)[0]
    if count == 0 or count > 10000 or 4 + 12 * count > len(buf):
        return None
    entries = []
    expect = 4 + 12 * count
    for i in range(count):
        offset, size, name_offset = struct.unpack_from("<III", buf, 4 + 12 * i)
        if offset != expect:
            return None
        expect = offset + size
        if expect > len(buf):
            return None
        entries.append((offset, size, name_offset))
    out = []
    for offset, size, name_offset in entries:
        raw = b""
        if name_offset < len(buf):
            end = buf.find(b"\0", name_offset)
            raw = buf[name_offset:len(buf) if end < 0 else end]
        e = Entry(buf, offset, size, raw.decode("ascii", "replace"))
        e.name_raw = raw
        out.append(e)
    return out


# --------------------------------------------------------------------------
# canm
# --------------------------------------------------------------------------

class Channel:
    def __init__(self, type_, index, keys):
        self.type = type_
        self.index = index
        self.keys = keys        # [(frame, (v0, ...)), ...]

    @property
    def scalar(self):
        """First key's first component — for the single-key channels."""
        return self.keys[0][1][0]

    def sample(self, frame):
        """Linear sample of the raw components at ``frame`` (no slerp)."""
        keys = self.keys
        if frame <= keys[0][0]:
            return keys[0][1]
        if frame >= keys[-1][0]:
            return keys[-1][1]
        lo = 0
        hi = len(keys) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if keys[mid][0] <= frame:
                lo = mid
            else:
                hi = mid
        f0, v0 = keys[lo]
        f1, v1 = keys[hi]
        t = 0.0 if f1 == f0 else (frame - f0) / float(f1 - f0)
        return tuple(a + (b - a) * t for a, b in zip(v0, v1))


class Canm:
    """One camera shot: a per-frame position / rotation / lens stream."""

    def __init__(self, name=""):
        self.name = name
        self.frame_count = 0
        self.frame_rate = 30
        self.channels = {}

    # --- convenience accessors -------------------------------------------

    @property
    def key_count(self):
        return len(self.channels[CH_POSITION].keys)

    @property
    def duration_s(self):
        return self.frame_count / float(self.frame_rate)

    @property
    def near(self):
        return self.channels[CH_NEAR].scalar

    @property
    def far(self):
        return self.channels[CH_FAR].scalar

    @property
    def aspect(self):
        return self.channels[CH_ASPECT].scalar

    def frames(self):
        """Frame indices actually keyed (dense: 0..frame_count in every file seen)."""
        return [f for f, _ in self.channels[CH_POSITION].keys]

    def position(self, frame):
        v = self.channels[CH_POSITION].sample(frame)
        return (v[0], v[1], v[2])

    def rotation(self, frame):
        """Quaternion (x, y, z, w) at ``frame``, slerped between keys."""
        keys = self.channels[CH_ROTATION].keys
        if frame <= keys[0][0]:
            return keys[0][1]
        if frame >= keys[-1][0]:
            return keys[-1][1]
        lo, hi = 0, len(keys) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if keys[mid][0] <= frame:
                lo = mid
            else:
                hi = mid
        f0, q0 = keys[lo]
        f1, q1 = keys[hi]
        t = 0.0 if f1 == f0 else (frame - f0) / float(f1 - f0)
        return slerp(q0, q1, t)

    def fov(self, frame):
        return self.channels[CH_FOV].sample(frame)[0]

    def sample(self, frame):
        """dict of everything at ``frame``, in Fox space."""
        return {
            "frame": frame,
            "position": self.position(frame),
            "rotation": self.rotation(frame),
            "fov": self.fov(frame),
            "near": self.near,
            "far": self.far,
            "aspect": self.aspect,
        }


def parse_canm(blob, name=""):
    if blob[:4] != CANM_MAGIC:
        raise ValueError("not a canm stream (magic %r)" % blob[:4])
    version, _magic, frame_count, frame_rate, track_count, track_offset = \
        struct.unpack_from("<HHIIII", blob, 0)
    if version != 1:
        raise ValueError("unsupported canm version %d" % version)
    if track_count != 1:
        raise ValueError("expected 1 track, got %d" % track_count)

    canm = Canm(name)
    canm.frame_count = frame_count
    canm.frame_rate = frame_rate

    channel_count = struct.unpack_from("<H", blob, track_offset)[0]
    offsets = [struct.unpack_from("<I", blob, track_offset + 8 + 4 * i)[0] + track_offset
               for i in range(channel_count)]
    ends = offsets[1:] + [len(blob)]

    for base, end in zip(offsets, ends):
        type_ = struct.unpack_from("<I", blob, base)[0]
        key_count, index = struct.unpack_from("<HH", blob, base + 4)
        times_offset, values_offset = struct.unpack_from("<II", blob, base + 8)
        ncomp = 4 if type_ in (1, 4) else 1
        stride = 4 * ncomp
        fmt = "<%df" % ncomp
        keys = []
        for k in range(key_count):
            frame = struct.unpack_from("<H", blob, base + times_offset + 2 * k)[0]
            values = struct.unpack_from(fmt, blob, base + values_offset + stride * k)
            keys.append((frame, values))
        canm.channels[index] = Channel(type_, index, keys)
    return canm


# --------------------------------------------------------------------------
# .fdc
# --------------------------------------------------------------------------

class CameraCut:
    """One tag-0x06 record of a cut table: when a shot starts and which clip."""

    def __init__(self, data):
        self.start_frame, self.unknown04 = struct.unpack_from("<II", data, 0)
        self.unknown08, self.unknown0a = struct.unpack_from("<HH", data, 8)
        end = data.find(b"\0", 0x0C)
        self.canm_name = data[0x0C:end].decode("ascii", "replace") if end > 0x0C else ""
        self.near, self.far, self.unknown98 = struct.unpack_from("<3f", data, 0x90)
        self.kind = data[0xB0]
        self.unknown_b4 = struct.unpack_from("<I", data, 0xB4)[0]
        self.blend = list(data[0xD8:0xF0])
        # PES16's record ends here (276 bytes total) - it does not carry the
        # trailing frame-rate float PES17 added at +0xF0.
        self.trailing = struct.unpack_from("<11f", data, 0xF0) if len(data) >= 0xF0 + 44 else ()

    @property
    def frame_rate(self):
        # PES ships every camera at a constant 30 fps regardless of
        # generation (measured across all four), so that is the honest
        # default for a record too short to carry the field.
        return self.trailing[10] if len(self.trailing) > 10 else 30.0

    @property
    def unknown04_float(self):
        return struct.unpack("<f", struct.pack("<I", self.unknown04))[0]

    def __repr__(self):
        return "CameraCut(start=%d near=%g far=%g canm=%r)" % (
            self.start_frame, self.near, self.far, self.canm_name)


class ActorCut:
    """One tag-0x04 record: where an actor spawns and which clip he plays.

    The _pl packs place the 22 players (slots 0-10 home, 11-21 away) and the
    officials (22-24); position/yaw are Fox space (Y up, metres, degrees).
    """

    def __init__(self, data):
        self.slot, self.unknown02 = struct.unpack_from("<HH", data, 0)
        self.position = struct.unpack_from("<3f", data, 4)
        self.yaw_deg = struct.unpack_from("<f", data, 0x10)[0]
        self.gani_path = _cstr(data, 0x14, 0x80)
        self.seq_path = _cstr(data, 0x94, 0x80)
        self.phase_ticks = struct.unpack_from("<f", data, 0x114)[0]
        self.flags = struct.unpack_from("<I", data, 0x118)[0]

    @property
    def gani_name(self):
        return os.path.basename(self.gani_path)

    def __repr__(self):
        return "ActorCut(slot=%d pos=(%.2f, %.2f, %.2f) yaw=%.1f phase=%g %s)" % (
            (self.slot,) + self.position + (self.yaw_deg, self.phase_ticks, self.gani_name))


def _cstr(data, offset, maxlen):
    end = data.find(b"\0", offset)
    if end < 0 or end > offset + maxlen:
        end = offset + maxlen
    return data[offset:end].decode("ascii", "replace")


class Fdc:
    def __init__(self, path=""):
        self.path = path
        self.cpk_path = ""       # the file's own path as recorded inside it
        self.cuts = []           # CameraCut, in table order
        self.actors = []         # ActorCut, in table order
        self.cameras = []        # Canm, in file order
        self.records = {}        # tag -> [Entry] of the cut table
        self.assets = []         # (name, size) of leaf refs (*.gani, *.seq, *.ask)

    def camera(self, name):
        for c in self.cameras:
            if c.name == name or os.path.basename(c.name) == name:
                return c
        return None

    def timeline(self):
        """[(CameraCut, Canm or None)] for the cuts that reference a clip."""
        out = []
        for cut in self.cuts:
            if not cut.canm_name:
                continue
            out.append((cut, self.camera(cut.canm_name)))
        return out


def parse_fdc(blob, path=""):
    fdc = Fdc(path)
    entries = read_container(blob)
    if entries is None:
        raise ValueError("not an fdc container")
    for entry in entries:
        data = entry.data
        if data[:4] == CANM_MAGIC:
            fdc.cameras.append(parse_canm(data, entry.name))
            continue
        sub = read_container(data)
        if sub is None:
            fdc.assets.append((entry.name, entry.size))
            continue
        # nested container -> the cut table
        fdc.cpk_path = entry.name
        for rec in sub:
            fdc.records.setdefault(rec.tag, []).append(rec)
            if rec.tag == TAG_CAMERA_CUT and rec.size in CAMERA_CUT_SIZES:
                fdc.cuts.append(CameraCut(rec.data))
            elif rec.tag == TAG_ACTOR_CUT and rec.size == RECORD_SIZES[TAG_ACTOR_CUT]:
                fdc.actors.append(ActorCut(rec.data))
    fdc.cuts.sort(key=lambda c: c.start_frame)
    return fdc


def load(path):
    with open(path, "rb") as fp:
        return parse_fdc(fp.read(), path)


# --------------------------------------------------------------------------
# quaternion helpers
# --------------------------------------------------------------------------

def quat_mul(a, b):
    """(x, y, z, w) Hamilton product, a applied after b."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def quat_rotate(q, v):
    """Rotate vector ``v`` by quaternion ``q`` = (x, y, z, w)."""
    x, y, z, w = q
    vx, vy, vz = v
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def slerp(a, b, t):
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0.0:
        b = tuple(-x for x in b)
        dot = -dot
    if dot > 0.9995:
        out = tuple(p + (q - p) * t for p, q in zip(a, b))
    else:
        theta = math.acos(max(-1.0, min(1.0, dot)))
        st = math.sin(theta)
        wa = math.sin((1.0 - t) * theta) / st
        wb = math.sin(t * theta) / st
        out = tuple(p * wa + q * wb for p, q in zip(a, b))
    n = math.sqrt(sum(x * x for x in out)) or 1.0
    return tuple(x / n for x in out)


def forward(q):
    """Camera view direction: local -Z rotated by ``q``."""
    return quat_rotate(q, (0.0, 0.0, -1.0))


def up(q):
    return quat_rotate(q, (0.0, 1.0, 0.0))


def ground_hit(position, q, plane_height=0.0, up_axis=1):
    """Where the view ray meets a horizontal plane, or None if it never does."""
    f = forward(q)
    if abs(f[up_axis]) < 1e-6:
        return None
    t = (plane_height - position[up_axis]) / f[up_axis]
    if t <= 0.0:
        return None
    return tuple(position[i] + t * f[i] for i in range(3)), t


# --------------------------------------------------------------------------
# GameplayFootball conversion
# --------------------------------------------------------------------------

# +90 deg about X: Y_fox -> Z_gf, Z_fox -> -Y_gf
FOX_TO_GF_QUAT = (_SQRT_HALF, 0.0, 0.0, _SQRT_HALF)


def to_gf_position(p, pitch_half_w=None, pitch_half_h=None):
    """Fox (x, y_up, z) metres -> GF (x, y, z_up) metres.

    Pass GF's ``pitchHalfW`` (55) and ``pitchHalfH`` (36) from src/gametypes.hpp
    to stretch PES's 105 x 68 m pitch onto GF's slightly larger one.
    """
    x, y, z = p[0], p[1], p[2]
    sx = 1.0 if pitch_half_w is None else pitch_half_w / FOX_PITCH_HALF_LENGTH
    sy = 1.0 if pitch_half_h is None else pitch_half_h / FOX_PITCH_HALF_WIDTH
    sz = 1.0 if pitch_half_w is None else 0.5 * (sx + sy)
    return (x * sx, -z * sy, y * sz)


def to_gf_quaternion(q):
    """Fox camera rotation -> GF camera rotation (same eye basis, new world basis)."""
    return quat_mul(FOX_TO_GF_QUAT, q)


def gf_fov(fov_deg, authored_aspect=1.5, screen_aspect=None):
    """Channel-2 FOV -> Match::cameraFOV (also a full vertical angle in degrees).

    Copies straight across when ``screen_aspect`` is None. Supply the real
    window aspect to preserve the *horizontal* framing the shot was composed
    for at 3:2 instead, which is usually what you want on a 16:9 display.
    """
    if screen_aspect is None:
        return fov_deg
    half = math.radians(fov_deg) * 0.5
    return math.degrees(2.0 * math.atan(math.tan(half) * authored_aspect / screen_aspect))


def to_gf(canm, frame, pitch_half_w=None, pitch_half_h=None, screen_aspect=None):
    """Everything Match::UpdateIngameCamera needs for one frame.

    ``rotation`` goes into cameraOrientation with cameraNodeOrientation left at
    QUATERNION_IDENTITY (GF composes camera world rotation as
    cameraNodeOrientation * cameraOrientation).
    """
    return {
        "position": to_gf_position(canm.position(frame), pitch_half_w, pitch_half_h),
        "rotation": to_gf_quaternion(canm.rotation(frame)),
        "fov": gf_fov(canm.fov(frame), canm.aspect, screen_aspect),
        "near": max(0.1, canm.near),
        "far": canm.far,
    }


# --------------------------------------------------------------------------
# __main__
# --------------------------------------------------------------------------

def _dump(path, args):
    fdc = load(path)
    print("== %s  (%d bytes)" % (path, os.path.getsize(path)))
    if fdc.cpk_path:
        print("   cpk path : %s" % fdc.cpk_path)
    print("   records  : %s" % ", ".join(
        "tag %#02x x%d" % (t, len(v)) for t, v in sorted(fdc.records.items())))
    if fdc.assets:
        print("   assets   : %d leaf refs, e.g. %s" % (
            len(fdc.assets), ", ".join(os.path.basename(n) for n, _ in fdc.assets[:3])))
    print("   cameras  : %d canm" % len(fdc.cameras))

    print("\n   -- cut timeline (tag 0x06 records) --")
    for cut in fdc.cuts:
        print("      start %5d  kind %d  near %-8g far %-8g  %s"
              % (cut.start_frame, cut.kind, cut.near, cut.far,
                 cut.canm_name or "(no clip)"))

    if fdc.actors:
        print("\n   -- actor placements (tag 0x04 records) --")
        for actor in fdc.actors:
            print("      slot %2d  pos (%8.3f, %6.3f, %8.3f)  yaw %7.2f  "
                  "phase %6g  %s"
                  % (actor.slot, actor.position[0], actor.position[1],
                     actor.position[2], actor.yaw_deg, actor.phase_ticks,
                     actor.gani_name))

    for canm in fdc.cameras:
        print("\n   -- %s --" % (canm.name or "<unnamed>"))
        print("      %d frames @ %d fps (%.2f s), %d keys, near %g far %g aspect %g"
              % (canm.frame_count, canm.frame_rate, canm.duration_s,
                 canm.key_count, canm.near, canm.far, canm.aspect))
        frames = canm.frames()
        if args.all:
            picks = frames
        else:
            n = len(frames)
            picks = sorted(set([frames[0], frames[min(1, n - 1)], frames[n // 4],
                                frames[n // 2], frames[3 * n // 4], frames[-1]]))
        print("      %-6s %-26s %-30s %6s  %s"
              % ("frame", "position (m, Fox Y-up)", "quaternion x y z w", "fov", "view ray hits y=0 at"))
        for f in picks:
            p = canm.position(f)
            q = canm.rotation(f)
            hit = ground_hit(p, q)
            where = "-" if hit is None else "(%.1f, %.1f) at %.1f m" % (hit[0][0], hit[0][2], hit[1])
            print("      %-6d (%8.3f,%8.3f,%8.3f)  (%6.3f,%6.3f,%6.3f,%6.3f) %6.2f  %s"
                  % (f, p[0], p[1], p[2], q[0], q[1], q[2], q[3], canm.fov(f), where))
        if args.gf:
            print("      -- same frames converted to GameplayFootball (Z-up, pitchHalfW=55, pitchHalfH=36) --")
            for f in picks:
                g = to_gf(canm, f, 55.0, 36.0)
                p, q = g["position"], g["rotation"]
                print("      %-6d pos (%8.3f,%8.3f,%8.3f)  quat (%6.3f,%6.3f,%6.3f,%6.3f)"
                      "  fov %6.2f near %g far %g"
                      % (f, p[0], p[1], p[2], q[0], q[1], q[2], q[3], g["fov"], g["near"], g["far"]))


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(
        description="Decode PES 2021 .fdc cut tables and their embedded .canm camera streams.")
    ap.add_argument("files", nargs="+", help="*.fdc extracted from dt12_g4.cpk")
    ap.add_argument("--all", action="store_true", help="dump every frame, not a sample")
    ap.add_argument("--gf", action="store_true", help="also print GameplayFootball-space values")
    args = ap.parse_args(argv)
    for path in args.files:
        _dump(path, args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
