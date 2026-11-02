// Node-only: validate one info.yaml on disk, including its card's custom panel
// manifest. Shared by `npm run validate-info` and the single-card dev build.

import path from 'node:path';
import { parseSourceFile } from './readSource.js';
import { validateInfoYaml } from './validateInfoYaml.js';
import { readCustomPanelManifest } from '../discover/customPanels.js';

/** Validate `file`, reporting its path relative to `root`. */
export async function validateInfoFile(file, { root, knownValues }) {
  const source = await parseSourceFile(file);
  // Report paths relative to repo root for stable, readable output.
  source.file = path.relative(root, file) || file;
  const customPanels = await readCustomPanelManifest(path.dirname(file));
  return validateInfoYaml(source, {
    customPanelsPresent: customPanels.present,
    panelIds: customPanels.items.map(item => item.id),
    knownValues,
    externalDiagnostics: customPanels.diagnostics.map(diagnostic => ({
      ...diagnostic,
      ruleId: 'custom-panel-manifest',
      key: 'panels',
    })),
  });
}
