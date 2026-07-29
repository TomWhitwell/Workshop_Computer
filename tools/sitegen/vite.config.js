import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const SITE_DIR = path.join(ROOT, 'site');
// Watching releases covers every info.yaml (plus README/docs/assets). Curation
// is under src/curation, so edits and sync-curation output are covered by src.
const INPUT_DIRS = [
  path.join(ROOT, 'releases'),
  path.join(__dirname, 'assets'),
  path.join(__dirname, 'src'),
];

function isWithin(file, directory) {
  const relative = path.relative(directory, path.resolve(file));
  return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative));
}

function rebuildSite() {
  let timer;
  let running = false;
  const pending = new Set();
  let server;

  function buildArgs(files) {
    if (files.length !== 1) return ['run', 'build'];
    const relative = path.relative(ROOT, files[0]).replaceAll(path.sep, '/');
    const release = relative.match(/^releases\/([^/]+)\/info\.yaml$/);
    if (release && existsSync(files[0])) {
      return ['--prefix', 'tools/sitegen', 'run', 'build', '--', '--incremental-release', release[1]];
    }
    if (relative === 'tools/sitegen/src/curation/discovery.yml') {
      return ['--prefix', 'tools/sitegen', 'run', 'build', '--', '--incremental-curation', 'discovery'];
    }
    if (relative === 'tools/sitegen/src/curation/flairs.yml') {
      return ['--prefix', 'tools/sitegen', 'run', 'build', '--', '--incremental-curation', 'flairs'];
    }
    return ['run', 'build'];
  }

  function runBuild() {
    if (running) {
      return;
    }

    const files = [...pending];
    pending.clear();
    if (!files.length) return;
    running = true;
    const relative = files.map(file => path.relative(ROOT, file)).join(', ');
    console.log(`\n[sitegen] ${relative} changed; rebuilding…`);
    const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
    const child = spawn(npm, buildArgs(files), { cwd: ROOT, stdio: 'inherit' });

    child.on('error', error => {
      running = false;
      console.error('[sitegen] Could not start rebuild:', error);
    });
    child.on('exit', code => {
      running = false;
      if (code === 0) {
        console.log('[sitegen] Rebuild complete; refreshing browsers.');
        server.ws.send({ type: 'full-reload', path: '*' });
      } else {
        console.error(`[sitegen] Rebuild failed with exit code ${code}.`);
      }
      if (pending.size) runBuild();
    });
  }

  return {
    name: 'workshop-site-rebuild',
    configureServer(viteServer) {
      server = viteServer;
      server.watcher.add(INPUT_DIRS);
      const schedule = file => {
        if (!INPUT_DIRS.some(directory => isWithin(file, directory))) return;
        pending.add(path.resolve(file));
        clearTimeout(timer);
        timer = setTimeout(runBuild, 100);
      };
      server.watcher.on('add', schedule);
      server.watcher.on('change', schedule);
      server.watcher.on('unlink', schedule);
    },
  };
}

export default defineConfig({
  root: SITE_DIR,
  cacheDir: path.join(__dirname, 'node_modules', '.vite'),
  appType: 'mpa',
  plugins: [rebuildSite()],
  resolve: {
    // Static author pages use an import map for these bare specifiers. Vite
    // resolves imports before the browser sees that map, so point development
    // mode at the same generated browser bundles explicitly.
    alias: {
      yaml: path.join(SITE_DIR, 'preview', 'vendor', 'yaml', 'index.js'),
      marked: path.join(SITE_DIR, 'preview', 'vendor', 'marked.esm.js'),
      'sanitize-html': path.join(SITE_DIR, 'preview', 'vendor', 'sanitize-html.esm.js'),
      ajv: path.join(SITE_DIR, 'preview', 'vendor', 'ajv.esm.js'),
    },
  },
  server: {
    host: true,
    port: 5173,
    strictPort: true,
    watch: {
      ignored: [
        '**/.git/**',
        '**/node_modules/**',
        '**/build/**',
        `${SITE_DIR.replaceAll(path.sep, '/')}/**`,
      ],
    },
  },
});