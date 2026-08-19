// Machine-readable structural schema for info.yaml.
//
// This deliberately allows additional properties: historical cards and future
// metadata must remain editable without AJV rejecting fields that are handled
// by semantic rules or preserved verbatim by Basic mode.

// YAML parses unquoted version-like values such as `1.0` as numbers. Preserve
// that long-standing compatibility while rejecting booleans as authored text.
const scalarText = {
  type: ['string', 'number'],
  minLength: 1,
};

const urlText = {
  type: 'string',
  pattern: '^https?://\\S+$',
};

const nonBlankText = { type: 'string', minLength: 1 };
const description = { type: 'string' };
const panelWhen = {
  type: 'object',
  properties: {
    z: { enum: ['up', 'middle', 'down'] },
    panel: { type: 'string', pattern: '^[a-z0-9]+(?:-[a-z0-9]+)*$' },
  },
  additionalProperties: false,
  not: { required: ['z', 'panel'] },
};
const panelControl = {
  type: 'object',
  required: ['name'],
  properties: { name: nonBlankText, description },
  additionalProperties: false,
};
const inputIds = ['AudioIn1', 'AudioIn2', 'CVIn1', 'CVIn2', 'PulseIn1', 'PulseIn2'];
const outputIds = ['AudioOut1', 'AudioOut2', 'CVOut1', 'CVOut2', 'PulseOut1', 'PulseOut2'];
const jack = ids => ({
  type: 'object',
  required: ['id', 'name'],
  properties: {
    id: { enum: ids },
    name: nonBlankText,
    description,
    type: { enum: ['audio', 'cv', 'pulse', 'other'] },
    when: panelWhen,
  },
  additionalProperties: false,
});
const ledItem = {
  type: 'object',
  required: ['id', 'name'],
  properties: { id: nonBlankText, name: nonBlankText, description },
  additionalProperties: false,
};
const audioItem = {
  oneOf: [
    { type: 'string', minLength: 1 },
    {
      type: 'object',
      required: ['url'],
      properties: { url: nonBlankText, title: nonBlankText },
      additionalProperties: false,
    },
  ],
};
const uf2Download = {
  type: 'object',
  required: ['url', 'sha256'],
  properties: {
    url: urlText,
    sha256: { type: 'string', pattern: '^[A-Fa-f0-9]{64}$' },
    flashable: { type: 'boolean' },
  },
  additionalProperties: false,
};
const uf2Entry = {
  type: 'object',
  properties: {
    path: { type: 'string', minLength: 1, pattern: '\\.[Uu][Ff]2$' },
    name: nonBlankText,
    flash: { enum: ['2mb', '16mb'] },
    sha256: { type: 'string', pattern: '^[A-Fa-f0-9]{64}$' },
    download: uf2Download,
  },
  anyOf: [{ required: ['path'] }, { required: ['download'] }],
  additionalProperties: false,
};

export const infoYamlJsonSchema = {
  $schema: 'http://json-schema.org/draft-07/schema#',
  $id: 'https://musicthing.co.uk/schemas/workshop-computer-info-yaml-v2.json',
  title: 'Workshop Computer info.yaml',
  type: 'object',
  required: ['Name', 'short-description', 'summary', 'Language', 'Creator', 'Version', 'Status'],
  properties: {
    draft: { type: 'boolean' },
    Name: scalarText,
    Title: scalarText,
    title: scalarText,
    'short-description': scalarText,
    summary: scalarText,
    Language: scalarText,
    Creator: scalarText,
    Version: scalarText,
    Status: scalarText,
    License: scalarText,
    'date-created': { type: 'string', pattern: '^\\d{4}-\\d{2}-\\d{2}$' },
    'date-updated': { type: 'string', pattern: '^\\d{4}-\\d{2}-\\d{2}$' },
    Editor: {
      oneOf: [
        { enum: ['web', 'dist', 'none'] },
        urlText,
      ],
    },
    'web-entry': { type: 'string', pattern: '^(?![/\\\\])(?!.*(?:^|[/\\\\])\\.\\.(?:[/\\\\]|$)).+\\.html$' },
    repository: urlText,
    Repository: urlText,
    discussion: urlText,
    'demo-link': urlText,
    tags: {
      oneOf: [
        { type: 'string' },
        { type: 'array', items: { type: 'string', minLength: 1 } },
      ],
    },
    'audio-sample': {
      oneOf: [
        { type: 'string', minLength: 1 },
        { type: 'array', minItems: 1, items: audioItem },
      ],
    },
    readme: { type: 'string' },
    contact: {
      type: 'object',
      properties: {
        email: { type: 'string' },
        website: urlText,
        social: {
          type: 'object',
          additionalProperties: urlText,
        },
      },
      additionalProperties: false,
    },
    panel: {
      type: 'object',
      properties: {
        inputs: { type: 'array', items: jack(inputIds) },
        outputs: { type: 'array', items: jack(outputIds) },
      },
      additionalProperties: false,
    },
    controls: {
      type: 'object',
      properties: {
        knobs: {
          type: 'array',
          items: {
            type: 'object',
            properties: { main: panelControl, x: panelControl, y: panelControl, when: panelWhen },
            additionalProperties: false,
            anyOf: [{ required: ['main'] }, { required: ['x'] }, { required: ['y'] }],
          },
        },
        switch: {
          type: 'object',
          properties: {
            up: { oneOf: [nonBlankText, panelControl] },
            middle: { oneOf: [nonBlankText, panelControl] },
            down: { oneOf: [nonBlankText, panelControl] },
            tap: { oneOf: [nonBlankText, panelControl] },
          },
          additionalProperties: false,
        },
        leds: {
          type: 'array',
          items: {
            type: 'object',
            required: ['items'],
            properties: {
              when: panelWhen,
              display: nonBlankText,
              items: { type: 'array', minItems: 1, items: ledItem },
            },
            additionalProperties: false,
          },
        },
      },
      additionalProperties: false,
    },
    host: {
      type: 'object',
      properties: {
        usb: {
          type: 'array',
          items: {
            type: 'object',
            required: ['name'],
            properties: { name: nonBlankText, role: nonBlankText, description },
            additionalProperties: false,
          },
        },
        notes: { type: 'string' },
      },
      additionalProperties: false,
    },
    uf2: { type: 'array', minItems: 1, items: uf2Entry },
  },
  additionalProperties: true,
};
