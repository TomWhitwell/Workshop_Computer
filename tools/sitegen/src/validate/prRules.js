// Pull-request hygiene checks for card submissions. These rules deliberately
// live outside the info.yaml schema validator because they inspect the complete
// Git diff and release-directory filesystem.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { isDeepStrictEqual } from 'node:util';
import YAML from 'yaml';
import { discoverCustomPanels, validateCustomPanelReferences } from '../discover/customPanels.js';

const posix = value => String(value || '').replaceAll('\\', '/').replace(/^\.\//, '');
const isUf2 = file => /\.uf2$/i.test(file);

function releaseFromPath(file) {
  const parts = posix(file).split('/').filter(Boolean);
  return parts[0] === 'releases' && parts.length >= 3 ? parts[1] : null;
}

function diagnostic(severity, ruleId, file, message) {
  return { severity, ruleId, file: posix(file), path: '', message };
}

export function summarizePrTrigger(changes) {
  const affectedReleases = new Set();
  const changedPaths = [];
  const directoryChanges = new Map();
  const addChangedPath = (status, file) => {
    const normalized = posix(file);
    const release = releaseFromPath(normalized);
    if (!release || normalized === `releases/${release}/info.yaml`) {
      changedPaths.push({ status, path: normalized });
      return;
    }
    const directory = `${path.posix.dirname(normalized)}/`;
    const normalizedStatus = status[0];
    directoryChanges.set(`${normalizedStatus}\0${directory}`, {
      status: normalizedStatus, path: directory, filesUnder: true,
    });
  };
  for (const change of changes) {
    const paths = change.oldPath ? [change.oldPath, change.path] : [change.path];
    for (const file of paths) {
      const release = releaseFromPath(file);
      if (release) affectedReleases.add(release);
    }
    if (change.oldPath) {
      addChangedPath('D', change.oldPath);
      addChangedPath('A', change.path);
    } else {
      addChangedPath(change.status, change.path);
    }
  }
  changedPaths.push(...directoryChanges.values());
  return { affectedReleases: [...affectedReleases].sort(), changedPaths };
}

function allowsSynchronizedFlairs(changes, currentFlairs, baseFlairs) {
  if (!currentFlairs || !baseFlairs) return false;
  const addedCards = new Set();
  const deletedCards = new Set();
  const cardFromInfoPath = file => posix(file).match(/^releases\/([^/]+)\/info\.yaml$/)?.[1];
  for (const change of changes) {
    if (change.oldPath) {
      const deleted = cardFromInfoPath(change.oldPath);
      const added = cardFromInfoPath(change.path);
      if (deleted) deletedCards.add(deleted);
      if (added) addedCards.add(added);
      continue;
    }
    const card = cardFromInfoPath(change.path);
    if (!card) continue;
    if (change.status.startsWith('A')) addedCards.add(card);
    if (change.status.startsWith('D')) deletedCards.add(card);
  }
  const currentAssignments = currentFlairs.assignments;
  const baseAssignments = baseFlairs.assignments;
  if (!currentAssignments || typeof currentAssignments !== 'object' || Array.isArray(currentAssignments)
      || !baseAssignments || typeof baseAssignments !== 'object' || Array.isArray(baseAssignments)) return false;
  const withoutAssignments = value => Object.fromEntries(Object.entries(value).filter(([key]) => key !== 'assignments'));
  if (!isDeepStrictEqual(withoutAssignments(currentFlairs), withoutAssignments(baseFlairs))) return false;
  let synchronizedChanges = 0;
  for (const card of new Set([...Object.keys(baseAssignments), ...Object.keys(currentAssignments)])) {
    const inBase = Object.hasOwn(baseAssignments, card);
    const inCurrent = Object.hasOwn(currentAssignments, card);
    if (inBase && inCurrent) {
      if (!isDeepStrictEqual(currentAssignments[card], baseAssignments[card])) return false;
    } else if (inCurrent) {
      if (!addedCards.has(card) || !Array.isArray(currentAssignments[card]) || currentAssignments[card].length !== 0) return false;
      synchronizedChanges += 1;
    } else {
      if (!deletedCards.has(card) || !Array.isArray(baseAssignments[card]) || baseAssignments[card].length !== 0) return false;
      synchronizedChanges += 1;
    }
  }
  return synchronizedChanges > 0;
}

function walkFiles(dir) {
  const files = [];
  if (!fs.existsSync(dir)) return files;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) files.push(...walkFiles(full));
    else if (entry.isFile()) files.push(full);
  }
  return files;
}

function hashFile(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function stripCmakeComments(source) {
  return String(source)
    .replace(/#\[(=*)\[[\s\S]*?\]\1\]/g, '')
    .split(/\r?\n/)
    .map(line => {
      let quoted = false;
      let escaped = false;
      for (let i = 0; i < line.length; i += 1) {
        const char = line[i];
        if (escaped) { escaped = false; continue; }
        if (char === '\\') { escaped = true; continue; }
        if (char === '"') quoted = !quoted;
        if (char === '#' && !quoted) return line.slice(0, i);
      }
      return line;
    })
    .join('\n');
}

function functionBodies(source, functionName) {
  const bodies = [];
  const pattern = new RegExp(`\\b${functionName}\\s*\\(`, 'ig');
  let match;
  while ((match = pattern.exec(source))) {
    let depth = 1;
    let quoted = false;
    let escaped = false;
    let index = pattern.lastIndex;
    for (; index < source.length && depth; index += 1) {
      const char = source[index];
      if (escaped) { escaped = false; continue; }
      if (char === '\\') { escaped = true; continue; }
      if (char === '"') { quoted = !quoted; continue; }
      if (quoted) continue;
      if (char === '(') depth += 1;
      else if (char === ')') depth -= 1;
    }
    if (!depth) bodies.push(source.slice(pattern.lastIndex, index - 1));
    pattern.lastIndex = index;
  }
  return bodies;
}

export function cmakeUsesPicoSdk(source) {
  return functionBodies(stripCmakeComments(source), 'pico_sdk_init').length > 0;
}

export function cmakeHasXosc64(source) {
  const clean = stripCmakeComments(source);
  return functionBodies(clean, 'target_compile_definitions').some(body =>
    /\bPICO_XOSC_STARTUP_DELAY_MULTIPLIER\s*=\s*64\b/.test(body)
  );
}

function cmakeBoardName(source) {
  const clean = stripCmakeComments(source);
  const match = clean.match(/\bset\s*\(\s*PICO_BOARD\s+(?:"([^"]+)"|([^\s)]+))/i);
  return match?.[1] || match?.[2] || '';
}

function headerHasXosc64(source) {
  const clean = String(source)
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/.*$/gm, '');
  return /^\s*#\s*define\s+PICO_XOSC_STARTUP_DELAY_MULTIPLIER\s+64\b/m.test(clean);
}

function customBoardHasXosc64(cmakeSource, releaseFiles) {
  const board = cmakeBoardName(cmakeSource);
  if (!board || board.toLowerCase() === 'pico') return false;
  const wanted = `${board}.h`.toLowerCase();
  return releaseFiles.some(file =>
    path.basename(file).toLowerCase() === wanted
    && headerHasXosc64(fs.readFileSync(file, 'utf8'))
  );
}

/**
 * Evaluate PR policy over parsed Git changes.
 * Change shape: { status, oldPath?, path }. `path` is the final/destination path.
 */
export async function evaluatePrRules(changes, { root, baseFlairs = null }) {
  const diagnostics = [];
  const endpoints = [];
  for (const change of changes) {
    if (change.oldPath) endpoints.push(change.oldPath);
    endpoints.push(change.path);
  }

  const releases = [...new Set(endpoints.map(releaseFromPath).filter(Boolean))].sort();
  if (releases.length > 1) {
    diagnostics.push(diagnostic('warning', 'multiple-release-directories', 'releases',
      `Changes affect ${releases.length} release directories: ${releases.join(', ')}.`));
  }

  const flairsPath = 'tools/sitegen/src/curation/flairs.yml';
  const flairsChanged = endpoints.map(posix).includes(flairsPath);
  let synchronizedFlairs = false;
  if (flairsChanged) {
    try {
      const currentFlairs = YAML.parse(fs.readFileSync(path.join(root, flairsPath), 'utf8')) || {};
      synchronizedFlairs = allowsSynchronizedFlairs(changes, currentFlairs, baseFlairs);
    } catch {}
  }
  for (const file of [...new Set(endpoints.map(posix))].sort()) {
    const parts = file.split('/').filter(Boolean);
    if (parts[0] !== 'releases') {
      if (file === flairsPath && synchronizedFlairs) continue;
      diagnostics.push(diagnostic('warning', 'change-outside-release-directory', file,
        'Card submissions must not include changes outside releases/<card>/ directories.'));
    } else if (parts.length < 3) {
      diagnostics.push(diagnostic('warning', 'change-at-releases-root', file,
        'Card submissions must not modify files directly under releases/.'));
    }
  }

  const includedUf2ByRelease = new Map();
  for (const change of changes) {
    const release = releaseFromPath(change.path);
    if (!release || change.status.startsWith('D') || !isUf2(change.path)) continue;
    const list = includedUf2ByRelease.get(release) || [];
    list.push(posix(change.path));
    includedUf2ByRelease.set(release, list);
  }

  for (const release of releases) {
    const releaseDir = path.join(root, 'releases', release);
    if (!fs.existsSync(releaseDir)) {
      diagnostics.push(diagnostic('warning', 'release-directory-deleted', `releases/${release}`,
        `Release directory ${release} is deleted in the proposed changes.`));
      continue;
    }
    const releaseFiles = walkFiles(releaseDir);
    const infoPath = path.join(releaseDir, 'info.yaml');
    let rawInfo = {};
    try {
      rawInfo = YAML.parse(fs.readFileSync(infoPath, 'utf8')) || {};
    } catch {}
    if (rawInfo.draft === true) {
      diagnostics.push(diagnostic('warning', 'draft-card-changed', `releases/${release}/info.yaml`,
        `Release ${release} is marked draft: true.`));
    }
    const readme = path.join(releaseDir, 'README.md');
    if (!fs.existsSync(readme) || !fs.statSync(readme).isFile()) {
      diagnostics.push(diagnostic('warning', 'release-readme-recommended', `releases/${release}`,
        `Release ${release} has no release-local README.md.`));
    }

    const panelsDir = path.join(releaseDir, 'panels');
    if (fs.existsSync(panelsDir)) {
      const panels = await discoverCustomPanels(releaseDir, '', { copyAssets: false });
      const panelDiagnostics = [
        ...panels.diagnostics,
        ...validateCustomPanelReferences(rawInfo, panels.panels),
      ];
      for (const item of panelDiagnostics) {
        diagnostics.push(diagnostic(
          item.severity === 'error' ? 'error' : 'warning',
          'custom-panels',
          `releases/${release}/${item.path || 'panels'}`,
          item.message,
        ));
      }
    }
    const included = includedUf2ByRelease.get(release) || [];
    const allUf2 = releaseFiles.filter(isUf2);
    if (!allUf2.length) {
      diagnostics.push(diagnostic('warning', 'uf2-required', `releases/${release}`,
        `No UF2 firmware file exists anywhere under releases/${release}/.`));
    } else {
      const groups = new Map();
      for (const file of allUf2) {
        const hash = hashFile(file);
        const relative = posix(path.relative(root, file));
        const list = groups.get(hash) || [];
        list.push(relative);
        groups.set(hash, list);
      }
      const includedSet = new Set(included);
      for (const files of groups.values()) {
        if (files.length < 2 || !files.some(file => includedSet.has(file))) continue;
        diagnostics.push(diagnostic('warning', 'duplicate-uf2', files.find(file => includedSet.has(file)) || files[0],
          `Byte-identical UF2 files found: ${files.sort().join(', ')}.`));
      }
    }

    for (const cmakeFile of releaseFiles.filter(file => path.basename(file).toLowerCase() === 'cmakelists.txt')) {
      const source = fs.readFileSync(cmakeFile, 'utf8');
      if (!cmakeUsesPicoSdk(source) || cmakeHasXosc64(source) || customBoardHasXosc64(source, releaseFiles)) continue;
      const relative = posix(path.relative(root, cmakeFile));
      diagnostics.push(diagnostic('warning', 'pico-xosc64-recommended', relative,
        'Pico SDK CMakeLists.txt must define PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64 in target_compile_definitions().'));
    }
  }

  return diagnostics;
}

/** Parse `git diff --name-status -z` output. */
export function parseNameStatusZ(buffer) {
  const fields = buffer.toString('utf8').split('\0');
  if (fields.at(-1) === '') fields.pop();
  const changes = [];
  for (let index = 0; index < fields.length;) {
    const status = fields[index++];
    if (/^[RC]/.test(status)) {
      changes.push({ status, oldPath: posix(fields[index++]), path: posix(fields[index++]) });
    } else {
      changes.push({ status, path: posix(fields[index++]) });
    }
  }
  return changes;
}
