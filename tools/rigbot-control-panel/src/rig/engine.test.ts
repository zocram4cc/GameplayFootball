// EngineLink against a mock engine: a net server speaking the real protocol.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { once } from 'node:events';

import { EngineLink } from './engine';
import type { EngineState } from './protocol';

const snapshot = JSON.stringify({
  score: [1, 0],
  time_ms: 60000,
  phase: 1,
  in_play: true,
  sub_window: false,
  teams: [
    { name: 'A', philosophy: 'balanced', mentality: 'balanced', instructions: [], tactics: {}, players: [] },
    { name: 'B', philosophy: 'balanced', mentality: 'balanced', instructions: [], tactics: {}, players: [] },
  ],
});

interface MockEngine {
  port: number;
  lines: string[];
  close: () => Promise<void>;
}

async function startMockEngine(): Promise<MockEngine> {
  const lines: string[] = [];
  const sockets = new Set<net.Socket>();
  const server = net.createServer((socket) => {
    sockets.add(socket);
    socket.on('close', () => sockets.delete(socket));
    let pending = '';
    socket.on('data', (chunk) => {
      pending += chunk.toString('utf8');
      let index;
      while ((index = pending.indexOf('\n')) >= 0) {
        const line = pending.slice(0, index);
        pending = pending.slice(index + 1);
        if (!line) continue;
        lines.push(line);
        if (line === 'state') socket.write(snapshot + '\n');
      }
    });
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  return {
    port: (server.address() as net.AddressInfo).port,
    lines,
    close: () => {
      const { promise, resolve } = Promise.withResolvers<void>();
      for (const socket of sockets) socket.destroy();
      server.close(() => resolve());
      return promise;
    },
  };
}

test('EngineLink delivers commands as protocol lines', async () => {
  const engine = await startMockEngine();
  const link = new EngineLink(engine.port);
  try {
    await link.connect();
    link.send('tactic 0 team_pressure 0.8');
    link.send('philosophy 1 gegenpressing');
    await link.requestState(); // ordered after the sends on the same socket
    assert.deepEqual(engine.lines.slice(0, 2), [
      'tactic 0 team_pressure 0.8',
      'philosophy 1 gegenpressing',
    ]);
  } finally {
    link.close();
    await engine.close();
  }
});

test('EngineLink round-trips a state request', async () => {
  const engine = await startMockEngine();
  const link = new EngineLink(engine.port);
  try {
    await link.connect();
    const state = await link.requestState();
    assert.ok(state);
    assert.deepEqual(state.score, [1, 0]);
    assert.equal(state.teams[0].name, 'A');
  } finally {
    link.close();
    await engine.close();
  }
});

test('EngineLink polls and emits state events', async () => {
  const engine = await startMockEngine();
  const link = new EngineLink(engine.port, { pollMs: 20 });
  try {
    await link.connect();
    // Two distinct polled snapshots prove the poll loop, not a lucky single shot.
    const first = once(link, 'state');
    const second = first.then(() => once(link, 'state'));
    // once() erases the emitter's payload type; EngineLink emits parsed EngineState.
    const [state] = (await second) as [EngineState];
    assert.deepEqual(state.score, [1, 0]);
  } finally {
    link.close();
    await engine.close();
  }
});

test('EngineLink reports the engine going away instead of throwing', async () => {
  const engine = await startMockEngine();
  const link = new EngineLink(engine.port, { pollMs: 20 });
  try {
    await link.connect();
    const down = once(link, 'down');
    await engine.close();
    await down; // resolves rather than the process dying on ECONNRESET
    assert.equal(link.isConnected(), false);
  } finally {
    link.close();
  }
});

test('EngineLink.requestState rejects cleanly when not connected', async () => {
  const link = new EngineLink(1); // nothing listens on port 1
  await assert.rejects(link.requestState());
  link.close();
});
