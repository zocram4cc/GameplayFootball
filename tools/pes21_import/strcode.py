"""Fox Engine StrCode hashes, as used for chunk, bone and animation names in
PES 2021 .gani/.mtar files.

Fox uses CityHash **1.0.x**, not the modern 1.1 algorithm — the two differ for
every input length, so the pip `cityhash` package gives wrong answers. The
implementation below is a transcription of the community port (FoxKit
Fox.Kernel CityHash, via Fox_Parser's GameCityHash.cs), which matches the
engine bit-for-bit.

    StrCode(str)   = CityHash64WithSeeds(str + '\\0',
                                         seed0 = K2,
                                         seed1 = (sbyte)str[0] * 0x10000 + len(str))
    StrCode32(str) = StrCode(str) & 0xFFFFFFFF     # gani track/chunk names
    GzHash(path)   = StrCode(path minus extension) & 0xFFFFFFFFFFFF  # 48-bit,
                     over the full "/Assets/..." path (mtar entry names)

Validated against real PES 2021 data: strcode32("MOTION") == 0xc6e937b9, the
chunk id in every .gani.
"""

MASK64 = 0xFFFFFFFFFFFFFFFF

K0 = 0xC3A5C85C97CB3127
K1 = 0xB492B66FBE98F273
K2 = 0x9AE16A3B2F90404F
K3 = 0xC949D7C7509E6557

_K_MUL = 0x9DDFEA08EB382D69


def _rotate(val, shift):
    if shift == 0:
        return val
    return ((val >> shift) | (val << (64 - shift))) & MASK64


def _rotate_at_least_1(val, shift):
    # CityHash 1.0's RotateByAtLeast1; callers guarantee 0 < shift < 64
    # except len==64 inputs never reach here with shift 64.
    shift &= 63
    if shift == 0:
        shift = 64
        return val
    return ((val >> shift) | (val << (64 - shift))) & MASK64


def _shift_mix(val):
    return (val ^ (val >> 47)) & MASK64


def _hash128_to_64(low, high):
    a = ((low ^ high) * _K_MUL) & MASK64
    a ^= a >> 47
    b = ((high ^ a) * _K_MUL) & MASK64
    b ^= b >> 47
    return (b * _K_MUL) & MASK64


def _hash_len_16(u, v):
    return _hash128_to_64(u, v)


def _fetch64(data, index):
    return int.from_bytes(data[index:index + 8], "little")


def _fetch32(data, index):
    return int.from_bytes(data[index:index + 4], "little")


def _hash_len_0_to_16(data):
    length = len(data)
    if length > 8:
        a = _fetch64(data, 0)
        b = _fetch64(data, length - 8)
        return (_hash_len_16(a, _rotate_at_least_1((b + length) & MASK64, length)) ^ b) & MASK64
    if length >= 4:
        a = _fetch32(data, 0)
        return _hash_len_16((length + (a << 3)) & MASK64, _fetch32(data, length - 4))
    if length > 0:
        a = data[0]
        b = data[length >> 1]
        c = data[length - 1]
        y = (a + (b << 8)) & 0xFFFFFFFF
        z = (length + (c << 2)) & 0xFFFFFFFF
        return (_shift_mix(((y * K2) ^ (z * K3)) & MASK64) * K2) & MASK64
    return K2


def _hash_len_17_to_32(data):
    length = len(data)
    a = (_fetch64(data, 0) * K1) & MASK64
    b = _fetch64(data, 8)
    c = (_fetch64(data, length - 8) * K2) & MASK64
    d = (_fetch64(data, length - 16) * K0) & MASK64
    return _hash_len_16(
        (_rotate((a - b) & MASK64, 43) + _rotate(c, 30) + d) & MASK64,
        (a + _rotate((b ^ K3) & MASK64, 20) - c + length) & MASK64)


def _weak_hash_len_32_with_seeds(w, x, y, z, a, b):
    a = (a + w) & MASK64
    b = _rotate((b + a + z) & MASK64, 21)
    c = a
    a = (a + x + y) & MASK64
    b = (b + _rotate(a, 44)) & MASK64
    return (a + z) & MASK64, (b + c) & MASK64


def _weak_hash_len_32_with_seeds_data(data, offset, a, b):
    return _weak_hash_len_32_with_seeds(
        _fetch64(data, offset), _fetch64(data, offset + 8),
        _fetch64(data, offset + 16), _fetch64(data, offset + 24), a, b)


def _hash_len_33_to_64(data):
    length = len(data)
    z = _fetch64(data, 24)
    a = (_fetch64(data, 0) + (length + _fetch64(data, length - 16)) * K0) & MASK64
    b = _rotate((a + z) & MASK64, 52)
    c = _rotate(a, 37)
    a = (a + _fetch64(data, 8)) & MASK64
    c = (c + _rotate(a, 7)) & MASK64
    a = (a + _fetch64(data, 16)) & MASK64
    vf = (a + z) & MASK64
    vs = (b + _rotate(a, 31) + c) & MASK64
    a = (_fetch64(data, 16) + _fetch64(data, length - 32)) & MASK64
    z = _fetch64(data, length - 8)
    b = _rotate((a + z) & MASK64, 52)
    c = _rotate(a, 37)
    a = (a + _fetch64(data, length - 24)) & MASK64
    c = (c + _rotate(a, 7)) & MASK64
    a = (a + _fetch64(data, length - 16)) & MASK64
    wf = (a + z) & MASK64
    ws = (b + _rotate(a, 31) + c) & MASK64
    r = _shift_mix(((vf + ws) * K2 + (wf + vs) * K0) & MASK64)
    return (_shift_mix((r * K0 + vs) & MASK64) * K2) & MASK64


def cityhash64(data):
    """CityHash 1.0.x CityHash64 over bytes (the Fox Engine's version)."""
    length = len(data)
    if length <= 32:
        return _hash_len_0_to_16(data) if length <= 16 else _hash_len_17_to_32(data)
    if length <= 64:
        return _hash_len_33_to_64(data)

    x = _fetch64(data, length - 40)
    y = (_fetch64(data, length - 16) + _fetch64(data, length - 56)) & MASK64
    z = _hash_len_16((_fetch64(data, length - 48) + length) & MASK64,
                     _fetch64(data, length - 24))
    v = _weak_hash_len_32_with_seeds_data(data, length - 64, length, z)
    w = _weak_hash_len_32_with_seeds_data(data, length - 32, (y + K1) & MASK64, x)
    x = (x * K1 + _fetch64(data, 0)) & MASK64

    remaining = (length - 1) & ~63
    offset = 0
    while True:
        x = (_rotate((x + y + v[0] + _fetch64(data, offset + 8)) & MASK64, 37) * K1) & MASK64
        y = (_rotate((y + v[1] + _fetch64(data, offset + 48)) & MASK64, 42) * K1) & MASK64
        x ^= w[1]
        y = (y + v[0] + _fetch64(data, offset + 40)) & MASK64
        z = (_rotate((z + w[0]) & MASK64, 33) * K1) & MASK64
        v = _weak_hash_len_32_with_seeds_data(data, offset, (v[1] * K1) & MASK64,
                                              (x + w[0]) & MASK64)
        w = _weak_hash_len_32_with_seeds_data(data, offset + 32, (z + w[1]) & MASK64,
                                              (y + _fetch64(data, offset + 16)) & MASK64)
        z, x = x, z
        offset += 64
        remaining -= 64
        if remaining == 0:
            break

    return _hash_len_16(
        (_hash_len_16(v[0], w[0]) + _shift_mix(y) * K1 + z) & MASK64,
        (_hash_len_16(v[1], w[1]) + x) & MASK64)


def cityhash64_with_seeds(data, seed0, seed1):
    return _hash_len_16((cityhash64(data) - seed0) & MASK64, seed1)


def strcode(text: str) -> int:
    """Fox StrCode: the engine's stringid_raw_hash. Full 64 bits, no mask."""
    if not text:
        return 0
    buf = text.encode("utf-8") + b"\0"
    first = buf[0] - 256 if buf[0] >= 128 else buf[0]      # (sbyte)str[0]
    seed1 = (first * 0x10000 + len(text)) & MASK64
    return cityhash64_with_seeds(buf, K2, seed1)


def strcode32(text: str) -> int:
    """32-bit StrCode: gani chunk names ("MOTION") and track/bone names."""
    return strcode(text) & 0xFFFFFFFF


def gz_hash(path: str) -> int:
    """48-bit mtar/archive entry name hash over the full '/Assets/...' path
    (backslashes normalised, extension dropped, leading slash KEPT)."""
    text = path.replace("\\", "/")
    dot = text.rfind(".")
    if dot > text.rfind("/"):
        text = text[:dot]
    return strcode(text) & 0xFFFFFFFFFFFF


class Dictionary:
    """Matches observed hashes against a list of candidate names."""

    def __init__(self, names):
        self.by_hash = {}
        for name in names:
            self.by_hash[strcode32(name)] = name

    def lookup(self, value: int):
        return self.by_hash.get(value & 0xFFFFFFFF)


if __name__ == "__main__":
    import sys
    for arg in sys.argv[1:]:
        print("%s  strcode=%016x  strcode32=%08x  gz48=%012x" %
              (arg, strcode(arg), strcode32(arg), gz_hash(arg)))
