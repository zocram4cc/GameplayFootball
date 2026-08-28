import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { PrismaClient, UserRole } from '@prisma/client';
import bcrypt from 'bcrypt';

import { UserPayload } from '../types/fastify';

const prisma = new PrismaClient();

export async function registrationRoutes(fastify: FastifyInstance) {

  fastify.get('/register', async (request, reply) => {
    const { token } = request.query as any;
    const tokenData = await prisma.registrationToken.findUnique({ where: { token } });

    if (!tokenData || tokenData.used || tokenData.expires < new Date()) {
      return reply.code(400).send({ error: 'Invalid or expired token' });
    }

    return reply.view('register.ejs', { token, username: tokenData.username, user: request.user }, { layout: 'layout.ejs' });
  });

  fastify.post('/register', async (request, reply) => {
    const { token, password } = request.body as any;
    const tokenData = await prisma.registrationToken.findUnique({ where: { token }, include: { roles: true } });

    if (!tokenData || tokenData.used || tokenData.expires < new Date()) {
      return reply.code(400).send({ error: 'Invalid or expired token' });
    }

    const salt = bcrypt.genSaltSync(10);
    const passwordHash = bcrypt.hashSync(password, salt);

    await prisma.user.create({
      data: {
        username: tokenData.username,
        passwordHash: passwordHash,
        roles: { connect: tokenData.roles.map(r => ({ id: r.id })) },
        teamId: tokenData.teamId,
      },
    });

    await prisma.registrationToken.update({
      where: { id: tokenData.id },
      data: { used: true },
    });

    return reply.redirect('/login');
  });
}
