// Launches and stops the engine for a scheduled match. The engine is started
// the way every verification harness starts it: gamescope's headless backend,
// a generated config, cwd = a run tree holding media/, databases/ and the
// binary. Scheduling is process management here; the engine itself needs no
// scheduling code.
import { spawn, ChildProcess } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import { engineConfig, EngineBootConfig } from './protocol';

// A prepared run tree: gameplayfootball, media/, databases/, locale/.
export const engineRunDir = path.resolve(
  process.env.ENGINE_RUN_DIR ?? path.join(__dirname, '../../../../data'),
);
export const engineDbPath =
  process.env.ENGINE_DB ?? path.join(engineRunDir, 'databases/default/database.sqlite');
export const controlPort = Number(process.env.ENGINE_CONTROL_PORT ?? 44700);

export class EngineLauncher {
  // ponytail: one match at a time; a per-match registry if the rig ever runs two.
  private child: ChildProcess | null = null;

  isRunning(): boolean {
    return this.child !== null && this.child.exitCode === null;
  }

  start(boot: EngineBootConfig, log: (line: string) => void): void {
    if (this.isRunning()) throw new Error('a match is already running');

    const configPath = path.join(engineRunDir, 'rigbot_remote.config');
    fs.writeFileSync(configPath, engineConfig(boot));

    const child = spawn(
      'gamescope',
      ['--backend', 'headless', '-W', '1280', '-H', '720', '--',
       './gameplayfootball', path.basename(configPath)],
      { cwd: engineRunDir, stdio: ['ignore', 'pipe', 'pipe'] },
    );
    child.stdout?.on('data', (chunk: Buffer) => log(chunk.toString('utf8').trimEnd()));
    child.stderr?.on('data', (chunk: Buffer) => log(chunk.toString('utf8').trimEnd()));
    child.on('exit', (code) => {
      log(`engine exited with code ${code}`);
      this.child = null;
    });
    this.child = child;
  }

  stop(): void {
    if (!this.child) return;
    // gamescope forwards termination to its client; SIGKILL after a grace period.
    const child = this.child;
    child.kill('SIGTERM');
    const hardKill = setTimeout(() => {
      if (child.exitCode === null) child.kill('SIGKILL');
    }, 5000);
    child.once('exit', () => clearTimeout(hardKill));
  }
}
