import Fastify, { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { Server } from 'socket.io';
import path from 'path';
import fastifyView from '@fastify/view';
import fastifyStatic from '@fastify/static';
import fastifyCookie from '@fastify/cookie';
import fastifyJwt from '@fastify/jwt';
import fastifyFormbody from '@fastify/formbody';
import ejs from 'ejs';
import jwt from 'jsonwebtoken'; // Import jsonwebtoken directly
import http from 'http'; // Node.js core http module
import socketIoPlugin from 'fastify-socket';

import { authRoutes } from './routes/auth';
import { adminRoutes } from './routes/admin';
import { commentatorRoutes } from './routes/commentator';
import { managerRoutes } from './routes/manager';
import { registrationRoutes } from './routes/register';

import { streamerRoutes } from './routes/streamer';
import { rigRoutes } from './routes/rig';
import { Tactics } from './types/fastify';


import { PrismaClient, UserRole } from '@prisma/client';

const prisma = new PrismaClient();
const JWT_SECRET = process.env.JWT_SECRET || 'supersecret'; // Define JWT_SECRET

async function startServer() {
  // 1. Create a Node.js HTTP server instance
  const httpServer = http.createServer();

  // 2. Create a Fastify instance and pass the custom HTTP server using serverFactory
  const fastify = Fastify({
    logger: true, // Enable logging for better visibility
    serverFactory: (handler) => {
      // Fastify will use this httpServer for its requests
      httpServer.on('request', handler);
      return httpServer;
    }
  });

  // Register plugins and middleware
  fastify.register(fastifyFormbody, {
    parser: str => require('qs').parse(str, { depth: 10 })
  }); // Register formbody first
  fastify.register(fastifyCookie);
  fastify.register(fastifyJwt, {
    secret: JWT_SECRET,
    cookie: {
      cookieName: 'token',
      signed: false
    }
  });

  // The user object will be passed to templates manually in each route.

  fastify.register(fastifyStatic, {
    root: path.join(__dirname, '../public'),
    prefix: '/public/',
  });
  fastify.register(fastifyView, {
    engine: {
      ejs: ejs,
    },
    root: path.join(__dirname, '../src/views')
  });

  // Register fastify-socket plugin
  fastify.register(socketIoPlugin, {
    cors: {
      origin: "*", // Allow all origins for development, adjust in production
      methods: ["GET", "POST"]
    }
  });

  // Register routes
  fastify.register(authRoutes);
  fastify.register(adminRoutes);
  fastify.register(commentatorRoutes);
  fastify.register(managerRoutes);
  fastify.register(registrationRoutes);

  fastify.register(streamerRoutes);
  fastify.register(rigRoutes);

  fastify.get('/', async (request, reply) => {
    return reply.redirect('/login');
  });

  fastify.setErrorHandler((error, request, reply) => {
    if (error.statusCode === 401) {
      fastify.log.warn('Caught 401 Unauthorized, redirecting to login.');
      // Clear the token cookie to prevent redirect loops
      reply.clearCookie('token');
      reply.redirect('/login');
    } else {
      // Let the default error handler manage other errors
      reply.send(error);
    }
  });

  fastify.ready((err) => {
    if (err) throw err;

    // Socket.IO event handling
    fastify.io.use(async (socket, next) => {
      try {
        const jwtToken = socket.request.headers.cookie?.split('; ').find(row => row.startsWith('token='))?.split('=')[1];

        // 1. Prioritize JWT authentication from cookie
        if (jwtToken) {
          try {
            const decoded: { userId: string; username: string; roles: UserRole[]; teamId?: string; } = jwt.verify(jwtToken, JWT_SECRET) as any;
            socket.data.userId = decoded.userId;
            socket.data.username = decoded.username;
            socket.data.roles = decoded.roles;
            socket.data.teamId = decoded.teamId;
            return next();
          } catch (jwtError) {
            // JWT is invalid, proceed to check for static token
            fastify.log.warn('JWT verification failed during socket connection, falling back...');
          }
        }

        // 2. Fallback to static streamer token from auth header
        const streamerTokenValue = socket.handshake.auth.token;
        if (streamerTokenValue) {
          const streamerToken = await prisma.setting.findUnique({ where: { key: 'streamer_token' } });
          if (streamerToken && streamerToken.value === streamerTokenValue) {
            socket.data.userId = 'streamer';
            socket.data.username = 'streamer';
            socket.data.roles = ['STREAMER'];
            return next();
          }
        }

        return next(new Error('Authentication error'));

      } catch (err) {
        next(new Error('Authentication error'));
      }
    });

    fastify.io.on('connection', async (socket) => {
      fastify.log.info(`User connected: ${socket.id}, roles: ${socket.data.roles}`);

      if (socket.data.roles.includes('ADMIN')) {
        socket.join('room_admins');
      }
      if (socket.data.roles.includes('COMMENTATOR')) {
        socket.join('room_commentators');
      }
      if (socket.data.roles.includes('STREAMER')) {
        socket.join('room_streamer');
        const activeMatch = await prisma.match.findFirst({ where: { status: 'ACTIVE' } });
        if (activeMatch) {
          socket.join(`room_team_${activeMatch.teamAId}`);
          socket.join(`room_team_${activeMatch.teamBId}`);
        }
      }
      if (socket.data.roles.includes('MANAGER') && socket.data.teamId) {
        const teamRoom = `room_team_${socket.data.teamId}`;
        socket.join(teamRoom); // Join the room immediately

        const socketsInRoom = await fastify.io.in(teamRoom).fetchSockets();
        const otherSockets = socketsInRoom.filter(s => s.id !== socket.id);

        if (otherSockets.length > 0) {
          const otherManagerUsernames = otherSockets.map(s => s.data.username);
          fastify.log.info(`Manager ${socket.data.username} connected, but found existing session(s) for: ${otherManagerUsernames.join(', ')}`);
          socket.emit('manager:existing_session', { managers: otherManagerUsernames });
        }
      }

      socket.on('streamer:request_teams', async () => {
        if (socket.data.roles.includes('STREAMER')) {
          fastify.log.info(`Received streamer:request_teams from ${socket.id}`);
          
          // 1. Check for an ACTIVE match first (no date constraint for reconnections)
          let matchToSend = await prisma.match.findFirst({
            where: { status: 'ACTIVE' },
            include: {
              teamA: true,
              teamB: true,
            },
          });

          // 2. If no ACTIVE match, look for a PENDING match for today
          if (!matchToSend) {
            const startOfToday = new Date();
            startOfToday.setHours(0, 0, 0, 0);
            const startOfTomorrow = new Date(startOfToday);
            startOfTomorrow.setDate(startOfToday.getDate() + 1);

            matchToSend = await prisma.match.findFirst({
              where: {
                status: 'PENDING',
                scheduledTime: {
                  gte: startOfToday,
                  lt: startOfTomorrow,
                },
              },
              orderBy: { scheduledTime: 'asc' },
              include: {
                teamA: true,
                teamB: true,
              },
            });
          }

          if (matchToSend) {
            fastify.log.info(`Found match to send: ${matchToSend.teamA.name} vs ${matchToSend.teamB.name} (Status: ${matchToSend.status})`);
            
            // Emit team info
            socket.emit('server:set_teams', {
              teamA: matchToSend.teamA.name,
              teamB: matchToSend.teamB.name,
            });

            const teamATactics = (matchToSend.teamATacticsJson || { presets: [], startPreset: 1 }) as unknown as Tactics;
            const teamBTactics = (matchToSend.teamBTacticsJson || { presets: [], startPreset: 1 }) as unknown as Tactics;

            socket.emit('server:set_tactics', {
                home_presets: teamATactics.presets,
                away_presets: teamBTactics.presets,
                home_start_preset: teamATactics.startPreset,
                away_start_preset: teamBTactics.startPreset,
                match_type: matchToSend.type
            });

            // If it was a PENDING match, update its status to ACTIVE
            if (matchToSend.status === 'PENDING') {
              await prisma.match.update({
                where: { id: matchToSend.id },
                data: { status: 'ACTIVE' },
              });
              fastify.log.info(`Match ${matchToSend.id} status updated to ACTIVE.`);
              const teamARoom = `room_team_${matchToSend.teamAId}`;
              const teamBRoom = `room_team_${matchToSend.teamBId}`;
              fastify.io.to(teamARoom).to(teamBRoom).emit('match:start', { matchId: matchToSend.id });
            }

          } else {
            fastify.log.warn('No active or pending matches found for streamer request.');
            socket.emit('error:no_match_found', { message: 'No active or pending matches available.' });
          }
        }
      });


      socket.on('streamer:request_tactics', async () => {
        if (socket.data.roles.includes('STREAMER')) {
          fastify.log.info(`Received streamer:request_tactics from ${socket.id}`);

          const activeMatch = await prisma.match.findFirst({
            where: { status: 'ACTIVE' },
            include: {
              teamA: true,
              teamB: true,
            },
          });

          if (activeMatch) {
            fastify.log.info(`Found active match for tactics request: ${activeMatch.teamA.name} vs ${activeMatch.teamB.name}`);

            const teamATactics = (activeMatch.teamATacticsJson || { presets: [], startPreset: 1 }) as unknown as Tactics;
            const teamBTactics = (activeMatch.teamBTacticsJson || { presets: [], startPreset: 1 }) as unknown as Tactics;

            socket.emit('server:set_tactics', {
                home_presets: teamATactics.presets,
                away_presets: teamBTactics.presets,
                home_start_preset: teamATactics.startPreset,
                away_start_preset: teamBTactics.startPreset,
                match_type: activeMatch.type
            });
          } else {
            fastify.log.warn('No active match found for streamer tactics request.');
            socket.emit('error:no_active_match', { message: 'No active match available for tactics.' });
          }
        }
      });

      socket.on('manager:command', async (data) => {
        if (socket.data.roles.includes('MANAGER') && socket.data.teamId) {
          const activeMatch = await prisma.match.findFirst({
            where: {
              status: 'ACTIVE',
              OR: [
                { teamAId: socket.data.teamId },
                { teamBId: socket.data.teamId },
              ],
            },
          });

          if (activeMatch) {
            const teamIdentifier = activeMatch.teamAId === socket.data.teamId ? 'A' : 'B';
            fastify.log.info(`Manager command from ${socket.data.username} for team ${teamIdentifier}: ${JSON.stringify(data)}`);
            
            // Transform the payload for the Rigbot
            const playerSide = teamIdentifier === 'A' ? 'home' : 'away';
            const newPayload = {
              tactic: data.action,
              player: playerSide
            };

            fastify.log.info(`Relaying transformed command to streamer: ${JSON.stringify(newPayload)}`);
            fastify.io.to('room_streamer').emit('manager:command', newPayload);
          } else {
            fastify.log.warn(`Unauthorized manager command from ${socket.data.username}. No active match for team ${socket.data.teamId}.`);
            socket.emit('command:error', { message: 'Your team is not in an active match.' });
          }
        }
      });

      socket.on('manager:force_disconnect', async () => {
        if (socket.data.roles.includes('MANAGER') && socket.data.teamId) {
          const teamRoom = `room_team_${socket.data.teamId}`;
          const socketsInRoom = await fastify.io.in(teamRoom).fetchSockets();
          const socketsToDisconnect = socketsInRoom.filter(s => s.id !== socket.id);

          if (socketsToDisconnect.length > 0) {
            fastify.log.info(`Manager ${socket.data.username} is forcing disconnection of ${socketsToDisconnect.length} other session(s).`);
            socketsToDisconnect.forEach(otherSocket => {
              otherSocket.disconnect(true);
            });
          }
          // Notify the client that the action was successful
          socket.emit('manager:force_disconnect_success');
        }
      });

      socket.on('streamer:state', async (data) => {
        fastify.log.info(`Streamer state update: ${JSON.stringify(data)}`);
        const activeMatch = await prisma.match.findFirst({ where: { status: 'ACTIVE' } });

        let rooms: string[] = ['room_admins', 'room_commentators'];
        if (activeMatch) {
          rooms.push(`room_team_${activeMatch.teamAId}`);
          rooms.push(`room_team_${activeMatch.teamBId}`);
        }
        fastify.io.to(rooms).emit('streamer:state', data);
      });

      socket.on('streamer:goal_scored', async (data) => {
        fastify.log.info(`Streamer goal scored: ${JSON.stringify(data)}`);
        const activeMatch = await prisma.match.findFirst({ where: { status: 'ACTIVE' } });

        let rooms: string[] = ['room_admins', 'room_commentators'];
        if (activeMatch) {
          rooms.push(`room_team_${activeMatch.teamAId}`);
          rooms.push(`room_team_${activeMatch.teamBId}`);
        }
        fastify.io.to(rooms).emit('streamer:goal_scored', data);
      });

      socket.on('streamer:match_end', async (data) => {
        if (socket.data.roles.includes('STREAMER')) {
            const activeMatch = await prisma.match.findFirst({ where: { status: 'ACTIVE' } });
            if (activeMatch) {
                await prisma.match.update({
                    where: { id: activeMatch.id },
                    data: { status: 'FINISHED', finalScoreA: data.home_score, finalScoreB: data.away_score }
                });
                const teamARoom = `room_team_${activeMatch.teamAId}`;
                const teamBRoom = `room_team_${activeMatch.teamBId}`;
                fastify.io.to(teamARoom).to(teamBRoom).to('room_streamer').to('room_admins').emit('match:end');
                fastify.log.info(`Match ${activeMatch.id} finished by streamer.`);
            }
        }
      });

      socket.on('chat:message', async (data) => {
        const message = data.message.replace(/</g, "&lt;").replace(/>/g, "&gt;");
        if (!message) return;

        const senderUsername = socket.data.username;
        const senderRoles = socket.data.roles;

        // An Admin or Streamer is sending a message to a specific team
        if ((senderRoles.includes('ADMIN') || senderRoles.includes('STREAMER')) && data.to) {
          const targetTeam = data.to; // Expects 'A' or 'B'
          const activeMatch = await prisma.match.findFirst({ where: { status: 'ACTIVE' } });

          if (activeMatch) {
            if (targetTeam === 'A') {
              const teamRoom = `room_team_${activeMatch.teamAId}`;
              const payload = { from: 'Streamer/Admin', team: 'A', message };
              fastify.io.to(teamRoom).to('room_admins').to('room_streamer').emit('chat:message', payload);
            } else if (targetTeam === 'B') {
              const teamRoom = `room_team_${activeMatch.teamBId}`;
              const payload = { from: 'Streamer/Admin', team: 'B', message };
              fastify.io.to(teamRoom).to('room_admins').to('room_streamer').emit('chat:message', payload);
            }
          }
        }
        // A Manager is sending a message from their own panel
        else if (senderRoles.includes('MANAGER') && socket.data.teamId) {
          const activeMatch = await prisma.match.findFirst({
            where: {
              status: 'ACTIVE',
              OR: [{ teamAId: socket.data.teamId }, { teamBId: socket.data.teamId }],
            },
          });

          if (activeMatch) {
            const teamIdentifier = activeMatch.teamAId === socket.data.teamId ? 'A' : 'B';
            const payload = {
              from: senderUsername,
              team: teamIdentifier, // 'A' or 'B'
              message,
            };
            // Send to all admins, all streamers, and all managers of that specific team
            const teamRoom = `room_team_${socket.data.teamId}`;
            fastify.io.to('room_admins').to('room_streamer').to(teamRoom).emit('chat:message', payload);
          }
        }
      });

      socket.on('disconnect', () => {
        fastify.log.info(`User disconnected: ${socket.id}`);
      });
    });
  });

  // Start the Fastify server. It will use the httpServer provided.
  try {
    await fastify.listen({ port: 3000, host: '0.0.0.0' });
    const address = fastify.server.address();
    if (address) {
      const port = typeof address === 'string' ? address : address.port;
      console.log(`Fastify server listening on ${port}`);
      console.log(`Socket.IO server attached to port ${port}`);
    }
  } catch (err) {
    fastify.log.error(err);
    process.exit(1);
  }
}

startServer();