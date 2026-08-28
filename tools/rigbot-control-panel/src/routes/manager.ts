import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { PrismaClient, UserRole } from '@prisma/client';
import { Tactics, TacticsPreset, UserPayload } from '../types/fastify';

const prisma = new PrismaClient();

export async function managerRoutes(fastify: FastifyInstance) {
  fastify.addHook('preHandler', async (request, reply) => {
    try {
      const token = request.cookies.token;
      if (!token) {
        return reply.code(401).redirect('/login');
      }
      const decoded: UserPayload = await request.jwtVerify();
      if (!decoded.roles || (!decoded.roles.includes('ADMIN') && !decoded.roles.includes('MANAGER'))) {
        return reply.code(403).send({ error: 'Forbidden' });
      }
      request.user = decoded;
    } catch (err) {
      return reply.code(401).send({ error: 'Unauthorized' });
    }
  });

  fastify.get('/manager', async (request, reply) => {
    const user = request.user as UserPayload;
    const streamUrl = await prisma.setting.findUnique({ where: { key: 'stream_url' } });

    // 1. Check for an ACTIVE match for this manager's team
    const activeMatch = await prisma.match.findFirst({
      where: {
        status: 'ACTIVE',
        OR: [
          { teamAId: user.teamId },
          { teamBId: user.teamId },
        ],
      },
    });

    // If there's an active match, show the active match view
    if (activeMatch) {
      return reply.view('manager/active_match.ejs', { streamUrl, user }, { layout: 'layout.ejs' });
    }

    // 2. If no active match, show the list of PENDING matches
    const pendingMatches = await prisma.match.findMany({
      where: {
        status: 'PENDING',
        OR: [
          { teamAId: user.teamId },
          { teamBId: user.teamId },
        ],
      },
      include: {
        teamA: true,
        teamB: true,
        cupDay: true,
      },
      orderBy: {
        scheduledTime: 'asc',
      },
    });

    return reply.view('manager/index.ejs', { streamUrl, matches: pendingMatches, currentUserTeamId: user.teamId, user: user }, { layout: 'layout.ejs' });
  });

  fastify.get('/manager/match/:matchId/tactics', async (request: FastifyRequest<{ Params: { matchId: string } }>, reply) => {
    const user = request.user as UserPayload;
    const { matchId } = request.params;
    const { teamId } = user;

    const match = await prisma.match.findUnique({
      where: { id: matchId },
      include: { cupDay: true, teamA: true, teamB: true },
    });

    if (!match) {
      return reply.code(404).send({ error: 'Match not found' });
    }

    const isTeamA = match.teamAId === teamId;
    const isTeamB = match.teamBId === teamId;

    if (!isTeamA && !isTeamB) {
      return reply.code(403).send({ error: 'You are not a manager for either team in this match.' });
    }

    const TACTICS_OPTIONS = (await import('../data/tactics')).TACTICS_OPTIONS;
    const gameVersion = match.cupDay.gameVersion as keyof typeof TACTICS_OPTIONS;
    const possibleTactics = TACTICS_OPTIONS[gameVersion] || TACTICS_OPTIONS['pes21']; // Fallback to pes21

    const savedTactics = (isTeamA ? match.teamATacticsJson : match.teamBTacticsJson) as unknown as Tactics | null;
    const presetCount = savedTactics?.presets?.length || 3;

    return reply.view('manager/tactics_form.ejs', { 
      match,
      possibleTactics,
      savedTactics: savedTactics || { presets: [{}, {}, {}], startPreset: 1 }, // Provide default structure
      presetCount,
      teamName: isTeamA ? match.teamA.name : match.teamB.name,
      user: user
    }, { layout: 'layout.ejs' });
  });

  fastify.post('/manager/match/:matchId/tactics', async (request: FastifyRequest<{ Params: { matchId: string } }>, reply) => {
    const user = request.user as UserPayload;
    const { matchId } = request.params;
    const { teamId } = user;
    const formData = request.body as {
      presets: any[];
      startPreset: string;
      presetCount: string;
    };

    const match = await prisma.match.findUnique({
      where: { id: matchId },
    });

    if (!match) {
      return reply.code(404).send({ error: 'Match not found' });
    }

    const isTeamA = match.teamAId === teamId;
    const isTeamB = match.teamBId === teamId;

    if (!isTeamA && !isTeamB) {
      return reply.code(403).send({ error: 'You are not a manager for either team in this match.' });
    }

    const presetCount = parseInt(formData.presetCount, 10) || 3;

    // Re-structure the form data into the desired JSON object
    const tacticsPayload: Tactics = {
      presets: (formData.presets || []).filter(p => p).slice(0, presetCount).map((p: any) => ({
        ...p,
        support_range: parseInt(p.support_range, 10) || undefined,
        defensive_line: parseInt(p.defensive_line, 10) || undefined,
        compactness: parseInt(p.compactness, 10) || undefined,
      })),
      startPreset: parseInt(formData.startPreset, 10),
    };

    console.log('Saving tactics payload:', JSON.stringify(tacticsPayload, null, 2));

    const dataField = isTeamA ? 'teamATacticsJson' : 'teamBTacticsJson';
    const savedByField = isTeamA ? 'teamATacticsSavedBy' : 'teamBTacticsSavedBy';
    const savedAtField = isTeamA ? 'teamATacticsSavedAt' : 'teamBTacticsSavedAt';

    await prisma.match.update({
      where: { id: matchId },
      data: {
        [dataField]: tacticsPayload as any, // Cast to any to avoid Prisma Json issues
        [savedByField]: user.username,
        [savedAtField]: new Date(),
      },
    });

    return reply.redirect('/manager');
  });
}
