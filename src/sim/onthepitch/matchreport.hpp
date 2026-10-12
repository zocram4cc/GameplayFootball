// The end-of-match report GF printed from its game-over page, and the
// determinism fingerprint of a headless run.

#ifndef _HPP_ONTHEPITCH_MATCHREPORT
#define _HPP_ONTHEPITCH_MATCHREPORT

#include <cstdint>
#include <cstdio>

class Match;

// The fingerprint is folded in once every this many ticks.
constexpr unsigned long kDeterminismSampleTicks = 100;

// FNV-1a 64-bit offset basis.
uint64_t DeterminismHashSeed();
// Folds the current tick number, ball position and every active player's
// position (bit patterns, team 0 then team 1) into `hash` with FNV-1a.
uint64_t FoldDeterminism(uint64_t hash, Match& match);

// "Full match complete", GF's [balance...] / [pass-...] / [stalemate] lines in
// GF's own formats (so its simbatch regexes parse them), then
// "[determinism] %016llx".
void PrintMatchReport(Match& match, FILE* out);

#endif
