// `npm run dev:card -- <card>`: build one card's detail page, serve it with
// Vite and rebuild it whenever a file in that card's release folder changes.
// Much faster than `npm run dev` because nothing else is built or watched.

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { slugify } from './utils/strings.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../../..');
const RELEASES_DIR = path.join(ROOT, 'releases');
// npm runs scripts from the package directory; INIT_CWD is where it was invoked.
const INVOKED_FROM = process.env.INIT_CWD || process.cwd();

const USAGE = `Usage: npm run dev:card -- <card>

  <card> names a folder under releases/, for example
    41_blackbird, releases/41_blackbird or just 41.
  Or run \`npm run dev:card\` from inside the card's folder.`;

/** The release folder containing `abs`, or '' when it is outside releases/. */
function releaseFolderOf(abs) {
  const relative = path.relative(RELEASES_DIR, abs);
  if (!relative || relative.startsWith('..') || path.isAbsolute(relative)) return '';
  return relative.split(path.sep)[0];
}

/** Resolve a card argument (folder, path, slug or number) to a release folder name. */
function resolveCard(arg, folders) {
  if (!arg) {
    const folder = releaseFolderOf(INVOKED_FROM);
    if (folder) return folder;
    throw new Error(`Which card? ${USAGE}`);
  }
  // A path, relative to where npm was run or to the repo root.
  for (const base of [INVOKED_FROM, ROOT]) {
    const folder = releaseFolderOf(path.resolve(base, arg));
    if (folders.includes(folder)) return folder;
  }
  const name = arg.replace(/[\\/]+$/, '');
  const matches = [
    folder => folder === name,
    folder => folder.toLowerCase() === name.toLowerCase() || slugify(folder) === slugify(name),
    // A bare card number: "41" or "041" finds 41_blackbird.
    folder => /^\d+$/.test(name) && Number.parseInt(folder, 10) === Number(name),
  ];
  for (const match of matches) {
    const found = folders.filter(match);
    if (found.length === 1) return found[0];
    if (found.length > 1) throw new Error(`"${arg}" matches several cards: ${found.join(', ')}. Use the full folder name.`);
  }
  throw new Error(`No card folder matches "${arg}".\n\n${USAGE}`);
}

const folders = fs.readdirSync(RELEASES_DIR, { withFileTypes: true })
  .filter(entry => entry.isDirectory())
  .map(entry => entry.name)
  .sort();

let folder;
try {
  folder = resolveCard(process.argv[2], folders);
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
const cardPath = `/programs/${encodeURIComponent(slugify(folder))}/`;

// vite.config.js reads this to build and watch only this card.
process.env.SITEGEN_CARD = folder;
const initial = spawnSync(process.execPath, [path.join(__dirname, 'build.js'), '--preview', '--card', folder], {
  cwd: ROOT,
  stdio: 'inherit',
});
if (initial.status !== 0) {
  console.error('\n[sitegen] The first build failed. Fix the error above and save to rebuild.');
}

const { createServer } = await import('vite');
const server = await createServer({
  configFile: path.join(__dirname, '..', 'vite.config.js'),
  server: { open: cardPath },
});
await server.listen();
server.printUrls();
const origin = server.resolvedUrls?.local?.[0]?.replace(/\/$/, '') || 'http://localhost:5173';
console.log(`\n  Card page: ${origin}${cardPath}`);
console.log(`  Watching releases/${folder}/; save any file there to rebuild.\n`);
