// The rig page: boot the engine into remote-control mode, schedule matches on
// it and steer them live - every tactic slider, philosophy, mentality,
// advanced instruction and substitutions, with state read back from the
// engine rather than assumed. The engine authenticates the panel with the
// streamer token, the same secret the original rigbot streamer client used.
import { FastifyInstance } from 'fastify';
import { PrismaClient } from '@prisma/client';

import { UserPayload } from '../types/fastify';
import { EngineLink } from '../rig/engine';
import { EngineLauncher, engineRunDir, engineDbPath, controlPort } from '../rig/launcher';
import { listTeams, listStadiums, EngineTeamRow } from '../rig/enginedb';
import {
  tacticCommand,
  philosophyCommand,
  mentalityCommand,
  instructionCommand,
  subCommand,
  scheduleCommand,
  resumeCommand,
  EngineState,
  MatchSetup,
  Side,
} from '../rig/protocol';

const prisma = new PrismaClient();
const launcher = new EngineLauncher();
let link: EngineLink | null = null;
let lastState: EngineState | null = null;
let reconnectTimer: NodeJS.Timeout | null = null;
// A schedule accepted while the engine was still booting; sent on link-up.
let pendingSchedule: MatchSetup | null = null;

interface CommandBody {
  kind: 'tactic' | 'philosophy' | 'mentality' | 'instruction' | 'sub' | 'resume';
  side: Side;
  name?: string;
  value?: number;
  on?: boolean;
  outId?: number;
  inId?: number;
}

async function streamerKey(): Promise<string> {
  const setting = await prisma.setting.findUnique({ where: { key: 'streamer_token' } });
  if (setting?.value) return setting.value;
  if (process.env.ENGINE_STREAMER_KEY) return process.env.ENGINE_STREAMER_KEY;
  throw new Error('no streamer key: set streamer_token in admin settings');
}

export async function rigRoutes(fastify: FastifyInstance) {
  const engineLog: string[] = [];
  const log = (line: string) => {
    engineLog.push(line);
    if (engineLog.length > 500) engineLog.splice(0, engineLog.length - 500);
    fastify.log.info(`[engine] ${line}`);
  };

  // The engine needs tens of seconds to boot into its waiting page; keep
  // knocking until the control port answers, and knock again whenever the
  // link drops while the engine is still alive.
  async function connectLink(): Promise<void> {
    if (link || !launcher.isRunning()) return;
    const attempt = new EngineLink(controlPort, { authKey: await streamerKey() });
    try {
      await attempt.connect();
    } catch {
      scheduleReconnect();
      return;
    }
    link = attempt;
    fastify.log.info('rig: engine link up');
    attempt.on('state', (state: EngineState) => {
      lastState = state;
      fastify.io.to('room_admins').emit('rig:state', state);
    });
    attempt.on('down', () => {
      fastify.log.warn('rig: engine link down');
      link = null;
      lastState = null;
      scheduleReconnect();
    });
    if (pendingSchedule) {
      attempt.send(scheduleCommand(pendingSchedule));
      fastify.log.info('rig: queued schedule sent');
      pendingSchedule = null;
    }
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      void connectLink();
    }, 2000);
  }

  fastify.addHook('preHandler', async (request, reply) => {
    try {
      if (!request.cookies.token) return reply.code(401).redirect('/login');
      const decoded: UserPayload = await request.jwtVerify();
      if (!decoded.roles || !decoded.roles.includes('ADMIN'))
        return reply.code(403).send({ error: 'Forbidden' });
      request.user = decoded;
    } catch {
      return reply.code(401).send({ error: 'Unauthorized' });
    }
  });

  fastify.get('/rig', async (request, reply) => {
    let teams: EngineTeamRow[] = [];
    let dbError: string | null = null;
    try {
      teams = listTeams(engineDbPath);
    } catch (err) {
      dbError = `engine database not readable at ${engineDbPath}: ${(err as Error).message}`;
    }
    return reply.view(
      'rig/index.ejs',
      {
        user: request.user,
        teams,
        stadiums: listStadiums(engineRunDir),
        running: launcher.isRunning(),
        connected: link !== null,
        dbError,
      },
      { layout: 'layout.ejs' },
    );
  });

  fastify.post('/rig/schedule', async (request, reply) => {
    const body = request.body as Record<string, string>;
    try {
      const setup: MatchSetup = {
        team1Id: Number(body.team1Id),
        team2Id: Number(body.team2Id),
        stadiumObject: String(body.stadiumObject ?? ''),
        durationMinutes: Number(body.durationMinutes ?? 5),
        team1KitNum: Number(body.team1KitNum ?? 1),
        team2KitNum: Number(body.team2KitNum ?? 1),
      };
      const line = scheduleCommand(setup); // validate before anything is spawned
      if (!launcher.isRunning()) {
        launcher.start({ controlPort, streamerKey: await streamerKey() }, log);
      }
      if (link) {
        link.send(line);
        fastify.log.info('rig: schedule sent');
      } else {
        pendingSchedule = setup; // goes out the moment the engine answers
        void connectLink();
      }
    } catch (err) {
      return reply.code(400).send({ error: (err as Error).message });
    }
    return reply.redirect('/rig');
  });

  fastify.post('/rig/stop', async (request, reply) => {
    pendingSchedule = null;
    launcher.stop();
    return reply.redirect('/rig');
  });

  fastify.post('/rig/command', async (request, reply) => {
    if (!link) return reply.code(503).send({ error: 'engine not connected' });
    const body = request.body as CommandBody;
    let line: string;
    try {
      switch (body.kind) {
        case 'tactic':
          line = tacticCommand(body.side, String(body.name), Number(body.value));
          break;
        case 'philosophy':
          line = philosophyCommand(body.side, String(body.name));
          break;
        case 'mentality':
          line = mentalityCommand(body.side, String(body.name));
          break;
        case 'instruction':
          line = instructionCommand(body.side, String(body.name), Boolean(body.on));
          break;
        case 'sub':
          line = subCommand(body.side, Number(body.outId), Number(body.inId));
          break;
        case 'resume':
          line = resumeCommand();
          break;
        default:
          return reply.code(400).send({ error: 'unknown command kind' });
      }
    } catch (err) {
      return reply.code(400).send({ error: (err as Error).message });
    }
    link.send(line);
    fastify.log.info(`rig: sent '${line}'`);
    return reply.send({ ok: true, line });
  });

  fastify.get('/rig/state', async (request, reply) => {
    if (!lastState) return reply.code(503).send({ error: 'no state yet' });
    return reply.send(lastState);
  });

  fastify.get('/rig/log', async (request, reply) => {
    return reply.send({ running: launcher.isRunning(), connected: link !== null, log: engineLog });
  });
}
