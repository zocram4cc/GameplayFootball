import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import fastifyView from '@fastify/view';
import { PrismaClient, UserRole, MatchType } from '@prisma/client';
import bcrypt from 'bcrypt';
import path from 'path';

import { UserPayload } from '../types/fastify';

const prisma = new PrismaClient();

export async function adminRoutes(fastify: FastifyInstance) {

  fastify.addHook('preHandler', async (request, reply) => {
    try {
      const token = request.cookies.token;
      fastify.log.info(`Admin preHandler: Token found: ${!!token}`);
      if (!token) {
        return reply.code(401).redirect('/login');
      }
      const decoded: UserPayload = await request.jwtVerify();
      fastify.log.info(`Admin preHandler: Decoded token: ${JSON.stringify(decoded)}, roles: ${decoded.roles}`);
      if (!decoded.roles || !decoded.roles.includes('ADMIN')) {
        fastify.log.warn(`Admin preHandler: User ${decoded.username} (roles: ${decoded.roles}) attempted to access admin panel without ADMIN role.`);
        return reply.code(403).send({ error: 'Forbidden' });
      }
      request.user = decoded;
    } catch (err: any) {
      fastify.log.error(`Admin preHandler: JWT verification failed: ${(err as Error).message}`);
      return reply.code(401).send({ error: 'Unauthorized' });
    }
  });

  fastify.get('/admin', async (request, reply) => {
    return reply.view('admin/index.ejs', { user: request.user }, { layout: 'layout.ejs' });
  });

  fastify.get('/admin/users', async (request, reply) => {
    const users = await prisma.user.findMany({ include: { team: true, roles: true } });
    const teams = await prisma.team.findMany();
    const tokens = await prisma.registrationToken.findMany({ where: { used: false, expires: { gt: new Date() } }, include: { roles: true } });
    return reply.view('admin/users.ejs', { users, teams, tokens, baseUrl: `${request.protocol}://${request.hostname}`, path: require('path'), user: request.user }, { layout: 'layout.ejs' });
  });

  fastify.post('/admin/users', async (request, reply) => {
    console.log('Request Body:', request.body);
    const { username, roles, teamId } = request.body as any;
    console.log('Parsed Data - Username:', username, 'Roles:', roles, 'TeamId:', teamId);

        // Check if user already exists
    const existingUser = await prisma.user.findUnique({ where: { username } });
    if (existingUser) {
      return reply.code(409).send({ error: `User ${username} already exists.` });
    }

    // Check for existing active registration token for the username
    const existingToken = await prisma.registrationToken.findFirst({
      where: {
        username,
        used: false,
        expires: { gt: new Date() },
      },
    });

    if (existingToken) {
      console.log('Existing token found:', existingToken);
      return reply.code(409).send({ error: `An active registration link already exists for user ${username}.` });
    }

    const crypto = require('crypto');
    const token = crypto.randomBytes(20).toString('hex');
    const expires = new Date(Date.now() + 24 * 60 * 60 * 1000); // 24 hours

    const rolesToConnect = Array.isArray(roles) ? roles : [roles];
    console.log('Roles to connect:', rolesToConnect);

    const createdToken = await prisma.registrationToken.create({
      data: {
        token,
        username,
        roles: { connect: rolesToConnect.map(r => ({ name: r })) },
        teamId: rolesToConnect.includes('MANAGER') && teamId ? teamId : null,
        expires,
      },
    });

    const users = await prisma.user.findMany({ include: { roles: true, team: true } });
    const teams = await prisma.team.findMany();
    const tokens = await prisma.registrationToken.findMany({ where: { used: false, expires: { gt: new Date() } }, include: { roles: true } });
    return reply.view('admin/user_management_content.ejs', { users, teams, tokens, baseUrl: `${request.protocol}://${request.hostname}`, user: request.user });
  });
  fastify.get('/admin/users/:userId/edit', async (request: FastifyRequest<{ Params: { userId: string } }>, reply) => {
    const { userId } = request.params;
    const userToEdit = await prisma.user.findUnique({
      where: { id: userId },
      include: { roles: true },
    });

    if (!userToEdit) {
      return reply.code(404).send({ error: 'User not found' });
    }

    const allTeams = await prisma.team.findMany();
    const allRoles = await prisma.role.findMany();

    return reply.view('admin/edit_user.ejs', {
      userToEdit,
      allTeams,
      allRoles,
      user: request.user, // for layout
    }, { layout: 'layout.ejs' });
  });

  fastify.post('/admin/users/:userId/edit', async (request: FastifyRequest<{ Params: { userId: string } }>, reply) => {
    const { userId } = request.params;
    const { roles, teamId } = request.body as { roles?: string | string[], teamId?: string };

    const rolesToSet = (Array.isArray(roles) ? roles : (roles ? [roles] : []));

    const updateData: any = {
      roles: {
        set: rolesToSet.map(roleName => ({ name: roleName as UserRole }))
      },
      teamId: teamId || null
    };

    await prisma.user.update({
      where: { id: userId },
      data: updateData,
    });

    return reply.redirect('/admin/users');
  });

  fastify.post('/admin/teams', async (request, reply) => {
    const { name } = request.body as any;
    await prisma.team.create({ data: { name } });
    const users = await prisma.user.findMany({ include: { roles: true, team: true } });
    const teams = await prisma.team.findMany();
    const tokens = await prisma.registrationToken.findMany({ where: { used: false, expires: { gt: new Date() } }, include: { roles: true } });
    return reply.view('admin/user_management_content.ejs', { users, teams, tokens, baseUrl: `${request.protocol}://${request.hostname}`, user: request.user });
  });

  fastify.get('/admin/monitor', async (request, reply) => {
    return reply.view('admin/monitor.ejs', { user: request.user }, { layout: 'layout.ejs' });
  });

  fastify.get('/admin/settings', async (request, reply) => {
    const streamUrl = await prisma.setting.findUnique({ where: { key: 'stream_url' } });
    const streamerToken = await prisma.setting.findUnique({ where: { key: 'streamer_token' } });
    return reply.view('admin/settings.ejs', { user: request.user, streamUrl, streamerToken }, { layout: 'layout.ejs' });
  });

  fastify.get('/admin/api/connections', async (request, reply) => {
    const sockets = await fastify.io.fetchSockets();
    const connections = sockets.map(socket => ({
      id: socket.id,
      role: socket.data.role,
      username: socket.data.username,
    }));
    return reply.view('admin/partials/connections.ejs', { connections, user: request.user });
  });

  fastify.post('/admin/api/kick/:socketId', async (request, reply) => {
    const { socketId } = request.params as any;
    const targetSocket = fastify.io.sockets.sockets.get(socketId);
    if (targetSocket) {
      targetSocket.disconnect(true);
    }
    return reply.header('HX-Trigger', 'updateConnections').send();
  });

  fastify.delete('/admin/users/:userId', async (request, reply) => {
    const { userId } = request.params as any;

    // Find the user to get their username for deleting registration tokens
    const userToDelete = await prisma.user.findUnique({ where: { id: userId } });

    if (userToDelete) {
      // Delete associated registration tokens
      await prisma.registrationToken.deleteMany({ where: { username: userToDelete.username } });

      // Delete the user
      await prisma.user.delete({ where: { id: userId } });
    }

    // Re-render the user management content
    const users = await prisma.user.findMany({ include: { roles: true, team: true } });
    const teams = await prisma.team.findMany();
    const tokens = await prisma.registrationToken.findMany({ where: { used: false, expires: { gt: new Date() } }, include: { roles: true } });
    return reply.view('admin/user_management_content.ejs', { users, teams, tokens, baseUrl: `${request.protocol}://${request.hostname}`, user: request.user });
  });

  fastify.delete('/admin/registration-tokens/:tokenId', async (request, reply) => {
    const { tokenId } = request.params as any;

    await prisma.registrationToken.delete({ where: { id: tokenId } });

    // Re-render the user management content
    const users = await prisma.user.findMany({ include: { team: true, roles: true } });
    const teams = await prisma.team.findMany();
    const tokens = await prisma.registrationToken.findMany({ where: { used: false, expires: { gt: new Date() } }, include: { roles: true } });
    return reply.view('admin/user_management_content.ejs', { users, teams, tokens, baseUrl: `${request.protocol}://${request.hostname}`, user: request.user });
  });

  fastify.post('/admin/settings', async (request, reply) => {
    const { stream_url, streamer_token } = request.body as any;
    if (stream_url) {
      await prisma.setting.upsert({
        where: { key: 'stream_url' },
        update: { value: stream_url },
        create: { key: 'stream_url', value: stream_url },
      });
    }
    if (streamer_token) {
        await prisma.setting.upsert({
            where: { key: 'streamer_token' },
            update: { value: streamer_token },
            create: { key: 'streamer_token', value: streamer_token },
        });
    }
    return reply.redirect('/admin/settings');
  });

  fastify.delete('/admin/teams/:teamId', async (request, reply) => {
    const { teamId } = request.params as any;

    // Find all users (managers) associated with the team
    const usersToDelete = await prisma.user.findMany({
      where: { teamId: teamId },
    });

    if (usersToDelete.length > 0) {
      const usernamesToDelete = usersToDelete.map(user => user.username);

      // Delete registration tokens for these users
      await prisma.registrationToken.deleteMany({
        where: { username: { in: usernamesToDelete } },
      });

      // Delete the users
      await prisma.user.deleteMany({
        where: { id: { in: usersToDelete.map(user => user.id) } },
      });
    }

    // Finally, delete the team
    await prisma.team.delete({ where: { id: teamId } });

    // Re-render the user management content
    const users = await prisma.user.findMany({ include: { roles: true, team: true } });
    const teams = await prisma.team.findMany();
    const tokens = await prisma.registrationToken.findMany({ where: { used: false, expires: { gt: new Date() } }, include: { roles: true } });
    return reply.view('admin/user_management_content.ejs', { users, teams, tokens, baseUrl: `${request.protocol}://${request.hostname}`, user: request.user });
  });

  fastify.get('/admin/schedule', async (request, reply) => {
    const gameVersions = ['pes17', 'pes21'];
    return reply.view('admin/schedule.ejs', { user: request.user, gameVersions }, { layout: 'layout.ejs' });
  });

  fastify.get('/admin/schedule/content', async (request, reply) => {
    const cupDays = await prisma.cupDay.findMany({
      include: {
        matches: {
          include: {
            teamA: true,
            teamB: true,
          },
          orderBy: {
            scheduledTime: 'asc',
          },
        },
      },
      orderBy: {
        date: 'asc',
      },
    });
    const teams = await prisma.team.findMany();
    const matchTypes = Object.values(MatchType);
    return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
  });

  fastify.post('/admin/cupdays', async (request, reply) => {
    const { name, date, gameVersion } = request.body as any;
    await prisma.cupDay.create({
      data: {
        name,
        date: new Date(date),
        gameVersion
      },
    });
    const cupDays = await prisma.cupDay.findMany({
      include: {
        matches: {
          include: {
            teamA: true,
            teamB: true,
          },
          orderBy: {
            scheduledTime: 'asc',
          },
        },
      },
      orderBy: {
        date: 'asc',
      },
    });
    const teams = await prisma.team.findMany();
    const matchTypes = Object.values(MatchType);
    return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
  });

  fastify.delete('/admin/cupdays/:cupDayId', async (request, reply) => {
    const { cupDayId } = request.params as any;
    await prisma.match.deleteMany({ where: { cupDayId } });
    await prisma.cupDay.delete({ where: { id: cupDayId } });
    const cupDays = await prisma.cupDay.findMany({
      include: {
        matches: {
          include: {
            teamA: true,
            teamB: true,
          },
          orderBy: {
            scheduledTime: 'asc',
          },
        },
      },
      orderBy: {
        date: 'asc',
      },
    });
    const teams = await prisma.team.findMany();
    const matchTypes = Object.values(MatchType);
    return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
  });

  fastify.post('/admin/matches', async (request, reply) => {
    const { cupDayId, teamAId, teamBId, scheduledTime, type } = request.body as any;
    await prisma.match.create({
      data: {
        cupDayId,
        teamAId,
        teamBId,
        scheduledTime: new Date(scheduledTime),
        type,
      },
    });
    const cupDays = await prisma.cupDay.findMany({
      include: {
        matches: {
          include: {
            teamA: true,
            teamB: true,
          },
          orderBy: {
            scheduledTime: 'asc',
          },
        },
      },
      orderBy: {
        date: 'asc',
      },
    });
    const teams = await prisma.team.findMany();
    const matchTypes = Object.values(MatchType);
    return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
  });

  fastify.delete('/admin/matches/:matchId', async (request, reply) => {
    const { matchId } = request.params as any;
    await prisma.match.delete({ where: { id: matchId } });
    const cupDays = await prisma.cupDay.findMany({
      include: {
        matches: {
          include: {
            teamA: true,
            teamB: true,
          },
          orderBy: {
            scheduledTime: 'asc',
          },
        },
      },
      orderBy: {
        date: 'asc',
      },
    });
    const teams = await prisma.team.findMany();
    const matchTypes = Object.values(MatchType);
    return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
  });

  fastify.post('/admin/matches/:matchId/edit', async (request, reply) => {
    const { matchId } = request.params as any;
    const { scheduledTime, teamAId, teamBId } = request.body as any;
        await prisma.match.update({
          where: { id: matchId },
          data: {
            scheduledTime: new Date(scheduledTime),
            teamAId: teamAId,
            teamBId: teamBId
          },
        });
        const cupDays = await prisma.cupDay.findMany({
            include: {
                matches: {
                    include: {
                        teamA: true,
                        teamB: true,
                    },
                    orderBy: {
                        scheduledTime: 'asc',
                    },
                },
            },
            orderBy: {
                date: 'asc',
            },
        });
        const teams = await prisma.team.findMany();
        const matchTypes = Object.values(MatchType);
        return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
      });
  fastify.post('/admin/api/matches/:matchId/start', async (request, reply) => {
    const { matchId } = request.params as any;
    const match = await prisma.match.findUnique({ where: { id: matchId } });
    if (match) {
        await prisma.match.update({ where: { id: matchId }, data: { status: 'ACTIVE' } });
        fastify.io.to('room_streamer').emit('load_match', match);
    }
    const cupDays = await prisma.cupDay.findMany({
        include: {
            matches: {
                include: {
                    teamA: true,
                    teamB: true,
                },
                orderBy: {
                    scheduledTime: 'asc',
                },
            },
        },
        orderBy: {
            date: 'asc',
        },
    });
    const teams = await prisma.team.findMany();
    const matchTypes = Object.values(MatchType);
    return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
  });

  fastify.post('/admin/api/matches/:matchId/force-finish', async (request, reply) => {
    const { matchId } = request.params as any;
    await prisma.match.update({ where: { id: matchId }, data: { status: 'FINISHED' } });
    const cupDays = await prisma.cupDay.findMany({
        include: {
            matches: {
                include: {
                    teamA: true,
                    teamB: true,
                },
                orderBy: {
                    scheduledTime: 'asc',
                },
            },
        },
        orderBy: {
            date: 'asc',
        },
    });
    const teams = await prisma.team.findMany();
    const matchTypes = Object.values(MatchType);
    return reply.view('admin/partials/schedule_content.ejs', { cupDays, teams, matchTypes, user: request.user });
  });
}
