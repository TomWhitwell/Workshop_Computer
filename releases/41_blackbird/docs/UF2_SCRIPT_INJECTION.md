# UF2 Script Injection (for Web-Druid)

This document describes how to create a **single self-contained `.uf2`** that:

1. Flashes the latest released Blackbird firmware (“platform”), and
2. Writes a Lua script into Blackbird’s **reserved user-script flash region**, so it is **auto-loaded on boot**.

Blackbird already supports boot-loading a user script from flash via `FlashStorage`.

## Summary

- Blackbird reserves the **last 16 KB of RP2040 flash** for a user script (matches crow v3+ behavior).
- On boot, Blackbird runs one of:
  - `First.lua` (compiled-in default)
  - the user script stored in flash (if present)
  - nothing (if script was intentionally cleared)

Web-Druid can generate a working UF2 **locally** by:

1. Taking a released “platform UF2” (no user-script erase blocks), and
2. Appending UF2 blocks that program the user-script region with a valid header + script bytes.

A build artifact suitable as a base is produced as:

- `UF2/platform/blackbird_platform.uf2` (raw UF2, does not clear user-script region)

The normal release UF2 remains:

- `UF2/blackbird.<version>.release.uf2`, for example `UF2/blackbird.1.2.release.uf2` (post-processed to *clear* the user-script region)

## Flash Layout

The user script region is the last 16 KB of flash.

- Flash size: 2 MB (0x200000)
- User script region size: 16 KB (0x4000)
- User script offset: `0x200000 - 0x4000 = 0x1FC000`
- XIP base: `0x10000000`
- User script address range (UF2 target addresses):
  - Start: `0x10000000 + 0x1FC000 = 0x101FC000`
  - End:   `0x101FFFFF`

UF2 writes are done in 256-byte payload chunks.

## User Script Data Format (in flash)

The user script region begins with:

- `status_word` (4 bytes, little-endian)
- `script_name` (32 bytes, NUL-terminated string or empty)
- `script_data` (Lua source, raw bytes)

Remaining bytes in the 16 KB region should be left as `0xFF`.

### `status_word` bit layout

`status_word` is a 32-bit little-endian value:

- Bits 0..3: **magic**
  - `0xA` => user script present
  - `0xC` => user script intentionally cleared
  - anything else => default script (`First.lua`)
- Bits 4..15: version word (currently unused for validation)
- Bits 16..31: script length in bytes (0..65535)

So, to mark a user script present:

- `status_word = 0xA | (version<<4) | (length<<16)`

## Web-Druid UF2 Generation Algorithm

Given:

- A base UF2 for Blackbird (recommended: `blackbird_platform.uf2`), and
- The user’s current Lua script as UTF-8 bytes

Steps:

1. Validate script length is <= max:
   - Max = `16 KB - 4 - 32 = 16348` bytes
2. Derive an optional `script_name`:
   - If the first line starts with `---`, treat the remainder (trimmed) as the name.
   - Truncate to 31 characters and NUL-terminate.
3. Build a 16 KB buffer initialized to `0xFF`.
4. Write `status_word` at offset 0.
5. Write `script_name` at offset 4 (32 bytes).
6. Write `script_data` at offset `4 + 32`.
7. Split that 16 KB buffer into 64 chunks of 256 bytes.
8. For each chunk `i` (0..63), create a UF2 block:
   - `targetAddr = 0x101FC000 + (i * 256)`
   - `payloadSize = 256`
   - `familyID = 0xe48bff56` (RP2040)
9. Ensure the output UF2’s `numBlocks` and `blockNo` are consistent:
   - Best practice: renumber all blocks 0..N-1 and set `numBlocks = N`.

### Important: avoid “erase blocks” collisions

Some Blackbird UF2s are post-processed to include blocks that erase/clear the user script region.

For Web-Druid generation you should either:

- Use `blackbird_platform.uf2` (preferred), OR
- If you must use a UF2 that contains clearing blocks, **filter out** any blocks whose
  `targetAddr` is within `0x101FC000..0x101FFFFF` before appending your own user-script blocks.

## UF2 Block Format (minimal reference)

UF2 files are a sequence of **512-byte blocks**.

Each block begins with a fixed header and ends with an end magic.

Constants:

- `UF2_MAGIC_START0 = 0x0A324655`  (ASCII `"UF2\n"`)
- `UF2_MAGIC_START1 = 0x9E5D5157`
- `UF2_MAGIC_END    = 0x0AB16F30`
- RP2040 family ID: `0xe48bff56`

Little-endian 32-bit fields (offsets in bytes from start of the 512-byte block):

- 0:  `magicStart0`
- 4:  `magicStart1`
- 8:  `flags`
  - Bit 13 (`0x2000`) indicates a family ID is present at offset 28.
- 12: `targetAddr` (absolute address in device memory map, e.g. `0x101FC000`)
- 16: `payloadSize` (for RP2040 UF2s typically `256`)
- 20: `blockNo` (0..numBlocks-1)
- 24: `numBlocks` (total blocks in the UF2)
- 28: `familyId` (present if `flags & 0x2000`)
- 32..(32+payloadSize-1): payload bytes
- 512-4: `magicEnd`

Notes:

- Most RP2040 UF2s use `payloadSize=256` and leave the remainder of the 476-byte data area unused.
- For generation, the simplest approach is to emit blocks with `flags=0x2000`, `payloadSize=256`, and include the RP2040 family ID.

## TypeScript-oriented Pseudocode (Copilot-friendly)

This is intentionally written to be easy to paste into a Copilot prompt and implement.

### Data model

```ts
type Uf2Block = {
  magicStart0: number;
  magicStart1: number;
  flags: number;
  targetAddr: number;
  payloadSize: number;
  blockNo: number;
  numBlocks: number;
  familyId: number;
  payload: Uint8Array; // length = payloadSize
};
```

### Parse UF2

```ts
function parseUf2(fileBytes: Uint8Array): Uf2Block[] {
  if (fileBytes.length % 512 !== 0) throw new Error("UF2 length must be a multiple of 512");
  const dv = new DataView(fileBytes.buffer, fileBytes.byteOffset, fileBytes.byteLength);
  const blocks: Uf2Block[] = [];

  for (let off = 0; off < fileBytes.length; off += 512) {
    const magic0 = dv.getUint32(off + 0, true);
    const magic1 = dv.getUint32(off + 4, true);
    const magicEnd = dv.getUint32(off + 512 - 4, true);
    if (magic0 !== 0x0a324655 || magic1 !== 0x9e5d5157 || magicEnd !== 0x0ab16f30) {
      throw new Error(`Bad UF2 magic at block offset ${off}`);
    }

    const flags = dv.getUint32(off + 8, true);
    const targetAddr = dv.getUint32(off + 12, true);
    const payloadSize = dv.getUint32(off + 16, true);
    const blockNo = dv.getUint32(off + 20, true);
    const numBlocks = dv.getUint32(off + 24, true);
    const familyId = dv.getUint32(off + 28, true);

    if (payloadSize > 476) throw new Error(`Invalid payloadSize ${payloadSize}`);
    const payload = fileBytes.slice(off + 32, off + 32 + payloadSize);

    blocks.push({
      magicStart0: magic0,
      magicStart1: magic1,
      flags,
      targetAddr,
      payloadSize,
      blockNo,
      numBlocks,
      familyId,
      payload,
    });
  }
  return blocks;
}
```

### Build user-script flash image (16KB)

```ts
const USER_FLASH_START = 0x101fc000;
const USER_FLASH_SIZE = 16 * 1024;
const USER_BLOCK_SIZE = 256;
const MAX_SCRIPT_LEN = USER_FLASH_SIZE - 4 - 32; // 16348

function deriveScriptName(scriptText: string): string {
  const firstLine = scriptText.split(/\r?\n/, 1)[0] ?? "";
  if (!firstLine.startsWith("---")) return "";
  return firstLine.slice(3).trim();
}

function buildUserFlashImage(scriptUtf8: Uint8Array, name: string, versionWord = 0x040): Uint8Array {
  if (scriptUtf8.length > MAX_SCRIPT_LEN) {
    throw new Error(`Script too large: ${scriptUtf8.length} > ${MAX_SCRIPT_LEN}`);
  }

  const image = new Uint8Array(USER_FLASH_SIZE);
  image.fill(0xff);

  // status_word = 0xA | (version<<4) | (len<<16)
  const statusWord = (0x0a) | ((versionWord & 0x0fff) << 4) | ((scriptUtf8.length & 0xffff) << 16);
  const dv = new DataView(image.buffer);
  dv.setUint32(0, statusWord >>> 0, true);

  // name: 32 bytes, NUL-terminated
  const nameBytes = new TextEncoder().encode(name);
  const n = Math.min(nameBytes.length, 31);
  image.set(nameBytes.slice(0, n), 4);
  image[4 + n] = 0x00;

  // script bytes at offset 4+32
  image.set(scriptUtf8, 4 + 32);
  return image;
}
```

### Create UF2 blocks for the script image

```ts
function createUf2Block(targetAddr: number, payload: Uint8Array): Uint8Array {
  if (payload.length !== 256) throw new Error("payload must be 256 bytes");
  const block = new Uint8Array(512);
  const dv = new DataView(block.buffer);
  dv.setUint32(0, 0x0a324655, true);
  dv.setUint32(4, 0x9e5d5157, true);
  dv.setUint32(8, 0x2000, true); // familyId present
  dv.setUint32(12, targetAddr >>> 0, true);
  dv.setUint32(16, 256, true);
  // blockNo/numBlocks filled later
  dv.setUint32(28, 0xe48bff56, true); // RP2040 family
  block.set(payload, 32);
  dv.setUint32(512 - 4, 0x0ab16f30, true);
  return block;
}

function makeUserScriptUf2Blocks(userFlashImage16k: Uint8Array): Uint8Array[] {
  if (userFlashImage16k.length !== USER_FLASH_SIZE) throw new Error("expected 16KB image");
  const blocks: Uint8Array[] = [];
  for (let i = 0; i < USER_FLASH_SIZE / USER_BLOCK_SIZE; i++) {
    const payload = userFlashImage16k.slice(i * 256, i * 256 + 256);
    blocks.push(createUf2Block(USER_FLASH_START + i * 256, payload));
  }
  return blocks;
}
```

### Merge base UF2 + injected script blocks (and renumber)

```ts
function inUserRange(addr: number): boolean {
  return addr >= 0x101fc000 && addr <= 0x101fffff;
}

function filterOutUserRegion(blocks: Uf2Block[]): Uf2Block[] {
  return blocks.filter(b => !inUserRange(b.targetAddr));
}

function serializeAndRenumber(allBlocks512: Uint8Array[]): Uint8Array {
  const total = allBlocks512.length;
  const out = new Uint8Array(total * 512);

  for (let i = 0; i < total; i++) {
    const block = allBlocks512[i];
    const dv = new DataView(block.buffer, block.byteOffset, block.byteLength);
    dv.setUint32(20, i, true);      // blockNo
    dv.setUint32(24, total, true);  // numBlocks
    out.set(block, i * 512);
  }
  return out;
}

function injectScriptIntoBaseUf2(baseUf2Bytes: Uint8Array, scriptText: string): Uint8Array {
  const scriptUtf8 = new TextEncoder().encode(scriptText);
  const name = deriveScriptName(scriptText);
  const image16k = buildUserFlashImage(scriptUtf8, name);
  const scriptBlocks512 = makeUserScriptUf2Blocks(image16k);

  const baseBlocks = filterOutUserRegion(parseUf2(baseUf2Bytes));

  // Re-serialize base blocks as 512-byte chunks, preserving their original bytes
  // Easiest is to keep the original 512-byte slices from baseUf2Bytes rather than reconstructing.
  const baseBlocks512: Uint8Array[] = [];
  for (let i = 0; i < baseUf2Bytes.length; i += 512) {
    const chunk = baseUf2Bytes.slice(i, i + 512);
    const dv = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength);
    const targetAddr = dv.getUint32(12, true);
    if (!inUserRange(targetAddr)) baseBlocks512.push(chunk);
  }

  return serializeAndRenumber([...baseBlocks512, ...scriptBlocks512]);
}
```

Implementation notes:

- The merge code above filters by `targetAddr` using the raw 512-byte chunks, which is simpler than reconstructing base blocks.
- If you want extra safety, validate that all base blocks have valid magic numbers before using them.

## Validation Checklist (recommended)

Before offering the UF2 for download, validate:

- File length is a multiple of 512.
- Every block has correct UF2 magics.
- After filtering, **no** base blocks remain with `targetAddr` in `0x101FC000..0x101FFFFF`.
- The output contains **exactly 64** blocks whose `targetAddr` spans:
  - `0x101FC000, 0x101FC100, ... , 0x101FFF00`
- Every injected block uses:
  - `payloadSize=256`
  - `flags & 0x2000` set
  - `familyId = 0xe48bff56`
- `blockNo` is contiguous `0..numBlocks-1` and `numBlocks` matches total.

If you want a human-visible smoke test, the injected script name (from `--- ...`) should print at boot (Blackbird prints the loaded script name if present).

## What Blackbird firmware must do

Nothing new is required.

On boot Blackbird already:

- checks the magic nibble in `status_word`
- loads and executes the script directly from flash (XIP)
- calls `init()` if present

This is implemented in `FlashStorage` and `BlackbirdCrow::load_boot_script()`.
