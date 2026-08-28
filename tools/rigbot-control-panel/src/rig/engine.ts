// The live link to a running engine's remote-control port: commands out,
// state polls back in. One socket, line-framed both ways (protocol.ts).
import { EventEmitter } from 'node:events';
import net from 'node:net';

import { EngineState, parseState } from './protocol';

export interface EngineLinkOptions {
  pollMs?: number;
}

// Events: 'state' (EngineState) on every poll, 'down' when the engine goes away.
export class EngineLink extends EventEmitter {
  private readonly port: number;
  private readonly pollMs: number;
  private socket: net.Socket | null = null;
  private pending = '';
  // Replies arrive in request order on one socket; resolvers queue in kind.
  private waiters: { resolve: (state: EngineState) => void; reject: (err: Error) => void }[] = [];
  private pollTimer: NodeJS.Timeout | null = null;
  private closed = false;

  constructor(port: number, options: EngineLinkOptions = {}) {
    super();
    this.port = port;
    this.pollMs = options.pollMs ?? 250;
  }

  connect(): Promise<void> {
    const { promise, resolve, reject } = Promise.withResolvers<void>();
    const socket = net.connect({ port: this.port, host: '127.0.0.1' });
    socket.setNoDelay(true);
    socket.once('connect', () => {
      this.socket = socket;
      socket.on('data', (chunk) => this.onData(chunk));
      socket.on('close', () => this.onDown());
      socket.on('error', () => {}); // 'close' follows; one path handles it
      this.pollTimer = setInterval(() => this.poll(), this.pollMs);
      resolve();
    });
    socket.once('error', (err) => reject(err));
    return promise;
  }

  send(line: string): void {
    if (!this.socket) throw new Error('engine link is not connected');
    this.socket.write(line + '\n');
  }

  requestState(): Promise<EngineState> {
    if (!this.socket) return Promise.reject(new Error('engine link is not connected'));
    const { promise, resolve, reject } = Promise.withResolvers<EngineState>();
    this.waiters.push({ resolve, reject });
    this.socket.write('state\n');
    return promise;
  }

  isConnected(): boolean {
    return this.socket !== null;
  }

  close(): void {
    this.closed = true;
    this.teardown();
  }

  private onData(chunk: Buffer): void {
    this.pending += chunk.toString('utf8');
    let index;
    while ((index = this.pending.indexOf('\n')) >= 0) {
      const line = this.pending.slice(0, index);
      this.pending = this.pending.slice(index + 1);
      if (!line) continue;
      const state = parseState(line);
      const waiter = this.waiters.shift();
      if (!state) continue; // "{}" before the first publish, or garbage
      if (waiter) waiter.resolve(state);
      this.emit('state', state);
    }
  }

  private poll(): void {
    // Fire-and-forget: the reply lands in onData and goes out as a 'state'
    // event; a poll that never returns is swept up when the socket closes.
    this.requestState().catch(() => {});
  }

  private onDown(): void {
    const wasConnected = this.socket !== null;
    this.teardown();
    if (wasConnected && !this.closed) this.emit('down');
  }

  private teardown(): void {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    const socket = this.socket;
    this.socket = null;
    if (socket) socket.destroy();
    const waiters = this.waiters;
    this.waiters = [];
    for (const waiter of waiters) waiter.reject(new Error('engine link closed'));
    this.pending = '';
  }
}
