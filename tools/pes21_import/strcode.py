"""Fox Engine StrCode hashes, used for bone and animation names in .gani/.mtar.

StrCode64 is a cityhash-derived 64-bit hash of which Fox keeps the low bits;
the 32-bit form seen in PES motion archives is StrCode64 & 0xFFFFFFFF... in
practice the PES community matches names by brute force against dictionaries
(bone names from .fmdl files, animation names from AnimeTable). This module
implements the PathFileNameCode / StrCode variant used by the tooling and a
dictionary matcher.
"""

def strcode(text: str) -> int:
    """The classic Fox StrCode: a 64-bit polynomial rolling hash."""
    seed1 = 0x9AE16A3B2F90404F
    mask = (1 << 64) - 1
    result = 0
    for ch in text.encode("utf-8"):
        result = ((result * 0x1F) + ch) & mask
    # Fox keeps 52 bits for StrCode64; 32-bit tables keep the low dword.
    return result


def strcode32(text: str) -> int:
    return strcode(text) & 0xFFFFFFFF


class Dictionary:
    """Matches observed hashes against a list of candidate names."""

    def __init__(self, names):
        self.by_hash = {}
        for name in names:
            self.by_hash[strcode32(name)] = name

    def lookup(self, value: int):
        return self.by_hash.get(value & 0xFFFFFFFF)
