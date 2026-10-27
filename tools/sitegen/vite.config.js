import { execFileSync, spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '../..');
const SITE_DIR = path.join(ROOT, 'site');
// Watching releases covers every info.yaml (plus README/docs/assets). Curation
// is under src/curation, so edits and sync-curation output are covered by src.
// `npm run dev:card` sets SITEGEN_CARD to a release folder name. The server
// then builds and watches only that card (see src/devCard.js).
const CARD = process.env.SITEGEN_CARD || '';
const CARD_DIR = CARD ? path.join(ROOT, 'releases', CARD) : '';
const INPUT_DIRS = [
  CARD_DIR || path.join(ROOT, 'releases'),
  path.join(__dirname, 'assets'),
  path.join(__dirname, 'src'),
];
// Card folders hold CMake output (build/, build-debug-*/…) that changes
// constantly while firmware compiles and never affects the site.
const CARD_BUILD_OUTPUT = /[\\/]releases[\\/][^\\/]+[\\/]build(?:-[^\\/]*)?[\\/]/;

/** Drop files Git ignores (editor swap files, local build products, …). */
function withoutIgnored(files) {
  try {
    const ignored = execFileSync('git', ['check-ignore', '--stdin'], {
      cwd: ROOT, input: files.join('\n'), encoding: 'utf8', stdio: ['pipe', 'pipe', 'ignore'],
    });
    const skip = new Set(ignored.split('\n').filter(Boolean).map(file => path.resolve(ROOT, file)));
    return files.filter(file => !skip.has(file));
  } catch {
    // Exit status 1 means nothing was ignored; any other failure keeps everything.
    return files;
  }
}

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
    if (CARD) return [path.join(__dirname, 'src', 'build.js'), '--preview', '--card', CARD];
    const preview = ['--prefix', 'tools/sitegen', 'run', 'build', '--', '--preview'];
    if (files.length !== 1) return preview;
    const relative = path.relative(ROOT, files[0]).replaceAll(path.sep, '/');
    const release = relative.match(/^releases\/([^/]+)\/info\.yaml$/);
    if (release && existsSync(files[0])) {
      return [...preview, '--incremental-release', release[1]];
    }
    if (relative === 'tools/sitegen/src/curation/discovery.yml') {
      return [...preview, '--incremental-curation', 'discovery'];
    }
    if (relative === 'tools/sitegen/src/curation/flairs.yml') {
      return [...preview, '--incremental-curation', 'flairs'];
    }
    return preview;
  }

  function runBuild() {
    if (running) {
      return;
    }

    const files = CARD ? withoutIgnored([...pending]) : [...pending];
    pending.clear();
    if (!files.length) return;
    running = true;
    const relative = files.map(file => path.relative(ROOT, file)).join(', ');
    console.log(`\n[sitegen] ${relative} changed; rebuilding…`);
    // Card builds skip npm (and its curation prebuild check) to stay fast.
    const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';
    const child = spawn(CARD ? process.execPath : npm, buildArgs(files), { cwd: ROOT, stdio: 'inherit' });

    child.on('error', error => {
      running = false;
      console.error('[sitegen] Could not start rebuild:', error);
    });
    child.on('exit', code => {
      running = false;
      if (code === 0) {
        // Vite ignores SITE_DIR (see server.watch.ignored) to avoid looping on
        // its own build output, so it never invalidates its cached transform of
        // the regenerated files. Clear the module graph before reloading so the
        // browser gets the freshly built HTML/CSS/JS instead of stale copies.
        server.moduleGraph.invalidateAll();
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
        if (CARD && CARD_BUILD_OUTPUT.test(path.resolve(file))) return;
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
        ...(CARD ? [CARD_BUILD_OUTPUT] : []),
      ],
    },
  },
});