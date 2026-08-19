// Tests for the shared validation pipeline (parseSource -> validateInfoYaml).
// This seam is consumed by the site build, the author preview, and CI, so its
// behavior — especially "never throws" and diagnostic shape — must stay stable.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseSource } from '../src/validate/parseSource.js';
import { validateInfoYaml } from '../src/validate/validateInfoYaml.js';
import { getInfoYamlSchemaAdapter } from '../src/schema/schemaAdapter.js';
import { infoYamlJsonSchema } from '../src/schema/infoYamlJsonSchema.js';
import { reportGithub, reportMarkdown } from '../src/validate/reporters/index.js';

function validate(yamlText) {
  return validateInfoYaml(parseSource(yamlText, 'test/info.yaml'));
}

function ruleIds(result) {
  return result.diagnostics.map(d => d.ruleId);
}

test('author schema fields are represented in the published JSON schema', () => {
  const adapter = getInfoYamlSchemaAdapter();
  for (const field of adapter.listFields()) {
    assert.ok(infoYamlJsonSchema.properties[field.path], `${field.path} is missing from the JSON schema`);
  }
  assert.deepEqual(
    new Set(infoYamlJsonSchema.required),
    new Set(adapter.requiredFields().map(field => field.path)),
  );
});

test('nested AJV diagnostics point to the nested YAML source line', () => {
  const result = validate(`Name: Test Card
Creator: Someone
Language: C++
Version: "1.0"
Status: Released
short-description: A test card.
summary: A longer summary.
panel:
  inputs:
    - id: AudioIn1
      name: 42
`);
  const diagnostic = result.diagnostics.find(item => item.ruleId === 'ajv-schema' && item.path === 'panel.inputs.0.name');
  assert.equal(diagnostic?.line, 11);
});

test('GitHub reporter emits inline annotations and a PR-check summary', () => {
  const output = reportGithub([{
    file: 'releases/42_test/info.yaml', ok: false, errorCount: 1, warningCount: 1,
    diagnostics: [
      { severity: 'error', ruleId: 'required', path: 'Name', line: 2, col: 1, message: 'Missing Name.' },
      { severity: 'warning', ruleId: 'tags', path: 'tags', message: 'Check tags.' },
    ],
  }]);
  assert.match(output, /::error file=releases\/42_test\/info\.yaml,line=2,col=1,title=info\.yaml required::Name: Missing Name\./);
  assert.match(output, /::warning file=releases\/42_test\/info\.yaml,title=info\.yaml tags::tags: Check tags\./);
  assert.match(output, /::notice title=info\.yaml validation::1 file\(s\), 1 failing — 1 error\(s\), 1 warning\(s\)\./);
});

test('PR Markdown report groups diagnostics by changed info.yaml', () => {
  const failed = [{
    file: 'releases/42_test/info.yaml', diagnostics: [
      { severity: 'warning', ruleId: 'tags', path: 'tags', line: 9, message: 'Use kebab case.' },
      { severity: 'error', ruleId: 'required', path: 'Creator', line: 2, message: 'Missing Creator.' },
    ], ok: false, errorCount: 1, warningCount: 1,
  }, {
    file: '/tmp/checkout/releases/43_clean/info.yaml', diagnostics: [],
    ok: true, errorCount: 0, warningCount: 0,
  }];
  const markdown = reportMarkdown(failed, {
    errorCount: 1,
    warningCount: 1,
    trigger: {
      affectedReleases: ['42_test', '43_clean'],
      changedPaths: [
        { status: 'M', path: 'releases/42_test/info.yaml' },
        { status: 'A', path: 'releases/43_clean/info.yaml' },
        { status: 'A', path: 'releases/43_clean/firmware/', filesUnder: true },
      ],
    },
    diagnostics: [
      { severity: 'warning', ruleId: 'multiple-release-directories', file: 'releases', message: 'Two release directories changed.' },
      { severity: 'error', ruleId: 'uf2-required', file: 'releases/42_test', message: 'No UF2 firmware file is included.' },
    ],
  });
  assert.match(markdown, /## ❌ Program card PR validation failed/);
  assert.match(markdown, /PR validation is intended for maintainers, not card authors/);
  assert.equal((markdown.match(/\| Severity \|/g) || []).length, 2);
  assert.match(markdown, /### `42_test\/info\.yaml`/);
  assert.match(markdown, /### `43_clean\/info\.yaml`/);
  assert.match(markdown, /43_clean\/info\.yaml`\n\n✅ This file validates cleanly\./);
  assert.match(markdown, /## Other rules/);
  assert.match(markdown, /\*\*Triggered by:\*\*/);
  assert.match(markdown, /`M releases\/42_test\/info.yaml`/);
  assert.match(markdown, /`A files under releases\/43_clean\/firmware\/`/);
  assert.match(markdown, /\*\*Affected release directories:\*\* `42_test`, `43_clean`/);
  assert.ok(markdown.indexOf('**Triggered by:**') < markdown.indexOf('### `42_test/info.yaml`'));
  assert.ok(markdown.indexOf('**Triggered by:**') < markdown.indexOf('## Other rules'));
  assert.match(markdown, /\| Severity \| Affected path \| Rule \| Message \|/);
  assert.match(markdown, /`uf2-required`.*No UF2 firmware file is included\./);
  assert.match(markdown, /\| Severity \| Field \| Rule \| Message \|/);
  assert.doesNotMatch(markdown, /\| File \||\| Location \|/);
  assert.match(markdown, /❌ Error.*`Creator`.*`required`.*Missing Creator\./);
  assert.match(markdown, /⚠️ Warning.*`tags`.*Use kebab case\./);
  assert.doesNotMatch(markdown, /<details>|New diagnostics|Existing diagnostics/);

  const succeeded = reportMarkdown([{
    file: 'releases/43_clean/info.yaml', diagnostics: [], ok: true, errorCount: 0, warningCount: 0,
  }]);
  assert.match(succeeded, /## ✅ Program card PR validation succeeded/);
  assert.match(succeeded, /validate cleanly/);

  const warned = reportMarkdown([{
    file: 'releases/44_warning/info.yaml', diagnostics: [
      { severity: 'warning', ruleId: 'tags', path: 'tags', message: 'Check tags.' },
    ], ok: true, errorCount: 0, warningCount: 1,
  }]);
  assert.match(warned, /## ⚠️ Program card PR validation succeeded with warnings/);
});

test('canonical card validates clean', () => {
  const result = validate(`
Name: Test Card
Creator: Someone
Language: C++
Version: "1.0"
Status: Released
short-description: A test card.
summary: A longer summary of the test card.
tags: [midi-host, utility]
License: MIT
contact: { website: https://example.com }
panel:
  inputs:
    - { id: AudioIn1, name: Input }
`);
  assert.equal(result.ok, true);
  assert.equal(result.errorCount, 0);
});

test('missing Name is an error, legacy Title satisfies it', () => {
  const missing = validate(`Creator: Someone\nLanguage: C\nVersion: "1"\nStatus: WIP\n`);
  assert.ok(missing.diagnostics.some(d =>
    d.ruleId === 'ajv-schema' && d.severity === 'error' && d.path === 'Name'));

  const withTitle = validate(`Title: Legacy Card\nCreator: S\nLanguage: C\nVersion: "1"\nStatus: WIP\n`);
  assert.ok(!withTitle.diagnostics.some(d => d.message.includes('"Name"')));
});

test('documented key spelling variants validate through the canonical schema', () => {
  const result = validate(`
name: Test
short description: Short
SUMMARY: Long
language: C++
creator: Someone
version: "1.0"
status: Released
License: MIT
contact: { website: https://example.com }
panel:
  inputs:
    - { id: AudioIn1, name: Input }
`);
  assert.equal(result.errorCount, 0);
  assert.equal(result.warningCount, 0);
});

test('completeness warnings apply to drafts and valid custom panels satisfy the panel check', () => {
  const source = parseSource(`
Name: Draft Panels
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: WIP
draft: true
`, 'test/info.yaml');
  const withoutPanel = validateInfoYaml(source, { customPanelsPresent: false });
  assert.ok(withoutPanel.diagnostics.some(d =>
    d.ruleId === 'metadata-completeness' && d.path === 'panel'));
  assert.ok(withoutPanel.diagnostics.some(d =>
    d.ruleId === 'metadata-completeness' && d.path === 'License'));
  assert.ok(withoutPanel.diagnostics.some(d =>
    d.ruleId === 'metadata-completeness' && d.path === 'contact'));

  const withCustomPanel = validateInfoYaml(source, {
    customPanelsPresent: true,
    panelIds: ['main'],
  });
  assert.ok(!withCustomPanel.diagnostics.some(d =>
    d.ruleId === 'metadata-completeness' && d.path === 'panel'));
});

test('boolean values are rejected for authored text while numeric versions remain compatible', () => {
  const result = validate(`
Name: true
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: 1.0
Status: Released
`);
  assert.ok(result.diagnostics.some(d => d.ruleId === 'ajv-schema' && d.path === 'Name'));
  assert.ok(!result.diagnostics.some(d => d.path === 'Version'));
});

test('current nested panel, control, LED, host, audio, and date shapes validate cleanly', () => {
  const result = validate(`
Name: Structured
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
License: MIT
contact: { website: https://example.com }
date-created: 2025-07-25
date-updated: 2026-07-25
audio-sample:
  - url: samples/demo.wav
    title: Demo
panel:
  inputs:
    - id: AudioIn1
      name: Audio
      type: audio
  outputs:
    - id: CVOut1
      name: Pitch
      when: { z: up }
controls:
  switch:
    up: { name: Alternate }
    middle: Normal
    down: { name: Write, description: Hold to write }
  knobs:
    - main: { name: Amount }
      x: { name: Shape, description: Selects shape }
  leds:
    - display: list
      items:
        - { id: LED0, name: Activity }
host:
  usb:
    - { name: MIDI, role: device, description: USB MIDI device }
  notes: Connect to a host.
`);
  assert.equal(result.errorCount, 0);
  assert.equal(result.warningCount, 0);
});

test('switch tap metadata does not require a down-position description', () => {
  const result = validate(`
Name: Tap Only
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
License: MIT
contact: { website: https://example.com }
controls:
  switch:
    tap: { name: Reset, description: Briefly press and release to reset }
panel:
  inputs:
    - { id: AudioIn1, name: Input }
`);
  assert.equal(result.errorCount, 0);
  assert.equal(result.warningCount, 0);
});

test('malformed nested metadata is diagnosed instead of silently ignored', () => {
  const result = validate(`
Name: Broken
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
date: yesterday
audio-sample:
  - title: Missing URL
panel:
  inputs:
    - { id: BogusJack, name: 123, type: banana }
controls:
  knobs:
    - { main: broken }
  leds:
    - { display: nonsense, items: broken }
host:
  usb: broken
`);
  for (const path of ['date-created', 'audio-sample.0.url', 'panel.inputs.0.id', 'panel.inputs.0.type', 'controls.knobs.0.main', 'controls.leds.0.items', 'host.usb']) {
    assert.ok(result.diagnostics.some(d => d.path === path), `missing diagnostic for ${path}`);
  }
});

test('legacy date aliases validate as date-created', () => {
  const result = validate(`
Name: Legacy Date
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
License: MIT
contact: { website: https://example.com }
date: 2024-11-30
panel:
  inputs:
    - { id: AudioIn1, name: Input }
`);
  assert.equal(result.errorCount, 0);
  assert.equal(result.warningCount, 0);
});

test('external firmware hashes must be complete SHA-256 values', () => {
  const result = validate(`
Name: Firmware
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
uf2:
  - name: Mirror
    download:
      url: https://example.com/fw.uf2
      sha256: not-a-hash
`);
  assert.ok(result.diagnostics.some(d => d.path === 'uf2.0.download.sha256'));
});

test('external firmware may opt into browser flashing', () => {
  const result = validate(`
Name: External Firmware
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
uf2:
  - name: Mirror
    download:
      url: https://downloads.example/card.uf2
      sha256: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
      flashable: true
`);
  assert.ok(!result.diagnostics.some(d => d.path.startsWith('uf2')));
});

test('repository-hosted firmware warns that an authored hash is unnecessary', () => {
  const result = validate(`
Name: Repository Firmware
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
uf2:
  - path: firmware/card.uf2
    sha256: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
`);
  assert.ok(result.diagnostics.some(d =>
    d.ruleId === 'uf2-entries'
    && d.path === 'uf2[0].sha256'
    && d.message.includes('not required for repository-hosted firmware')));
  assert.ok(!result.diagnostics.some(d =>
    d.ruleId === 'ajv-schema' && d.path === 'uf2.0.sha256'));
});

test('filesystem consumers can validate custom panel references', () => {
  const source = parseSource(`
Name: Panels
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
controls:
  knobs:
    - when: { panel: alternate }
      main: { name: Amount }
`, 'test/info.yaml');
  const absent = validateInfoYaml(source, { customPanelsPresent: false });
  assert.ok(absent.diagnostics.some(d => d.ruleId === 'custom-panel-reference' && d.severity === 'error'));
  const unknown = validateInfoYaml(source, { customPanelsPresent: true, panelIds: ['main'] });
  assert.ok(unknown.diagnostics.some(d => d.message.includes('unknown custom panel id')));
  const valid = validateInfoYaml(source, { customPanelsPresent: true, panelIds: ['main', 'alternate'] });
  assert.ok(!valid.diagnostics.some(d => d.ruleId === 'custom-panel-reference'));
});

test('filesystem consumers can attach manifest diagnostics to the shared result', () => {
  const source = parseSource('Name: X\nshort-description: S\nsummary: L\nLanguage: C\nCreator: A\nVersion: "1"\nStatus: WIP\nLicense: MIT\ncontact: { website: https://example.com }\npanel:\n  inputs:\n    - { id: AudioIn1, name: Input }\n');
  const result = validateInfoYaml(source, { externalDiagnostics: [{
    severity: 'error', ruleId: 'custom-panel-manifest', path: 'panels/manifest.yaml',
    message: 'Invalid custom panel manifest.',
  }] });
  assert.ok(result.diagnostics.some(d => d.ruleId === 'custom-panel-manifest'));
  assert.equal(result.errorCount, 1);
  assert.equal(result.warningCount, 0);
});

test('broken YAML yields a yaml-syntax diagnostic with a line, never throws', () => {
  const result = validate('Name: ok\n  bad: [unclosed\n');
  assert.equal(result.ok, false);
  const syntax = result.diagnostics.find(d => d.ruleId === 'yaml-syntax');
  assert.ok(syntax);
  assert.equal(typeof syntax.line, 'number');
});

test('multiple recoverable YAML syntax errors are all reported', () => {
  const result = validate('panel:\n  inputs\n    - id: A\nBlah\n  outputs:\n    - id: B\n');
  const syntax = result.diagnostics.filter(d => d.ruleId === 'yaml-syntax');
  assert.ok(syntax.length > 1);
  assert.deepEqual(new Set(syntax.map(d => d.line)), new Set([2, 4]));
});

test('diagnostics are anchored to the offending key source line', () => {
  const result = validate(`Name: X\nCreator: S\nLanguage: C\nVersion: "1"\nStatus: WIP\ntags:\n  - Not Kebab\n`);
  const tagDiag = result.diagnostics.find(d => d.ruleId === 'tags-format');
  assert.ok(tagDiag);
  assert.equal(tagDiag.line, 6); // the `tags:` key line
});

test('legacy object tags and non-kebab tags warn', () => {
  const result = validate(`Name: X\ntags:\n  - id: midi\n    label: MIDI\n  - Not Kebab\n`);
  const messages = result.diagnostics.filter(d => d.ruleId === 'tags-format').map(d => d.message);
  assert.ok(messages.some(m => m.includes('legacy shape')));
  assert.ok(messages.some(m => m.includes('"Not Kebab"')));
});

test('uf2 semantic rule is advisory when sha256 is missing', () => {
  const result = validate(`
Name: X
uf2:
  - name: firmware
    download:
      url: https://example.com/fw.uf2
`);
  assert.ok(result.diagnostics.some(d =>
    d.ruleId === 'uf2-entries' && d.severity === 'warning' && d.path === 'uf2[0].download.sha256'));
  assert.ok(result.diagnostics.some(d =>
    d.ruleId === 'ajv-schema' && d.severity === 'error' && d.path === 'uf2.0.download.sha256'));
});

test('uf2 flash defaults to 2MB and accepts 16mb', () => {
  const omitted = validate(`
Name: Firmware
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
uf2:
  - path: firmware/card.uf2
`);
  assert.ok(!omitted.diagnostics.some(d => String(d.path || '').includes('flash')));

  const explicit = validate(`
Name: Firmware
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
uf2:
  - path: firmware/card.uf2
    flash: 16mb
`);
  assert.ok(!explicit.diagnostics.some(d => String(d.path || '').includes('flash')));
});

test('invalid uf2 flash values are rejected', () => {
  const result = validate(`
Name: Firmware
short-description: Short
summary: Long
Language: C++
Creator: Someone
Version: "1.0"
Status: Released
uf2:
  - path: firmware/card.uf2
    flash: 32mb
`);
  assert.ok(result.diagnostics.some(d =>
    d.ruleId === 'uf2-entries' && d.severity === 'warning' && d.path === 'uf2[0].flash'));
  assert.ok(result.diagnostics.some(d =>
    d.ruleId === 'ajv-schema' && d.path === 'uf2.0.flash'));
});

test('when cannot combine z and panel', () => {
  const result = validate(`
Name: X
panel:
  inputs:
    - id: audio-in-1
      when: { z: up, panel: alt }
`);
  assert.ok(result.diagnostics.some(d =>
    d.severity === 'error' && d.path === 'panel.inputs[0].when'));
});

test('generated card-model keys in source are flagged once', () => {
  const result = validate(`Name: X\nslug: x\nurl: programs/x/\ndownload_url: https://example.com\n`);
  const hits = result.diagnostics.filter(d => d.ruleId === 'generated-model-shape');
  assert.equal(hits.length, 1);
});

test('a crashing rule becomes a diagnostic, not an exception', () => {
  const boom = { id: 'boom', check() { throw new Error('nope'); } };
  const result = validateInfoYaml(parseSource('Name: X\n'), { rules: [boom] });
  assert.ok(result.diagnostics.some(d =>
    d.ruleId === 'rule-crash:boom' && d.severity === 'error'));
});
