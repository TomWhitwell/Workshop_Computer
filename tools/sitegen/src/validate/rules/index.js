// Reusable info.yaml validation rules.
//
// Each rule is `{ id, check(ctx) -> diagnostic[] }`. Rules read from `ctx`
// (schema adapter + normalized data + source line map) and must not depend on
// HTML rendering or the normalized card model — this keeps them safe to run in
// the author preview, the site build, and CI alike.
//
// Diagnostic shape: { severity, ruleId, path, message, line?, col?, suggestion? }

function isPlainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function isBlank(value) {
  return value === undefined || value === null || String(value).trim() === '';
}

function hasContent(value) {
  if (Array.isArray(value)) return value.some(hasContent);
  if (isPlainObject(value)) return Object.values(value).some(hasContent);
  return !isBlank(value);
}

function looksLikeUrl(value) {
  return /^https?:\/\/\S+$/i.test(String(value).trim());
}

// ---------------------------------------------------------------------------

export const unknownTopLevelKeys = {
  id: 'unknown-field',
  check(ctx) {
    const out = [];
    const known = ctx.schema.knownKeys();
    for (const [nk, entry] of Object.entries(ctx.normalized)) {
      if (!known.has(nk)) {
        out.push({ severity: 'warning', path: entry.key, key: entry.key,
          message: `Unknown field "${entry.key}" is not in the schema (possible typo?).` });
      }
    }
    return out;
  },
};

export const tagsFormat = {
  id: 'tags-format',
  check(ctx) {
    const raw = ctx.get('tags');
    if (raw === undefined || raw === null) return [];
    const list = Array.isArray(raw) ? raw : String(raw).split(',');
    const out = [];
    for (const tag of list) {
      // Legacy MTM-imported cards use `tags: [{ id, label }]`; canonical wants
      // plain kebab-case strings. Flag the object shape explicitly.
      if (tag !== null && typeof tag === 'object') {
        out.push({ severity: 'warning', path: 'tags', key: 'tags',
          message: 'Tag entries should be plain kebab-case strings, not objects (legacy shape).' });
        continue;
      }
      const t = String(tag).trim();
      if (!t) continue;
      if (!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(t)) {
        out.push({ severity: 'warning', path: 'tags', key: 'tags',
          message: `Tag "${t}" should be lowercase kebab-case (e.g. "midi-host").` });
      }
    }
    return out;
  },
};

export const editorValue = {
  id: 'editor-value',
  check(ctx) {
    const value = ctx.get('editor');
    if (isBlank(value)) return [];
    const v = String(value).trim();
    const known = v === 'web' || v === 'dist' || v === 'none' || looksLikeUrl(v);
    if (!known) {
      const unsafe = /(?:^|[\\/])\.\.(?:[\\/]|$)/.test(v)
        || pathLikeAbsolute(v)
        || /^[a-z][a-z0-9+.-]*:/i.test(v);
      return [{ severity: unsafe ? 'error' : 'warning', path: 'Editor', key: 'Editor',
        message: `Editor "${v}" is not a recognized value (expected web, dist, none, or an https URL).` }];
    }
    return [];
  },
};

function pathLikeAbsolute(value) {
  return /^[/\\]/.test(value) || /^[a-z]:[/\\]/i.test(value) || String(value).includes('\0');
}

export const contactShape = {
  id: 'contact-shape',
  check(ctx) {
    const contact = ctx.get('contact');
    if (contact === undefined || contact === null) return [];
    if (!isPlainObject(contact)) {
      return [{ severity: 'warning', path: 'contact', key: 'contact',
        message: 'Field "contact" should be an object with email/website/social.' }];
    }
    const out = [];
    if (contact.email && !/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(String(contact.email).trim())) {
      out.push({ severity: 'warning', path: 'contact.email', key: 'contact',
        message: `contact.email "${contact.email}" does not look like an email address.` });
    }
    if (contact.website && !looksLikeUrl(contact.website)) {
      out.push({ severity: 'warning', path: 'contact.website', key: 'contact',
        message: `contact.website "${contact.website}" should be an http(s) URL.` });
    }
    if (contact.social !== undefined && !isPlainObject(contact.social)) {
      out.push({ severity: 'warning', path: 'contact.social', key: 'contact',
        message: 'contact.social should be a map of platform → profile URL.' });
    }
    return out;
  },
};

export const mediaFields = {
  id: 'media-url',
  check(ctx) {
    const out = [];
    const demo = ctx.get('demolink');
    if (!isBlank(demo) && !looksLikeUrl(demo)) {
      out.push({ severity: 'warning', path: 'demo-link', key: 'demo-link',
        message: `demo-link "${demo}" should be an http(s) URL.` });
    }
    const discussion = ctx.get('discussion');
    if (!isBlank(discussion) && !looksLikeUrl(discussion)) {
      out.push({ severity: 'warning', path: 'discussion', key: 'discussion',
        message: `discussion "${discussion}" should be an http(s) URL.` });
    }
    return out;
  },
};

export const metadataCompleteness = {
  id: 'metadata-completeness',
  check(ctx) {
    // Draft status controls publication readiness, not diagnostic visibility:
    // incomplete metadata should remain visible while a card is being drafted.
    const out = [];
    for (const field of ['License', 'contact']) {
      if (isBlank(ctx.get(field))) {
        out.push({ severity: 'warning', path: field, key: field,
          message: `Metadata field "${field}" is missing.` });
      }
    }
    const panel = ctx.get('panel');
    const controls = ctx.get('controls');
    const generatedPanelDefined = [panel, controls].some(value =>
      isPlainObject(value) && hasContent(value),
    );
    const customPanelDefined = ctx.customPanelsPresent === true && ctx.panelIds.size > 0;
    if (!generatedPanelDefined && !customPanelDefined) {
      out.push({ severity: 'warning', path: 'panel', key: 'panel',
        message: 'No generated panel metadata or valid custom panel definition was found.' });
    }
    return out;
  },
};

export const panelStructure = {
  id: 'panel-structure',
  check(ctx) {
    const panel = ctx.get('panel');
    if (panel === undefined || panel === null) return [];
    if (!isPlainObject(panel)) {
      return [{ severity: 'warning', path: 'panel', key: 'panel',
        message: 'Field "panel" should be an object with inputs/outputs.' }];
    }
    const out = [];
    for (const side of ['inputs', 'outputs']) {
      const jacks = panel[side];
      if (jacks === undefined) continue;
      if (!Array.isArray(jacks)) {
        out.push({ severity: 'warning', path: `panel.${side}`, key: 'panel',
          message: `panel.${side} should be a list of jacks (canonical shape).` });
        continue;
      }
      jacks.forEach((jack, i) => {
        if (!isPlainObject(jack)) {
          out.push({ severity: 'warning', path: `panel.${side}[${i}]`, key: 'panel',
            message: `panel.${side}[${i}] should be an object with id/name.` });
        } else if (isBlank(jack.id) && isBlank(jack.name)) {
          out.push({ severity: 'warning', path: `panel.${side}[${i}]`, key: 'panel',
            message: `panel.${side}[${i}] should have an "id" (ComputerCard API jack) or "name".` });
        }
        if (isPlainObject(jack)) validateWhen(jack.when, `panel.${side}[${i}].when`, 'panel', out);
      });
    }
    return out;
  },
};

function validateWhen(when, path, key, out) {
  if (when === undefined || when === null) return;
  if (!isPlainObject(when)) {
    out.push({ severity: 'warning', path, key, message: `${path} should be an object.` });
    return;
  }
  if (when.z !== undefined) {
    if (when.z === 'any') {
      out.push({ severity: 'warning', path: `${path}.z`, key,
        message: `${path}.z "any" is legacy syntax; omit "when" for metadata shared by every switch position.` });
    } else if (!['up', 'middle', 'down'].includes(when.z)) {
      out.push({ severity: 'warning', path: `${path}.z`, key,
        message: `${path}.z should be up, middle, or down; tap is switch action metadata and hold is the down position.` });
    }
  }
  if (when.panel !== undefined) {
    if (typeof when.panel !== 'string' || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(when.panel)) {
      out.push({ severity: 'warning', path: `${path}.panel`, key,
        message: `${path}.panel should be a lowercase kebab-case id from panels/manifest.yaml.` });
    }
  }
  if (when.z !== undefined && when.panel !== undefined) {
    out.push({ severity: 'error', path, key,
      message: `${path} cannot combine z and panel; use one physical-position or custom-panel context.` });
  }
  if (when.gesture !== undefined) {
    out.push({ severity: 'warning', path: `${path}.gesture`, key,
      message: `${path}.gesture is legacy syntax; describe a tap in controls.switch.tap, and use when.z: down for the held-down panel state.` });
  }
}

export const controlsStructure = {
  id: 'controls-structure',
  check(ctx) {
    const controls = ctx.get('controls');
    if (controls === undefined || controls === null) return [];
    if (!isPlainObject(controls)) {
      return [{ severity: 'warning', path: 'controls', key: 'controls',
        message: 'Field "controls" should be an object with knobs/switch/leds.' }];
    }
    const out = [];
    if (controls.switch !== undefined) {
      if (!isPlainObject(controls.switch)) {
        out.push({ severity: 'warning', path: 'controls.switch', key: 'controls',
          message: 'controls.switch should be an object keyed by up/middle/down/tap.' });
      } else {
        for (const pos of Object.keys(controls.switch)) {
          if (!['up', 'middle', 'down', 'tap'].includes(pos)) {
            out.push({ severity: 'warning', path: `controls.switch.${pos}`, key: 'controls',
              message: `controls.switch key "${pos}" should be up, middle, down, or tap.` });
          } else if (!isPlainObject(controls.switch[pos]) && typeof controls.switch[pos] !== 'string') {
            out.push({ severity: 'warning', path: `controls.switch.${pos}`, key: 'controls',
              message: `controls.switch.${pos} should be text or an object with name/description.` });
          }
        }
      }
    }
    for (const listKey of ['knobs', 'leds']) {
      if (controls[listKey] !== undefined && !Array.isArray(controls[listKey])) {
        out.push({ severity: 'warning', path: `controls.${listKey}`, key: 'controls',
          message: `controls.${listKey} should be a list.` });
      } else if (Array.isArray(controls[listKey])) {
        controls[listKey].forEach((row, i) => {
          if (!isPlainObject(row)) return;
          validateWhen(row.when, `controls.${listKey}[${i}].when`, 'controls', out);
        });
      }
    }
    return out;
  },
};

export const customPanelReferences = {
  id: 'custom-panel-reference',
  check(ctx) {
    // Cross-file checks are available to filesystem consumers such as the CLI.
    // Browser-only callers omit customPanelsPresent and still receive syntax
    // validation for when.panel from validateWhen().
    if (ctx.customPanelsPresent === undefined) return [];
    const out = [];
    const panel = ctx.get('panel');
    const controls = ctx.get('controls');
    const lists = [
      ['panel.inputs', panel?.inputs],
      ['panel.outputs', panel?.outputs],
      ['controls.knobs', controls?.knobs],
      ['controls.leds', controls?.leds],
    ];
    for (const [listPath, rows] of lists) {
      if (!Array.isArray(rows)) continue;
      rows.forEach((row, index) => {
        const panelId = isPlainObject(row?.when) ? row.when.panel : undefined;
        if (panelId === undefined) return;
        const path = `${listPath}[${index}].when.panel`;
        if (!ctx.customPanelsPresent) {
          out.push({ severity: 'error', path, key: listPath.split('.')[0],
            message: `${path} requires a panels/manifest.yaml custom-panel override.` });
        } else if (typeof panelId === 'string' && !ctx.panelIds.has(panelId)) {
          out.push({ severity: 'error', path, key: listPath.split('.')[0],
            message: `${path} references unknown custom panel id "${panelId}".` });
        }
      });
    }
    return out;
  },
};

// Keys that only ever appear in the *generated* normalized card model, never in
// author-authored info.yaml. Their presence means the source file was
// overwritten with build output (the split-brain the migration plan warns
// against). Reported once so it doesn't drown out the underlying field errors.
const GENERATED_ONLY_KEYS = ['source_file', 'source_url', 'readme_url', 'download_url', 'slug', 'url'];

export const generatedModelShape = {
  id: 'generated-model-shape',
  check(ctx) {
    const found = GENERATED_ONLY_KEYS.filter(k => ctx.entry(k) !== undefined);
    if (!found.length) return [];
    return [{ severity: 'warning', path: found[0], key: found[0],
      message: `info.yaml contains generated card-model keys (${found.join(', ')}); it looks like normalized build output rather than author source. Restore author-shaped source per documentation/info.yaml.md.` }];
  },
};

export const uf2Entries = {
  id: 'uf2-entries',
  check(ctx) {
    const entry = ctx.entry('uf2');
    if (!entry) return [];
    const raw = entry.value;
    // Declared but empty (null or []) — likely half-authored. Warn, but the
    // build keeps auto-discovery so downloads aren't silently removed.
    if (raw === null || raw === undefined || (Array.isArray(raw) && raw.length === 0)) {
      return [{ severity: 'warning', path: 'uf2', key: 'uf2',
        message: 'Field "uf2" is empty; add firmware entries (each with a "path") or remove it to use auto-discovery.' }];
    }
    if (!Array.isArray(raw)) {
      return [{ severity: 'error', path: 'uf2', key: 'uf2',
        message: 'Field "uf2" should be a list of firmware entries.' }];
    }
    const out = [];
    raw.forEach((entry, i) => {
      const at = `uf2[${i}]`;
      if (!isPlainObject(entry)) {
        out.push({ severity: 'error', path: at, key: 'uf2',
          message: `${at} should be an object with a "path" or "download.url".` });
        return;
      }
      const dl = isPlainObject(entry.download) ? entry.download : null;
      const urlEntry = dl && Object.entries(dl).find(([k]) => k.toLowerCase() === 'url');
      const shaEntry = Object.entries(entry).find(([k]) => k.toLowerCase() === 'sha256');
      const hasPath = !isBlank(entry.path);
      const hasUrl = urlEntry && !isBlank(urlEntry[1]);
      // An entry needs either a repo firmware path or an external download URL.
      if (!hasPath && !hasUrl) {
        out.push({ severity: 'error', path: at, key: 'uf2',
          message: `${at} needs a "path" (repo firmware) or a "download.url" (external link).` });
      } else if (entry.path !== undefined && typeof entry.path !== 'string') {
        out.push({ severity: 'error', path: `${at}.path`, key: 'uf2',
          message: `${at}.path should be a string path.` });
      }
      if (entry.name !== undefined && typeof entry.name !== 'string') {
        out.push({ severity: 'warning', path: `${at}.name`, key: 'uf2',
          message: `${at}.name should be a string.` });
      }
      if (hasPath && !hasUrl && shaEntry && !isBlank(shaEntry[1])) {
        out.push({ severity: 'warning', path: `${at}.sha256`, key: 'uf2',
          message: `${at}.sha256 is not required for repository-hosted firmware; the build computes it automatically.` });
      }
      if (entry.download !== undefined) {
        if (!isPlainObject(entry.download)) {
          out.push({ severity: 'warning', path: `${at}.download`, key: 'uf2',
            message: `${at}.download should be an object with url and sha256.` });
        } else {
          if (urlEntry && urlEntry[1] !== undefined && !isBlank(urlEntry[1]) && !looksLikeUrl(urlEntry[1])) {
            out.push({ severity: 'warning', path: `${at}.download.url`, key: 'uf2',
              message: `${at}.download.url should be an http(s) URL.` });
          }
          const shaEntry = Object.entries(entry.download).find(([k]) => k.toLowerCase() === 'sha256');
          if (!shaEntry || isBlank(shaEntry[1])) {
            out.push({ severity: entry.download.flashable === true ? 'error' : 'warning', path: `${at}.download.sha256`, key: 'uf2',
              message: `${at}.download${entry.download.flashable === true ? ' is flashable and' : ''} should provide a "sha256" hash.` });
          }
        }
      }
    });
    return out;
  },
};

export const allRules = [
  unknownTopLevelKeys,
  tagsFormat,
  editorValue,
  contactShape,
  mediaFields,
  metadataCompleteness,
  panelStructure,
  controlsStructure,
  customPanelReferences,
  uf2Entries,
  generatedModelShape,
];
