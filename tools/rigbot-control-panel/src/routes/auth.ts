import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { PrismaClient, UserRole } from '@prisma/client';
import bcrypt from 'bcrypt';
import jwt from 'jsonwebtoken';

import { UserPayload } from '../types/fastify';

const prisma = new PrismaClient();
const JWT_SECRET = process.env.JWT_SECRET || 'supersecret';

export async function authRoutes(fastify: FastifyInstance) {
  // This preHandler will run for all auth routes
  // It tries to get the user from the JWT if it exists, but doesn't fail if it doesn't
  fastify.addHook('preHandler', async (request, reply) => {
    try {
      if (request.cookies.token) {
        const decoded: UserPayload = await request.jwtVerify();
        request.user = decoded;
      }
    } catch (err) {
      // Ignore error, user will be undefined
    }
  });

  fastify.get('/login', async (request, reply) => {
    return reply.view('login.ejs', { user: request.user }, { layout: 'layout.ejs' });
  });

  fastify.post('/login', async (request, reply) => {
    const { username, password } = request.body as any;
    const user = await prisma.user.findUnique({ where: { username }, include: { roles: true } });
    fastify.log.info(`Login attempt for user: ${username}, found user: ${!!user}`);

    if (!user || !bcrypt.compareSync(password, user.passwordHash)) {
      fastify.log.warn(`Login failed for user: ${username}. Invalid credentials.`);
      return reply.code(401).send({ error: 'Invalid credentials' });
    }

    const userRoles = user.roles.map(r => r.name);
    const token = jwt.sign({ userId: user.id, username: user.username, roles: userRoles, teamId: user.teamId }, JWT_SECRET, { expiresIn: '1d' });
    fastify.log.info(`Login successful for user: ${username}, roles: ${userRoles}, token payload: ${JSON.stringify(jwt.decode(token))}`);
    reply.setCookie('token', token, { path: '/', httpOnly: true, sameSite: 'lax' });
    
    return reply.redirect('/');
  });

  fastify.get('/logout', async (request, reply) => {
    reply.clearCookie('token', { path: '/' });
    return reply.redirect('/login');
  });
}
