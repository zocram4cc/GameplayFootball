// The wire protocol the panel speaks to the engine's remote-control port,
// and the engine config a scheduled match is launched with. Pure functions.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  tacticCommand,
  philosophyCommand,
  mentalityCommand,
  instructionCommand,
  subCommand,
  parseState,
  engineConfig,
} from './protocol';

// ── Command lines ────────────────────────────────────────────────────────────

test('tacticCommand builds a protocol line and clamps the value', () => {
  assert.equal(tacticCommand(0, 'team_pressure', 0.8), 'tactic 0 team_pressure 0.8');
  assert.equal(tacticCommand(1, 'team_pressure', 7), 'tactic 1 team_pressure 1');
  assert.equal(tacticCommand(1, 'team_pressure', -2), 'tactic 1 team_pressure 0');
});

test('tacticCommand refuses unsafe names and sides', () => {
  assert.throws(() => tacticCommand(0, 'team pressure', 0.5)); // embedded space
  assert.throws(() => tacticCommand(0, 'team\npressure', 0.5)); // line injection
  assert.throws(() => tacticCommand(0, '', 0.5));
  assert.throws(() => tacticCommand(2 as never, 'team_pressure', 0.5));
  assert.throws(() => tacticCommand(0, 'team_pressure', NaN));
});

test('philosophy, mentality and instruction commands', () => {
  assert.equal(philosophyCommand(1, 'gegenpressing'), 'philosophy 1 gegenpressing');
  assert.equal(mentalityCommand(0, 'all_out_attack'), 'mentality 0 all_out_attack');
  assert.equal(instructionCommand(0, 'tiki_taka', true), 'instruction 0 tiki_taka on');
  assert.equal(instructionCommand(1, 'tiki_taka', false), 'instruction 1 tiki_taka off');
  assert.throws(() => philosophyCommand(0, 'tiki taka'));
  assert.throws(() => instructionCommand(0, 'x y', true));
});

test('subCommand takes integer player ids only', () => {
  assert.equal(subCommand(0, 104, 117), 'sub 0 104 117');
  assert.throws(() => subCommand(0, 1.5, 2));
  assert.throws(() => subCommand(0, NaN, 2));
});

// ── State parsing ────────────────────────────────────────────────────────────

const stateLine = JSON.stringify({
  score: [2, 1],
  time_ms: 2700000,
  phase: 1,
  in_play: true,
  sub_window: false,
  teams: [
    {
      name: 'HDG',
      philosophy: 'gegenpressing',
      mentality: 'attacking',
      instructions: ['frontline_pressure'],
      tactics: { team_pressure: 0.75 },
      players: [{ id: 104, name: 'Kaban', role: 'CM', on_pitch: true }],
    },
    {
      name: '2HUG',
      philosophy: 'balanced',
      mentality: 'balanced',
      instructions: [],
      tactics: {},
      players: [],
    },
  ],
});

test('parseState reads a snapshot line', () => {
  const state = parseState(stateLine);
  assert.ok(state);
  assert.deepEqual(state.score, [2, 1]);
  assert.equal(state.time_ms, 2700000);
  assert.equal(state.teams[0].philosophy, 'gegenpressing');
  assert.equal(state.teams[0].players[0].name, 'Kaban');
  assert.equal(state.teams[1].name, '2HUG');
});

test('parseState refuses garbage rather than throwing', () => {
  assert.equal(parseState('not json'), null);
  assert.equal(parseState('{}'), null);
  assert.equal(parseState('{"score":[0,0]}'), null);
  assert.equal(parseState(''), null);
});

// ── Engine launch config ─────────────────────────────────────────────────────

test('engineConfig writes the menu-smoke launch keys', () => {
  const text = engineConfig({
    team1Id: 11,
    team2Id: 9,
    stadiumObject: 'media/objects/stadiums/pes_st002/pes_st002.object',
    durationMinutes: 5,
    team1KitNum: 1,
    team2KitNum: 2,
    controlPort: 44700,
  });
  assert.match(text, /"showcase_team1" "11"/);
  assert.match(text, /"showcase_team2" "9"/);
  assert.match(text, /"stadium_object" "media\/objects\/stadiums\/pes_st002\/pes_st002\.object"/);
  assert.match(text, /"match_duration_minutes" "5"/);
  assert.match(text, /"team1_kit_num" "1"/);
  assert.match(text, /"team2_kit_num" "2"/);
  assert.match(text, /"remote_control_port" "44700"/);
  // The self-driving menu path that actually starts the match.
  assert.match(text, /"menu_smoke_test_full_match" "true"/);
  // Both benches must answer to the panel, not to the CPU manager.
  assert.match(text, /"coach_mode" "true"/);
});

test('engineConfig refuses values that would break out of the config format', () => {
  assert.throws(() =>
    engineConfig({
      team1Id: 11,
      team2Id: 9,
      stadiumObject: 'media/x.object"\n"debug" "true',
      durationMinutes: 5,
      team1KitNum: 1,
      team2KitNum: 1,
      controlPort: 44700,
    }),
  );
  assert.throws(() =>
    engineConfig({
      team1Id: NaN,
      team2Id: 9,
      stadiumObject: 'media/x.object',
      durationMinutes: 5,
      team1KitNum: 1,
      team2KitNum: 1,
      controlPort: 44700,
    }),
  );
});
