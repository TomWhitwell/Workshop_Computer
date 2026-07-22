// Public surface for the shared info.yaml validator.
//
// Import this from any consumer — the site build, the author preview, or future
// PR / commit-time CI hooks — to validate source YAML without pulling in the
// site renderer.

export { parseSource } from './parseSource.js';
export { parseSourceFile } from './readSource.js';
export { validateInfoYaml } from './validateInfoYaml.js';
export { allRules } from './rules/index.js';
export { reporters, reportText, reportJson, reportGithub } from './reporters/index.js';

import { parseSourceFile } from './readSource.js';
import { validateInfoYaml } from './validateInfoYaml.js';

/** Convenience: read + parse + validate a single info.yaml file on disk. */
export async function validateFile(file, opts = {}) {
  const source = await parseSourceFile(file);
  return validateInfoYaml(source, opts);
}
