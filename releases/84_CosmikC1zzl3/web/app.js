const SAMPLE_RATE = 48000;
const MAX_LEVEL = 4095;
const STAGES = 8;
const CUSTOM_SLOT_COUNT = 8;
const MAX_BROWSER_CUSTOM_PRESETS = 32;
const AUDITION_TIME_SCALE = 4;
const EDITOR_VIEW_SAMPLES = SAMPLE_RATE * 4;
const MIN_SEND_SAMPLES = 960;
const SETTINGS_READ_TIMEOUT_MS = 3000;
const ENVELOPE_READ_TIMEOUT_MS = 7000;
const ENVELOPE_READ_RETRIES = 2;
const SAVE_VERIFY_DELAY_MS = 2500;
const DELETE_VERIFY_DELAY_MS = 500;
const AUX_ENVELOPE_REQUEST_DELAY_MS = 120;
const SYSEX_MANUFACTURER = 0x7d;
const SYSEX_ID = [0x43, 0x31, 0x5a, 0x33];
const SYSEX_COMMAND_PREVIEW = 0x01;
const SYSEX_COMMAND_SAVE = 0x02;
const SYSEX_COMMAND_SETTINGS = 0x03;
const SYSEX_COMMAND_SAVE_SETTINGS = 0x04;
const SYSEX_COMMAND_DELETE = 0x05;
const SYSEX_COMMAND_REQUEST_SETTINGS = 0x06;
const SYSEX_COMMAND_SETTINGS_RESPONSE = 0x07;
const SYSEX_COMMAND_REQUEST_ENVELOPE_SLOTS = 0x08;
const SYSEX_COMMAND_ENVELOPE_SLOTS_RESPONSE = 0x09;
const SYSEX_COMMAND_REQUEST_ENVELOPE = 0x0a;
const SYSEX_COMMAND_ENVELOPE_RESPONSE = 0x0b;
const SYSEX_COMMAND_REQUEST_PITCH_ENVELOPE = 0x0c;
const SYSEX_COMMAND_PITCH_ENVELOPE_RESPONSE = 0x0d;
const SYSEX_COMMAND_PD2_ENVELOPE_RESPONSE = 0x0e;
const SYSEX_COMMAND_AMP2_ENVELOPE_RESPONSE = 0x0f;
const SYSEX_COMMAND_REQUEST_PD2_ENVELOPE = 0x10;
const SYSEX_COMMAND_REQUEST_AMP2_ENVELOPE = 0x11;
const ENVELOPE_PROTOCOL_VERSION = 9;
const MAX_READABLE_ENVELOPE_PROTOCOL_VERSION = 9;

const factoryPresets = [
  preset("Off", fill(0, 1), fill(0, 1)),
  preset("Pluck",
    [[4095, 480], [0, 12000], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1]],
    [[1024, 480], [0, 12000], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1]]),
  preset("Double pluck",
    [[4095, 180], [700, 4800], [0, 2400], [3600, 180], [900, 6000], [350, 6000], [120, 6000], [0, 12000]],
    [[1600, 180], [500, 4800], [0, 2400], [2200, 180], [700, 6000], [300, 6000], [120, 6000], [0, 12000]]),
  preset("Bounce",
    [[4095, 120], [1200, 3600], [3300, 3600], [1700, 4800], [2600, 4800], [900, 7200], [1600, 7200], [0, 12000]],
    [[2500, 120], [800, 3600], [2200, 3600], [700, 4800], [1600, 4800], [500, 7200], [1200, 7200], [0, 12000]]),
  preset("Bell",
    [[4095, 240], [2600, 12000], [1200, 24000], [0, 36000], [0, 1], [0, 1], [0, 1], [0, 1]],
    [[2048, 240], [1600, 6000], [700, 24000], [0, 42000], [0, 1], [0, 1], [0, 1], [0, 1]]),
  preset("Brass",
    [[4095, 4800], [3400, 30000], [3200, 18000], [3000, 18000], [3300, 18000], [3100, 18000], [1600, 18000], [0, 24000]],
    [[1792, 4800], [900, 30000], [1200, 18000], [1000, 18000], [1300, 18000], [900, 18000], [500, 18000], [0, 24000]]),
  preset("Strings",
    [[2200, 24000], [3600, 24000], [3800, 48000], [3600, 48000], [3400, 48000], [3000, 48000], [1800, 48000], [0, 96000]],
    [[400, 12000], [900, 36000], [1200, 36000], [900, 48000], [700, 48000], [500, 48000], [400, 48000], [300, 96000]]),
  preset("Reverse swell",
    [[200, 12000], [900, 18000], [1800, 18000], [3000, 18000], [4095, 12000], [2600, 2400], [900, 2400], [0, 4800]],
    [[200, 12000], [500, 18000], [1000, 18000], [1800, 18000], [2600, 12000], [1300, 2400], [500, 2400], [0, 4800]]),
  preset("Evolving digital",
    [[4095, 480], [3900, 12000], [3800, 12000], [3600, 12000], [3200, 18000], [2600, 18000], [1600, 24000], [0, 36000]],
    [[3000, 480], [800, 12000], [3600, 12000], [1200, 12000], [2600, 18000], [600, 18000], [1800, 24000], [0, 36000]])
];

const FACTORY_PRESET_COUNT = factoryPresets.length;

const PRESET_STORAGE_KEY = "c1zzl3-full-dual-oscillators-envelope-presets";
const PERFORMANCE_STORAGE_KEY = "c1zzl3-full-dual-oscillators-performance-settings";
const CZ_IMPORT_HANDOFF_KEY = "c1zzl3-full-dual-oscillators-import-draft";
const CZ_IMPORT_QUEUE_KEY = "c1zzl3-full-dual-oscillators-import-queue";
const ENVELOPE_LAB_HEARTBEAT_KEY = "c1zzl3-full-dual-oscillators-envelope-lab-heartbeat";
const HANDOFF_CHANNEL_NAME = "c1zzl3-full-dual-oscillators-handoff";
const HOSTED_EDITOR_URL = "https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/index.html";
const HOSTED_IMPORT_LAB_URL = "https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/import/";
const LOCAL_IMPORT_LAB_URL = "import/index.html";

let presets = loadPresets();
let selected = 3;
let audioCtx;
let activeNodes = [];
let midiAccess;
let sendingSysex = false;
let auditionLoop = false;
let auditionToken = 0;
let dragTarget = null;
let pitchDragTarget = null;
let performanceSettings = loadPerformanceSettings();
let activeLaneView = "amp";
let activePitchLane = "pitch";
let developerMode = false;
let themeMode = loadThemeMode();
let developerLogLines = [];
let settingsProtocol = "unknown";
let settingsRequestPending = false;
let settingsRequestTimer = null;
let settingsRequestResolver = null;
let envelopeReadSession = null;
let envelopeReadTimer = null;
let envelopeReadSupported = null;
let detectedEnvelopeProtocol = null;
let auxiliaryEnvelopeRequestTimer = null;
let lastLoggedSysexAt = 0;
let messageImportedDraft = null;
let lastConsumedHandoffId = null;
const handoffChannel = typeof BroadcastChannel === "function"
  ? new BroadcastChannel(HANDOFF_CHANNEL_NAME)
  : null;
const WAVE_FAMILIES = [
  "Saw",
  "Square",
  "Narrow pulse",
  "Double sine",
  "Saw pulse",
  "Resonant saw window",
  "Resonant triangle window",
  "Resonant trapezoid window"
];

window.addEventListener("message", (event) => {
  if (event.data?.type === "cz-import-handoff" && event.data?.payload) {
    handleLiveImportedDraft(event.data.payload);
  }
});

handoffChannel?.addEventListener("message", (event) => {
  if (event.data?.type === "cz-import-handoff" && event.data?.payload) {
    handleLiveImportedDraft(event.data.payload);
  }
});

window.addEventListener("storage", (event) => {
  if (event.key !== CZ_IMPORT_QUEUE_KEY || !event.newValue) return;
  try {
    handleLiveImportedDraft(JSON.parse(event.newValue));
  } catch {
    /* Ignore unrelated or partial storage writes. */
  }
});

const el = {
  presetList: document.querySelector("#presetList"),
  addPreset: document.querySelector("#addPreset"),
  presetName: document.querySelector("#presetName"),
  pitchInput: document.querySelector("#pitchInput"),
  waveSelect: document.querySelector("#waveSelect"),
  customSlot: document.querySelector("#customSlot"),
  canvas: document.querySelector("#curveCanvas"),
  pitchCanvas: document.querySelector("#pitchCanvas"),
  pitchSourceLabel: document.querySelector("#pitchSourceLabel"),
  spreadPitchPoints: document.querySelector("#spreadPitchPoints"),
  pitch1View: document.querySelector("#pitch1View"),
  pitch2View: document.querySelector("#pitch2View"),
  pitchStageTitle: document.querySelector("#pitchStageTitle"),
  themeToggle: document.querySelector("#themeToggle"),
  onlineEditorLink: document.querySelector("#onlineEditorLink"),
  importLabLink: document.querySelector("#importLabLink"),
  ampStages: document.querySelector("#ampStages"),
  amp2Stages: document.querySelector("#amp2Stages"),
  pdStages: document.querySelector("#pdStages"),
  pd2Stages: document.querySelector("#pd2Stages"),
  pitchStages: document.querySelector("#pitchStages"),
  ampView: document.querySelector("#ampView"),
  amp2View: document.querySelector("#amp2View"),
  pdView: document.querySelector("#pdView"),
  pd2View: document.querySelector("#pd2View"),
  developerToggle: document.querySelector("#developerToggle"),
  stagePanel: document.querySelector("#stagePanel"),
  stagePanelTitle: document.querySelector("#stagePanelTitle"),
  stagePanelSubtitle: document.querySelector("#stagePanelSubtitle"),
  developerPanel: document.querySelector("#developerPanel"),
  exportText: document.querySelector("#exportText"),
  developerPorts: document.querySelector("#developerPorts"),
  developerMidiRaw: document.querySelector("#developerMidiRaw"),
  developerLog: document.querySelector("#developerLog"),
  resetBrowserState: document.querySelector("#resetBrowserState"),
  clearDeveloperLog: document.querySelector("#clearDeveloperLog"),
  status: document.querySelector("#status"),
  audition: document.querySelector("#audition"),
  stop: document.querySelector("#stop"),
  midiToggle: document.querySelector("#midiToggle"),
  pdControl: document.querySelector("#pdControl"),
  detuneControl: document.querySelector("#detuneControl"),
  performanceWaveSelect: document.querySelector("#performanceWaveSelect"),
  performanceWave2Select: document.querySelector("#performanceWave2Select"),
  midiOutput: document.querySelector("#midiOutput"),
  copyCpp: document.querySelector("#copyCpp"),
  copySysex: document.querySelector("#copySysex"),
  sendSysex: document.querySelector("#sendSysex"),
  loadEnvelopeAndSettings: document.querySelector("#loadEnvelopeAndSettings"),
  saveEnvelopeOnly: document.querySelector("#saveEnvelopeOnly"),
  flashSysex: document.querySelector("#flashSysex"),
  deleteSlot: document.querySelector("#deleteSlot"),
  requestSettings: document.querySelector("#requestSettings"),
  requestEnvelopes: document.querySelector("#requestEnvelopes"),
  sendSettings: document.querySelector("#sendSettings"),
  downloadJson: document.querySelector("#downloadJson"),
  resetPreset: document.querySelector("#resetPreset"),
  ringControl: document.querySelector("#ringControl"),
  noiseControl: document.querySelector("#noiseControl"),
  midiInChannel: document.querySelector("#midiInChannel"),
  turingRange: document.querySelector("#turingRange"),
  turingMidiOut: document.querySelector("#turingMidiOut"),
  turingMidiChannel: document.querySelector("#turingMidiChannel")
};

function preset(name, amp, pd, slot = null, cardDirty = false, pitch = null, pitchSource = null, pitch2 = null, pd2 = null, amp2 = null, sustain = null, performance = null) {
  const pitchLane = normalizePitchStages(pitch);
  const ampLane = normalizeStages(amp);
  const pdLane = normalizeStages(pd);
  return {
    name,
    amp: ampLane,
    amp2: normalizeStages(amp2 || ampLane),
    pd: pdLane,
    pd2: normalizeStages(pd2 || pdLane),
    pitch: pitchLane,
    pitch2: normalizePitchStages(pitch2 || pitchLane),
    sustain: normalizeSustainStages(sustain),
    performance: normalizePerformanceSettings(performance),
    pitchSource: pitchSource || null,
    slot: Number.isInteger(slot) ? clampInt(slot, 0, CUSTOM_SLOT_COUNT - 1) : null,
    cardDirty: Boolean(cardDirty)
  };
}

function fill(level, time) {
  return Array.from({ length: STAGES }, () => [level, time]);
}

function defaultCustomPreset() {
  return preset("Custom preset",
    [[0, 6000], [4095, 6000], [3200, 9000], [2100, 9000], [1400, 9000], [900, 6000], [400, 6000], [0, 3000]],
    [[0, 6000], [1800, 6000], [900, 9000], [2300, 9000], [1200, 9000], [700, 6000], [300, 6000], [0, 3000]]);
}

function normalizeStages(stages) {
  return Array.from({ length: STAGES }, (_, i) => ({
    level: clampInt(stages[i]?.level ?? stages[i]?.[0] ?? 0, 0, MAX_LEVEL),
    time: clampInt(stages[i]?.time ?? stages[i]?.[1] ?? 1, 1, 192000)
  }));
}

function normalizePitchStages(stages) {
  return Array.from({ length: STAGES }, (_, i) => ({
    level: clampInt(stages?.[i]?.level ?? stages?.[i]?.[0] ?? 2048, 0, MAX_LEVEL),
    time: clampInt(stages?.[i]?.time ?? stages?.[i]?.[1] ?? 1, 1, 192000)
  }));
}

function normalizeSustainStages(sustain) {
  return Array.from({ length: 6 }, (_, index) => {
    const value = Array.isArray(sustain) ? sustain[index] : undefined;
    const stage = Number(value);
    return Number.isInteger(stage) && stage >= 0 && stage < STAGES ? stage : 0x7f;
  });
}

function envelopeMaxSamples(presetItem) {
  return Math.max(
    totalSamples(normalizeStages(presetItem.amp)),
    totalSamples(normalizeStages(presetItem.amp2 || presetItem.amp)),
    totalSamples(normalizeStages(presetItem.pd)),
    totalSamples(normalizeStages(presetItem.pd2 || presetItem.pd)),
    1
  );
}

function constrainPitchToEnvelope(presetItem) {
  const maxSamples = envelopeMaxSamples(presetItem);
  presetItem.pitch = constrainPitchLaneToSamples(presetItem.pitch, maxSamples);
  presetItem.pitch2 = constrainPitchLaneToSamples(presetItem.pitch2 || presetItem.pitch, maxSamples);
  return presetItem;
}

function constrainPitchLaneToSamples(stages, maxSamples) {
  let remaining = maxSamples;
  return normalizePitchStages(stages).map((stage, index) => {
    const stagesLeft = STAGES - index - 1;
    const reserve = stagesLeft;
    const time = Math.max(1, Math.min(stage.time, Math.max(1, remaining - reserve)));
    remaining = Math.max(0, remaining - time);
    return {
      level: remaining > 0 || index === 0 ? stage.level : 2048,
      time
    };
  });
}

function pitchLaneLabel(lane = activePitchLane) {
  return lane === "pitch2" ? "Pitch 2 / Oscillator 2" : "Pitch 1 / Oscillator 1";
}

function mainLaneLabel(lane = activeLaneView) {
  if (lane === "amp2") return "Amp 2 / Oscillator 2";
  if (lane === "pd") return "PD 1 / Oscillator 1";
  if (lane === "pd2") return "PD 2 / Oscillator 2";
  return "Amp 1 / Oscillator 1";
}

function activePitchStages(current = presets[selected]) {
  return current[activePitchLane] || current.pitch;
}

function spreadPitchPoints() {
  if (selected < FACTORY_PRESET_COUNT) {
    if (!duplicateFactoryPreset()) return;
  }

  const current = presets[selected];
  const maxSamples = envelopeMaxSamples(current);
  const baseTime = Math.max(1, Math.floor(maxSamples / STAGES));
  let remainder = Math.max(0, maxSamples - (baseTime * STAGES));

  const lane = activePitchLane;
  current[lane] = normalizePitchStages(current[lane] || current.pitch).map((stage) => {
    const extra = remainder > 0 ? 1 : 0;
    if (remainder > 0) remainder--;
    return { level: stage.level, time: baseTime + extra };
  });
  constrainPitchToEnvelope(current);
  if (current.slot !== null) current.cardDirty = true;
  savePresets();
  render();
  setStatus(`${pitchLaneLabel(lane)} points spread across the current amplitude/PD envelope length.`);
}

function loadPresets() {
  try {
    const saved = JSON.parse(localStorage.getItem(PRESET_STORAGE_KEY));
    if (Array.isArray(saved)) {
      const custom = saved
        .slice(FACTORY_PRESET_COUNT)
        .map((item) => constrainPitchToEnvelope(preset(item.name || "Custom preset", item.amp, item.pd, item.slot, item.cardDirty, item.pitch, item.pitchSource, item.pitch2, item.pd2, item.amp2, item.sustain, item.performance)))
        .slice(0, MAX_BROWSER_CUSTOM_PRESETS);
      return [...structuredClone(factoryPresets), ...custom];
    }
    if (Array.isArray(saved?.customPresets)) {
      const custom = saved.customPresets
        .map((item) => constrainPitchToEnvelope(preset(item.name || "Custom preset", item.amp, item.pd, item.slot, item.cardDirty, item.pitch, item.pitchSource, item.pitch2, item.pd2, item.amp2, item.sustain, item.performance)))
        .slice(0, MAX_BROWSER_CUSTOM_PRESETS);
      return [...structuredClone(factoryPresets), ...custom];
    }
  } catch {
    /* Keep factory presets if saved data is malformed. */
  }
  return structuredClone(factoryPresets);
}

function savePresets() {
  localStorage.setItem(PRESET_STORAGE_KEY, JSON.stringify({
    customPresets: presets
      .slice(FACTORY_PRESET_COUNT, FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS)
      .map((item) => ({
        name: item.name,
        amp: item.amp,
        amp2: item.amp2 || item.amp,
        pd: item.pd,
        pd2: item.pd2 || item.pd,
        pitch: item.pitch,
        pitch2: item.pitch2,
        sustain: normalizeSustainStages(item.sustain),
        performance: normalizePerformanceSettings(item.performance),
        pitchSource: item.pitchSource || null,
        slot: Number.isInteger(item.slot) ? item.slot : null,
        cardDirty: Boolean(item.cardDirty)
      }))
  }));
}

function loadPerformanceSettings() {
  try {
    const saved = JSON.parse(localStorage.getItem(PERFORMANCE_STORAGE_KEY));
    if (saved && typeof saved === "object") {
      return {
        pd: clampInt(saved.pd ?? 0, 0, MAX_LEVEL),
        detune: clampInt(saved.detune ?? 2048, 0, MAX_LEVEL),
        waveform: clampInt(saved.waveform ?? 0, 0, 7),
        waveform2: clampInt(saved.waveform2 ?? saved.waveform ?? 0, 0, 7),
        ring: clampInt(saved.ring, 0, MAX_LEVEL),
        noise: clampInt(saved.noise, 0, MAX_LEVEL),
        midiInChannel: clampInt(saved.midiInChannel, 1, 16),
        turingRange: clampInt(saved.turingRange ?? 2, 1, 8),
        turingMidiOut: saved.turingMidiOut === true,
        turingMidiChannel: clampInt(saved.turingMidiChannel ?? 1, 1, 16)
      };
    }
  } catch {
    /* Keep defaults if saved data is malformed. */
  }

  return {
    pd: 0,
    detune: 2048,
    waveform: 0,
    waveform2: 0,
    ring: 0,
    noise: 0,
    midiInChannel: 1,
    turingRange: 2,
    turingMidiOut: false,
    turingMidiChannel: 1
  };
}

function normalizePerformanceSettings(settings = null) {
  if (!settings || typeof settings !== "object") return null;
  return {
    pd: clampInt(settings.pd ?? 0, 0, MAX_LEVEL),
    detune: clampInt(settings.detune ?? 2048, 0, MAX_LEVEL),
    waveform: clampInt(settings.waveform ?? 0, 0, 7),
    waveform2: clampInt(settings.waveform2 ?? settings.waveform ?? 0, 0, 7),
    ring: clampInt(settings.ring ?? 0, 0, MAX_LEVEL),
    noise: clampInt(settings.noise ?? 0, 0, MAX_LEVEL),
    midiInChannel: clampInt(settings.midiInChannel ?? 1, 1, 16),
    turingRange: clampInt(settings.turingRange ?? 2, 1, 8),
    turingMidiOut: settings.turingMidiOut === true,
    turingMidiChannel: clampInt(settings.turingMidiChannel ?? 1, 1, 16)
  };
}

function applyPerformanceSettings(settings) {
  const next = normalizePerformanceSettings(settings);
  if (!next) return false;
  performanceSettings = next;
  savePerformanceSettings();
  renderPerformanceSettings();
  return true;
}

function savePerformanceSettings() {
  localStorage.setItem(PERFORMANCE_STORAGE_KEY, JSON.stringify(performanceSettings));
}

function resetBrowserState() {
  localStorage.removeItem(PRESET_STORAGE_KEY);
  localStorage.removeItem(PERFORMANCE_STORAGE_KEY);
  localStorage.removeItem(CZ_IMPORT_HANDOFF_KEY);
  setStatus("Browser state cleared. Reloading baseline editor...");
  window.setTimeout(() => window.location.reload(), 450);
}

function loadThemeMode() {
  try {
    const saved = localStorage.getItem("c1zzl3-theme-mode");
    if (saved === "light" || saved === "dark") return saved;
  } catch {
    /* Keep default theme if saved data is unavailable. */
  }
  return "dark";
}

function saveThemeMode() {
  localStorage.setItem("c1zzl3-theme-mode", themeMode);
}

function clearImportedDraftSources() {
  messageImportedDraft = null;
  localStorage.removeItem(CZ_IMPORT_HANDOFF_KEY);
  localStorage.removeItem(CZ_IMPORT_QUEUE_KEY);
  window.history.replaceState(null, "", window.location.pathname);
}

function markEnvelopeLabActive() {
  try {
    localStorage.setItem(ENVELOPE_LAB_HEARTBEAT_KEY, String(Date.now()));
  } catch {
    /* Heartbeat is only used to avoid reloading an already-open lab. */
  }
}

function importedPitchStages(draft) {
  const directPitch = draft?.pitch || draft?.sourceEnvelopes?.pitch;
  return Array.isArray(directPitch) ? directPitch : null;
}

function importedPitch2Stages(draft) {
  const directPitch = draft?.pitch2 || draft?.sourceEnvelopes?.pitch2;
  return Array.isArray(directPitch) ? directPitch : importedPitchStages(draft);
}

function importedPd2Stages(draft) {
  const directPd = draft?.pd2 || draft?.sourceEnvelopes?.pd2;
  return Array.isArray(directPd) ? directPd : draft?.pd;
}

function importedPitchSource(draft) {
  const cz = draft?.sourceEnvelopes?.cz;
  if (!cz) return null;
  return {
    dco1Pitch: cz.dco1Pitch || null,
    dco2Pitch: cz.dco2Pitch || null,
    pitchMapping: draft?.sourceEnvelopes?.pitchMapping || null,
    pitchAlternatives: draft?.sourceEnvelopes?.pitchAlternatives || null
  };
}

function makeImportedPreset(draft) {
  return preset(
    draft.name || "Imported CZ patch",
    draft.amp,
    draft.pd,
    null,
    false,
    importedPitchStages(draft),
    importedPitchSource(draft),
    importedPitch2Stages(draft),
    importedPd2Stages(draft),
    draft.amp2 || draft.sourceEnvelopes?.amp2 || draft.amp,
    draft.sustain || draft.sourceEnvelopes?.sustain
  );
}

function applyImportedPerformanceSettings(draft) {
  const performance = draft?.performance;
  if (!performance || typeof performance !== "object") return false;

  const nextSettings = { ...performanceSettings };
  if (performance.pd != null) nextSettings.pd = clampInt(performance.pd, 0, MAX_LEVEL);
  if (performance.detune != null) nextSettings.detune = clampInt(performance.detune, 0, MAX_LEVEL);
  if (performance.waveform != null) nextSettings.waveform = clampInt(performance.waveform, 0, 7);
  else if (draft?.wave?.value != null) nextSettings.waveform = clampInt(draft.wave.value, 0, 7);
  if (performance.waveform2 != null) nextSettings.waveform2 = clampInt(performance.waveform2, 0, 7);
  else nextSettings.waveform2 = nextSettings.waveform;
  if (performance.ring != null) nextSettings.ring = clampInt(performance.ring, 0, MAX_LEVEL);
  if (performance.noise != null) nextSettings.noise = clampInt(performance.noise, 0, MAX_LEVEL);

  performanceSettings = nextSettings;
  savePerformanceSettings();
  return true;
}

function addImportedDraft(draft) {
  if (!draft || !Array.isArray(draft.amp) || !Array.isArray(draft.pd)) return false;
  const imported = makeImportedPreset(draft);
  if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
    clearImportedDraftSources();
    setStatus(`Browser draft limit reached (${MAX_BROWSER_CUSTOM_PRESETS}). Delete an envelope draft before importing another CZ patch.`);
    return false;
  } else {
    presets.push(imported);
    selected = presets.length - 1;
  }

  applyImportedPerformanceSettings(draft);
  savePresets();
  clearImportedDraftSources();
  return true;
}

function handleLiveImportedDraft(payload) {
  const handoffId = payload?.id || payload?.createdAt || null;
  if (!payload || handoffId === lastConsumedHandoffId) return false;
  messageImportedDraft = payload;
  if (!consumeImportedDraft()) return false;
  lastConsumedHandoffId = handoffId;
  render();
  setStatus("Imported CZ draft added without reloading the Envelope Lab.");
  return true;
}

function consumeImportedDraft() {
  try {
    if (messageImportedDraft) {
      const payload = messageImportedDraft;
      const handoffId = payload?.id || payload?.createdAt || null;
      if (handoffId && handoffId === lastConsumedHandoffId) return false;
      const draft = payload?.draft;
      if (addImportedDraft(draft)) {
        lastConsumedHandoffId = handoffId;
        return true;
      }
    }

    const params = new URLSearchParams(window.location.search);
    const queryPayload = params.get("cz-import");
    if (queryPayload) {
      const payload = JSON.parse(decodeURIComponent(queryPayload));
      const draft = payload?.draft;
      if (addImportedDraft(draft)) return true;
    }

    const hashMatch = window.location.hash.match(/(?:^|&)cz-import=([^&]+)/);
    if (hashMatch) {
      const payload = JSON.parse(decodeURIComponent(hashMatch[1]));
      const draft = payload?.draft;
      if (addImportedDraft(draft)) return true;
    }

    const raw = localStorage.getItem(CZ_IMPORT_HANDOFF_KEY);
    const queueRaw = localStorage.getItem(CZ_IMPORT_QUEUE_KEY);
    if (!raw && !queueRaw) return false;

    const payload = JSON.parse(raw || queueRaw);
    const handoffId = payload?.id || payload?.createdAt || null;
    if (handoffId && handoffId === lastConsumedHandoffId) return false;
    const draft = payload?.draft;
    if (!draft || !Array.isArray(draft.amp) || !Array.isArray(draft.pd)) {
      localStorage.removeItem(CZ_IMPORT_HANDOFF_KEY);
      localStorage.removeItem(CZ_IMPORT_QUEUE_KEY);
      return false;
    }

    if (!addImportedDraft(draft)) return false;
    lastConsumedHandoffId = handoffId;
    return true;
  } catch {
    localStorage.removeItem(CZ_IMPORT_HANDOFF_KEY);
    localStorage.removeItem(CZ_IMPORT_QUEUE_KEY);
    return false;
  }
}

function render() {
  selected = Math.max(0, Math.min(selected, presets.length - 1));
  const current = presets[selected];
  constrainPitchToEnvelope(current);

  renderPresetList();
  el.presetName.value = current.name;
  el.presetName.disabled = selected < FACTORY_PRESET_COUNT;
  el.addPreset.disabled = customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS;
  renderStages("amp", el.ampStages);
  renderStages("amp2", el.amp2Stages);
  renderStages("pd", el.pdStages);
  renderStages("pd2", el.pd2Stages);
  renderStages(activePitchLane, el.pitchStages);
  renderPitchSource(current);
  renderPerformanceSettings();
  renderThemeMode();
  renderLaneView();
  renderPitchLaneView();
  renderDeveloperMode();
  drawCurves();
  drawPitchCurve();
  updateExport();
}

function renderThemeMode() {
  document.body.classList.toggle("theme-light", themeMode === "light");
  el.themeToggle.classList.toggle("is-active", themeMode === "light");
  el.themeToggle.textContent = themeMode === "light" ? "Light" : "Dark";
  el.themeToggle.setAttribute("aria-checked", String(themeMode === "light"));
  if (el.onlineEditorLink) {
    const host = window.location.hostname;
    const isLocalDev = host === "localhost" || host === "127.0.0.1" || host === "::1";
    el.onlineEditorLink.classList.toggle("is-active", !isLocalDev && window.location.href.startsWith(HOSTED_EDITOR_URL));
  }
  if (el.importLabLink) {
    const host = window.location.hostname;
    const isLocalDev = host === "localhost" || host === "127.0.0.1" || host === "::1";
    el.importLabLink.href = isLocalDev ? LOCAL_IMPORT_LAB_URL : HOSTED_IMPORT_LAB_URL;
  }
  drawCurves();
  drawPitchCurve();
}

function renderLaneView() {
  const ampActive = activeLaneView === "amp";
  const amp2Active = activeLaneView === "amp2";
  const pd1Active = activeLaneView === "pd";
  const pd2Active = activeLaneView === "pd2";
  el.ampView.classList.toggle("is-active", ampActive);
  el.ampView.setAttribute("aria-selected", String(ampActive));
  el.amp2View.classList.toggle("is-active", amp2Active);
  el.amp2View.setAttribute("aria-selected", String(amp2Active));
  el.pdView.classList.toggle("is-active", pd1Active);
  el.pdView.setAttribute("aria-selected", String(pd1Active));
  el.pd2View.classList.toggle("is-active", pd2Active);
  el.pd2View.setAttribute("aria-selected", String(pd2Active));
  el.stagePanelTitle.textContent = mainLaneLabel(activeLaneView);
  el.stagePanelSubtitle.textContent = "Level / samples";
  el.ampStages.classList.toggle("is-hidden", !ampActive);
  el.amp2Stages.classList.toggle("is-hidden", !amp2Active);
  el.pdStages.classList.toggle("is-hidden", !pd1Active);
  el.pd2Stages.classList.toggle("is-hidden", !pd2Active);
  el.ampStages.setAttribute("aria-hidden", String(!ampActive));
  el.amp2Stages.setAttribute("aria-hidden", String(!amp2Active));
  el.pdStages.setAttribute("aria-hidden", String(!pd1Active));
  el.pd2Stages.setAttribute("aria-hidden", String(!pd2Active));
}

function renderPitchLaneView() {
  const pitch1Active = activePitchLane === "pitch";
  el.pitch1View.classList.toggle("is-active", pitch1Active);
  el.pitch1View.setAttribute("aria-selected", String(pitch1Active));
  el.pitch2View.classList.toggle("is-active", !pitch1Active);
  el.pitch2View.setAttribute("aria-selected", String(!pitch1Active));
  el.pitchStageTitle.textContent = pitchLaneLabel(activePitchLane);
}

function renderPitchSource(current) {
  if (!el.pitchSourceLabel) return;
  const hasSource = Boolean(current.pitchSource?.dco1Pitch || current.pitchSource?.dco2Pitch);
  const mappingLabel = current.pitchSource?.pitchMapping?.label;
  el.pitchSourceLabel.textContent = hasSource
    ? `CZ pitch source: ${mappingLabel || "DCO1/DCO2 loaded"}`
    : "No CZ pitch source loaded";
}

function renderDeveloperMode() {
  el.developerToggle.classList.toggle("is-active", developerMode);
  el.developerToggle.setAttribute("aria-checked", String(developerMode));
  el.developerToggle.textContent = developerMode ? "Developer tools: On" : "Developer tools: Off";
  el.developerPanel.classList.toggle("is-hidden", !developerMode);
  renderDeveloperPorts();
  renderDeveloperMidiRaw();
  renderDeveloperLog();
}

function renderPerformanceSettings() {
  el.pdControl.value = performanceSettings.pd;
  el.detuneControl.value = performanceSettings.detune;
  el.performanceWaveSelect.value = performanceSettings.waveform;
  el.performanceWave2Select.value = performanceSettings.waveform2;
  el.ringControl.value = performanceSettings.ring;
  el.noiseControl.value = performanceSettings.noise;
  el.midiInChannel.value = performanceSettings.midiInChannel;
  el.turingRange.value = performanceSettings.turingRange;
  el.turingMidiOut.checked = performanceSettings.turingMidiOut;
  el.turingMidiChannel.value = performanceSettings.turingMidiChannel;
}

function renderPresetList() {
  el.presetList.innerHTML = "";
  presets.forEach((item, index) => {
    const row = document.createElement("div");
    row.className = "preset-row";
    const button = document.createElement("button");
    button.className = `preset-button${index === selected ? " is-active" : ""}`;
    button.type = "button";
    if (index < FACTORY_PRESET_COUNT) {
      button.innerHTML = `<span class="preset-name">${escapeHtml(item.name)}</span><span class="preset-code">${index}</span>`;
    } else {
      const status = item.slot === null
        ? { label: "Local only", className: "is-local" }
        : item.cardDirty
          ? { label: `Changed - slot ${item.slot + 1}`, className: "is-changed" }
          : { label: `Saved - slot ${item.slot + 1}`, className: "is-saved" };
      button.innerHTML = `
        <span class="preset-name">${escapeHtml(item.name)}</span>
        <span class="preset-meta">
          <span class="preset-state ${status.className}">${status.label}</span>
          <span class="preset-code">C${index - FACTORY_PRESET_COUNT + 1}</span>
        </span>`;
      button.title = status.label;
    }
    button.addEventListener("click", () => {
      selected = index;
      if (Number.isInteger(item.slot)) el.customSlot.value = String(item.slot);
      render();
    });
    row.append(button);

    if (index >= FACTORY_PRESET_COUNT) {
      const remove = document.createElement("button");
      remove.className = "preset-remove";
      remove.type = "button";
      remove.title = "Remove custom preset";
      remove.textContent = "×";
      remove.addEventListener("click", () => removeCustomPreset(index));
      row.append(remove);
    }

    el.presetList.append(row);
  });
}

function customPresetCount() {
  return Math.max(0, presets.length - FACTORY_PRESET_COUNT);
}

function removeCustomPreset(index) {
  if (index < FACTORY_PRESET_COUNT) return;
  presets.splice(index, 1);
  selected = Math.min(Math.max(1, index - 1), presets.length - 1);
  savePresets();
  render();
  setStatus("Custom preset removed.");
}

function bindSelectedPresetToSlot(slot) {
  if (selected < FACTORY_PRESET_COUNT || !presets[selected]) return;

  presets.forEach((item, index) => {
    if (index >= FACTORY_PRESET_COUNT && item.slot === slot) {
      item.slot = null;
    }
  });

  presets[selected].slot = slot;
  presets[selected].cardDirty = false;
  savePresets();
  renderPresetList();
}

function renderStages(lane, container) {
  container.innerHTML = "";
  presets[selected][lane].forEach((stage, index) => {
    const row = document.createElement("div");
    row.className = "stage-row";
    row.dataset.lane = lane;
    row.dataset.index = String(index);
    row.innerHTML = `
      <strong>${index + 1}</strong>
      <input type="number" min="0" max="${MAX_LEVEL}" value="${stage.level}" aria-label="${lane} stage ${index + 1} level">
      <input type="number" min="1" max="192000" value="${stage.time}" aria-label="${lane} stage ${index + 1} time">
    `;
    const [levelInput, timeInput] = row.querySelectorAll("input");
    levelInput.addEventListener("input", () => updateStage(lane, index, "level", levelInput.value));
    timeInput.addEventListener("input", () => updateStage(lane, index, "time", timeInput.value));
    container.append(row);
  });
}

function updateStage(lane, index, key, value) {
  if (selected < FACTORY_PRESET_COUNT) {
    if (!duplicateFactoryPreset()) return;
  }

  const current = presets[selected];
  const before = key === "time" ? samplesBeforeStage(current[lane], index) : 0;
  const isPitchLane = lane === "pitch" || lane === "pitch2";
  const pitchMax = isPitchLane && key === "time"
    ? Math.max(1, envelopeMaxSamples(current) - before)
    : 192000;
  const max = key === "level" ? MAX_LEVEL : pitchMax;
  const min = key === "level" ? 0 : 1;
  current[lane][index][key] = clampInt(value, min, max);
  constrainPitchToEnvelope(current);
  if (current.slot !== null) current.cardDirty = true;
  savePresets();
  renderPresetList();
  syncStageInputs(lane, index);
  if (isPitchLane) drawPitchCurve();
  else {
    drawCurves();
    drawPitchCurve();
    renderStages(activePitchLane, el.pitchStages);
  }
  updateExport();
}

function duplicateFactoryPreset() {
  if (selected >= FACTORY_PRESET_COUNT) return true;
  if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
    setStatus("Custom preset limit reached.");
    return false;
  }

  const source = presets[selected];
  presets.push(preset(`${source.name} copy`, source.amp, source.pd, null, false, source.pitch, source.pitchSource, source.pitch2, source.pd2, source.amp2, source.sustain, source.performance));
  selected = presets.length - 1;
  savePresets();
  render();
  return true;
}

function drawCurves() {
  const ctx = el.canvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const rect = el.canvas.getBoundingClientRect();
  el.canvas.width = Math.max(640, Math.floor(rect.width * dpr));
  el.canvas.height = Math.max(260, Math.floor(rect.height * dpr));
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  const theme = getThemeColors();

  const w = rect.width;
  const h = rect.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = theme.canvasBg;
  ctx.fillRect(0, 0, w, h);

  ctx.strokeStyle = theme.grid;
  ctx.lineWidth = 1;
  for (let i = 1; i < 4; i++) {
    const y = (h * i) / 4;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  const viewSamples = editorViewSamples();
  drawLane(ctx, presets[selected].amp2 || presets[selected].amp, theme.amp2, w, h, viewSamples);
  drawLane(ctx, presets[selected].pd2 || presets[selected].pd, theme.pd2, w, h, viewSamples);
  drawLane(ctx, presets[selected].amp, theme.amp, w, h, viewSamples);
  drawLane(ctx, presets[selected].pd, theme.pd, w, h, viewSamples);
  drawHandles(ctx, presets[selected].amp2 || presets[selected].amp, theme.amp2, w, h, viewSamples, activeLaneView === "amp2", theme);
  drawHandles(ctx, presets[selected].pd2 || presets[selected].pd, theme.pd2, w, h, viewSamples, activeLaneView === "pd2", theme);
  drawHandles(ctx, presets[selected].amp, theme.amp, w, h, viewSamples, activeLaneView === "amp", theme);
  drawHandles(ctx, presets[selected].pd, theme.pd, w, h, viewSamples, activeLaneView === "pd", theme);

  ctx.fillStyle = theme.amp;
  ctx.font = "12px system-ui";
  ctx.fillText(`Amp1`, 14, 22);
  ctx.fillStyle = theme.amp2;
  ctx.fillText(`Amp2`, 62, 22);
  ctx.fillStyle = theme.pd;
  ctx.fillText(`PD1`, 112, 22);
  ctx.fillStyle = theme.pd2;
  ctx.fillText(`PD2`, 156, 22);
  ctx.fillStyle = theme.muted;
  ctx.fillText(`${(totalSamples(presets[selected].amp) / SAMPLE_RATE).toFixed(2)}s / ${(viewSamples / SAMPLE_RATE).toFixed(0)}s`, w - 94, h - 16);
}

function drawPitchCurve() {
  if (!el.pitchCanvas) return;
  const ctx = el.pitchCanvas.getContext("2d");
  const dpr = window.devicePixelRatio || 1;
  const rect = el.pitchCanvas.getBoundingClientRect();
  el.pitchCanvas.width = Math.max(640, Math.floor(rect.width * dpr));
  el.pitchCanvas.height = Math.max(180, Math.floor(rect.height * dpr));
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

  const theme = getThemeColors();
  const w = rect.width;
  const h = rect.height;
  const centerY = h / 2;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = theme.canvasBg;
  ctx.fillRect(0, 0, w, h);

  ctx.strokeStyle = theme.grid;
  ctx.lineWidth = 1;
  [0.25, 0.5, 0.75].forEach((ratio) => {
    const y = h * ratio;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  });

  ctx.strokeStyle = theme.pitchCenter;
  ctx.lineWidth = 2;
  ctx.setLineDash([6, 6]);
  ctx.beginPath();
  ctx.moveTo(0, centerY);
  ctx.lineTo(w, centerY);
  ctx.stroke();
  ctx.setLineDash([]);

  const current = presets[selected];
  const viewSamples = envelopeMaxSamples(current);
  const pitch2Color = theme.pitch2 || theme.pd;
  drawLane(ctx, current.pitch2 || current.pitch, pitch2Color, w, h, viewSamples);
  drawLane(ctx, current.pitch, theme.pitch, w, h, viewSamples);
  drawHandles(ctx, current.pitch2 || current.pitch, pitch2Color, w, h, viewSamples, activePitchLane === "pitch2", theme);
  drawHandles(ctx, current.pitch, theme.pitch, w, h, viewSamples, activePitchLane === "pitch", theme);

  ctx.fillStyle = theme.pitch;
  ctx.font = "12px system-ui";
  ctx.fillText("Pitch 1", 14, 22);
  ctx.fillStyle = pitch2Color;
  ctx.fillText("Pitch 2", 74, 22);
  ctx.fillStyle = theme.muted;
  ctx.fillText("center = no pitch offset", 136, 22);
  ctx.fillText(`${pitchLaneLabel(activePitchLane)} ${(totalSamples(activePitchStages(current)) / SAMPLE_RATE).toFixed(2)}s / max ${(viewSamples / SAMPLE_RATE).toFixed(2)}s`, w - 238, h - 16);
}

function drawHandles(ctx, stages, color, w, h, viewSamples, active, theme) {
  let x = 0;
  const points = [];
  stages.forEach((stage) => {
    x = Math.min(w, x + (stage.time / viewSamples) * w);
    const y = levelY(stage.level, h);
    points.push({ x, y });
  });

  points.forEach((point, index) => {
    const pointColor = color === theme.amp
      ? theme.ampPoint
      : color === theme.amp2
        ? theme.amp2Point
      : color === theme.pd2
        ? theme.pd2Point
      : color === theme.pitch
        ? theme.pitchPoint
        : color === theme.pitch2
          ? theme.pitch2Point
        : theme.pdPoint;
    ctx.fillStyle = pointColor;
    ctx.beginPath();
    ctx.arc(point.x, point.y, active ? 7 : 5, 0, Math.PI * 2);
    ctx.fill();
    ctx.lineWidth = active ? 2 : 1;
    ctx.strokeStyle = theme.canvasBg;
    ctx.stroke();

    ctx.save();
    ctx.font = "bold 11px system-ui";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    const labelY = Math.max(12, point.y - 14);
    ctx.fillStyle = theme.canvasBg;
    ctx.fillText(String(index + 1), point.x + 1, labelY + 1);
    ctx.fillStyle = theme.text;
    ctx.fillText(String(index + 1), point.x, labelY);
    ctx.restore();
  });
}

function getThemeColors() {
  const styles = getComputedStyle(document.body);
  return {
    canvasBg: styles.getPropertyValue("--canvas-bg").trim() || "#0c0e10",
    grid: styles.getPropertyValue("--line").trim() || "#242932",
    amp: styles.getPropertyValue("--graph-amp").trim() || styles.getPropertyValue("--amp").trim() || "#ffcc66",
    amp2: styles.getPropertyValue("--graph-amp-2").trim() || styles.getPropertyValue("--graph-amp").trim() || "#ffd84d",
    pd: styles.getPropertyValue("--graph-pd").trim() || styles.getPropertyValue("--pd").trim() || "#6ee7c8",
    pd2: styles.getPropertyValue("--graph-pd-2").trim() || "#ffb347",
    pitch: styles.getPropertyValue("--graph-pitch").trim() || "#b38cff",
    pitch2: styles.getPropertyValue("--graph-pitch-2").trim() || styles.getPropertyValue("--graph-pd").trim() || "#49d7ff",
    ampPoint: styles.getPropertyValue("--graph-amp-point").trim() || styles.getPropertyValue("--amp").trim() || "#ffcc66",
    amp2Point: styles.getPropertyValue("--graph-amp-2-point").trim() || styles.getPropertyValue("--graph-amp-2").trim() || "#fff0a8",
    pdPoint: styles.getPropertyValue("--graph-pd-point").trim() || styles.getPropertyValue("--pd").trim() || "#6ee7c8",
    pd2Point: styles.getPropertyValue("--graph-pd-2-point").trim() || styles.getPropertyValue("--graph-pd-2").trim() || "#ffe0ad",
    pitchPoint: styles.getPropertyValue("--graph-pitch-point").trim() || styles.getPropertyValue("--graph-pitch").trim() || "#d8c7ff",
    pitch2Point: styles.getPropertyValue("--graph-pitch-2-point").trim() || styles.getPropertyValue("--graph-pitch-2").trim() || "#b7efff",
    pitchCenter: styles.getPropertyValue("--graph-pitch-center").trim() || styles.getPropertyValue("--muted").trim() || "#a5adba",
    muted: styles.getPropertyValue("--muted").trim() || "#a5adba",
    text: styles.getPropertyValue("--text").trim() || "#f4f0e8"
  };
}

function drawLane(ctx, stages, color, w, h, viewSamples) {
  let x = 0;
  let y = levelY(0, h);

  ctx.strokeStyle = color;
  ctx.lineWidth = 3;
  ctx.beginPath();
  ctx.moveTo(x, y);

  stages.forEach((stage) => {
    x = Math.min(w, x + (stage.time / viewSamples) * w);
    y = levelY(stage.level, h);
    ctx.lineTo(x, y);
  });

  ctx.stroke();
}

function levelY(level, height) {
  return 18 + (1 - level / MAX_LEVEL) * (height - 36);
}

function totalSamples(stages) {
  return stages.reduce((sum, stage) => sum + stage.time, 0);
}

function editorViewSamples() {
  const current = presets[selected];
  const longest = Math.max(
    EDITOR_VIEW_SAMPLES,
    totalSamples(current.amp),
    totalSamples(current.amp2 || current.amp),
    totalSamples(current.pd),
    totalSamples(current.pd2 || current.pd),
    totalSamples(current.pitch),
    totalSamples(current.pitch2 || current.pitch));
  return Math.ceil(longest / SAMPLE_RATE) * SAMPLE_RATE;
}

function samplesBeforeStage(stages, index) {
  return stages.slice(0, index).reduce((sum, stage) => sum + stage.time, 0);
}

function updateExport() {
  if (!developerMode) {
    el.exportText.value = "";
    return;
  }
  const current = presets[selected];
  const className = cppName(current.name);
  el.exportText.value = `case EnvelopePreset::${className}:\n    return {{\n${cppLane(current.amp)}\n    }, {\n${cppLane(current.pd)}\n    }, {\n${cppLane(current.pitch)}\n    }, {\n${cppLane(current.pitch2 || current.pitch)}\n    }, {\n${cppLane(current.pd2 || current.pd)}\n    }, {\n${cppLane(current.amp2 || current.amp)}\n    }};`;
}

function cppLane(stages) {
  const rows = [];
  for (let i = 0; i < STAGES; i += 4) {
    rows.push("        " + stages.slice(i, i + 4).map((stage) => `{${stage.level}, ${stage.time}}`).join(", "));
  }
  return rows.join(",\n");
}

function cppName(name) {
  const cleaned = name.replace(/[^a-z0-9]+/gi, " ").trim();
  return cleaned ? cleaned.split(" ").map((part) => part[0].toUpperCase() + part.slice(1)).join("") : "CustomPreset";
}

async function audition(note = Number(el.pitchInput.value)) {
  stopAudio();
  auditionLoop = true;
  const token = ++auditionToken;
  audioCtx ||= new AudioContext();
  await audioCtx.resume();

  playAudition(note, token);
}

function playAudition(note, token) {
  if (!auditionLoop || token !== auditionToken) return;

  const now = audioCtx.currentTime;
  const current = presets[selected];
  const osc = audioCtx.createOscillator();
  const gain = audioCtx.createGain();
  const filter = audioCtx.createBiquadFilter();
  const baseFrequency = midiToHz(note);

  osc.type = auditionOscType(el.waveSelect.value);
  osc.frequency.value = baseFrequency;
  filter.type = "lowpass";
  filter.frequency.value = 220;
  gain.gain.value = 0;

  const duration = scaledSamples(longestAuditionLane(current)) / SAMPLE_RATE;
  scheduleEnvelope(gain.gain, current.amp, now, 0, 0.35);
  scheduleEnvelope(filter.frequency, current.pd, now, 220, 4200);
  schedulePitchEnvelope(osc.frequency, current.pitch, now, baseFrequency);

  osc.connect(filter).connect(gain).connect(audioCtx.destination);
  osc.start(now);
  osc.stop(now + duration + 0.15);
  osc.onended = () => {
    if (auditionLoop && token === auditionToken) playAudition(note, token);
  };
  activeNodes = [osc, gain, filter];
  setStatus(`Looping browser audition for ${current.name} at MIDI note ${note}.`);
}

function auditionOscType(waveIndex) {
  const index = clampInt(waveIndex, 0, 7);
  if (index === 0 || index === 4 || index === 5) return "sawtooth";
  if (index === 1 || index === 2) return "square";
  if (index === 3 || index === 6 || index === 7) return "triangle";
  return "sine";
}

function scheduleEnvelope(param, stages, startTime, base, depth) {
  let t = startTime;
  let lastValue = base;
  param.cancelScheduledValues(startTime);
  param.setValueAtTime(lastValue, startTime);

  stages.forEach((stage) => {
    t += (stage.time * AUDITION_TIME_SCALE) / SAMPLE_RATE;
    lastValue = base + (stage.level / MAX_LEVEL) * depth;
    param.linearRampToValueAtTime(lastValue, t);
  });
}

function schedulePitchEnvelope(param, stages, startTime, baseFrequency) {
  let t = startTime;
  param.cancelScheduledValues(startTime);
  param.setValueAtTime(baseFrequency, startTime);

  stages.forEach((stage) => {
    t += (stage.time * AUDITION_TIME_SCALE) / SAMPLE_RATE;
    const semitoneOffset = ((stage.level - 2048) / 2047) * 24;
    param.linearRampToValueAtTime(baseFrequency * Math.pow(2, semitoneOffset / 12), t);
  });
}

function stopAudio() {
  auditionLoop = false;
  auditionToken++;
  activeNodes.forEach((node) => {
    try {
      node.stop?.();
      node.disconnect?.();
    } catch {
      /* Node may already be stopped. */
    }
  });
  activeNodes = [];
}

function scaledSamples(stages) {
  return totalSamples(stages) * AUDITION_TIME_SCALE;
}

function longestAuditionLane(current) {
  return [current.amp, current.amp2 || current.amp, current.pd, current.pd2 || current.pd, current.pitch, current.pitch2 || current.pitch]
    .sort((a, b) => totalSamples(b) - totalSamples(a))[0];
}

function midiToHz(note) {
  return 440 * Math.pow(2, (Number(note) - 69) / 12);
}

function renderDeveloperLog() {
  if (!el.developerLog) return;
  el.developerLog.textContent = developerLogLines.length
    ? developerLogLines.join("\n\n")
    : "No diagnostics yet.";
}

function collectMidiPorts(portMap) {
  if (!portMap) return [];

  const ports = [];
  const seen = new Set();
  const addPort = (port) => {
    if (!port) return;
    const key = port.id || `${port.name || "unnamed"}:${port.type || "port"}:${ports.length}`;
    if (seen.has(key)) return;
    seen.add(key);
    ports.push(port);
  };

  if (typeof portMap.forEach === "function") {
    try {
      portMap.forEach((port) => addPort(port));
    } catch {
      /* Some mobile browsers expose partial map behavior. */
    }
  }

  if (typeof portMap.values === "function") {
    try {
      for (const port of portMap.values()) addPort(port);
    } catch {
      /* Fall through to other access paths. */
    }
  }

  if (typeof portMap[Symbol.iterator] === "function") {
    try {
      for (const entry of portMap) {
        if (Array.isArray(entry)) addPort(entry[1]);
        else addPort(entry);
      }
    } catch {
      /* Fall through to key-based access. */
    }
  }

  if (ports.length === 0 && typeof portMap.keys === "function" && typeof portMap.get === "function") {
    try {
      for (const key of portMap.keys()) addPort(portMap.get(key));
    } catch {
      /* Keep empty if the browser exposes no usable enumeration path. */
    }
  }

  return ports;
}

function inspectPortMap(portMap) {
  if (!portMap) return "Unavailable";

  const details = {
    constructor: constructorName(portMap),
    size: typeof portMap.size === "number" ? portMap.size : "n/a",
    hasForEach: typeof portMap.forEach === "function",
    hasValues: typeof portMap.values === "function",
    hasKeys: typeof portMap.keys === "function",
    hasGet: typeof portMap.get === "function",
    iterable: typeof portMap[Symbol.iterator] === "function",
    ownKeys: Object.keys(portMap)
  };

  const sampleKeys = [];
  if (typeof portMap.keys === "function") {
    try {
      for (const key of portMap.keys()) {
        sampleKeys.push(key);
        if (sampleKeys.length >= 6) break;
      }
    } catch {
      sampleKeys.push("keys() failed");
    }
  }
  details.sampleKeys = sampleKeys;
  details.enumeratedCount = collectMidiPorts(portMap).length;

  return Object.entries(details)
    .map(([key, value]) => `${key}: ${previewValue(value)}`)
    .join("\n");
}

function renderDeveloperMidiRaw() {
  if (!el.developerMidiRaw) return;
  if (!midiAccess) {
    el.developerMidiRaw.textContent = "No MIDI access yet.";
    return;
  }

  el.developerMidiRaw.textContent = [
    `MIDIAccess: ${constructorName(midiAccess)}`,
    `Settings protocol: ${describeSettingsProtocol()}`,
    `Envelope readback: ${describeEnvelopeReadback()}`,
    `Envelope send mode: ${envelopePayloadModeLabel()}`,
    "",
    "Inputs map",
    inspectPortMap(midiAccess.inputs),
    "",
    "Outputs map",
    inspectPortMap(midiAccess.outputs)
  ].join("\n");
}

function formatPort(port, index, kind) {
  if (!port) return `${kind} ${index + 1}: unavailable`;
  const name = port.name || `${kind} ${index + 1}`;
  const manufacturer = port.manufacturer || "Unknown maker";
  const state = port.state || "unknown";
  const connection = port.connection || "unknown";
  return `${name}\n  maker: ${manufacturer}\n  state: ${state}\n  connection: ${connection}\n  id: ${port.id || "none"}`;
}

function renderDeveloperPorts() {
  if (!el.developerPorts) return;
  if (!midiAccess) {
    el.developerPorts.textContent = "No MIDI access yet.";
    return;
  }

  const inputs = collectMidiPorts(midiAccess.inputs);
  const outputs = collectMidiPorts(midiAccess.outputs);
  const inputText = inputs.length
    ? inputs.map((port, index) => formatPort(port, index, "Input")).join("\n\n")
    : "None detected";
  const outputText = outputs.length
    ? outputs.map((port, index) => formatPort(port, index, "Output")).join("\n\n")
    : "None detected";

  el.developerPorts.textContent = `Settings protocol: ${describeSettingsProtocol()}\nEnvelope readback: ${describeEnvelopeReadback()}\nEnvelope send mode: ${envelopePayloadModeLabel()}\n\nInputs (${inputs.length})\n${inputText}\n\nOutputs (${outputs.length})\n${outputText}`;
  renderDeveloperMidiRaw();
}

function describeSettingsProtocol() {
  if (settingsProtocol === "stable") return "Stable firmware";
  if (settingsProtocol === "extended") return "Extended firmware";
  if (settingsProtocol === "dual-oscillator") return "Full dual-oscillator firmware";
  return "Not detected yet";
}

function describeEnvelopeReadback() {
  if (envelopeReadSupported === true) {
    return detectedEnvelopeProtocol
      ? `Supported, protocol v${detectedEnvelopeProtocol}`
      : "Supported";
  }
  if (envelopeReadSupported === false) return "No response";
  return "Not checked yet";
}

function cardCapabilityLabel() {
  if (settingsProtocol === "dual-oscillator" || (settingsProtocol === "unknown" && detectedEnvelopeProtocol !== null && detectedEnvelopeProtocol >= 9)) {
    return "Rad dual-oscillator";
  }
  if (settingsProtocol === "extended") return "Core extended";
  if (settingsProtocol === "stable") return "Core stable";
  return "undetected";
}

function supportsDualOscillatorEnvelopePayload() {
  return settingsProtocol === "dual-oscillator" || (detectedEnvelopeProtocol !== null && detectedEnvelopeProtocol >= 5);
}

function supportsSoundPresetPayload() {
  return settingsProtocol === "dual-oscillator" || (settingsProtocol === "unknown" && detectedEnvelopeProtocol !== null && detectedEnvelopeProtocol >= 9);
}

function envelopePayloadMode() {
  if (supportsDualOscillatorEnvelopePayload()) return "dual";
  if (detectedEnvelopeProtocol === 1) return "legacy";
  return "core";
}

function envelopePayloadModeLabel(mode = envelopePayloadMode()) {
  if (mode === "dual") return "full-dual";
  if (mode === "legacy") return "legacy Amp/PD";
  return "Core Amp/PD/Pitch";
}

function clearDeveloperLog() {
  developerLogLines = [];
  renderDeveloperLog();
}

function previewValue(value) {
  if (value == null) return String(value);
  if (Array.isArray(value)) {
    return `Array(${value.length}) [${value.slice(0, 8).join(", ")}${value.length > 8 ? ", ..." : ""}]`;
  }
  if (typeof value === "object") {
    try {
      return JSON.stringify(value);
    } catch {
      return Object.prototype.toString.call(value);
    }
  }
  return String(value);
}

function constructorName(value) {
  return value?.constructor?.name || typeof value;
}

function logDeveloper(message, detail = {}) {
  const time = new Date().toLocaleTimeString([], { hour12: false });
  const lines = [`[${time}] ${message}`];
  Object.entries(detail).forEach(([key, value]) => {
    lines.push(`${key}: ${previewValue(value)}`);
  });
  developerLogLines = [...developerLogLines.slice(-29), lines.join("\n")];
  renderDeveloperLog();
}

function failDeveloper(message, detail = {}) {
  logDeveloper(message, detail);
  throw new Error(message);
}

function ensureArray(value, label) {
  if (!Array.isArray(value)) {
    failDeveloper(`${label} must be an array before spreading.`, {
      label,
      type: typeof value,
      constructor: constructorName(value),
      value
    });
  }
  return value;
}

async function connectMidi() {
  if (!navigator.requestMIDIAccess) {
    setStatus("Web MIDI is not available in this browser.");
    return;
  }

  try {
    midiAccess = await navigator.requestMIDIAccess({ sysex: true });
    await prepareMidiPorts();
    midiAccess.onstatechange = refreshMidiPorts;
    el.midiToggle.classList.add("is-active");
    pulseButton(el.midiToggle, "On");
    logDeveloper("Web MIDI connected.", {
      inputs: midiAccess.inputs?.size ?? 0,
      outputs: midiAccess.outputs?.size ?? 0
    });
  } catch (error) {
    logDeveloper("Web MIDI connection failed.", {
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus(`MIDI access denied or unavailable: ${error.message}`);
  }
}

async function openMidiPort(port, kind) {
  if (!port || typeof port.open !== "function" || port.connection === "open") return;
  try {
    await port.open();
  } catch (error) {
    logDeveloper(`Could not explicitly open MIDI ${kind}.`, {
      port: port.name || port.id || "Unnamed port",
      message: error?.message || "Unknown error"
    });
  }
}

async function prepareMidiPorts() {
  if (!midiAccess) return;
  refreshMidiPorts();
  const inputs = collectMidiPorts(midiAccess.inputs);
  const outputs = collectMidiPorts(midiAccess.outputs);
  await Promise.allSettled([
    ...inputs.map((input) => openMidiPort(input, "input")),
    ...outputs.map((output) => openMidiPort(output, "output"))
  ]);
  refreshMidiPorts();
}

function refreshMidiPorts() {
  if (!midiAccess) return;

  const inputs = collectMidiPorts(midiAccess.inputs);
  inputs.forEach((input) => {
    input.onmidimessage = handleMidi;
  });

  const previousOutput = el.midiOutput.value;
  el.midiOutput.innerHTML = "";

  const outputs = collectMidiPorts(midiAccess.outputs);
  outputs.forEach((output, index) => {
    const option = document.createElement("option");
    option.value = output.id;
    option.textContent = output.name || `MIDI output ${index + 1}`;
    el.midiOutput.append(option);
  });

  if (outputs.length === 0) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "No output found";
    el.midiOutput.append(option);
  } else if (outputs.some((output) => output.id === previousOutput)) {
    el.midiOutput.value = previousOutput;
  }

  renderDeveloperPorts();
  logDeveloper("MIDI ports refreshed.", {
    inputs: inputs.length,
    outputs: outputs.length,
    inputNames: inputs.map((input) => input.name || input.id || "Unnamed input"),
    outputNames: outputs.map((output) => output.name || output.id || "Unnamed output")
  });
  if (outputs.length === 0) {
    settingsProtocol = "unknown";
    detectedEnvelopeProtocol = null;
  }
  setStatus(`MIDI ready: ${inputs.length} input${inputs.length === 1 ? "" : "s"}, ${outputs.length} output${outputs.length === 1 ? "" : "s"}.`);
}

function selectedMidiOutput() {
  if (!midiAccess) return null;
  const selectedOutput = typeof midiAccess.outputs.get === "function"
    ? midiAccess.outputs.get(el.midiOutput.value)
    : null;
  if (selectedOutput) return selectedOutput;
  return collectMidiPorts(midiAccess.outputs)[0] || null;
}

async function sendSysex(command = SYSEX_COMMAND_PREVIEW, options = {}) {
  const includePerformance = options.includePerformance !== false;
  if (sendingSysex) {
    setStatus("SysEx send already in progress.");
    return;
  }

  if (!canSendSelectedEnvelope()) {
    setStatus("Preset 0, silent envelopes, and very short envelopes are not sent to custom card slots.");
    return;
  }

  if (!midiAccess) {
    await connectMidi();
  }

  const output = selectedMidiOutput();
  if (!output) {
    setStatus("No MIDI output found for SysEx send.");
    return;
  }

  if (settingsProtocol === "unknown" && includePerformance) {
    await requestPerformanceSettings(true);
  }

  let frame;
  try {
    frame = buildSysex(command, { includePerformance });
  } catch (error) {
    logDeveloper("SysEx build failed.", {
      command,
      preset: presets[selected]?.name,
      selected,
      slot: el.customSlot.value,
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus("Web MIDI send failed. Open Developer tools for details.");
    return;
  }
  const isFlash = command === SYSEX_COMMAND_SAVE;
  const slot = clampInt(el.customSlot.value, 0, CUSTOM_SLOT_COUNT - 1);
  const payloadMode = envelopePayloadMode();
  const expectedEnvelope = isFlash
    ? expectedEnvelopeForPayloadMode(payloadMode, includePerformance)
    : null;
  const sendCooldownMs = isFlash ? 1800 : 250;
  const summary = frameSummary();
  sendingSysex = true;
  el.sendSysex.disabled = true;
  el.loadEnvelopeAndSettings.disabled = true;
  el.saveEnvelopeOnly.disabled = true;
  el.flashSysex.disabled = true;
  el.deleteSlot.disabled = true;
  try {
    output.send(frame);
  } catch (error) {
    logDeveloper("SysEx send failed.", {
      command,
      output: output.name || output.id || "Unknown output",
      frameLength: frame.length,
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    sendingSysex = false;
    el.sendSysex.disabled = false;
    el.loadEnvelopeAndSettings.disabled = false;
    el.saveEnvelopeOnly.disabled = false;
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
    setStatus("Web MIDI send failed. Open Developer tools for details.");
    return;
  }
  const slotLabel = `Custom ${slot + 1}`;
  const compatibilityNote = compatibilityStatusNote(payloadMode, includePerformance);
  const soundPresetConfirmed = includePerformance && supportsSoundPresetPayload();
  const status = isFlash
    ? soundPresetConfirmed
      ? `Saved sound preset ${slotLabel}; after reset it appears after the factory presets. Amp max ${summary.ampMax}, ${summary.seconds}s.`
      : includePerformance
        ? settingsProtocol === "unknown"
          ? `Saved ${slotLabel}; card capability was not confirmed before this save, so the editor used the safe compatibility payload. Amp max ${summary.ampMax}, ${summary.seconds}s.`
          : `Saved envelope ${slotLabel}; ${cardCapabilityLabel()} stores the envelope shape only in this compatibility mode, not full sound-preset settings. Amp max ${summary.ampMax}, ${summary.seconds}s.`
        : `Saved envelope only ${slotLabel}; saved settings for this slot were left unchanged when the card supports sound presets. Amp max ${summary.ampMax}, ${summary.seconds}s.`
    : `Loaded ${slotLabel} until reset. Amp max ${summary.ampMax}, ${summary.seconds}s.`;
  if (isFlash) {
    if (includePerformance) {
      presets[selected].performance = normalizePerformanceSettings(performanceSettings);
    }
    bindSelectedPresetToSlot(slot);
    if (envelopeReadSupported === true) {
      window.setTimeout(() => requestCardEnvelopes("save", slot, expectedEnvelope), SAVE_VERIFY_DELAY_MS);
    }
  }
  const pulseTarget = isFlash
    ? includePerformance ? el.flashSysex : el.saveEnvelopeOnly
    : el.sendSysex;
  pulseButton(pulseTarget, isFlash ? "Saved" : "Loaded");
  setStatus(`${status} ${frame.length} byte ${envelopePayloadModeLabel(payloadMode)} SysEx to ${output.name || "MIDI output"}.${compatibilityNote}`);

  window.setTimeout(() => {
    sendingSysex = false;
    el.sendSysex.disabled = false;
    el.loadEnvelopeAndSettings.disabled = false;
    el.saveEnvelopeOnly.disabled = false;
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
  }, sendCooldownMs);
}

async function loadEnvelopeAndSettings() {
  if (sendingSysex) {
    setStatus("SysEx send already in progress.");
    return;
  }
  if (!canSendSelectedEnvelope()) {
    setStatus("Preset 0, silent envelopes, and very short envelopes cannot be loaded to the card.");
    return;
  }
  if (!midiAccess) await connectMidi();
  const output = selectedMidiOutput();
  if (!output) {
    setStatus("No MIDI output found for combined load.");
    return;
  }

  try {
    const usedStoredSettings = applyPerformanceSettings(presets[selected].performance);
    const payloadMode = envelopePayloadMode();
    const envelopeFrame = buildSysex(SYSEX_COMMAND_PREVIEW, { includePerformance: true });
    sendingSysex = true;
  el.sendSysex.disabled = true;
  el.loadEnvelopeAndSettings.disabled = true;
  el.saveEnvelopeOnly.disabled = true;
  el.flashSysex.disabled = true;
    el.deleteSlot.disabled = true;
    output.send(envelopeFrame);
    sendSettingsFrames(output, SYSEX_COMMAND_SETTINGS, 60);
    const slot = clampInt(el.customSlot.value, 0, CUSTOM_SLOT_COUNT - 1);
    pulseButton(el.loadEnvelopeAndSettings, "Loaded");
    setStatus(`Loaded sound preset ${presets[selected].name} into RAM slot ${slot + 1} with a ${envelopePayloadModeLabel(payloadMode)} envelope payload and sent ${usedStoredSettings ? "its saved settings" : "the current settings"}. Nothing has been saved to card flash yet.${temporaryLoadSaveHint(payloadMode)}${compatibilityStatusNote(payloadMode, true)}`);
  } catch (error) {
    logDeveloper("Combined envelope and settings load failed.", {
      preset: presets[selected]?.name,
      output: output.name || output.id || "Unknown output",
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus("Combined load failed. Open Developer tools for details.");
  }

  window.setTimeout(() => {
    sendingSysex = false;
    el.sendSysex.disabled = false;
    el.loadEnvelopeAndSettings.disabled = false;
    el.saveEnvelopeOnly.disabled = false;
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
  }, 350);
}

async function deleteCustomSlot() {
  if (sendingSysex) {
    setStatus("SysEx send already in progress.");
    return;
  }

  if (!midiAccess) {
    await connectMidi();
  }

  const output = selectedMidiOutput();
  if (!output) {
    setStatus("No MIDI output found for custom slot delete.");
    return;
  }

  const selectedPreset = presets[selected];
  const slot = Number.isInteger(selectedPreset?.slot)
    ? selectedPreset.slot
    : clampInt(el.customSlot.value, 0, CUSTOM_SLOT_COUNT - 1);
  let frame;
  try {
    frame = buildDeleteSlotSysex(slot);
    sendingSysex = true;
    el.sendSysex.disabled = true;
    el.loadEnvelopeAndSettings.disabled = true;
    el.flashSysex.disabled = true;
    el.deleteSlot.disabled = true;
    output.send(frame);
    logDeveloper("Delete slot SysEx sent.", {
      slot: slot + 1,
      preset: selectedPreset?.name || "No selected custom preset",
      output: output.name || output.id || "Unknown output",
      bytes: frame
    });
  } catch (error) {
    logDeveloper("Delete slot SysEx failed.", {
      slot,
      output: output.name || output.id || "Unknown output",
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    sendingSysex = false;
    el.sendSysex.disabled = false;
    el.loadEnvelopeAndSettings.disabled = false;
    el.saveEnvelopeOnly.disabled = false;
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
    setStatus("Delete failed. Open Developer tools for details.");
    return;
  }
  if (envelopeReadSupported !== true) {
    removeCardSlotDraft(slot);
    savePresets();
    render();
    pulseButton(el.deleteSlot, "Sent");
    setStatus(`Delete sent for card slot ${slot + 1}. Use Read Envelopes from Card to confirm it.`);
  } else {
    pulseButton(el.deleteSlot, "Sent");
    setStatus(`Delete sent for card slot ${slot + 1}; checking the card...`);
    window.setTimeout(() => requestCardEnvelopes("delete", slot), DELETE_VERIFY_DELAY_MS);
  }

  window.setTimeout(() => {
    sendingSysex = false;
    el.sendSysex.disabled = false;
    el.loadEnvelopeAndSettings.disabled = false;
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
  }, 1800);
}

function buildSysex(command, options = {}) {
  const includePerformance = options.includePerformance !== false;
  if (!canSendSelectedEnvelope()) {
    throw new Error("Cannot build SysEx for preset 0, a silent envelope, or a very short envelope.");
  }

  const slot = clampInt(el.customSlot.value, 0, CUSTOM_SLOT_COUNT - 1);
  const mode = envelopePayloadMode();
  if (mode === "legacy") return buildLegacyEnvelopeSysex(command, slot);
  if (mode === "core") return buildCoreEnvelopeSysex(command, slot);
  return buildDualEnvelopeSysex(command, slot, includePerformance);
}

function buildLegacyEnvelopeSysex(command, slot) {
  const payload = [slot & 0x7f];
  appendStages(payload, presets[selected].amp);
  appendStages(payload, presets[selected].pd);
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "legacy envelope payload"), 0xf7];
}

function buildCoreEnvelopeSysex(command, slot) {
  const nameBytes = ensureArray(encodeName(presets[selected].name), "encodeName()");
  const payload = [slot & 0x7f, ...nameBytes];
  constrainPitchToEnvelope(presets[selected]);
  appendStages(payload, presets[selected].amp);
  appendStages(payload, presets[selected].pd);
  appendStages(payload, presets[selected].pitch);
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "Core envelope payload"), 0xf7];
}

function buildDualEnvelopeSysex(command, slot, includePerformance) {
  const nameBytes = ensureArray(encodeName(presets[selected].name), "encodeName()");
  const shouldIncludePerformance = includePerformance && supportsSoundPresetPayload();
  const performanceBytes = shouldIncludePerformance
    ? ensureArray(encodePerformanceSettings(performanceSettings), "encodePerformanceSettings()")
    : [];
  const payload = [slot & 0x7f, ...nameBytes, ...performanceBytes];
  constrainPitchToEnvelope(presets[selected]);
  appendStages(payload, presets[selected].amp);
  appendStages(payload, presets[selected].pd);
  appendStages(payload, presets[selected].pitch);
  appendStages(payload, presets[selected].pitch2 || presets[selected].pitch);
  appendStages(payload, presets[selected].pd2 || presets[selected].pd);
  appendStages(payload, presets[selected].amp2 || presets[selected].amp);
  payload.push(...normalizeSustainStages(presets[selected].sustain));
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "payload"), 0xf7];
}

function expectedEnvelopeForPayloadMode(mode, includePerformance) {
  const source = structuredClone(presets[selected]);
  if (mode === "legacy") {
    return {
      ...source,
      name: "",
      amp2: source.amp,
      pd2: source.pd,
      pitch: normalizePitchStages(null),
      pitch2: normalizePitchStages(null),
      sustain: normalizeSustainStages(null),
      performance: null
    };
  }
  if (mode === "core") {
    return {
      ...source,
      amp2: source.amp,
      pd2: source.pd,
      pitch2: source.pitch,
      sustain: normalizeSustainStages(null),
      performance: null
    };
  }
  return {
    ...source,
    performance: includePerformance && supportsSoundPresetPayload()
      ? normalizePerformanceSettings(performanceSettings)
      : normalizePerformanceSettings(source.performance)
  };
}

function compatibilityStatusNote(mode, includePerformance) {
  const capability = cardCapabilityLabel();
  if (mode === "dual") {
    return ` Card capability: ${capability}.`;
  }
  if (includePerformance && !supportsSoundPresetPayload()) {
    return ` Card capability: ${capability}; the editor could not confirm Rad before sending, so it used the safe compatibility format. Read Settings from Card or Read Envelopes from Card to refresh the capability display.`;
  }
  return ` Card capability: ${capability}; dual lanes are collapsed for compatibility.`;
}

function temporaryLoadSaveHint(mode) {
  if (mode === "dual") {
    return " This is temporary; use Save Sound Preset to write the envelope, name, and settings to card flash.";
  }
  return " This is temporary; use Save Envelope Only or Save Sound Preset to write the collapsed Core-compatible envelope to card flash. Core firmware does not store full-dual sound-preset settings in the slot.";
}

function buildSettingsSysex(command) {
  if (settingsProtocol === "dual-oscillator") {
    return buildDualOscillatorSettingsSysex(command);
  }
  if (settingsProtocol === "extended") {
    return buildExtendedSettingsSysex(command);
  }

  return buildStableSettingsSysex(command);
}

function buildStableSettingsSysex(command) {
  const payload = [];
  payload.push(...ensureArray(packUint14(performanceSettings.ring), "packUint14(ring)"));
  payload.push(...ensureArray(packUint14(performanceSettings.noise), "packUint14(noise)"));
  payload.push(clampInt(performanceSettings.midiInChannel, 1, 16) - 1);
  payload.push(clampInt(performanceSettings.turingRange, 1, 8));
  payload.push(performanceSettings.turingMidiOut ? 1 : 0);
  payload.push(clampInt(performanceSettings.turingMidiChannel, 1, 16) - 1);
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "settings payload"), 0xf7];
}

function buildExtendedSettingsSysex(command) {
  const payload = [];
  payload.push(...ensureArray(packUint14(performanceSettings.ring), "packUint14(ring)"));
  payload.push(...ensureArray(packUint14(performanceSettings.noise), "packUint14(noise)"));
  payload.push(...ensureArray(packUint14(performanceSettings.pd), "packUint14(pd)"));
  payload.push(...ensureArray(packUint14(performanceSettings.detune), "packUint14(detune)"));
  payload.push(...ensureArray(packUint14(waveFamilyToControl(performanceSettings.waveform)), "packUint14(waveform)"));
  payload.push(clampInt(performanceSettings.midiInChannel, 1, 16) - 1);
  payload.push(clampInt(performanceSettings.turingRange, 1, 8));
  payload.push(performanceSettings.turingMidiOut ? 1 : 0);
  payload.push(clampInt(performanceSettings.turingMidiChannel, 1, 16) - 1);
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "extended settings payload"), 0xf7];
}

function buildDualOscillatorSettingsSysex(command) {
  const payload = [];
  payload.push(...ensureArray(packUint14(performanceSettings.ring), "packUint14(ring)"));
  payload.push(...ensureArray(packUint14(performanceSettings.noise), "packUint14(noise)"));
  payload.push(...ensureArray(packUint14(performanceSettings.pd), "packUint14(pd)"));
  payload.push(...ensureArray(packUint14(performanceSettings.detune), "packUint14(detune)"));
  payload.push(...ensureArray(packUint14(waveFamilyToControl(performanceSettings.waveform)), "packUint14(waveform)"));
  payload.push(...ensureArray(packUint14(waveFamilyToControl(performanceSettings.waveform2)), "packUint14(waveform2)"));
  payload.push(clampInt(performanceSettings.midiInChannel, 1, 16) - 1);
  payload.push(clampInt(performanceSettings.turingRange, 1, 8));
  payload.push(performanceSettings.turingMidiOut ? 1 : 0);
  payload.push(clampInt(performanceSettings.turingMidiChannel, 1, 16) - 1);
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "dual oscillator settings payload"), 0xf7];
}

function buildDeleteSlotSysex(slot) {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, SYSEX_COMMAND_DELETE, slot & 0x7f, 0xf7];
}

function buildRequestSettingsSysex() {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, SYSEX_COMMAND_REQUEST_SETTINGS, 0xf7];
}

function buildRequestEnvelopeSlotsSysex() {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, SYSEX_COMMAND_REQUEST_ENVELOPE_SLOTS, 0xf7];
}

function buildRequestEnvelopeSysex(slot) {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, SYSEX_COMMAND_REQUEST_ENVELOPE, slot & 0x07, 0xf7];
}

function buildRequestPitchEnvelopeSysex(slot) {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, SYSEX_COMMAND_REQUEST_PITCH_ENVELOPE, slot & 0x07, 0xf7];
}

function buildRequestPd2EnvelopeSysex(slot) {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, SYSEX_COMMAND_REQUEST_PD2_ENVELOPE, slot & 0x07, 0xf7];
}

function buildRequestAmp2EnvelopeSysex(slot) {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, SYSEX_COMMAND_REQUEST_AMP2_ENVELOPE, slot & 0x07, 0xf7];
}

function canSendSelectedEnvelope() {
  return selected !== 0 &&
    presets[selected].amp.some((stage) => stage.level > 0) &&
    totalSamples(presets[selected].amp) >= MIN_SEND_SAMPLES;
}

function frameSummary() {
  const amp = presets[selected].amp;
  return {
    ampMax: Math.max(...amp.map((stage) => stage.level)),
    seconds: (totalSamples(amp) / SAMPLE_RATE).toFixed(2)
  };
}

function appendStages(payload, stages) {
  stages.forEach((stage) => {
    payload.push(...ensureArray(packUint14(stage.level), `packUint14(stage level ${stage.level})`));
    payload.push(...ensureArray(packUint21(stage.time), `packUint21(stage time ${stage.time})`));
  });
}

function packUint14(value) {
  const v = clampInt(value, 0, 0x3fff);
  return [v & 0x7f, (v >> 7) & 0x7f];
}

function waveFamilyToControl(family) {
  return Math.round((clampInt(family, 0, 7) * MAX_LEVEL) / 7);
}

function waveControlToFamily(control) {
  return clampInt(Math.round((clampInt(control, 0, MAX_LEVEL) * 7) / MAX_LEVEL), 0, 7);
}

function unpackUint14(data, offset) {
  return (data[offset] & 0x7f) | ((data[offset + 1] & 0x7f) << 7);
}

function unpackUint21(data, offset) {
  return (data[offset] & 0x7f) |
    ((data[offset + 1] & 0x7f) << 7) |
    ((data[offset + 2] & 0x7f) << 14);
}

function packUint21(value) {
  const v = clampInt(value, 0, 0x1fffff);
  return [v & 0x7f, (v >> 7) & 0x7f, (v >> 14) & 0x7f];
}

function encodeName(name) {
  return Array.from({ length: 16 }, (_, index) => {
    const code = name.charCodeAt(index) || 32;
    return code >= 32 && code <= 126 ? code : 32;
  });
}

function decodeNameBytes(data, offset) {
  return Array.from({ length: 16 }, (_, index) => {
    const code = data[offset + index] & 0x7f;
    return code >= 32 && code <= 126 ? String.fromCharCode(code) : " ";
  }).join("").trim();
}

function encodePerformanceSettings(settings = performanceSettings) {
  const normalized = normalizePerformanceSettings(settings) || normalizePerformanceSettings(performanceSettings);
  return [
    ...packUint14(normalized.pd),
    ...packUint14(normalized.detune),
    ...packUint14(waveFamilyToControl(normalized.waveform)),
    ...packUint14(waveFamilyToControl(normalized.waveform2)),
    ...packUint14(normalized.ring),
    ...packUint14(normalized.noise),
    clampInt(normalized.midiInChannel, 1, 16) - 1,
    clampInt(normalized.turingRange, 1, 8),
    normalized.turingMidiOut ? 1 : 0,
    clampInt(normalized.turingMidiChannel, 1, 16) - 1
  ];
}

function decodePerformanceSettingsBytes(data, offset) {
  return normalizePerformanceSettings({
    pd: unpackUint14(data, offset),
    detune: unpackUint14(data, offset + 2),
    waveform: waveControlToFamily(unpackUint14(data, offset + 4)),
    waveform2: waveControlToFamily(unpackUint14(data, offset + 6)),
    ring: unpackUint14(data, offset + 8),
    noise: unpackUint14(data, offset + 10),
    midiInChannel: (data[offset + 12] & 0x0f) + 1,
    turingRange: data[offset + 13],
    turingMidiOut: (data[offset + 14] & 0x01) !== 0,
    turingMidiChannel: (data[offset + 15] & 0x0f) + 1
  });
}

function sysexHex() {
  return Array.from(ensureArray(buildSysex(SYSEX_COMMAND_PREVIEW), "buildSysex()"))
    .map((byte) => byte.toString(16).toUpperCase().padStart(2, "0"))
    .join(" ");
}

function stagesMatch(a, b) {
  return a.length === b.length && a.every((stage, index) =>
    stage.level === b[index].level && stage.time === b[index].time);
}

function envelopesMatch(a, b) {
  return stagesMatch(a.amp, b.amp) &&
    stagesMatch(normalizeStages(a.amp2 || a.amp), normalizeStages(b.amp2 || b.amp)) &&
    stagesMatch(a.pd, b.pd) &&
    stagesMatch(normalizeStages(a.pd2 || a.pd), normalizeStages(b.pd2 || b.pd)) &&
    stagesMatch(normalizePitchStages(a.pitch), normalizePitchStages(b.pitch)) &&
    stagesMatch(normalizePitchStages(a.pitch2 || a.pitch), normalizePitchStages(b.pitch2 || b.pitch)) &&
    normalizeSustainStages(a.sustain).every((stage, index) => stage === normalizeSustainStages(b.sustain)[index]);
}

function markCardSlotLocal(slot) {
  presets.forEach((item, index) => {
    if (index >= FACTORY_PRESET_COUNT && item.slot === slot) {
      item.slot = null;
      item.cardDirty = false;
    }
  });
}

function removeCardSlotDraft(slot) {
  const removedSelected = presets[selected]?.slot === slot;
  presets = presets.filter((item, index) =>
    index < FACTORY_PRESET_COUNT || item.slot !== slot);
  if (removedSelected) {
    selected = Math.min(3, presets.length - 1);
  } else {
    selected = Math.min(selected, presets.length - 1);
  }
  if (selected < 0) selected = 0;
}

function reconcileCardEnvelopes(mask, cardEnvelopes) {
  let preservedChanges = 0;
  let skipped = 0;
  let replacedLocal = 0;

  for (let slot = 0; slot < CUSTOM_SLOT_COUNT; slot++) {
    if ((mask & (1 << slot)) === 0) removeCardSlotDraft(slot);
  }

  cardEnvelopes.forEach((cardEnvelope, slot) => {
    const matching = [];
    presets.forEach((item, index) => {
      if (index >= FACTORY_PRESET_COUNT && item.slot === slot) matching.push(index);
    });
    const existingIndex = matching.shift();
    matching.forEach((index) => {
      presets[index].slot = null;
      presets[index].cardDirty = false;
    });

    if (existingIndex !== undefined) {
      const existing = presets[existingIndex];
      if (existing.cardDirty && !envelopesMatch(existing, cardEnvelope)) {
        existing.slot = null;
        existing.cardDirty = false;
        preservedChanges++;
      } else {
        existing.name = cardEnvelope.name || existing.name || `Card Envelope ${slot + 1}`;
        existing.amp = normalizeStages(cardEnvelope.amp);
        existing.amp2 = normalizeStages(cardEnvelope.amp2 || cardEnvelope.amp);
        existing.pd = normalizeStages(cardEnvelope.pd);
        existing.pd2 = normalizeStages(cardEnvelope.pd2 || cardEnvelope.pd);
        existing.pitch = normalizePitchStages(cardEnvelope.pitch);
        existing.pitch2 = normalizePitchStages(cardEnvelope.pitch2 || cardEnvelope.pitch);
        existing.sustain = normalizeSustainStages(cardEnvelope.sustain);
        existing.performance = normalizePerformanceSettings(cardEnvelope.performance);
        constrainPitchToEnvelope(existing);
        existing.cardDirty = false;
        return;
      }
    }

    const cardPreset = constrainPitchToEnvelope(preset(cardEnvelope.name || `Card Envelope ${slot + 1}`, cardEnvelope.amp, cardEnvelope.pd, slot, false, cardEnvelope.pitch, null, cardEnvelope.pitch2, cardEnvelope.pd2 || cardEnvelope.pd, cardEnvelope.amp2 || cardEnvelope.amp, cardEnvelope.sustain, cardEnvelope.performance));
    if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
      const replacementIndex = presets.findIndex((item, index) =>
        index >= FACTORY_PRESET_COUNT && item.slot === null);
      if (replacementIndex >= FACTORY_PRESET_COUNT) {
        presets[replacementIndex] = cardPreset;
        replacedLocal++;
        return;
      }

      const fallbackIndex = presets.length - 1;
      if (fallbackIndex >= FACTORY_PRESET_COUNT) {
        presets[fallbackIndex] = cardPreset;
        replacedLocal++;
        return;
      }

      skipped++;
      return;
    }
    presets.push(cardPreset);
  });

  savePresets();
  render();
  return { preservedChanges, skipped, replacedLocal };
}

function clearEnvelopeReadTimer() {
  if (envelopeReadTimer !== null) {
    window.clearTimeout(envelopeReadTimer);
    envelopeReadTimer = null;
  }
}

function clearAuxiliaryEnvelopeRequestTimer() {
  if (auxiliaryEnvelopeRequestTimer !== null) {
    window.clearTimeout(auxiliaryEnvelopeRequestTimer);
    auxiliaryEnvelopeRequestTimer = null;
  }
}

function scheduleAuxiliaryEnvelopeRequest(label, slot, buildRequest, applyWaitingState) {
  if (!envelopeReadSession) return;
  const output = selectedMidiOutput();
  if (!output) return;

  clearAuxiliaryEnvelopeRequestTimer();
  auxiliaryEnvelopeRequestTimer = window.setTimeout(() => {
    auxiliaryEnvelopeRequestTimer = null;
    if (!envelopeReadSession) return;

    applyWaitingState();
    try {
      const request = buildRequest(slot);
      output.send(request);
      logDeveloper(`${label} envelope request sent.`, {
        slot: slot + 1,
        output: output.name || output.id || "Unknown output",
        bytes: request
      });
      armEnvelopeReadTimeout();
    } catch (error) {
      logDeveloper(`${label} envelope request failed.`, {
        slot: slot + 1,
        message: error?.message || "Unknown error",
        stack: error?.stack || "No stack trace"
      });
    }
  }, AUX_ENVELOPE_REQUEST_DELAY_MS);
}

function armEnvelopeReadTimeout() {
  clearEnvelopeReadTimer();
  envelopeReadTimer = window.setTimeout(() => {
    const session = envelopeReadSession;
    const output = selectedMidiOutput();
    if (session && output && session.retriesRemaining > 0) {
      session.retriesRemaining--;
      envelopeReadTimer = null;
      try {
        if (session.waitingForAmp2Slot !== null) {
          output.send(buildRequestAmp2EnvelopeSysex(session.waitingForAmp2Slot));
          setStatus(`Still waiting for card Amp2 envelope slot ${session.waitingForAmp2Slot + 1}; retrying readback...`);
        } else if (session.waitingForPd2Slot !== null) {
          output.send(buildRequestPd2EnvelopeSysex(session.waitingForPd2Slot));
          setStatus(`Still waiting for card PD2 envelope slot ${session.waitingForPd2Slot + 1}; retrying readback...`);
        } else if (session.waitingForPitchSlot !== null) {
          output.send(buildRequestPitchEnvelopeSysex(session.waitingForPitchSlot));
          setStatus(`Still waiting for card pitch envelope slot ${session.waitingForPitchSlot + 1}; retrying readback...`);
        } else if (session.waitingForSlot !== null) {
          output.send(buildRequestEnvelopeSysex(session.waitingForSlot));
          setStatus(`Still waiting for card envelope slot ${session.waitingForSlot + 1}; retrying readback...`);
        } else {
          output.send(buildRequestEnvelopeSlotsSysex());
          setStatus("Still waiting for the card envelope list; retrying readback...");
        }
        armEnvelopeReadTimeout();
        return;
      } catch (error) {
        logDeveloper("Envelope read retry failed.", {
          reason: session.reason,
          waitingForSlot: session.waitingForSlot,
          message: error?.message || "Unknown error",
          stack: error?.stack || "No stack trace"
        });
      }
    }

    if (session?.waitingForSlot !== null && session?.waitingForSlot !== undefined) {
      const skippedSlot = session.waitingForSlot;
      logDeveloper("Envelope slot did not reply; skipping slot.", {
        slot: skippedSlot + 1,
        pendingSlots: session.pendingSlots.map((pendingSlot) => pendingSlot + 1)
      });
      completeEnvelopeSlotRead(skippedSlot);
      return;
    }

    envelopeReadSession = null;
    envelopeReadTimer = null;
    clearAuxiliaryEnvelopeRequestTimer();
    envelopeReadSupported = false;
    el.requestEnvelopes.disabled = false;
    renderDeveloperPorts();
    if (session?.reason === "delete") {
      removeCardSlotDraft(session.slot);
      savePresets();
      render();
    }
    setStatus("The card did not reply to the envelope read request. This firmware may not support envelope readback.");
  }, ENVELOPE_READ_TIMEOUT_MS);
}

function sendNextEnvelopeRequest() {
  if (!envelopeReadSession) return;
  if (envelopeReadSession.pendingSlots.length === 0) {
    finishEnvelopeRead();
    return;
  }

  const output = selectedMidiOutput();
  if (!output) {
    envelopeReadSession = null;
    el.requestEnvelopes.disabled = false;
    setStatus("No MIDI output found for envelope readback.");
    return;
  }
  const slot = envelopeReadSession.pendingSlots[0];
  envelopeReadSession.waitingForSlot = slot;
  envelopeReadSession.retriesRemaining = ENVELOPE_READ_RETRIES;
  try {
    const request = buildRequestEnvelopeSysex(slot);
    output.send(request);
    logDeveloper("Envelope slot request sent.", {
      slot: slot + 1,
      output: output.name || output.id || "Unknown output",
      bytes: request
    });
    armEnvelopeReadTimeout();
  } catch (error) {
    envelopeReadSession = null;
    el.requestEnvelopes.disabled = false;
    logDeveloper("Envelope slot request failed.", {
      slot: slot + 1,
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus(`Could not request card envelope slot ${slot + 1}.`);
  }
}

function finishEnvelopeRead() {
  const session = envelopeReadSession;
  if (!session) return;
  clearEnvelopeReadTimer();
  envelopeReadSession = null;
  envelopeReadSupported = true;
  el.requestEnvelopes.disabled = false;
  renderDeveloperPorts();

  const result = reconcileCardEnvelopes(session.mask, session.cardEnvelopes);
  const count = session.cardEnvelopes.size;
  if (count > 0 && session.reason === "manual") {
    const firstSlot = [...session.cardEnvelopes.keys()][0];
    const loadedIndex = presets.findIndex((item, index) =>
      index >= FACTORY_PRESET_COUNT && item.slot === firstSlot);
    if (loadedIndex >= FACTORY_PRESET_COUNT) {
      selected = loadedIndex;
      applyPerformanceSettings(presets[loadedIndex].performance);
      render();
    }
  }
  if (session.reason === "save") {
    const cardEnvelope = session.cardEnvelopes.get(session.slot);
    const verified = cardEnvelope && session.expectedEnvelope &&
      envelopesMatch(cardEnvelope, session.expectedEnvelope);
    setStatus(verified
      ? `Verified sound preset saved in card slot ${session.slot + 1}.`
      : `The sound preset in card slot ${session.slot + 1} did not match the saved draft.`);
    return;
  }
  if (session.reason === "delete") {
    const deleted = (session.mask & (1 << session.slot)) === 0;
    setStatus(deleted
      ? `Verified card slot ${session.slot + 1} is empty. Any matching full-dual-oscillator card-slot draft was removed.`
      : `Card slot ${session.slot + 1} still reports a saved envelope.`);
    return;
  }

  const notes = [];
  if (result.preservedChanges) notes.push(`${result.preservedChanges} changed local draft${result.preservedChanges === 1 ? " was" : "s were"} preserved`);
  if (result.replacedLocal) notes.push(`${result.replacedLocal} local draft${result.replacedLocal === 1 ? " was" : "s were"} replaced to show card data`);
  if (result.skipped) notes.push(`${result.skipped} card envelope${result.skipped === 1 ? " was" : "s were"} not added because the browser list is full`);
  const browserDraftLabel = envelopePayloadMode() === "dual"
    ? "full-dual-oscillator browser drafts"
    : "local browser drafts for the current compatibility mode";
  const emptyNote = count === 0
    ? ` No saved card envelopes were reported; any remaining presets are ${browserDraftLabel}.`
    : "";
  if (count === 0 && session.reason === "manual") {
    selected = Math.min(3, presets.length - 1);
    render();
  }
  setStatus(`Loaded ${count} envelope${count === 1 ? "" : "s"} from the card.${emptyNote}${notes.length ? ` ${notes.join("; ")}.` : ""}`);
}

async function requestCardEnvelopes(reason = "manual", slot = null, expectedEnvelope = null) {
  if (envelopeReadSession) {
    setStatus("An envelope read is already in progress.");
    return;
  }
  if (!midiAccess) await connectMidi();
  await prepareMidiPorts();
  const output = selectedMidiOutput();
  if (!output) {
    setStatus("No MIDI output found for envelope readback.");
    return;
  }

  envelopeReadSession = {
    reason,
    slot,
    expectedEnvelope,
    mask: 0,
    pendingSlots: [],
    waitingForSlot: null,
    waitingForAmp2Slot: null,
    waitingForPd2Slot: null,
    waitingForPitchSlot: null,
    lastEnvelopeSlot: null,
    protocolVersion: null,
    cardEnvelopes: new Map(),
    retriesRemaining: ENVELOPE_READ_RETRIES
  };
  el.requestEnvelopes.disabled = true;
  try {
    const request = buildRequestEnvelopeSlotsSysex();
    output.send(request);
    logDeveloper("Envelope slot list request sent.", {
      output: output.name || output.id || "Unknown output",
      bytes: request
    });
    armEnvelopeReadTimeout();
    if (reason === "manual") {
      pulseButton(el.requestEnvelopes, "Read");
      setStatus("Reading saved envelopes from the card...");
    }
  } catch (error) {
    envelopeReadSession = null;
    clearEnvelopeReadTimer();
    el.requestEnvelopes.disabled = false;
    logDeveloper("Envelope inventory request failed.", {
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus("Envelope read request failed. Open Developer tools for details.");
  }
}

function handleEnvelopeSlotsResponse(data) {
  if (!envelopeReadSession || data.length !== 11) return;
  const version = data[7] & 0x7f;
  if (version < 1 || version > MAX_READABLE_ENVELOPE_PROTOCOL_VERSION) {
    clearEnvelopeReadTimer();
    envelopeReadSession = null;
    envelopeReadSupported = false;
    el.requestEnvelopes.disabled = false;
    renderDeveloperPorts();
    setStatus(`Unsupported card envelope protocol version ${version}.`);
    return;
  }

  envelopeReadSupported = true;
  detectedEnvelopeProtocol = version;
  renderDeveloperPorts();
  envelopeReadSession.protocolVersion = version;
  envelopeReadSession.mask = unpackUint14(data, 8) & 0xff;
  envelopeReadSession.pendingSlots = Array.from({ length: CUSTOM_SLOT_COUNT }, (_, slot) => slot)
    .filter((slot) => (envelopeReadSession.mask & (1 << slot)) !== 0);
  logDeveloper("Envelope slot list received.", {
    protocolVersion: version,
    mask: envelopeReadSession.mask,
    pendingSlots: envelopeReadSession.pendingSlots.map((slot) => slot + 1)
  });
  clearEnvelopeReadTimer();
  sendNextEnvelopeRequest();
}

function handleEnvelopeResponse(data) {
  if (!envelopeReadSession || (data.length !== 89 && data.length !== 105 && data.length !== 121 && data.length !== 129 && data.length !== 145 && data.length !== 161)) return;
  const slot = data[7] & 0x07;
  if (!envelopeReadSession.pendingSlots.includes(slot)) return;
  const protocolVersion = envelopeReadSession.protocolVersion || 1;

  let offset = 8;
  const hasNameFrame = protocolVersion >= 8 && (data.length === 105 || data.length === 121 || data.length === 145 || data.length === 161);
  const name = hasNameFrame
    ? decodeNameBytes(data, offset)
    : "";
  if (hasNameFrame) offset += 16;
  const hasPerformanceFrame = protocolVersion >= 9 && (data.length === 121 || data.length === 161);
  const performance = hasPerformanceFrame
    ? decodePerformanceSettingsBytes(data, offset)
    : null;
  if (hasPerformanceFrame) offset += 16;
  const readStages = () => Array.from({ length: STAGES }, () => {
    const stage = {
      level: clampInt(unpackUint14(data, offset), 0, MAX_LEVEL),
      time: clampInt(unpackUint21(data, offset + 2), 1, 192000)
    };
    offset += 5;
    return stage;
  });
  const amp = readStages();
  const pd = readStages();
  const thirdLane = data.length === 129 || data.length === 145 || data.length === 161 ? readStages() : null;
  const pd2 = protocolVersion >= 4 && thirdLane ? thirdLane : pd;
  const amp2 = amp;
  const pitch = protocolVersion >= 4 ? normalizePitchStages(null) : (thirdLane || normalizePitchStages(null));
  envelopeReadSession.cardEnvelopes.set(slot, {
    name,
    performance,
    amp,
    amp2,
    pd,
    pd2,
    pitch,
    amp2Read: protocolVersion < 5,
    pd2Read: protocolVersion < 4 || Boolean(thirdLane),
    pitchRead: protocolVersion < 4
  });
  envelopeReadSession.waitingForSlot = null;
  envelopeReadSession.lastEnvelopeSlot = slot;

  logDeveloper("Envelope slot response received.", {
    slot: slot + 1,
    length: data.length,
    protocolVersion,
    responseType: (data.length === 129 || data.length === 145 || data.length === 161) && protocolVersion >= 4
      ? "amp/pd1/pd2; pitch follows separately"
      : data.length === 129 || data.length === 145 || data.length === 161
        ? "amp/pd/pitch"
        : performance
          ? "name/settings/amp/pd standard; pitch may follow separately"
        : name
          ? "name/amp/pd standard; pitch may follow separately"
          : "amp/pd standard; pitch may follow separately"
  });
  if (protocolVersion >= 5) {
    requestAmp2EnvelopeForSlot(slot);
    armEnvelopeReadTimeout();
    return;
  }
  if (data.length === 89 || protocolVersion >= 4) {
    requestPitchEnvelopeForSlot(slot);
    armEnvelopeReadTimeout();
    return;
  }
  completeEnvelopeSlotRead(slot);
}

function requestAmp2EnvelopeForSlot(slot) {
  scheduleAuxiliaryEnvelopeRequest("Amp2", slot, buildRequestAmp2EnvelopeSysex, () => {
    envelopeReadSession.waitingForAmp2Slot = slot;
  });
}

function requestPd2EnvelopeForSlot(slot) {
  scheduleAuxiliaryEnvelopeRequest("PD2", slot, buildRequestPd2EnvelopeSysex, () => {
    envelopeReadSession.waitingForPd2Slot = slot;
  });
}

function requestPitchEnvelopeForSlot(slot) {
  scheduleAuxiliaryEnvelopeRequest("Pitch", slot, buildRequestPitchEnvelopeSysex, () => {
    envelopeReadSession.waitingForPitchSlot = slot;
  });
}

function handlePd2EnvelopeResponse(data) {
  if (data.length !== 49) return;
  const slot = data[7] & 0x07;

  let offset = 8;
  const pd2 = Array.from({ length: STAGES }, () => {
    const stage = {
      level: clampInt(unpackUint14(data, offset), 0, MAX_LEVEL),
      time: clampInt(unpackUint21(data, offset + 2), 1, 192000)
    };
    offset += 5;
    return stage;
  });

  if (!envelopeReadSession) {
    const existingIndex = presets.findIndex((item, index) =>
      index >= FACTORY_PRESET_COUNT && item.slot === slot);
    if (existingIndex >= FACTORY_PRESET_COUNT) {
      presets[existingIndex].pd2 = normalizeStages(pd2);
      presets[existingIndex].cardDirty = false;
      savePresets();
      render();
    }
    return;
  }

  const existing = envelopeReadSession.cardEnvelopes.get(slot);
  if (existing) {
    existing.pd2 = pd2;
    existing.pd2Read = true;
  }
  if (slot === envelopeReadSession.waitingForPd2Slot) {
    envelopeReadSession.waitingForPd2Slot = null;
  }

  logDeveloper("PD2 envelope response received.", {
    slot: slot + 1,
    length: data.length,
    responseType: "pd2"
  });
  if ((envelopeReadSession.protocolVersion || 1) >= 5 && slot === envelopeReadSession.lastEnvelopeSlot) {
    requestPitchEnvelopeForSlot(slot);
    armEnvelopeReadTimeout();
    return;
  }
  maybeCompleteProtocol5SlotRead(slot);
}

function handleAmp2EnvelopeResponse(data) {
  if (data.length !== 49) return;
  const slot = data[7] & 0x07;

  let offset = 8;
  const amp2 = Array.from({ length: STAGES }, () => {
    const stage = {
      level: clampInt(unpackUint14(data, offset), 0, MAX_LEVEL),
      time: clampInt(unpackUint21(data, offset + 2), 1, 192000)
    };
    offset += 5;
    return stage;
  });

  if (!envelopeReadSession) {
    const existingIndex = presets.findIndex((item, index) =>
      index >= FACTORY_PRESET_COUNT && item.slot === slot);
    if (existingIndex >= FACTORY_PRESET_COUNT) {
      presets[existingIndex].amp2 = normalizeStages(amp2);
      presets[existingIndex].cardDirty = false;
      savePresets();
      render();
    }
    return;
  }

  const existing = envelopeReadSession.cardEnvelopes.get(slot);
  if (existing) {
    existing.amp2 = amp2;
    existing.amp2Read = true;
  }
  if (slot === envelopeReadSession.waitingForAmp2Slot) {
    envelopeReadSession.waitingForAmp2Slot = null;
  }

  logDeveloper("Amp2 envelope response received.", {
    slot: slot + 1,
    length: data.length,
    responseType: "amp2"
  });
  if ((envelopeReadSession.protocolVersion || 1) >= 5 && slot === envelopeReadSession.lastEnvelopeSlot) {
    requestPd2EnvelopeForSlot(slot);
    armEnvelopeReadTimeout();
    return;
  }
  maybeCompleteProtocol5SlotRead(slot);
}

function handlePitchEnvelopeResponse(data) {
  if (data.length !== 49 && data.length !== 89 && data.length !== 95) return;
  const slot = data[7] & 0x07;

  let offset = 8;
  const readPitchStages = () => Array.from({ length: STAGES }, () => {
    const stage = {
      level: clampInt(unpackUint14(data, offset), 0, MAX_LEVEL),
      time: clampInt(unpackUint21(data, offset + 2), 1, 192000)
    };
    offset += 5;
    return stage;
  });
  const pitch = readPitchStages();
  const pitch2 = data.length === 89 || data.length === 95 ? readPitchStages() : pitch;
  const sustain = data.length === 95
    ? Array.from({ length: 6 }, () => clampInt(data[offset++] & 0x7f, 0, 0x7f))
    : null;

  if (!envelopeReadSession) {
    applyLatePitchReadback(slot, pitch, pitch2, sustain);
    return;
  }

  if (slot !== envelopeReadSession.waitingForPitchSlot &&
      slot !== envelopeReadSession.lastEnvelopeSlot) {
    const existingSlot = envelopeReadSession.cardEnvelopes.get(slot);
    if (existingSlot) {
      existingSlot.pitch = pitch;
      existingSlot.pitch2 = pitch2;
      if (sustain) existingSlot.sustain = normalizeSustainStages(sustain);
    }
    return;
  }

  const existing = envelopeReadSession.cardEnvelopes.get(slot);
  if (existing) {
    existing.pitch = pitch;
    existing.pitch2 = pitch2;
    if (sustain) existing.sustain = normalizeSustainStages(sustain);
    else if (envelopeReadSession.reason === "save" &&
        envelopeReadSession.slot === slot &&
        envelopeReadSession.expectedEnvelope?.sustain) {
      existing.sustain = normalizeSustainStages(envelopeReadSession.expectedEnvelope.sustain);
    }
    existing.pitchRead = true;
  }
  logDeveloper("Pitch envelope response received.", {
    slot: slot + 1,
    length: data.length,
    responseType: data.length === 95 ? "pitch osc1/osc2 + sustain" : data.length === 89 ? "pitch osc1/osc2" : "single pitch"
  });
  envelopeReadSession.waitingForPitchSlot = null;
  envelopeReadSession.lastEnvelopeSlot = null;
  completeEnvelopeSlotRead(slot);
}

function maybeCompleteProtocol5SlotRead(slot) {
  if (!envelopeReadSession || (envelopeReadSession.protocolVersion || 1) < 5) return;
  if (slot !== envelopeReadSession.waitingForPitchSlot &&
      slot !== envelopeReadSession.lastEnvelopeSlot) return;

  const existing = envelopeReadSession.cardEnvelopes.get(slot);
  if (!existing?.amp2Read || !existing?.pd2Read) return;
  if ((envelopeReadSession.protocolVersion || 1) >= 6 && !existing.pitchRead) return;

  logDeveloper("Dual-amplitude envelope readback completed from Amp2/PD2 responses.", {
    slot: slot + 1,
    pitchRead: Boolean(existing.pitchRead)
  });
  completeEnvelopeSlotRead(slot);
}

function applyLatePitchReadback(slot, pitch, pitch2 = pitch, sustain = null) {
  let updated = false;
  presets.forEach((item, index) => {
    if (index >= FACTORY_PRESET_COUNT && item.slot === slot) {
      item.pitch = normalizePitchStages(pitch);
      item.pitch2 = normalizePitchStages(pitch2);
      if (sustain) item.sustain = normalizeSustainStages(sustain);
      constrainPitchToEnvelope(item);
      item.cardDirty = false;
      updated = true;
    }
  });
  if (!updated) return;

  savePresets();
  render();
  logDeveloper("Applied late pitch envelope readback.", { slot: slot + 1 });
}

function completeEnvelopeSlotRead(slot) {
  if (!envelopeReadSession) return;
  if (slot === undefined) envelopeReadSession.pendingSlots.shift();
  else envelopeReadSession.pendingSlots =
    envelopeReadSession.pendingSlots.filter((pendingSlot) => pendingSlot !== slot);
  clearAuxiliaryEnvelopeRequestTimer();
  envelopeReadSession.waitingForSlot = null;
  envelopeReadSession.waitingForAmp2Slot = null;
  envelopeReadSession.waitingForPd2Slot = null;
  envelopeReadSession.waitingForPitchSlot = null;
  envelopeReadSession.lastEnvelopeSlot = null;
  clearEnvelopeReadTimer();
  sendNextEnvelopeRequest();
}

function handleMidi(event) {
  if (event.data[0] === 0xf0) {
    const now = Date.now();
    if (now - lastLoggedSysexAt > 50) {
      logDeveloper("Incoming SysEx received.", {
        length: event.data.length,
        command: event.data.length > 6 ? event.data[6] : "n/a",
        bytes: Array.from(event.data).slice(0, 16)
      });
      lastLoggedSysexAt = now;
    }
    handleSysexResponse(event.data);
    return;
  }

  const [status, note, velocity] = event.data;
  const type = status & 0xf0;
  if (type === 0x90 && velocity > 0) {
    el.pitchInput.value = note;
    audition(note);
  }
  if (type === 0x80 || (type === 0x90 && velocity === 0)) {
    stopAudio();
  }
}

function handleSysexResponse(data) {
  if (data[0] !== 0xf0 ||
      data[1] !== SYSEX_MANUFACTURER ||
      data[2] !== SYSEX_ID[0] ||
      data[3] !== SYSEX_ID[1] ||
      data[4] !== SYSEX_ID[2] ||
      data[5] !== SYSEX_ID[3] ||
      data[data.length - 1] !== 0xf7) {
    logDeveloper("Ignored non-C1ZZL3 SysEx frame.", {
      length: data.length,
      bytes: Array.from(data).slice(0, 16)
    });
    return;
  }

  if (data[6] === SYSEX_COMMAND_ENVELOPE_SLOTS_RESPONSE) {
    handleEnvelopeSlotsResponse(data);
    return;
  }
  if (data[6] === SYSEX_COMMAND_ENVELOPE_RESPONSE) {
    handleEnvelopeResponse(data);
    return;
  }
  if (data[6] === SYSEX_COMMAND_PITCH_ENVELOPE_RESPONSE) {
    handlePitchEnvelopeResponse(data);
    return;
  }
  if (data[6] === SYSEX_COMMAND_PD2_ENVELOPE_RESPONSE) {
    handlePd2EnvelopeResponse(data);
    return;
  }
  if (data[6] === SYSEX_COMMAND_AMP2_ENVELOPE_RESPONSE) {
    handleAmp2EnvelopeResponse(data);
    return;
  }
  if (data[6] !== SYSEX_COMMAND_SETTINGS_RESPONSE) {
    logDeveloper("Ignored unhandled C1ZZL3 SysEx command.", {
      command: data[6],
      length: data.length,
      bytes: Array.from(data).slice(0, 16)
    });
    return;
  }

  if (!settingsRequestPending) {
    logDeveloper("Ignored unsolicited settings response.", {
      length: data.length,
      reason: "Use Read Settings from Card to replace editor settings with card settings."
    });
    return;
  }

  settingsRequestPending = false;
  if (settingsRequestTimer !== null) {
    window.clearTimeout(settingsRequestTimer);
    settingsRequestTimer = null;
  }

  if (data.length === 16) {
    settingsProtocol = "stable";
    performanceSettings = {
      ...performanceSettings,
      ring: clampInt(unpackUint14(data, 7), 0, MAX_LEVEL),
      noise: clampInt(unpackUint14(data, 9), 0, MAX_LEVEL),
      midiInChannel: clampInt((data[11] & 0x0f) + 1, 1, 16),
      turingRange: clampInt(data[12], 1, 8),
      turingMidiOut: (data[13] & 0x01) !== 0,
      turingMidiChannel: clampInt((data[14] & 0x0f) + 1, 1, 16)
    };
  } else if (data.length === 22) {
    settingsProtocol = "extended";
    performanceSettings = {
      ...performanceSettings,
      ring: clampInt(unpackUint14(data, 7), 0, MAX_LEVEL),
      noise: clampInt(unpackUint14(data, 9), 0, MAX_LEVEL),
      pd: clampInt(unpackUint14(data, 11), 0, MAX_LEVEL),
      detune: clampInt(unpackUint14(data, 13), 0, MAX_LEVEL),
      waveform: waveControlToFamily(unpackUint14(data, 15)),
      waveform2: waveControlToFamily(unpackUint14(data, 15)),
      midiInChannel: clampInt((data[17] & 0x0f) + 1, 1, 16),
      turingRange: clampInt(data[18], 1, 8),
      turingMidiOut: (data[19] & 0x01) !== 0,
      turingMidiChannel: clampInt((data[20] & 0x0f) + 1, 1, 16)
    };
  } else if (data.length === 24) {
    settingsProtocol = "dual-oscillator";
    performanceSettings = {
      ...performanceSettings,
      ring: clampInt(unpackUint14(data, 7), 0, MAX_LEVEL),
      noise: clampInt(unpackUint14(data, 9), 0, MAX_LEVEL),
      pd: clampInt(unpackUint14(data, 11), 0, MAX_LEVEL),
      detune: clampInt(unpackUint14(data, 13), 0, MAX_LEVEL),
      waveform: waveControlToFamily(unpackUint14(data, 15)),
      waveform2: waveControlToFamily(unpackUint14(data, 17)),
      midiInChannel: clampInt((data[19] & 0x0f) + 1, 1, 16),
      turingRange: clampInt(data[20], 1, 8),
      turingMidiOut: (data[21] & 0x01) !== 0,
      turingMidiChannel: clampInt((data[22] & 0x0f) + 1, 1, 16)
    };
  } else {
    logDeveloper("Ignored unexpected settings response.", {
      length: data.length,
      bytes: Array.from(data)
    });
    return;
  }
  savePerformanceSettings();
  renderPerformanceSettings();
  renderDeveloperPorts();
  if (settingsRequestResolver) {
    settingsRequestResolver(true);
    settingsRequestResolver = null;
  }
  setStatus(`Loaded settings from card: PD ${performanceSettings.pd}, detune ${performanceSettings.detune}, osc1 wave ${WAVE_FAMILIES[performanceSettings.waveform] || performanceSettings.waveform}, osc2 wave ${WAVE_FAMILIES[performanceSettings.waveform2] || performanceSettings.waveform2}, ring ${performanceSettings.ring}, noise ${performanceSettings.noise}, MIDI in ch ${performanceSettings.midiInChannel}, Turing ${performanceSettings.turingRange} oct, Turing MIDI ${performanceSettings.turingMidiOut ? "on" : "off"} ch ${performanceSettings.turingMidiChannel}.`);
}

function downloadJson() {
  const blob = new Blob([JSON.stringify(presets, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "c1zzl3-full-dual-oscillators-presets.json";
  link.click();
  URL.revokeObjectURL(url);
}

function clampInt(value, min, max) {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isFinite(parsed)) return min;
  return Math.max(min, Math.min(max, parsed));
}

function escapeHtml(value) {
  return value.replace(/[&<>"']/g, (char) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    "\"": "&quot;",
    "'": "&#39;"
  }[char]));
}

function setStatus(message) {
  el.status.textContent = message;
}

function pulseButton(button, label = "Done") {
  if (!button) return;
  button.dataset.activatedLabel = label;
  button.classList.remove("was-activated");
  void button.offsetWidth;
  button.classList.add("was-activated");
  window.setTimeout(() => {
    button.classList.remove("was-activated");
    delete button.dataset.activatedLabel;
  }, 1200);
}

function canvasPoint(event) {
  const rect = el.canvas.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(rect.width, event.clientX - rect.left)),
    y: Math.max(0, Math.min(rect.height, event.clientY - rect.top)),
    width: rect.width,
    height: rect.height
  };
}

function pitchCanvasPoint(event) {
  const rect = el.pitchCanvas.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(rect.width, event.clientX - rect.left)),
    y: Math.max(0, Math.min(rect.height, event.clientY - rect.top)),
    width: rect.width,
    height: rect.height
  };
}

function findDragTarget(point) {
  const current = presets[selected];
  let best = null;
  const viewSamples = editorViewSamples();

  const lane = activeLaneView;
  const stages = current[lane];
  let x = 0;

  stages.forEach((stage, index) => {
    x = Math.min(point.width, x + (stage.time / viewSamples) * point.width);
    const y = levelY(stage.level, point.height);
    const distance = Math.hypot(point.x - x, point.y - y);
    if (!best || distance < best.distance) {
      best = { lane, index, distance };
    }
  });

  return best && best.distance <= 28 ? best : null;
}

function findPitchDragTarget(point) {
  const current = presets[selected];
  let best = null;
  const viewSamples = envelopeMaxSamples(current);
  let x = 0;
  const lane = activePitchLane;
  const stages = current[lane] || current.pitch;

  stages.forEach((stage, index) => {
    x = Math.min(point.width, x + (stage.time / viewSamples) * point.width);
    const y = levelY(stage.level, point.height);
    const distance = Math.hypot(point.x - x, point.y - y);
    if (!best || distance < best.distance) {
      best = { lane, index, distance };
    }
  });

  return best && best.distance <= 28 ? best : null;
}

function updateDraggedStage(event) {
  if (!dragTarget) return;
  const point = canvasPoint(event);

  if (selected < FACTORY_PRESET_COUNT) {
    if (!duplicateFactoryPreset()) return;
  }

  const level = Math.round((1 - ((point.y - 18) / Math.max(1, point.height - 36))) * MAX_LEVEL);
  const stages = presets[selected][dragTarget.lane];
  const stage = stages[dragTarget.index];
  const before = samplesBeforeStage(stages, dragTarget.index);
  const targetEnd = Math.round((point.x / Math.max(1, point.width)) * editorViewSamples());
  const maxStageTime = Math.max(1, 192000 - before);

  stage.level = clampInt(level, 0, MAX_LEVEL);
  stage.time = clampInt(targetEnd - before, 1, maxStageTime);
  constrainPitchToEnvelope(presets[selected]);
  if (presets[selected].slot !== null) presets[selected].cardDirty = true;
  savePresets();
  renderPresetList();
  syncStageInputs(dragTarget.lane, dragTarget.index);
  drawCurves();
  drawPitchCurve();
  updateExport();
}

function updateDraggedPitchStage(event) {
  if (!pitchDragTarget) return;
  const point = pitchCanvasPoint(event);

  if (selected < FACTORY_PRESET_COUNT) {
    if (!duplicateFactoryPreset()) return;
  }

  const level = Math.round((1 - ((point.y - 18) / Math.max(1, point.height - 36))) * MAX_LEVEL);
  const stages = presets[selected][pitchDragTarget.lane] || presets[selected].pitch;
  const stage = stages[pitchDragTarget.index];
  const before = samplesBeforeStage(stages, pitchDragTarget.index);
  const pitchMaxSamples = envelopeMaxSamples(presets[selected]);
  const targetEnd = Math.round((point.x / Math.max(1, point.width)) * pitchMaxSamples);
  const maxStageTime = Math.max(1, pitchMaxSamples - before);

  stage.level = clampInt(level, 0, MAX_LEVEL);
  stage.time = clampInt(targetEnd - before, 1, maxStageTime);
  constrainPitchToEnvelope(presets[selected]);
  if (presets[selected].slot !== null) presets[selected].cardDirty = true;
  savePresets();
  renderPresetList();
  syncStageInputs(pitchDragTarget.lane, pitchDragTarget.index);
  drawPitchCurve();
  updateExport();
}

function syncStageInputs(lane, index) {
  const container = lane === "pitch2" ? el.pitchStages : el[`${lane}Stages`];
  const row = container.querySelector(`[data-lane="${lane}"][data-index="${index}"]`);
  if (!row) return;
  const [levelInput, timeInput] = row.querySelectorAll("input");
  const stage = presets[selected][lane][index];
  levelInput.value = stage.level;
  timeInput.value = stage.time;
}

function updatePerformanceSetting(key, value) {
  if (key === "midiInChannel") {
    performanceSettings[key] = clampInt(value, 1, 16);
  } else if (key === "turingRange") {
    performanceSettings[key] = clampInt(value, 1, 8);
  } else if (key === "turingMidiOut") {
    performanceSettings[key] = Boolean(value);
  } else if (key === "turingMidiChannel") {
    performanceSettings[key] = clampInt(value, 1, 16);
  } else if (key === "ring" || key === "noise" || key === "pd" || key === "detune") {
    performanceSettings[key] = clampInt(value, 0, MAX_LEVEL);
  } else if (key === "waveform") {
    performanceSettings.waveform = clampInt(value, 0, 7);
  } else if (key === "waveform2") {
    performanceSettings.waveform2 = clampInt(value, 0, 7);
  }

  if (selected >= FACTORY_PRESET_COUNT) {
    presets[selected].performance = normalizePerformanceSettings(performanceSettings);
    if (presets[selected].slot !== null) presets[selected].cardDirty = true;
    savePresets();
    renderPresetList();
  }
  savePerformanceSettings();
}

async function sendPerformanceSettings(command = SYSEX_COMMAND_SETTINGS) {
  if (!midiAccess) {
    await connectMidi();
  }

  const output = selectedMidiOutput();
  if (!output) {
    setStatus("No MIDI output found for settings send.");
    return;
  }

  try {
    sendSettingsFrames(output, command);
  } catch (error) {
    logDeveloper("Settings SysEx failed.", {
      command,
      output: output.name || output.id || "Unknown output",
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus("Card settings send failed. Open Developer tools for details.");
    return;
  }
  const action = command === SYSEX_COMMAND_SAVE_SETTINGS ? "Saved to card" : "Sent to card";
  const protocolLabel = settingsProtocol === "unknown" ? "Protocol not confirmed yet." : `Protocol: ${describeSettingsProtocol()}.`;
  pulseButton(command === SYSEX_COMMAND_SAVE_SETTINGS ? null : el.sendSettings, command === SYSEX_COMMAND_SAVE_SETTINGS ? "Saved" : "Sent");
  setStatus(`${action}: PD ${performanceSettings.pd}, detune ${performanceSettings.detune}, osc1 wave ${WAVE_FAMILIES[performanceSettings.waveform] || performanceSettings.waveform}, osc2 wave ${WAVE_FAMILIES[performanceSettings.waveform2] || performanceSettings.waveform2}, ring ${performanceSettings.ring}, noise ${performanceSettings.noise}, MIDI in ch ${performanceSettings.midiInChannel}, Turing ${performanceSettings.turingRange} oct, Turing MIDI ${performanceSettings.turingMidiOut ? "on" : "off"} ch ${performanceSettings.turingMidiChannel} on ${output.name || "MIDI output"}. ${protocolLabel}`);
}

function sendSettingsFrames(output, command, delayMs = 0) {
  const sendAt = (frame, extraDelay = 0) => {
    const delay = delayMs + extraDelay;
    if (delay > 0) output.send(frame, window.performance.now() + delay);
    else output.send(frame);
  };

  if (settingsProtocol === "dual-oscillator") {
    sendAt(buildDualOscillatorSettingsSysex(command));
  } else if (settingsProtocol === "extended") {
    sendAt(buildExtendedSettingsSysex(command));
  } else if (settingsProtocol === "stable") {
    sendAt(buildStableSettingsSysex(command));
  } else {
    sendAt(buildStableSettingsSysex(command));
    if (command === SYSEX_COMMAND_SETTINGS) {
      sendAt(buildExtendedSettingsSysex(command), 40);
      sendAt(buildDualOscillatorSettingsSysex(command), 90);
    }
  }
}

async function requestPerformanceSettings(isProbe = false) {
  if (!midiAccess) {
    await connectMidi();
  }
  await prepareMidiPorts();

  const output = selectedMidiOutput();
  if (!output) {
    if (!isProbe) setStatus("No MIDI output found for settings request.");
    return false;
  }

  const result = new Promise((resolve) => {
    settingsRequestResolver = resolve;
    settingsRequestPending = true;
    if (settingsRequestTimer !== null) window.clearTimeout(settingsRequestTimer);
    settingsRequestTimer = window.setTimeout(() => {
      settingsRequestPending = false;
      settingsRequestTimer = null;
      if (settingsRequestResolver) {
        settingsRequestResolver(false);
        settingsRequestResolver = null;
      }
      if (!isProbe) setStatus("The card did not reply to Read Settings from Card.");
    }, SETTINGS_READ_TIMEOUT_MS);
  });

  try {
    const request = buildRequestSettingsSysex();
    output.send(request);
    logDeveloper("Settings request sent.", {
      output: output.name || output.id || "Unknown output",
      probe: isProbe,
      bytes: request
    });
  } catch (error) {
    settingsRequestPending = false;
    if (settingsRequestTimer !== null) {
      window.clearTimeout(settingsRequestTimer);
      settingsRequestTimer = null;
    }
    if (settingsRequestResolver) {
      settingsRequestResolver(false);
      settingsRequestResolver = null;
    }
    logDeveloper("Settings request failed.", {
      output: output.name || output.id || "Unknown output",
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    if (!isProbe) setStatus("Settings request failed. Open Developer tools for details.");
    return false;
  }
  if (!isProbe) {
    pulseButton(el.requestSettings, "Read");
    setStatus(`Requested settings from ${output.name || "MIDI output"}.`);
  }
  return result;
}

el.addPreset.addEventListener("click", () => {
  if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
    setStatus("Custom preset limit reached.");
    return;
  }

  presets.push(defaultCustomPreset());
  selected = presets.length - 1;
  savePresets();
  render();
  pulseButton(el.addPreset, "Added");
});

el.presetName.addEventListener("input", () => {
  if (selected < FACTORY_PRESET_COUNT) return;
  presets[selected].name = el.presetName.value || "Untitled";
  savePresets();
  renderPresetList();
  updateExport();
});

el.audition.addEventListener("click", () => {
  audition();
  pulseButton(el.audition, "Play");
});
el.stop.addEventListener("click", () => {
  stopAudio();
  pulseButton(el.stop, "Stop");
});
el.midiToggle.addEventListener("click", connectMidi);
el.themeToggle.addEventListener("click", () => {
  themeMode = themeMode === "light" ? "dark" : "light";
  saveThemeMode();
  renderThemeMode();
  pulseButton(el.themeToggle, themeMode === "light" ? "Light" : "Dark");
});
el.midiOutput.addEventListener("change", () => {
  const output = selectedMidiOutput();
  setStatus(output ? `Selected ${output.name || "MIDI output"}.` : "No MIDI output selected.");
});
el.downloadJson.addEventListener("click", () => {
  downloadJson();
  pulseButton(el.downloadJson, "Exported");
});
el.resetPreset.addEventListener("click", () => {
  const factory = factoryPresets.find((preset) => preset.name === "Bounce") || factoryPresets[0];
  presets[selected] = structuredClone(factory);
  savePresets();
  render();
  pulseButton(el.resetPreset, "Reset");
  setStatus("Preset restored to Bounce.");
});
el.ampView.addEventListener("click", () => {
  activeLaneView = "amp";
  renderLaneView();
  drawCurves();
  pulseButton(el.ampView, "Shown");
});
el.amp2View.addEventListener("click", () => {
  activeLaneView = "amp2";
  renderLaneView();
  drawCurves();
  pulseButton(el.amp2View, "Shown");
});
el.pdView.addEventListener("click", () => {
  activeLaneView = "pd";
  renderLaneView();
  drawCurves();
  pulseButton(el.pdView, "Shown");
});
el.pd2View.addEventListener("click", () => {
  activeLaneView = "pd2";
  renderLaneView();
  drawCurves();
  pulseButton(el.pd2View, "Shown");
});
el.pitch1View.addEventListener("click", () => {
  activePitchLane = "pitch";
  renderPitchLaneView();
  renderStages(activePitchLane, el.pitchStages);
  drawPitchCurve();
  pulseButton(el.pitch1View, "Shown");
});
el.pitch2View.addEventListener("click", () => {
  activePitchLane = "pitch2";
  renderPitchLaneView();
  renderStages(activePitchLane, el.pitchStages);
  drawPitchCurve();
  pulseButton(el.pitch2View, "Shown");
});
el.developerToggle.addEventListener("click", () => {
  developerMode = !developerMode;
  renderDeveloperMode();
  updateExport();
  pulseButton(el.developerToggle, developerMode ? "On" : "Off");
});
el.clearDeveloperLog.addEventListener("click", () => {
  clearDeveloperLog();
  pulseButton(el.clearDeveloperLog, "Cleared");
  setStatus("Developer diagnostics cleared.");
});
el.resetBrowserState.addEventListener("click", () => {
  pulseButton(el.resetBrowserState, "Cleared");
  resetBrowserState();
});
el.copyCpp.addEventListener("click", async () => {
  await navigator.clipboard.writeText(el.exportText.value);
  pulseButton(el.copyCpp, "Copied");
  setStatus("C++ preset copied.");
});
el.copySysex.addEventListener("click", async () => {
  if (!canSendSelectedEnvelope()) {
    setStatus("Preset 0, silent envelopes, and very short envelopes are not copied as custom SysEx.");
    return;
  }

  try {
    await navigator.clipboard.writeText(sysexHex());
  } catch (error) {
    logDeveloper("Copy SysEx failed.", {
      preset: presets[selected]?.name,
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus("Copy SysEx failed. Open Developer tools for details.");
    return;
  }
  pulseButton(el.copySysex, "Copied");
  setStatus(`SysEx preview frame copied for Custom ${Number(el.customSlot.value) + 1}.`);
});
el.sendSysex.addEventListener("click", () => sendSysex(SYSEX_COMMAND_PREVIEW));
el.loadEnvelopeAndSettings.addEventListener("click", loadEnvelopeAndSettings);
el.saveEnvelopeOnly.addEventListener("click", () => sendSysex(SYSEX_COMMAND_SAVE, { includePerformance: false }));
el.flashSysex.addEventListener("click", () => sendSysex(SYSEX_COMMAND_SAVE));
el.deleteSlot.addEventListener("click", deleteCustomSlot);
el.requestEnvelopes.addEventListener("click", () => requestCardEnvelopes());
el.requestSettings.addEventListener("click", requestPerformanceSettings);
el.sendSettings.addEventListener("click", () => sendPerformanceSettings(SYSEX_COMMAND_SETTINGS));
el.spreadPitchPoints.addEventListener("click", () => {
  spreadPitchPoints();
  pulseButton(el.spreadPitchPoints, "Spread");
});
el.pdControl.addEventListener("input", () => updatePerformanceSetting("pd", el.pdControl.value));
el.detuneControl.addEventListener("input", () => updatePerformanceSetting("detune", el.detuneControl.value));
el.performanceWaveSelect.addEventListener("change", () => updatePerformanceSetting("waveform", el.performanceWaveSelect.value));
el.performanceWave2Select.addEventListener("change", () => updatePerformanceSetting("waveform2", el.performanceWave2Select.value));
el.ringControl.addEventListener("input", () => updatePerformanceSetting("ring", el.ringControl.value));
el.noiseControl.addEventListener("input", () => updatePerformanceSetting("noise", el.noiseControl.value));
el.midiInChannel.addEventListener("input", () => updatePerformanceSetting("midiInChannel", el.midiInChannel.value));
el.turingRange.addEventListener("input", () => updatePerformanceSetting("turingRange", el.turingRange.value));
el.turingMidiOut.addEventListener("change", () => updatePerformanceSetting("turingMidiOut", el.turingMidiOut.checked));
el.turingMidiChannel.addEventListener("input", () => updatePerformanceSetting("turingMidiChannel", el.turingMidiChannel.value));
el.canvas.addEventListener("pointerdown", (event) => {
  const target = findDragTarget(canvasPoint(event));
  if (!target) return;
  dragTarget = target;
  el.canvas.setPointerCapture(event.pointerId);
  updateDraggedStage(event);
});
el.canvas.addEventListener("pointermove", updateDraggedStage);
el.canvas.addEventListener("pointerup", () => {
  dragTarget = null;
});
el.canvas.addEventListener("pointercancel", () => {
  dragTarget = null;
});

el.pitchCanvas.addEventListener("pointerdown", (event) => {
  const target = findPitchDragTarget(pitchCanvasPoint(event));
  if (!target) return;
  pitchDragTarget = target;
  el.pitchCanvas.setPointerCapture(event.pointerId);
  updateDraggedPitchStage(event);
});
el.pitchCanvas.addEventListener("pointermove", updateDraggedPitchStage);
el.pitchCanvas.addEventListener("pointerup", () => {
  pitchDragTarget = null;
});
el.pitchCanvas.addEventListener("pointercancel", () => {
  pitchDragTarget = null;
});

window.addEventListener("resize", () => {
  drawCurves();
  drawPitchCurve();
});
markEnvelopeLabActive();
window.setInterval(markEnvelopeLabActive, 2000);
renderDeveloperLog();
consumeImportedDraft();
render();
