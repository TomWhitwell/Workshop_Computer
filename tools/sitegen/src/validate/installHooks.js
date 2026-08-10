import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const current = spawnSync('git', ['config', '--local', '--get', 'core.hooksPath'], {
  cwd: root, encoding: 'utf8',
});
const previous = current.status === 0 ? current.stdout.trim() : '';
const update = spawnSync('git', ['config', '--local', 'core.hooksPath', '.githooks'], {
  cwd: root, encoding: 'utf8',
});
if (update.status !== 0) {
  console.error(update.stderr.trim() || 'Could not configure Git hooks.');
  process.exit(1);
}
if (previous && previous !== '.githooks') {
  console.log(`Replaced clone-local core.hooksPath ${JSON.stringify(previous)} with ".githooks".`);
}
console.log('Installed Workshop Computer Git hooks for this clone.');
console.log('Run `npm run validate-staged` to invoke staged card validation manually.');
