// Store against synthetic fixture documents (written here for the test,
// not a real exported match) matching the frozen gf-match-stats contract.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import { getMatchStats, getWikiText, listMatchStats } from './statsStore';

const MATCH_A = '2026-09-16-11-13-193000';
const MATCH_B = '2026-09-10-11-13-193000';

const DOC_A = {
  format: 'gf-match-stats',
  version: 1,
  matchId: MATCH_A,
  date: '2026-09-16',
  competition: 'Friendly',
  stadium: 'st017',
  attendance: null,
  referee: null,
  durationMinutes: 25,
  teams: [
    {
      id: 11, name: 'HDG', side: 'home', score: 2,
      goals: [{ minute: 34, player: 'J. Smith', own: false, penalty: false }],
      cards: [{ minute: 57, player: 'A. Doe', type: 'yellow' }],
      shots: 14, shotsOnTarget: 8, saves: 3, corners: 3, fouls: 9,
      offsides: 1, possessionPercent: 54, passes: 210, passesCompleted: 149,
      passAccuracyPercent: 71, expectedGoals: 1.32,
    },
    {
      id: 13, name: 'OOO', side: 'away', score: 1,
      goals: [], cards: [], shots: 9, shotsOnTarget: 5, saves: 4, corners: 1,
      fouls: 11, offsides: 3, possessionPercent: 46, passes: 190,
      passesCompleted: 120, passAccuracyPercent: 63, expectedGoals: 0.94,
    },
  ],
};

const WIKI_A = `{{footballbox
|date       = 16 September 2026
|time       =
|team1      = HDG
|score      = 2 – 1
|team2      = OOO
|goals1     = J. Smith {{goal|34}}
|goals2     =
|stadium    = st017
|attendance =
|referee    =
|report     =
}}
`;

// A second, sparser match: no wikitext yet, and a couple of fields the
// engine genuinely didn't record (no fabricated zeros or placeholders).
const DOC_B = {
  format: 'gf-match-stats',
  version: 1,
  matchId: MATCH_B,
  date: '2026-09-10',
  competition: 'Friendly',
  teams: [
    { id: 11, name: 'HDG', side: 'home', score: 0, goals: [], cards: [] },
    { id: 13, name: 'OOO', side: 'away', score: 0, goals: [], cards: [] },
  ],
};

function writeFixtures(dir: string) {
  fs.writeFileSync(path.join(dir, `${MATCH_A}.json`), JSON.stringify(DOC_A));
  fs.writeFileSync(path.join(dir, `${MATCH_A}.wiki.txt`), WIKI_A);
  fs.writeFileSync(path.join(dir, `${MATCH_B}.json`), JSON.stringify(DOC_B));
}

test('lists exported matches, newest first, with parsed team fields', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gf-stats-'));
  writeFixtures(dir);
  const matches = listMatchStats(dir);
  assert.deepEqual(
    matches.map(m => m.matchId),
    [MATCH_A, MATCH_B],
  );
  assert.equal(matches[0].home?.name, 'HDG');
  assert.equal(matches[0].away?.score, 1);
  assert.equal(matches[0].hasWiki, true);
  assert.equal(matches[1].hasWiki, false);
});

test('reads a match document and its wikitext verbatim', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gf-stats-'));
  writeFixtures(dir);
  const doc = getMatchStats(MATCH_A, dir);
  assert.equal(doc?.teams[0].goals[0].player, 'J. Smith');
  assert.equal(doc?.teams[1].expectedGoals, 0.94);
  assert.equal(getWikiText(MATCH_A, dir), WIKI_A);
  assert.equal(getWikiText(MATCH_B, dir), null);
});

test('omitted fields stay absent rather than defaulted', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gf-stats-'));
  writeFixtures(dir);
  const doc = getMatchStats(MATCH_B, dir);
  assert.equal(doc?.stadium, undefined);
  assert.equal(doc?.teams[0].shots, undefined);
});

test('a missing stats directory yields an empty listing, not a throw', () => {
  assert.deepEqual(listMatchStats('/nonexistent/gf-stats-dir'), []);
});

test('rejects match ids that could escape the stats directory', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gf-stats-'));
  writeFixtures(dir);
  assert.equal(getMatchStats('../../etc/passwd', dir), null);
  assert.equal(getWikiText('../../etc/passwd', dir), null);
});
