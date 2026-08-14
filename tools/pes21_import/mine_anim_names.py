#!/usr/bin/env python3
"""Recover the real names of PES 2021 body animations from their StrCode32 hashes.

The body-animation archives (``common/anime/FoxAnim/Body/body_anime_file*.mtar``)
identify every entry only by ``strcode32(bare_name)`` -- the plain-text names are
not stored anywhere in the archive.  This script rebuilds the name dictionary by
collecting name-shaped strings from the places Konami *did* leave them, hashing
each candidate, and keeping the ones whose hash is an actual archive entry.  A
hit is therefore self-verifying: no candidate is written out unless its hash is
present in the target set.

Mining sources, in descending order of trust
--------------------------------------------

1. ``common/anime/Mbinfo/json/anim_infos.json`` (source ``json``)
   The per-animation metadata table ships as *plain JSON* and every record has a
   ``"file_name"`` field holding the animation's real name.  4906 distinct names,
   4105 of which hash into the body archives.  The remaining ~800 belong to other
   FoxAnim sets (Hand/Eye/Face) or to animations cut from the shipped archives.

2. ``PES2021.exe`` (source ``exe``)
   The executable embeds the complete animation-name table as C string literals.
   Scanning it for ``[A-Za-z0-9_]{3,90}`` runs yields ~537k candidates which,
   on the retail build tested, cover **all 4389** archive hashes on their own --
   this is the source that closes the dictionary.  It is kept as a separate
   source because it needs the game install, which the json does not.

3. Grammar-mutation closure (source ``mutate``, opt-in via ``--mutate``)
   Names are ``_``-joined token compounds (``block_0_0_y03_000_near_thigh``,
   ``kick_long_3_0_y0_stagger_l_f090_down``) drawn from a small per-position
   vocabulary, so unknown names can be generated from known ones by substituting,
   deleting or appending one token and re-hashing.  Iterated to a fixpoint this
   recovers 111 names that source 1 alone misses.  It is *off by default* because
   synthetic names are only as trustworthy as a 32-bit hash match: against 4389
   targets a 9.6M-candidate sweep expects ~10 accidental collisions, and that is
   exactly what was observed.  Use it only when the exe is unavailable, and treat
   its extra names as provisional.

Collision handling
------------------

Two different strings can share a 32-bit hash, so a hash may attract more than
one candidate.  Candidates are ranked by source trust (json > exe > mutate) and
the best-ranked name wins each hash; ties within a source are broken
alphabetically for reproducibility.  On the retail data the two verbatim sources
agree completely -- 4389 names, 4389 distinct hashes, zero internal collisions --
and every one of the 10 ambiguous hashes is a verbatim name colliding with a
synthetic mutation, which the ranking discards.

Usage
-----

    python3 mine_anim_names.py --body-dir <dir with body_anime_file*.mtar> \\
                               [--anim-infos <anim_infos.json>] \\
                               [--exe <PES2021.exe>] \\
                               [--mutate] [--dry-run] [-o anim_names.txt]

With no ``-o`` the dictionary is merged into ``anim_names.txt`` next to this
file, sorted and de-duplicated; existing entries are preserved.  ``--dry-run``
reports coverage without writing.
"""

import argparse
import collections
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mtar
import strcode

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_NAMES = os.path.join(HERE, "anim_names.txt")

# A name is a run of identifier characters.  Names are mixed-case
# (`seeOff`, `_L`, `gkmoveSeriesA`), so case must be preserved -- lowercasing the
# candidate pool silently loses several hundred hits.
IDENT_RE = re.compile(rb"[A-Za-z0-9_]{3,90}")

# Lower rank == more trustworthy.  Used to settle hash collisions.
SOURCE_RANK = {"json": 0, "exe": 1, "mutate": 2}


def load_target_hashes(body_dir):
    """Every distinct strcode32 entry name in the body mtar archives."""
    paths = sorted(glob.glob(os.path.join(body_dir, "body_anime_file*.mtar")))
    if not paths:
        raise SystemExit("no body_anime_file*.mtar under %s" % body_dir)
    hashes = set()
    for path in paths:
        hashes |= {e.name_hash for e in mtar.read_entries(path)}
    return hashes, paths


def candidates_from_anim_infos(path):
    """Animation names as spelled in the shipped metadata JSON."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        data = json.load(f)
    names = set()
    for record in data.get("animations", {}).values():
        name = record.get("file_name")
        if name:
            names.add(name)
    return names


def candidates_from_binary(path, chunk_size=1 << 24):
    """Identifier-shaped strings in a binary, read in overlapping chunks so a
    name straddling a chunk boundary is not split."""
    overlap = 128
    names = set()
    with open(path, "rb") as f:
        carry = b""
        while True:
            block = f.read(chunk_size)
            if not block:
                break
            buf = carry + block
            for match in IDENT_RE.finditer(buf):
                names.add(match.group().decode("ascii"))
            carry = buf[-overlap:]
    return names


def mutate_closure(seed_names, targets, max_iters=8, verbose=True):
    """Grow the name set by one-token edits, iterating until no new hash lands.

    Tokens are pooled *per position* (position 0 is the action, later positions
    carry counts, angles, body parts and version suffixes), which keeps the
    candidate count near 10M instead of exploding combinatorially.
    """
    h = strcode.strcode32
    corpus = set(seed_names)
    found = {n for n in corpus if h(n) in targets}
    tried = set()
    for it in range(max_iters):
        by_pos = collections.defaultdict(set)
        for name in corpus:
            for i, token in enumerate(name.split("_")):
                by_pos[i].add(token)
        all_tokens = set().union(*by_pos.values()) if by_pos else set()

        cands = set()
        for name in corpus:
            parts = name.split("_")
            for i in range(len(parts)):
                original = parts[i]
                for token in by_pos[i]:          # substitute
                    if token != original:
                        parts[i] = token
                        cands.add("_".join(parts))
                parts[i] = original
                cands.add("_".join(parts[:i] + parts[i + 1:]))   # delete
            for token in all_tokens:             # append
                cands.add(name + "_" + token)
        cands -= tried
        cands -= corpus
        tried |= cands

        new = {c for c in cands if h(c) in targets} - found
        if verbose:
            print("    mutate iter %d: %d candidates, %d new"
                  % (it, len(cands), len(new)))
        if not new:
            break
        found |= new
        corpus |= new
    return found


def verify(candidates, targets):
    """Keep only candidates whose hash is a real archive entry.

    Returns {hash: name}, resolving collisions by source trust then name.
    """
    h = strcode.strcode32
    by_hash = collections.defaultdict(list)
    for name, source in candidates.items():
        value = h(name)
        if value in targets:
            by_hash[value].append((SOURCE_RANK.get(source, 99), name))
    return {value: min(entries)[1] for value, entries in by_hash.items()}, by_hash


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--body-dir", required=True,
                    help="directory holding body_anime_file*.mtar")
    ap.add_argument("--anim-infos", help="path to Mbinfo/json/anim_infos.json")
    ap.add_argument("--exe", help="path to PES2021.exe (or any binary to scan)")
    ap.add_argument("--mutate", action="store_true",
                    help="also run the grammar-mutation closure (slow, may add "
                         "hash-collision false positives)")
    ap.add_argument("-o", "--output", default=DEFAULT_NAMES,
                    help="dictionary to merge into (default: anim_names.txt)")
    ap.add_argument("--dry-run", action="store_true", help="report, do not write")
    args = ap.parse_args()

    targets, paths = load_target_hashes(args.body_dir)
    print("target hashes: %d distinct, from %d archives" % (len(targets), len(paths)))

    candidates = {}   # name -> best (lowest-rank) source seen

    def add(names, source):
        added = 0
        for name in names:
            prior = candidates.get(name)
            if prior is None or SOURCE_RANK[source] < SOURCE_RANK[prior]:
                candidates[name] = source
                added += 1
        print("  source %-7s %7d strings (%d new/promoted)"
              % (source, len(names), added))

    if args.anim_infos:
        add(candidates_from_anim_infos(args.anim_infos), "json")
    if args.exe:
        add(candidates_from_binary(args.exe), "exe")
    if not candidates:
        raise SystemExit("no sources given; pass --anim-infos and/or --exe")

    if args.mutate:
        print("  running mutation closure...")
        seed = {n for n in candidates if strcode.strcode32(n) in targets}
        add(mutate_closure(seed, targets), "mutate")

    resolved, by_hash = verify(candidates, targets)
    ambiguous = {v: e for v, e in by_hash.items() if len(e) > 1}
    print("resolved %d / %d hashes (%.1f%%); %d hashes had >1 candidate"
          % (len(resolved), len(targets), 100.0 * len(resolved) / len(targets),
             len(ambiguous)))
    for value, entries in sorted(ambiguous.items()):
        print("  collision %08x -> %s (kept: %s)"
              % (value, sorted(n for _, n in entries), min(entries)[1]))
    missing = len(targets) - len(resolved)
    if missing:
        print("still unresolved: %d hashes" % missing)

    names = set(resolved.values())
    if os.path.isfile(args.output):
        with open(args.output) as f:
            existing = {l.strip() for l in f if l.strip()}
        print("existing dictionary: %d names (%d not re-derived here)"
              % (len(existing), len(existing - names)))
        names |= existing

    if args.dry_run:
        print("dry run; %d names would be written to %s" % (len(names), args.output))
        return
    with open(args.output, "w") as f:
        f.write("\n".join(sorted(names)) + "\n")
    print("wrote %d names to %s" % (len(names), args.output))


if __name__ == "__main__":
    main()
