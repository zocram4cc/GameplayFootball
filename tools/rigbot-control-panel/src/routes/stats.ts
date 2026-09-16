// Browses the match-stats documents the engine exports and serves the
// Wikipedia footballbox wikitext next to them. The panel only reads what
// the engine already wrote; it never generates or edits either file.
import { FastifyInstance } from 'fastify';

import { UserPayload } from '../types/fastify';
import { getMatchStats, getWikiText, listMatchStats } from '../rig/statsStore';

export async function statsRoutes(fastify: FastifyInstance) {
  // Exported stats are rig data: same ADMIN-only gate as /rig.
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

  fastify.get('/stats', async (request, reply) => {
    return reply.view(
      'stats/index.ejs',
      { user: request.user, matches: listMatchStats() },
      { layout: 'layout.ejs' },
    );
  });

  fastify.get('/stats/:matchId', async (request, reply) => {
    const { matchId } = request.params as { matchId: string };
    const doc = getMatchStats(matchId);
    if (!doc) return reply.code(404).send({ error: 'match not found' });
    return reply.view(
      'stats/show.ejs',
      { user: request.user, doc, wiki: getWikiText(matchId) },
      { layout: 'layout.ejs' },
    );
  });

  // The stored document, verbatim.
  fastify.get('/stats/:matchId/json', async (request, reply) => {
    const { matchId } = request.params as { matchId: string };
    const doc = getMatchStats(matchId);
    if (!doc) return reply.code(404).send({ error: 'match not found' });
    return reply.send(doc);
  });

  // Download of the engine-written footballbox wikitext, verbatim.
  fastify.get('/stats/:matchId/wiki.txt', async (request, reply) => {
    const { matchId } = request.params as { matchId: string };
    const wiki = getWikiText(matchId);
    if (wiki === null) return reply.code(404).send({ error: 'wikitext not found' });
    reply.type('text/plain; charset=utf-8');
    reply.header('content-disposition', `attachment; filename="${matchId}.wiki.txt"`);
    return reply.send(wiki);
  });
}
