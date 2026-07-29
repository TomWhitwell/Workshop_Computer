import { promises as fs } from 'node:fs';
import { createReadStream } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';

export async function ensureDir(dir) {
  await fs.mkdir(dir, { recursive: true });
}

export async function writeFileEnsured(filePath, content) {
  await ensureDir(path.dirname(filePath));
  await fs.writeFile(filePath, content);
}

export async function listSubdirs(dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  return entries.filter(e => e.isDirectory()).map(e => e.name);
}

export async function fileExists(file) {
  try {
    await fs.access(file);
    return true;
  } catch {
    return false;
  }
}

export function toPosix(p) {
  return p.replace(/\\/g, '/');
}

export function encodePathSegments(p) {
  return toPosix(p).split('/').map(encodeURIComponent).join('/');
}

// Stream a file through sha256; resolves to a lowercase hex digest, or '' when
// the file can't be read. Used to record firmware hashes at build time.
export function sha256File(absPath) {
  return new Promise((resolve) => {
    const hash = createHash('sha256');
    const stream = createReadStream(absPath);
    stream.on('error', () => resolve(''));
    stream.on('data', (chunk) => hash.update(chunk));
    stream.on('end', () => resolve(hash.digest('hex')));
  });
}

export const fsAsync = fs;
