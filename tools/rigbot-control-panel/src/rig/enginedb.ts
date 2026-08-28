// Read-only look into the engine's own SQLite database and asset tree, for the
// schedule form: which teams exist, which stadiums are installed.
import fs from 'node:fs';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';

export interface EngineTeamRow {
  id: number;
  shortname: string;
  name: string;
}

export function listTeams(dbPath: string): EngineTeamRow[] {
  const db = new DatabaseSync(dbPath, { readOnly: true });
  try {
    const rows = db.prepare('SELECT id, shortname, name FROM teams ORDER BY name').all();
    return rows.map((row) => ({
      id: Number(row.id),
      shortname: String(row.shortname ?? ''),
      name: String(row.name ?? ''),
    }));
  } finally {
    db.close();
  }
}

// Every installed stadium: media/objects/stadiums/<dir>/<file>.object,
// as a path relative to the run tree - the form the config key wants.
export function listStadiums(runDir: string): string[] {
  const stadiumsDir = path.join(runDir, 'media/objects/stadiums');
  let entries: fs.Dirent[];
  try {
    entries = fs.readdirSync(stadiumsDir, { withFileTypes: true });
  } catch {
    return [];
  }
  const stadiums: string[] = [];
  for (const entry of entries) {
    if (!entry.isDirectory() && !entry.isSymbolicLink()) continue;
    const dir = path.join(stadiumsDir, entry.name);
    let files: string[];
    try {
      files = fs.readdirSync(dir);
    } catch {
      continue;
    }
    for (const file of files) {
      if (file.endsWith('.object'))
        stadiums.push(path.join('media/objects/stadiums', entry.name, file));
    }
  }
  return stadiums.sort();
}
