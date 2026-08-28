// The wire protocol the panel speaks to the engine's remote-control port
// (src/onthepitch/remotecontrol.hpp on the engine side), and the config file a
// scheduled match is launched with. Pure functions; the socket lives in
// engine.ts and never composes protocol text itself.

export type Side = 0 | 1;

export interface EnginePlayer {
  id: number;
  name: string;
  role: string;
  on_pitch: boolean;
}

export interface EngineTeam {
  name: string;
  philosophy: string;
  mentality: string;
  instructions: string[];
  tactics: Record<string, number>;
  players: EnginePlayer[];
}

export interface EngineState {
  score: [number, number];
  time_ms: number;
  phase: number;
  in_play: boolean;
  sub_window: boolean;
  teams: [EngineTeam, EngineTeam];
}

export interface MatchSetup {
  team1Id: number;
  team2Id: number;
  stadiumObject: string;
  durationMinutes: number;
  team1KitNum: number;
  team2KitNum: number;
  controlPort: number;
}

// Tokens travel on a space-separated line; anything else is an injection.
function token(name: string): string {
  if (!/^[A-Za-z0-9_-]+$/.test(name)) throw new Error(`unsafe protocol token: ${JSON.stringify(name)}`);
  return name;
}

function side(value: Side): string {
  if (value !== 0 && value !== 1) throw new Error(`side must be 0 or 1, got ${value}`);
  return String(value);
}

function integer(value: number, what: string): string {
  if (!Number.isInteger(value)) throw new Error(`${what} must be an integer, got ${value}`);
  return String(value);
}

export function tacticCommand(teamSide: Side, name: string, value: number): string {
  if (!Number.isFinite(value)) throw new Error(`tactic value must be a number, got ${value}`);
  const clamped = Math.max(0, Math.min(1, value));
  return `tactic ${side(teamSide)} ${token(name)} ${clamped}`;
}

export function philosophyCommand(teamSide: Side, name: string): string {
  return `philosophy ${side(teamSide)} ${token(name)}`;
}

export function mentalityCommand(teamSide: Side, name: string): string {
  return `mentality ${side(teamSide)} ${token(name)}`;
}

export function instructionCommand(teamSide: Side, name: string, on: boolean): string {
  return `instruction ${side(teamSide)} ${token(name)} ${on ? 'on' : 'off'}`;
}

export function subCommand(teamSide: Side, playerOutId: number, playerInId: number): string {
  return `sub ${side(teamSide)} ${integer(playerOutId, 'playerOutId')} ${integer(playerInId, 'playerInId')}`;
}

export function parseState(line: string): EngineState | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(line);
  } catch {
    return null;
  }
  const state = parsed as EngineState;
  if (
    !state ||
    !Array.isArray(state.score) ||
    state.score.length !== 2 ||
    typeof state.time_ms !== 'number' ||
    !Array.isArray(state.teams) ||
    state.teams.length !== 2 ||
    state.teams.some(
      (team) =>
        !team ||
        typeof team.name !== 'string' ||
        typeof team.philosophy !== 'string' ||
        typeof team.mentality !== 'string' ||
        !Array.isArray(team.instructions) ||
        typeof team.tactics !== 'object' ||
        !Array.isArray(team.players),
    )
  ) {
    return null;
  }
  return state;
}

// The launch config a scheduled match runs with: the same self-driving
// menu-smoke keys every verification harness uses, plus the control port.
// Values are validated because they end up in a quoted key/value file.
export function engineConfig(setup: MatchSetup): string {
  const entries: [string, string][] = [
    ['showcase_team1', integer(setup.team1Id, 'team1Id')],
    ['showcase_team2', integer(setup.team2Id, 'team2Id')],
    ['stadium_object', setup.stadiumObject],
    ['match_duration_minutes', String(setup.durationMinutes)],
    ['team1_kit_num', integer(setup.team1KitNum, 'team1KitNum')],
    ['team2_kit_num', integer(setup.team2KitNum, 'team2KitNum')],
    ['remote_control_port', integer(setup.controlPort, 'controlPort')],
    // The self-driving menu path that starts the match unattended.
    ['menu_smoke_test_full_match', 'true'],
    ['quick_start', 'false'],
    // Both benches answer to the panel, not to the CPU manager: with coach
    // mode on, AIManagerRunsTeam is false for both sides, so a philosophy or
    // mentality set from the panel stands instead of being adapted away.
    ['coach_mode', 'true'],
    ['substitutions_enabled', 'true'],
  ];
  if (!Number.isFinite(setup.durationMinutes) || setup.durationMinutes <= 0)
    throw new Error(`durationMinutes must be positive, got ${setup.durationMinutes}`);
  return (
    entries
      .map(([key, value]) => {
        if (/[\n\r"]/.test(value)) throw new Error(`unsafe config value for ${key}: ${JSON.stringify(value)}`);
        return `"${key}" "${value}"`;
      })
      .join('\n') + '\n'
  );
}
