const SAMPLE_RATE = 48000;
const MAX_LEVEL = 4095;
const STAGES = 8;
const CUSTOM_SLOT_COUNT = 8;
const MAX_BROWSER_CUSTOM_PRESETS = 32;
const AUDITION_TIME_SCALE = 4;
const EDITOR_VIEW_SAMPLES = SAMPLE_RATE * 4;
const MIN_SEND_SAMPLES = 960;
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
const ENVELOPE_PROTOCOL_VERSION = 1;

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
    [[4095, 4800], [3400, 30000], [0, 18000], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1]],
    [[1792, 4800], [900, 30000], [0, 18000], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1]]),
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

let presets = loadPresets();
let selected = 3;
let audioCtx;
let activeNodes = [];
let midiAccess;
let sendingSysex = false;
let auditionLoop = false;
let auditionToken = 0;
let dragTarget = null;
let performanceSettings = loadPerformanceSettings();
let activeLaneView = "amp";
let developerMode = false;
let themeMode = loadThemeMode();
let developerLogLines = [];
let settingsProtocol = "unknown";
let settingsRequestPending = false;
let settingsRequestTimer = null;
let envelopeReadSession = null;
let envelopeReadTimer = null;
let envelopeReadSupported = null;
const CZ_IMPORT_HANDOFF_KEY = "c1zzl3-cz-import-draft";
const HOSTED_EDITOR_URL = "https://soveda.github.io/CozmikC1zzl3/web-midi/editor/index.html";
let messageImportedDraft = null;
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
    messageImportedDraft = event.data.payload;
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
  themeToggle: document.querySelector("#themeToggle"),
  onlineEditorLink: document.querySelector("#onlineEditorLink"),
  ampStages: document.querySelector("#ampStages"),
  pdStages: document.querySelector("#pdStages"),
  ampView: document.querySelector("#ampView"),
  pdView: document.querySelector("#pdView"),
  developerToggle: document.querySelector("#developerToggle"),
  stagePanel: document.querySelector("#stagePanel"),
  stagePanelTitle: document.querySelector("#stagePanelTitle"),
  stagePanelSubtitle: document.querySelector("#stagePanelSubtitle"),
  developerPanel: document.querySelector("#developerPanel"),
  exportText: document.querySelector("#exportText"),
  developerPorts: document.querySelector("#developerPorts"),
  developerMidiRaw: document.querySelector("#developerMidiRaw"),
  developerLog: document.querySelector("#developerLog"),
  clearDeveloperLog: document.querySelector("#clearDeveloperLog"),
  status: document.querySelector("#status"),
  audition: document.querySelector("#audition"),
  stop: document.querySelector("#stop"),
  midiToggle: document.querySelector("#midiToggle"),
  pdControl: document.querySelector("#pdControl"),
  detuneControl: document.querySelector("#detuneControl"),
  performanceWaveSelect: document.querySelector("#performanceWaveSelect"),
  midiOutput: document.querySelector("#midiOutput"),
  copyCpp: document.querySelector("#copyCpp"),
  copySysex: document.querySelector("#copySysex"),
  sendSysex: document.querySelector("#sendSysex"),
  loadEnvelopeAndSettings: document.querySelector("#loadEnvelopeAndSettings"),
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

function preset(name, amp, pd, slot = null, cardDirty = false) {
  return {
    name,
    amp: normalizeStages(amp),
    pd: normalizeStages(pd),
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

function loadPresets() {
  try {
    const saved = JSON.parse(localStorage.getItem("c1zzl3-envelope-presets"));
    if (Array.isArray(saved)) {
      const custom = saved
        .slice(FACTORY_PRESET_COUNT)
        .map((item) => preset(item.name || "Custom preset", item.amp, item.pd, item.slot, item.cardDirty))
        .slice(0, MAX_BROWSER_CUSTOM_PRESETS);
      return [...structuredClone(factoryPresets), ...custom];
    }
    if (Array.isArray(saved?.customPresets)) {
      const custom = saved.customPresets
        .map((item) => preset(item.name || "Custom preset", item.amp, item.pd, item.slot, item.cardDirty))
        .slice(0, MAX_BROWSER_CUSTOM_PRESETS);
      return [...structuredClone(factoryPresets), ...custom];
    }
  } catch {
    /* Keep factory presets if saved data is malformed. */
  }
  return structuredClone(factoryPresets);
}

function savePresets() {
  localStorage.setItem("c1zzl3-envelope-presets", JSON.stringify({
    customPresets: presets
      .slice(FACTORY_PRESET_COUNT, FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS)
      .map((item) => ({
        name: item.name,
        amp: item.amp,
        pd: item.pd,
        slot: Number.isInteger(item.slot) ? item.slot : null,
        cardDirty: Boolean(item.cardDirty)
      }))
  }));
}

function loadPerformanceSettings() {
  try {
    const saved = JSON.parse(localStorage.getItem("c1zzl3-performance-settings"));
    if (saved && typeof saved === "object") {
      return {
        pd: clampInt(saved.pd ?? 0, 0, MAX_LEVEL),
        detune: clampInt(saved.detune ?? 2048, 0, MAX_LEVEL),
        waveform: clampInt(saved.waveform ?? 0, 0, 7),
        ring: clampInt(saved.ring, 0, MAX_LEVEL),
        noise: clampInt(saved.noise, 0, MAX_LEVEL),
        midiInChannel: clampInt(saved.midiInChannel, 1, 16),
        turingRange: clampInt(saved.turingRange ?? 2, 1, 8),
        turingMidiOut: saved.turingMidiOut !== false,
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
    ring: 0,
    noise: 0,
    midiInChannel: 1,
    turingRange: 2,
    turingMidiOut: true,
    turingMidiChannel: 1
  };
}

function savePerformanceSettings() {
  localStorage.setItem("c1zzl3-performance-settings", JSON.stringify(performanceSettings));
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
  window.history.replaceState(null, "", window.location.pathname);
}

function applyImportedPerformance(draft) {
  if (!draft || typeof draft !== "object") return;

  performanceSettings = {
    ...performanceSettings,
    detune: clampInt(draft.performance?.detune ?? performanceSettings.detune, 0, MAX_LEVEL),
    ring: clampInt(draft.performance?.ring ?? performanceSettings.ring, 0, MAX_LEVEL),
    noise: clampInt(draft.performance?.noise ?? performanceSettings.noise, 0, MAX_LEVEL),
    waveform: clampInt(draft.wave?.value ?? performanceSettings.waveform, 0, 7)
  };
}

function consumeImportedDraft() {
  try {
    if (messageImportedDraft) {
      const payload = messageImportedDraft;
      const draft = payload?.draft;
      if (draft && Array.isArray(draft.amp) && Array.isArray(draft.pd)) {
        const imported = preset(draft.name || "Imported CZ patch", draft.amp, draft.pd);
        if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
          presets[FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1] = imported;
          selected = FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1;
        } else {
          presets.push(imported);
          selected = presets.length - 1;
        }

        applyImportedPerformance(draft);

        savePresets();
        savePerformanceSettings();
        clearImportedDraftSources();
        return true;
      }
    }

    const params = new URLSearchParams(window.location.search);
    const queryPayload = params.get("cz-import");
    if (queryPayload) {
      const payload = JSON.parse(decodeURIComponent(queryPayload));
      const draft = payload?.draft;
      if (draft && Array.isArray(draft.amp) && Array.isArray(draft.pd)) {
        const imported = preset(draft.name || "Imported CZ patch", draft.amp, draft.pd);
        if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
          presets[FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1] = imported;
          selected = FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1;
        } else {
          presets.push(imported);
          selected = presets.length - 1;
        }

        applyImportedPerformance(draft);

        savePresets();
        savePerformanceSettings();
        clearImportedDraftSources();
        return true;
      }
    }

    const hashMatch = window.location.hash.match(/(?:^|&)cz-import=([^&]+)/);
    if (hashMatch) {
      const payload = JSON.parse(decodeURIComponent(hashMatch[1]));
      const draft = payload?.draft;
      if (draft && Array.isArray(draft.amp) && Array.isArray(draft.pd)) {
        const imported = preset(draft.name || "Imported CZ patch", draft.amp, draft.pd);
        if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
          presets[FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1] = imported;
          selected = FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1;
        } else {
          presets.push(imported);
          selected = presets.length - 1;
        }

        applyImportedPerformance(draft);

        savePresets();
        savePerformanceSettings();
        clearImportedDraftSources();
        return true;
      }
    }

    const raw = localStorage.getItem(CZ_IMPORT_HANDOFF_KEY);
    if (!raw) return false;

    const payload = JSON.parse(raw);
    const draft = payload?.draft;
    if (!draft || !Array.isArray(draft.amp) || !Array.isArray(draft.pd)) {
      localStorage.removeItem(CZ_IMPORT_HANDOFF_KEY);
      return false;
    }

    const imported = preset(draft.name || "Imported CZ patch", draft.amp, draft.pd);
    if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
      presets[FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1] = imported;
      selected = FACTORY_PRESET_COUNT + MAX_BROWSER_CUSTOM_PRESETS - 1;
    } else {
      presets.push(imported);
      selected = presets.length - 1;
    }

    applyImportedPerformance(draft);

    savePresets();
    savePerformanceSettings();
    clearImportedDraftSources();
    return true;
  } catch {
    localStorage.removeItem(CZ_IMPORT_HANDOFF_KEY);
    return false;
  }
}

function render() {
  selected = Math.max(0, Math.min(selected, presets.length - 1));
  const current = presets[selected];

  renderPresetList();
  el.presetName.value = current.name;
  el.presetName.disabled = selected < FACTORY_PRESET_COUNT;
  el.addPreset.disabled = customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS;
  renderStages("amp", el.ampStages);
  renderStages("pd", el.pdStages);
  renderPerformanceSettings();
  renderThemeMode();
  renderLaneView();
  renderDeveloperMode();
  drawCurves();
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
  drawCurves();
}

function renderLaneView() {
  const ampActive = activeLaneView === "amp";
  el.ampView.classList.toggle("is-active", ampActive);
  el.ampView.setAttribute("aria-selected", String(ampActive));
  el.pdView.classList.toggle("is-active", !ampActive);
  el.pdView.setAttribute("aria-selected", String(!ampActive));
  el.stagePanelTitle.textContent = ampActive ? "Amplitude" : "Phase Distortion";
  el.stagePanelSubtitle.textContent = "Level / samples";
  el.ampStages.classList.toggle("is-hidden", !ampActive);
  el.pdStages.classList.toggle("is-hidden", ampActive);
  el.ampStages.setAttribute("aria-hidden", String(!ampActive));
  el.pdStages.setAttribute("aria-hidden", String(ampActive));
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

  const max = key === "level" ? MAX_LEVEL : 192000;
  const min = key === "level" ? 0 : 1;
  presets[selected][lane][index][key] = clampInt(value, min, max);
  if (presets[selected].slot !== null) presets[selected].cardDirty = true;
  savePresets();
  renderPresetList();
  drawCurves();
  updateExport();
}

function duplicateFactoryPreset() {
  if (selected >= FACTORY_PRESET_COUNT) return true;
  if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
    setStatus("Custom preset limit reached.");
    return false;
  }

  const source = presets[selected];
  presets.push(preset(`${source.name} copy`, source.amp, source.pd));
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
  drawLane(ctx, presets[selected].amp, theme.amp, w, h, viewSamples);
  drawLane(ctx, presets[selected].pd, theme.pd, w, h, viewSamples);
  drawHandles(ctx, presets[selected].amp, theme.amp, w, h, viewSamples, activeLaneView === "amp", theme);
  drawHandles(ctx, presets[selected].pd, theme.pd, w, h, viewSamples, activeLaneView === "pd", theme);

  ctx.fillStyle = theme.muted;
  ctx.font = "12px system-ui";
  ctx.fillText(`Amp`, 14, 22);
  ctx.fillStyle = theme.pd;
  ctx.fillText(`PD`, 62, 22);
  ctx.fillStyle = theme.muted;
  ctx.fillText(`${(totalSamples(presets[selected].amp) / SAMPLE_RATE).toFixed(2)}s / ${(viewSamples / SAMPLE_RATE).toFixed(0)}s`, w - 94, h - 16);
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
    const pointColor = color === theme.amp ? theme.ampPoint : theme.pdPoint;
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
    pd: styles.getPropertyValue("--graph-pd").trim() || styles.getPropertyValue("--pd").trim() || "#6ee7c8",
    ampPoint: styles.getPropertyValue("--graph-amp-point").trim() || styles.getPropertyValue("--amp").trim() || "#ffcc66",
    pdPoint: styles.getPropertyValue("--graph-pd-point").trim() || styles.getPropertyValue("--pd").trim() || "#6ee7c8",
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
    totalSamples(current.pd));
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
  el.exportText.value = `case EnvelopePreset::${className}:\n    return {{\n${cppLane(current.amp)}\n    }, {\n${cppLane(current.pd)}\n    }};`;
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

  osc.type = auditionOscType(el.waveSelect.value);
  osc.frequency.value = midiToHz(note);
  filter.type = "lowpass";
  filter.frequency.value = 220;
  gain.gain.value = 0;

  const duration = scaledSamples(current.amp) / SAMPLE_RATE;
  scheduleEnvelope(gain.gain, current.amp, now, 0, 0.35);
  scheduleEnvelope(filter.frequency, current.pd, now, 220, 4200);

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

  el.developerPorts.textContent = `Settings protocol: ${describeSettingsProtocol()}\nEnvelope readback: ${describeEnvelopeReadback()}\n\nInputs (${inputs.length})\n${inputText}\n\nOutputs (${outputs.length})\n${outputText}`;
  renderDeveloperMidiRaw();
}

function describeSettingsProtocol() {
  if (settingsProtocol === "stable") return "Stable firmware";
  if (settingsProtocol === "experimental") return "Experimental firmware";
  return "Not detected yet";
}

function describeEnvelopeReadback() {
  if (envelopeReadSupported === true) return "Supported";
  if (envelopeReadSupported === false) return "No response";
  return "Not checked yet";
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
    refreshMidiPorts();
    midiAccess.onstatechange = refreshMidiPorts;
    el.midiToggle.classList.add("is-active");
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

async function sendSysex(command = SYSEX_COMMAND_PREVIEW) {
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

  let frame;
  try {
    frame = buildSysex(command);
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
  const expectedEnvelope = isFlash ? structuredClone(presets[selected]) : null;
  const sendCooldownMs = isFlash ? 1800 : 250;
  const summary = frameSummary();
  sendingSysex = true;
  el.sendSysex.disabled = true;
  el.loadEnvelopeAndSettings.disabled = true;
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
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
    setStatus("Web MIDI send failed. Open Developer tools for details.");
    return;
  }
  const slotLabel = `Custom ${slot + 1}`;
  const status = isFlash
    ? `Saved ${slotLabel}; after reset it appears after the factory presets. Amp max ${summary.ampMax}, ${summary.seconds}s.`
    : `Loaded ${slotLabel} until reset. Amp max ${summary.ampMax}, ${summary.seconds}s.`;
  if (isFlash) {
    bindSelectedPresetToSlot(slot);
    if (envelopeReadSupported === true) {
      window.setTimeout(() => requestCardEnvelopes("save", slot, expectedEnvelope), 350);
    }
  }
  setStatus(`${status} ${frame.length} byte SysEx to ${output.name || "MIDI output"}.`);

  window.setTimeout(() => {
    sendingSysex = false;
    el.sendSysex.disabled = false;
    el.loadEnvelopeAndSettings.disabled = false;
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
    const envelopeFrame = buildSysex(SYSEX_COMMAND_PREVIEW);
    sendingSysex = true;
    el.sendSysex.disabled = true;
    el.loadEnvelopeAndSettings.disabled = true;
    el.flashSysex.disabled = true;
    el.deleteSlot.disabled = true;
    output.send(envelopeFrame);
    sendSettingsFrames(output, SYSEX_COMMAND_SETTINGS, 60);
    const slot = clampInt(el.customSlot.value, 0, CUSTOM_SLOT_COUNT - 1);
    setStatus(`Loaded ${presets[selected].name} into RAM slot ${slot + 1} and sent the current settings. Both are temporary until saved.`);
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
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
    setStatus("Delete failed. Open Developer tools for details.");
    return;
  }
  if (envelopeReadSupported !== true) {
    markCardSlotLocal(slot);
    savePresets();
    render();
    setStatus(`Delete sent for card slot ${slot + 1}. Use Read Envelopes from Card to confirm it.`);
  } else {
    setStatus(`Delete sent for card slot ${slot + 1}; checking the card...`);
    window.setTimeout(() => requestCardEnvelopes("delete", slot), 350);
  }

  window.setTimeout(() => {
    sendingSysex = false;
    el.sendSysex.disabled = false;
    el.loadEnvelopeAndSettings.disabled = false;
    el.flashSysex.disabled = false;
    el.deleteSlot.disabled = false;
  }, 1800);
}

function buildSysex(command) {
  if (!canSendSelectedEnvelope()) {
    throw new Error("Cannot build SysEx for preset 0, a silent envelope, or a very short envelope.");
  }

  const slot = clampInt(el.customSlot.value, 0, CUSTOM_SLOT_COUNT - 1);
  const nameBytes = ensureArray(encodeName(presets[selected].name), "encodeName()");
  const payload = [slot & 0x7f, ...nameBytes];
  appendStages(payload, presets[selected].amp);
  appendStages(payload, presets[selected].pd);
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "payload"), 0xf7];
}

function buildSettingsSysex(command) {
  if (settingsProtocol === "experimental") {
    return buildExperimentalSettingsSysex(command);
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

function buildExperimentalSettingsSysex(command) {
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
  return [0xf0, SYSEX_MANUFACTURER, ...ensureArray(SYSEX_ID, "SYSEX_ID"), command, ...ensureArray(payload, "experimental settings payload"), 0xf7];
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
  return stagesMatch(a.amp, b.amp) && stagesMatch(a.pd, b.pd);
}

function markCardSlotLocal(slot) {
  presets.forEach((item, index) => {
    if (index >= FACTORY_PRESET_COUNT && item.slot === slot) {
      item.slot = null;
      item.cardDirty = false;
    }
  });
}

function reconcileCardEnvelopes(mask, cardEnvelopes) {
  let preservedChanges = 0;
  let skipped = 0;

  for (let slot = 0; slot < CUSTOM_SLOT_COUNT; slot++) {
    if ((mask & (1 << slot)) === 0) markCardSlotLocal(slot);
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
        existing.amp = normalizeStages(cardEnvelope.amp);
        existing.pd = normalizeStages(cardEnvelope.pd);
        existing.cardDirty = false;
        return;
      }
    }

    if (customPresetCount() >= MAX_BROWSER_CUSTOM_PRESETS) {
      skipped++;
      return;
    }
    presets.push(preset(`Card Envelope ${slot + 1}`, cardEnvelope.amp, cardEnvelope.pd, slot, false));
  });

  savePresets();
  render();
  return { preservedChanges, skipped };
}

function clearEnvelopeReadTimer() {
  if (envelopeReadTimer !== null) {
    window.clearTimeout(envelopeReadTimer);
    envelopeReadTimer = null;
  }
}

function armEnvelopeReadTimeout() {
  clearEnvelopeReadTimer();
  envelopeReadTimer = window.setTimeout(() => {
    const session = envelopeReadSession;
    envelopeReadSession = null;
    envelopeReadTimer = null;
    envelopeReadSupported = false;
    el.requestEnvelopes.disabled = false;
    renderDeveloperPorts();
    if (session?.reason === "delete") {
      markCardSlotLocal(session.slot);
      savePresets();
      render();
    }
    setStatus("The card did not reply to the envelope read request. This firmware may not support envelope readback.");
  }, 2500);
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
  try {
    output.send(buildRequestEnvelopeSysex(slot));
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
  if (session.reason === "save") {
    const cardEnvelope = session.cardEnvelopes.get(session.slot);
    const verified = cardEnvelope && session.expectedEnvelope &&
      envelopesMatch(cardEnvelope, session.expectedEnvelope);
    setStatus(verified
      ? `Verified envelope saved in card slot ${session.slot + 1}.`
      : `The envelope in card slot ${session.slot + 1} did not match the saved draft.`);
    return;
  }
  if (session.reason === "delete") {
    const deleted = (session.mask & (1 << session.slot)) === 0;
    setStatus(deleted
      ? `Verified card slot ${session.slot + 1} is empty. The browser draft was retained as Local only.`
      : `Card slot ${session.slot + 1} still reports a saved envelope.`);
    return;
  }

  const notes = [];
  if (result.preservedChanges) notes.push(`${result.preservedChanges} changed local draft${result.preservedChanges === 1 ? " was" : "s were"} preserved`);
  if (result.skipped) notes.push(`${result.skipped} card envelope${result.skipped === 1 ? " was" : "s were"} not added because the browser list is full`);
  setStatus(`Loaded ${count} envelope${count === 1 ? "" : "s"} from the card.${notes.length ? ` ${notes.join("; ")}.` : ""}`);
}

async function requestCardEnvelopes(reason = "manual", slot = null, expectedEnvelope = null) {
  if (envelopeReadSession) {
    setStatus("An envelope read is already in progress.");
    return;
  }
  if (!midiAccess) await connectMidi();
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
    cardEnvelopes: new Map()
  };
  el.requestEnvelopes.disabled = true;
  try {
    output.send(buildRequestEnvelopeSlotsSysex());
    armEnvelopeReadTimeout();
    if (reason === "manual") setStatus("Reading saved envelopes from the card...");
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
  if (version !== ENVELOPE_PROTOCOL_VERSION) {
    clearEnvelopeReadTimer();
    envelopeReadSession = null;
    envelopeReadSupported = false;
    el.requestEnvelopes.disabled = false;
    renderDeveloperPorts();
    setStatus(`Unsupported card envelope protocol version ${version}.`);
    return;
  }

  envelopeReadSupported = true;
  renderDeveloperPorts();
  envelopeReadSession.mask = unpackUint14(data, 8) & 0xff;
  envelopeReadSession.pendingSlots = Array.from({ length: CUSTOM_SLOT_COUNT }, (_, slot) => slot)
    .filter((slot) => (envelopeReadSession.mask & (1 << slot)) !== 0);
  clearEnvelopeReadTimer();
  sendNextEnvelopeRequest();
}

function handleEnvelopeResponse(data) {
  if (!envelopeReadSession || data.length !== 89) return;
  const slot = data[7] & 0x07;
  if (slot !== envelopeReadSession.waitingForSlot) return;

  let offset = 8;
  const readStages = () => Array.from({ length: STAGES }, () => {
    const stage = {
      level: clampInt(unpackUint14(data, offset), 0, MAX_LEVEL),
      time: clampInt(unpackUint21(data, offset + 2), 1, 192000)
    };
    offset += 5;
    return stage;
  });
  envelopeReadSession.cardEnvelopes.set(slot, { amp: readStages(), pd: readStages() });
  envelopeReadSession.pendingSlots.shift();
  envelopeReadSession.waitingForSlot = null;
  clearEnvelopeReadTimer();
  sendNextEnvelopeRequest();
}

function handleMidi(event) {
  if (event.data[0] === 0xf0) {
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
  if (data[6] !== SYSEX_COMMAND_SETTINGS_RESPONSE) return;

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
    settingsProtocol = "experimental";
    performanceSettings = {
      ...performanceSettings,
      ring: clampInt(unpackUint14(data, 7), 0, MAX_LEVEL),
      noise: clampInt(unpackUint14(data, 9), 0, MAX_LEVEL),
      pd: clampInt(unpackUint14(data, 11), 0, MAX_LEVEL),
      detune: clampInt(unpackUint14(data, 13), 0, MAX_LEVEL),
      waveform: waveControlToFamily(unpackUint14(data, 15)),
      midiInChannel: clampInt((data[17] & 0x0f) + 1, 1, 16),
      turingRange: clampInt(data[18], 1, 8),
      turingMidiOut: (data[19] & 0x01) !== 0,
      turingMidiChannel: clampInt((data[20] & 0x0f) + 1, 1, 16)
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
  setStatus(`Loaded settings from card: PD ${performanceSettings.pd}, detune ${performanceSettings.detune}, wave ${WAVE_FAMILIES[performanceSettings.waveform] || performanceSettings.waveform}, ring ${performanceSettings.ring}, noise ${performanceSettings.noise}, MIDI in ch ${performanceSettings.midiInChannel}, Turing ${performanceSettings.turingRange} oct, Turing MIDI ${performanceSettings.turingMidiOut ? "on" : "off"} ch ${performanceSettings.turingMidiChannel}.`);
}

function downloadJson() {
  const blob = new Blob([JSON.stringify(presets, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "c1zzl3-envelope-presets.json";
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

function canvasPoint(event) {
  const rect = el.canvas.getBoundingClientRect();
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
  if (presets[selected].slot !== null) presets[selected].cardDirty = true;
  savePresets();
  renderPresetList();
  syncStageInputs(dragTarget.lane, dragTarget.index);
  drawCurves();
  updateExport();
}

function syncStageInputs(lane, index) {
  const row = el[`${lane}Stages`].querySelector(`[data-lane="${lane}"][data-index="${index}"]`);
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
  setStatus(`${action}: PD ${performanceSettings.pd}, detune ${performanceSettings.detune}, wave ${WAVE_FAMILIES[performanceSettings.waveform] || performanceSettings.waveform}, ring ${performanceSettings.ring}, noise ${performanceSettings.noise}, MIDI in ch ${performanceSettings.midiInChannel}, Turing ${performanceSettings.turingRange} oct, Turing MIDI ${performanceSettings.turingMidiOut ? "on" : "off"} ch ${performanceSettings.turingMidiChannel} on ${output.name || "MIDI output"}. ${protocolLabel}`);
}

function sendSettingsFrames(output, command, delayMs = 0) {
  const sendAt = (frame, extraDelay = 0) => {
    const delay = delayMs + extraDelay;
    if (delay > 0) output.send(frame, window.performance.now() + delay);
    else output.send(frame);
  };

  if (settingsProtocol === "experimental") {
    sendAt(buildExperimentalSettingsSysex(command));
  } else if (settingsProtocol === "stable") {
    sendAt(buildStableSettingsSysex(command));
  } else {
    sendAt(buildStableSettingsSysex(command));
    if (command === SYSEX_COMMAND_SETTINGS) {
      sendAt(buildExperimentalSettingsSysex(command), 40);
    }
  }
}

async function requestPerformanceSettings(isProbe = false) {
  if (!midiAccess) {
    await connectMidi();
  }

  const output = selectedMidiOutput();
  if (!output) {
    setStatus("No MIDI output found for settings request.");
    return;
  }

  try {
    settingsRequestPending = true;
    if (settingsRequestTimer !== null) window.clearTimeout(settingsRequestTimer);
    settingsRequestTimer = window.setTimeout(() => {
      settingsRequestPending = false;
      settingsRequestTimer = null;
      setStatus("The card did not reply to Read Settings from Card.");
    }, 1500);
    output.send(buildRequestSettingsSysex());
  } catch (error) {
    settingsRequestPending = false;
    if (settingsRequestTimer !== null) {
      window.clearTimeout(settingsRequestTimer);
      settingsRequestTimer = null;
    }
    logDeveloper("Settings request failed.", {
      output: output.name || output.id || "Unknown output",
      message: error?.message || "Unknown error",
      stack: error?.stack || "No stack trace"
    });
    setStatus("Settings request failed. Open Developer tools for details.");
    return;
  }
  if (!isProbe) {
    setStatus(`Requested settings from ${output.name || "MIDI output"}.`);
  }
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
});

el.presetName.addEventListener("input", () => {
  if (selected < FACTORY_PRESET_COUNT) return;
  presets[selected].name = el.presetName.value || "Untitled";
  savePresets();
  renderPresetList();
  updateExport();
});

el.audition.addEventListener("click", () => audition());
el.stop.addEventListener("click", stopAudio);
el.midiToggle.addEventListener("click", connectMidi);
el.themeToggle.addEventListener("click", () => {
  themeMode = themeMode === "light" ? "dark" : "light";
  saveThemeMode();
  renderThemeMode();
});
el.midiOutput.addEventListener("change", () => {
  const output = selectedMidiOutput();
  setStatus(output ? `Selected ${output.name || "MIDI output"}.` : "No MIDI output selected.");
});
el.downloadJson.addEventListener("click", downloadJson);
el.resetPreset.addEventListener("click", () => {
  const factory = factoryPresets.find((preset) => preset.name === "Bounce") || factoryPresets[0];
  presets[selected] = structuredClone(factory);
  savePresets();
  render();
  setStatus("Preset restored to Bounce.");
});
el.ampView.addEventListener("click", () => {
  activeLaneView = "amp";
  renderLaneView();
  drawCurves();
});
el.pdView.addEventListener("click", () => {
  activeLaneView = "pd";
  renderLaneView();
  drawCurves();
});
el.developerToggle.addEventListener("click", () => {
  developerMode = !developerMode;
  renderDeveloperMode();
  updateExport();
});
el.clearDeveloperLog.addEventListener("click", () => {
  clearDeveloperLog();
  setStatus("Developer diagnostics cleared.");
});
el.copyCpp.addEventListener("click", async () => {
  await navigator.clipboard.writeText(el.exportText.value);
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
  setStatus(`SysEx preview frame copied for Custom ${Number(el.customSlot.value) + 1}.`);
});
el.sendSysex.addEventListener("click", () => sendSysex(SYSEX_COMMAND_PREVIEW));
el.loadEnvelopeAndSettings.addEventListener("click", loadEnvelopeAndSettings);
el.flashSysex.addEventListener("click", () => sendSysex(SYSEX_COMMAND_SAVE));
el.deleteSlot.addEventListener("click", deleteCustomSlot);
el.requestEnvelopes.addEventListener("click", () => requestCardEnvelopes());
el.requestSettings.addEventListener("click", requestPerformanceSettings);
el.sendSettings.addEventListener("click", () => sendPerformanceSettings(SYSEX_COMMAND_SETTINGS));
el.pdControl.addEventListener("input", () => updatePerformanceSetting("pd", el.pdControl.value));
el.detuneControl.addEventListener("input", () => updatePerformanceSetting("detune", el.detuneControl.value));
el.performanceWaveSelect.addEventListener("change", () => updatePerformanceSetting("waveform", el.performanceWaveSelect.value));
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

window.addEventListener("resize", drawCurves);
renderDeveloperLog();
consumeImportedDraft();
render();
