// A card's metadata file may be named info.yaml (preferred) or info.yml. Every
// lookup goes through here so the two spellings are treated alike.

import fs from 'node:fs';
import path from 'node:path';

/** Accepted names, in preference order: info.yaml wins when both exist. */
export const INFO_FILE_NAMES = ['info.yaml', 'info.yml'];

/** Regex source matching either name, for building path patterns. */
export const INFO_FILE_PATTERN = 'info\\.ya?ml';

const infoFileName = new RegExp(`^${INFO_FILE_PATTERN}$`);

export function isInfoFileName(name) {
  return infoFileName.test(String(name || ''));
}

function isFile(file) {
  try {
    return fs.statSync(file).isFile();
  } catch {
    return false;
  }
}

/** Every info file present in `dir`, in preference order. */
export function listInfoFilesSync(dir) {
  return INFO_FILE_NAMES.map(name => path.join(dir, name)).filter(isFile);
}

/** The info file in `dir` (info.yaml before info.yml), or null. */
export function findInfoFileSync(dir) {
  return listInfoFilesSync(dir)[0] || null;
}

export async function findInfoFile(dir) {
  for (const name of INFO_FILE_NAMES) {
    const file = path.join(dir, name);
    const stat = await fs.promises.stat(file).catch(() => null);
    if (stat?.isFile()) return file;
  }
  return null;
}
