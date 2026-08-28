import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { PrismaClient, UserRole } from '@prisma/client';

import { UserPayload } from '../types/fastify';

const prisma = new PrismaClient();

export async function commentatorRoutes(fastify: FastifyInstance) {
  fastify.addHook('preHandler', async (request, reply) => {
    try {
      const token = request.cookies.token;
      if (!token) {
        return reply.code(401).redirect('/login');
      }
      const decoded: UserPayload = await request.jwtVerify();
      if (!decoded.roles || (!decoded.roles.includes('ADMIN') && !decoded.roles.includes('COMMENTATOR'))) {
        return reply.code(403).send({ error: 'Forbidden' });
      }
      request.user = decoded;
    } catch (err) {
      return reply.code(401).send({ error: 'Unauthorized' });
    }
  });

  fastify.get('/commentator', async (request, reply) => {
    const streamUrl = await prisma.setting.findUnique({ where: { key: 'stream_url' } });
    return reply.view('commentator/index.ejs', { streamUrl, user: request.user }, { layout: 'layout.ejs' });
  });
}
