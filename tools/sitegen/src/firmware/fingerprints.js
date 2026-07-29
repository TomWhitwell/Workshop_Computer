import crypto from 'node:crypto';
import path from 'node:path';
import { fsAsync as fs, fileExists } from '../utils/fs.js';
import { uf2ToFlashBuffer } from '../../assets/js/uf2.js';

export const FINGERPRINT_ADDRESS = 0x10000000;
export const FINGERPRINT_LENGTH = 16 * 1024;

function fingerprintRegion(parsed) {
  const region = new Uint8Array(FINGERPRINT_LENGTH);
  region.fill(0xff);
  const sourceOffset = Math.max(0, FINGERPRINT_ADDRESS - parsed.address);
  const destinationOffset = Math.max(0, parsed.address - FINGERPRINT_ADDRESS);
  const count = Math.min(parsed.data.length - sourceOffset, FINGERPRINT_LENGTH - destinationOffset);
  if (count > 0) region.set(parsed.data.subarray(sourceOffset, sourceOffset + count), destinationOffset);
  return region;
}

/** Build compact fingerprints for every locally tracked UF2 in the card catalogue. */
export async function buildFirmwareFingerprints(cards, repoRoot) {
  const byHash = new Map();
  const originalLog = console.log;
  // The shared browser converter logs every UF2 range; suppress that build noise.
  console.log = () => {};
  try {
    for (const card of cards) {
      for (const download of card.uf2_downloads || []) {
        if (!download.rel || download.external) continue;
        const file = path.resolve(repoRoot, download.rel);
        if (!(await fileExists(file))) continue;
        try {
          const bytes = new Uint8Array(await fs.readFile(file));
          const parsed = uf2ToFlashBuffer(bytes);
          const hash = crypto.createHash('sha256').update(fingerprintRegion(parsed)).digest('hex');
          const entry = byHash.get(hash) || { hash, cards: [], firmware: [] };
          if (!entry.cards.some(candidate => candidate.slug === card.slug)) {
            entry.cards.push({ id: card.id, slug: card.slug, title: card.title });
          }
          entry.firmware.push({ card: card.id, name: download.name });
          byHash.set(hash, entry);
        } catch (error) {
          console.warn(`[sitegen] Could not fingerprint ${download.rel}: ${error.message}`);
        }
      }
    }
  } finally {
    console.log = originalLog;
  }
  return {
    version: 1,
    algorithm: 'SHA-256',
    address: FINGERPRINT_ADDRESS,
    length: FINGERPRINT_LENGTH,
    entries: [...byHash.values()].sort((a, b) => a.hash.localeCompare(b.hash)),
  };
}
