// Reads the match-stats documents the engine exports into its stats
// directory (`<matchId>.json` + `<matchId>.wiki.txt`, the frozen
// gf-match-stats contract). The engine writes; this panel only reads.
// Wikitext output format reference: Two-Scoops/SEN_P-AI's footballbox writer.
import fs from 'node:fs';
import path from 'node:path';

import { engineRunDir } from './launcher';

// Same env-configured-path pattern as ENGINE_RUN_DIR / ENGINE_DB.
export const statsDir =
  process.env.ENGINE_STATS_DIR ?? path.join(engineRunDir, 'stats');

// Match ids arrive in URLs; only accept the shape the engine emits so a
// crafted id cannot read files outside the stats directory.
const MATCH_ID = /^[a-zA-Z0-9-]+$/;

export interface StatsGoal {
  minute: number;
  player: string;
  own: boolean;
  penalty: boolean;
}

export interface StatsCard {
  minute: number;
  player: string;
  type: string;
}

// Optional fields stay optional: the engine omits what it cannot measure.
export interface StatsTeam {
  id: number;
  name: string;
  side: 'home' | 'away';
  score: number;
  goals: StatsGoal[];
  cards: StatsCard[];
  shots?: number;
  shotsOnTarget?: number;
  saves?: number;
  corners?: number;
  fouls?: number;
  offsides?: number;
  possessionPercent?: number;
  passes?: number;
  passesCompleted?: number;
  passAccuracyPercent?: number;
  expectedGoals?: number;
}

export interface MatchStatsDoc {
  format: 'gf-match-stats';
  version: number;
  matchId: string;
  date: string;
  competition?: string;
  stadium?: string;
  attendance?: number | null;
  referee?: string | null;
  durationMinutes?: number;
  teams: StatsTeam[];
}

export interface MatchSummary {
  matchId: string;
  date: string;
  competition?: string;
  home?: StatsTeam;
  away?: StatsTeam;
  hasWiki: boolean;
}

export function getMatchStats(
  matchId: string,
  dir = statsDir,
): MatchStatsDoc | null {
  if (!MATCH_ID.test(matchId)) return null;
  try {
    return JSON.parse(
      fs.readFileSync(path.join(dir, matchId + '.json'), 'utf8'),
    ) as MatchStatsDoc;
  } catch {
    return null;
  }
}

export function getWikiText(matchId: string, dir = statsDir): string | null {
  if (!MATCH_ID.test(matchId)) return null;
  try {
    return fs.readFileSync(path.join(dir, matchId + '.wiki.txt'), 'utf8');
  } catch {
    return null;
  }
}

// Newest first (ids start with the zero-padded date). A missing or empty
// directory simply means nothing has been exported yet.
export function listMatchStats(dir = statsDir): MatchSummary[] {
  let names: string[];
  try {
    names = fs.readdirSync(dir);
  } catch {
    return [];
  }
  const summaries: MatchSummary[] = [];
  for (const name of names) {
    if (!name.endsWith('.json')) continue;
    const matchId = name.slice(0, -'.json'.length);
    const doc = getMatchStats(matchId, dir);
    if (!doc) continue;
    summaries.push({
      matchId,
      date: doc.date,
      competition: doc.competition,
      home: doc.teams?.find(t => t.side === 'home'),
      away: doc.teams?.find(t => t.side === 'away'),
      hasWiki: fs.existsSync(path.join(dir, matchId + '.wiki.txt')),
    });
  }
  return summaries.sort((a, b) => b.matchId.localeCompare(a.matchId));
}
