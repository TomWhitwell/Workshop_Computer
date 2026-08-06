// Node-only filesystem loader for info.yaml sources.
//
// Kept separate from `parseSource.js` so the parser/validator stays browser-safe
// (the client-side author preview reads source from fetch(), not fs).

import { fsAsync as fs } from '../utils/fs.js';
import { parseSource } from './parseSource.js';

/** Read and parse an info.yaml file from disk. */
export async function parseSourceFile(file) {
  let raw;
  try {
    raw = await fs.readFile(file, 'utf8');
  } catch (err) {
    return {
      file, raw: '', data: null, keyLines: {},
      error: { severity: 'error', ruleId: 'source-read', file, path: '', message: `Cannot read file: ${err.message}` },
    };
  }
  return parseSource(raw, file);
}
