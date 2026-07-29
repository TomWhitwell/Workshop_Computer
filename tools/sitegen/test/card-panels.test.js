import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildCanonicalCardModel } from '../src/model/card.js';

function build(rawYaml, extra = {}) {
  return buildCanonicalCardModel({
    folderName: '42_panel_test',
    slug: '42-panel-test',
    rawYaml,
    ...extra,
  });
}

test('direct socket metadata maps API IDs and aliases to physical panel slots', () => {
  const card = build({
    Name: 'Panel Test',
    panel: {
      inputs: { AudioIn1: { name: 'Left input' }, audio_in_2: { name: 'Right input' } },
      outputs: [{ id: 'CVOut1', name: 'Pitch' }, { id: 'NotAJack', name: 'Ignored' }],
    },
  });
  assert.equal(card.panel.inputs.audio_l.label, 'Left input');
  assert.equal(card.panel.inputs.audio_r.label, 'Right input');
  assert.equal(card.panel.outputs.cv_out_1.label, 'Pitch');
  assert.ok(card.warnings.some(warning => warning.includes('NotAJack')));
  assert.equal(card.panel_views, undefined);
});

test('panel labels preserve author wording without abbreviation or truncation', () => {
  const card = build({
    Name: 'Panel Test',
    panel: { inputs: [{ id: 'PulseIn1', name: 'External Trigger / Pattern Input' }] },
    controls: {
      switch: { up: 'Preset Select Pattern' },
      knobs: [{ main: { name: 'Quantized Modulation Channel' } }],
    },
  });
  assert.equal(card.panel.inputs.pulse_1.label, 'External Trigger / Pattern Input');
  assert.equal(card.panel_views.items[0].panel.controls.main.label, 'Quantized Modulation Channel');
  assert.equal(card.panel_views.items[0].panel.controls.z.label, 'Preset Select Pattern');
});

test('generated switch views inherit base rows and apply positional overlays', () => {
  const card = build({
    Name: 'Panel Test',
    controls: {
      switch: { up: 'High', middle: 'Normal', down: 'Low' },
      knobs: [
        { main: { name: 'Base main' }, x: { name: 'Base X' } },
        { when: { z: 'up' }, main: { name: 'Up main' } },
        { when: { z: 'middle' }, y: { name: 'Middle Y' } },
        { when: { z: 'down', gesture: 'hold' }, main: { name: 'Held down' } },
      ],
    },
  });
  assert.deepEqual(card.panel_views.items.map(item => item.id), ['up', 'middle', 'down']);
  assert.equal(card.panel_views.default, 'middle');
  const [up, middle, down] = card.panel_views.items;
  assert.equal(up.panel.controls.main.label, 'Up main');
  assert.equal(up.panel.controls.x.label, 'Base X');
  assert.equal(middle.panel.controls.y.label, 'Middle Y');
  assert.equal(down.panel.controls.main.label, 'Held down');
  assert.deepEqual(card.panel, middle.panel);
});

test('tap gestures become switch actions but do not overwrite the Down panel', () => {
  const card = build({
    Name: 'Panel Test',
    controls: { knobs: [
      { when: { z: 'down' }, main: { name: 'Down mode' } },
      { when: { z: 'down', gesture: 'momentary' }, main: { name: 'Tap action' } },
      { when: { z: 'down', gesture: 'hold' }, x: { name: 'Held X' } },
    ] },
  });
  const down = card.panel_views.items.find(item => item.id === 'down');
  assert.equal(down.panel.controls.main.label, 'Down mode');
  assert.equal(down.panel.controls.x.label, 'Held X');
  assert.ok(!JSON.stringify(down.panel).includes('Tap action'));
  assert.match(card.switch_modes.tap, /MAIN: Tap action/);
});

test('custom panels preserve order, select their default, and suppress generated views', () => {
  const customPanels = {
    source: 'custom',
    default: 'alternate',
    items: [
      { id: 'main', name: 'Main', image: { url: 'panels/main.svg', format: 'svg', width: 560, height: 1785 }, content_html: '<p>Main</p>' },
      { id: 'alternate', name: 'Alternate', image: { url: 'panels/alt.svg', format: 'svg', width: 560, height: 1785 }, content_html: '<p>Alt</p>' },
    ],
  };
  const card = build({
    Name: 'Panel Test',
    controls: { knobs: [
      { when: { z: 'up' }, main: { name: 'Generated' } },
      { main: { name: 'Shared' } },
      { when: { panel: 'alternate' }, main: { name: 'Alternate main' } },
    ] },
  }, { customPanels });
  assert.equal(card.panel_views.source, 'custom');
  assert.equal(card.panel_views.default, 'alternate');
  assert.deepEqual(card.panel_views.items.map(item => item.id), ['main', 'alternate']);
  assert.equal(card.panel_views.items[0].panel.controls.main.label, 'Shared');
  assert.equal(card.panel_views.items[1].panel.controls.main.label, 'Alternate main');
  assert.ok(!card.panel_views.items.some(item => item.id === 'up'));

  const invalidOverride = build({ Name: 'Panel Test', controls: { switch: { up: 'Up' } } }, {
    customPanels: { source: 'custom', default: '', items: [] },
  });
  assert.deepEqual(invalidOverride.panel_views, { source: 'custom', default: '', items: [] });
});