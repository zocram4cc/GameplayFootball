import { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';

export const socketIoIgnorePlugin = async (fastify: FastifyInstance) => {
  fastify.addHook('preHandler', (request: FastifyRequest, reply: FastifyReply, done) => {
    if (request.url.startsWith('/socket.io/')) {
      // Mark the request as handled by Socket.IO
      // This prevents Fastify from trying to route it
      reply.sent = true;
      // Pass control to the underlying HTTP server, which Socket.IO is attached to
      fastify.server.emit('request', request.raw, reply.raw);
      return;
    }
    done();
  });
};
