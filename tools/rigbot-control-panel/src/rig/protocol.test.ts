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
  authCommand,
  scheduleCommand,
  resumeCommand,
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

// ── Auth, schedule and resume lines ─────────────────────────────────────────

test('authCommand and resumeCommand', () => {
  assert.equal(authCommand('s3cret-key_1'), 'auth s3cret-key_1');
  assert.throws(() => authCommand('two tokens'));
  assert.throws(() => authCommand(''));
  assert.equal(resumeCommand(), 'resume');
});

test('scheduleCommand carries the match setup, stadium last', () => {
  assert.equal(
    scheduleCommand({
      team1Id: 11,
      team2Id: 9,
      durationMinutes: 5,
      team1KitNum: 1,
      team2KitNum: 2,
      stadiumObject: 'media/objects/stadiums/043 - benuldys/stadium.object',
    }),
    'schedule 11 9 5 1 2 media/objects/stadiums/043 - benuldys/stadium.object',
  );
});

test('scheduleCommand refuses line breaks and bad numbers', () => {
  const setup = {
    team1Id: 11,
    team2Id: 9,
    durationMinutes: 5,
    team1KitNum: 1,
    team2KitNum: 1,
    stadiumObject: 'media/x.object',
  };
  assert.throws(() => scheduleCommand({ ...setup, stadiumObject: 'a\nb' }));
  assert.throws(() => scheduleCommand({ ...setup, stadiumObject: '' }));
  assert.throws(() => scheduleCommand({ ...setup, team1Id: NaN }));
  assert.throws(() => scheduleCommand({ ...setup, durationMinutes: 0 }));
});

// ── Engine launch config ─────────────────────────────────────────────────────

test('engineConfig boots straight into remote-control mode', () => {
  const text = engineConfig({ controlPort: 44700, streamerKey: 's3cret' });
  assert.match(text, /"remote_control_mode" "true"/);
  assert.match(text, /"remote_control_key" "s3cret"/);
  assert.match(text, /"remote_control_port" "44700"/);
  // No match keys: the engine waits limp until a schedule command arrives.
  assert.doesNotMatch(text, /showcase_team/);
  assert.doesNotMatch(text, /menu_smoke_test_full_match/);
});

test('engineConfig refuses values that would break out of the config format', () => {
  assert.throws(() => engineConfig({ controlPort: 44700, streamerKey: 'a"b' }));
  assert.throws(() => engineConfig({ controlPort: NaN, streamerKey: 's' }));
});
