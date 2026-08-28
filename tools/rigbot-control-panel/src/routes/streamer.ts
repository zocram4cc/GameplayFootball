
import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { PrismaClient, UserRole } from '@prisma/client';

import { UserPayload } from '../types/fastify';

const prisma = new PrismaClient();

export async function streamerRoutes(fastify: FastifyInstance) {
  fastify.addHook('preHandler', async (request, reply) => {
    try {
      const token = request.cookies.token;
      if (!token) {
        return reply.code(401).redirect('/login');
      }
      const decoded: UserPayload = await request.jwtVerify();
      if (!decoded.roles || (!decoded.roles.includes('ADMIN') && !decoded.roles.includes('STREAMER'))) {
        return reply.code(403).send({ error: 'Forbidden' });
      }
      request.user = decoded;
    } catch (err) {
      return reply.code(401).send({ error: 'Unauthorized' });
    }
  });

  fastify.get('/streamer', async (request, reply) => {
    const streamUrl = await prisma.setting.findUnique({ where: { key: 'stream_url' } });
    const streamerToken = await prisma.setting.findUnique({ where: { key: 'streamer_token' } });
    return reply.view('streamer/index.ejs', { streamUrl, streamerToken, user: request.user }, { layout: 'layout.ejs' });
  });

  fastify.post('/streamer/settings', async (request, reply) => {
    const { stream_url } = request.body as any;
    if (stream_url) {
      await prisma.setting.upsert({
        where: { key: 'stream_url' },
        update: { value: stream_url },
        create: { key: 'stream_url', value: stream_url },
      });
    }
    return reply.redirect('/streamer');
  });
}
