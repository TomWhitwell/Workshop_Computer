// Node-only: index the tag/Language/Status values of every card on disk, for
// the `similar-values` rule. Unparseable files are skipped; the validator
// reports those separately.

import fs from 'node:fs';
import path from 'node:path';
import YAML from 'yaml';
import { collectKnownValues } from '../utils/similarValues.js';

/** Read every `<releasesDir>/<card>/info.yaml` as `[{ id, file, source, data }]`. */
export function readReleaseInfos(releasesDir) {
  let folders = [];
  try {
    folders = fs.readdirSync(releasesDir, { withFileTypes: true }).filter(d => d.isDirectory()).map(d => d.name).sort();
  } catch {
    return [];
  }
  const out = [];
  for (const id of folders) {
    const file = path.join(releasesDir, id, 'info.yaml');
    if (!fs.existsSync(file)) continue;
    const source = fs.readFileSync(file, 'utf8');
    let data = null;
    try {
      data = YAML.parse(source);
    } catch {
      continue;
    }
    out.push({ id, file, source, data });
  }
  return out;
}

export function loadKnownValues(releasesDir) {
  return collectKnownValues(readReleaseInfos(releasesDir));
}
