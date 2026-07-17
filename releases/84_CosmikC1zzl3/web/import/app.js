const el = {
  file: document.querySelector("#czFile"),
  dropzone: document.querySelector(".dropzone"),
  importStatus: document.querySelector("#importStatus"),
  themeToggle: document.querySelector("#themeToggle"),
  validationBox: document.querySelector("#validationBox"),
  patchTypeBox: document.querySelector("#patchTypeBox"),
  summaryBox: document.querySelector("#summaryBox"),
  decodedPatchBox: document.querySelector("#decodedPatchBox"),
  mappedDraftBox: document.querySelector("#mappedDraftBox"),
  confidenceBox: document.querySelector("#confidenceBox"),
  supportedOutputBox: document.querySelector("#supportedOutputBox"),
  decodedBox: document.querySelector("#decodedBox"),
  draftNameBox: document.querySelector("#draftNameBox"),
  waveBox: document.querySelector("#waveBox"),
  perfBox: document.querySelector("#perfBox"),
  ampDraftBox: document.querySelector("#ampDraftBox"),
  pdDraftBox: document.querySelector("#pdDraftBox"),
  openDraft: document.querySelector("#openDraft"),
};

let currentDraft = null;
let themeMode = loadThemeMode();
const HANDOFF_KEY = "c1zzl3-cz-import-draft";
const THEME_KEY = "c1zzl3-theme-mode";
const LOCAL_EDITOR_URL = "../index.html";
const HOSTED_EDITOR_URL = "https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/index.html";
const ENVELOPE_LAB_WINDOW_NAME = "c1zzl3-envelope-lab";
const WAVE_FAMILIES = [
  { label: "Saw", value: 0, hint: "lower CC23 range" },
  { label: "Square", value: 1, hint: "lower-mid CC23 range" },
  { label: "Narrow pulse", value: 2, hint: "pulse-focused region" },
  { label: "Double sine", value: 3, hint: "mid CC23 range" },
  { label: "Saw pulse", value: 4, hint: "lower-mid to mid CC23 range" },
  { label: "Resonant saw window", value: 5, hint: "upper CC23 range" },
  { label: "Resonant triangle window", value: 6, hint: "upper CC23 range" },
  { label: "Resonant trapezoid window", value: 7, hint: "upper CC23 range" },
];
const MIN_DECODED_PATCH_BYTES = 48;
const MAX_DECODED_PATCH_BYTES = 512;
const CZ_SEND_PATCH_COMMAND = 0x20;
const CZ_REQUEST_PATCH_COMMAND = 0x10;

function loadThemeMode() {
  try {
    const saved = localStorage.getItem(THEME_KEY);
    if (saved === "light" || saved === "dark") return saved;
  } catch {
    /* Keep default theme if saved data is unavailable. */
  }
  return "dark";
}

function saveThemeMode() {
  try {
    localStorage.setItem(THEME_KEY, themeMode);
  } catch {
    /* Theme persistence is optional. */
  }
}

function renderThemeMode() {
  document.body.classList.toggle("theme-light", themeMode === "light");
  el.themeToggle.classList.toggle("is-active", themeMode === "light");
  el.themeToggle.textContent = themeMode === "light" ? "Light" : "Dark";
  el.themeToggle.setAttribute("aria-checked", String(themeMode === "light"));
}

function getEditorUrl() {
  const host = window.location.hostname;
  const isLocalDev = host === "localhost" || host === "127.0.0.1" || host === "::1";
  return isLocalDev ? LOCAL_EDITOR_URL : HOSTED_EDITOR_URL;
}

function setStatus(text, tone = "ok") {
  el.importStatus.textContent = text;
  el.importStatus.dataset.tone = tone;
}

function toHex(bytes, limit = 32) {
  return Array.from(bytes.slice(0, limit), (b) => b.toString(16).padStart(2, "0")).join(" ");
}

function findSysexFrame(data) {
  let start = -1;
  for (let i = 0; i < data.length; i++) {
    if (data[i] === 0xf0) start = i;
    if (data[i] === 0xf7 && start >= 0) return data.slice(start, i + 1);
  }
  return null;
}

function unpackNibbles(payload) {
  const bytes = [];
  for (let i = 0; i + 1 < payload.length; i += 2) {
    bytes.push((payload[i] & 0x0f) | ((payload[i + 1] & 0x0f) << 4));
  }
  return bytes;
}

function isNibblePayload(payload) {
  return Array.from(payload).every((b) => b >= 0 && b <= 15);
}

function commandName(command) {
  if (command === CZ_SEND_PATCH_COMMAND) return "send patch data";
  if (command === CZ_REQUEST_PATCH_COMMAND) return "request patch data";
  return `unknown command 0x${command.toString(16).padStart(2, "0")}`;
}

function locationName(location) {
  if (location === 0x60) return "current sound / edit buffer";
  if (location >= 0x20 && location <= 0x2f) return `internal memory ${location - 0x1f}`;
  if (location >= 0x00 && location <= 0x0f) return `preset memory ${location + 1}`;
  if (location >= 0x40 && location <= 0x4f) return `cartridge memory ${location - 0x3f}`;
  return `location 0x${location.toString(16).padStart(2, "0")}`;
}

function decodedLengthSupported(length) {
  return length >= MIN_DECODED_PATCH_BYTES && length <= MAX_DECODED_PATCH_BYTES;
}

function analyzePayloadCandidate(frame, offset, label) {
  const payload = frame.slice(offset, -1);
  const nibblePayload = isNibblePayload(payload);
  const evenNibbleCount = payload.length % 2 === 0;
  const decodedBytes = nibblePayload ? unpackNibbles(payload) : [];
  const decodedLengthOk = decodedLengthSupported(decodedBytes.length);

  return {
    offset,
    label,
    payload,
    nibblePayload,
    evenNibbleCount,
    decodedBytes,
    decodedLengthOk,
    supported: nibblePayload && evenNibbleCount && decodedLengthOk
  };
}

function analyzeCzFrame(frame) {
  const manufacturer = frame[1];
  const familyA = frame[2];
  const familyB = frame[3];
  const channelByte = frame[4];
  const command = frame[5] ?? 0;
  const location = frame[6] ?? 0;
  const looksCasio = manufacturer === 0x44;
  const looksCz =
    looksCasio &&
    familyA === 0x00 &&
    familyB === 0x00 &&
    channelByte >= 0x70 &&
    channelByte <= 0x7f;
  const channel = looksCz ? (channelByte - 0x70) + 1 : null;
  const candidates = [];

  if (frame.length > 9) {
    candidates.push(analyzePayloadCandidate(frame, 9, "CZ-101/CZ-1000 style data after two send bytes"));
  }
  if (frame.length > 7) {
    candidates.push(analyzePayloadCandidate(frame, 7, "short CZ send data after location byte"));
  }

  const preferred =
    candidates.find((candidate) => command === CZ_SEND_PATCH_COMMAND && candidate.offset === 9 && candidate.supported) ||
    candidates.find((candidate) => candidate.supported) ||
    candidates[0] ||
    analyzePayloadCandidate(frame, 7, "fallback data after location byte");

  return {
    manufacturer,
    familyA,
    familyB,
    channelByte,
    channel,
    command,
    location,
    looksCasio,
    looksCz,
    commandSupported: command === CZ_SEND_PATCH_COMMAND,
    candidates,
    preferred,
    headerBytes: Array.from(frame.slice(0, Math.min(frame.length, preferred.offset)))
  };
}

function summarizeDecodedBytes(bytes, czInfo) {
  const header = bytes.slice(0, 16);
  const tail = bytes.slice(-16);
  const lines = [
    `Decoded bytes: ${bytes.length}`,
    `Head: ${toHex(header, 16)}`,
    `Tail: ${toHex(tail, 16)}`,
    `Non-zero count: ${bytes.filter((b) => b !== 0).length}`,
  ];

  if (czInfo) {
    lines.unshift(
      `CZ header bytes: ${toHex(czInfo.headerBytes, czInfo.headerBytes.length)}`,
      `CZ channel: ${czInfo.channel ?? "not detected"}`,
      `CZ command: ${commandName(czInfo.command)}`,
      `CZ location: ${locationName(czInfo.location)}`,
      `Patch data offset: ${czInfo.preferred.offset} (${czInfo.preferred.label})`
    );

    lines.push(
      "",
      "Payload candidates:",
      ...czInfo.candidates.map((candidate) => {
        const status = candidate.supported ? "usable" : "not selected";
        return `- offset ${candidate.offset}: ${candidate.payload.length} payload bytes, ${candidate.decodedBytes.length} decoded bytes, ${status}`;
      })
    );
  }

  return lines.join("\n");
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function roundStage(level, time) {
  return {
    level: clamp(Math.round(level), 0, 4095),
    time: clamp(Math.round(time), 1, 192000)
  };
}

function mapByteToLevel(byte, bias = 0) {
  return clamp(Math.round(((byte + bias) / 255) * 4095), 0, 4095);
}

function mapByteToTime(byte, minTime, maxTime) {
  const scaled = byte / 255;
  const eased = scaled * scaled;
  return clamp(Math.round(minTime + (maxTime - minTime) * eased), 1, 192000);
}

function buildStagesFromBytes(bytes, startIndex, levelBias = 0, timeMin = 120, timeMax = 12000) {
  const stages = [];
  for (let i = 0; i < 8; i++) {
    const levelByte = bytes[(startIndex + i) % bytes.length] ?? 0;
    const timeByte = bytes[(startIndex + i + 8) % bytes.length] ?? 0;
    stages.push(roundStage(
      mapByteToLevel(levelByte, levelBias),
      mapByteToTime(timeByte, timeMin, timeMax)
    ));
  }
  return stages;
}

function classifyWave(bytes) {
  const avg = bytes.reduce((sum, b) => sum + b, 0) / Math.max(1, bytes.length);
  const peak = Math.max(...bytes);
  const mid = bytes.slice(32, 64).reduce((sum, b) => sum + b, 0) / 32;

  if (peak > 220 && avg > 110) {
    return WAVE_FAMILIES[5];
  }
  if (mid > 95) {
    return WAVE_FAMILIES[3];
  }
  if (peak > 190) {
    return WAVE_FAMILIES[2];
  }
  if (avg > 80) {
    return WAVE_FAMILIES[4];
  }
  return WAVE_FAMILIES[0];
}

function buildDraftPreset(decodedBytes, patchName) {
  if (!decodedBytes.length) return null;

  const baseName = patchName
    .replace(/[_-]+/g, " ")
    .replace(/\s+/g, " ")
    .trim() || "Imported CZ patch";

  const wave = classifyWave(decodedBytes);
  const pitchEnvelope = buildStagesFromBytes(decodedBytes, 0, 0, 120, 18000);
  const dcwEnvelope = buildStagesFromBytes(decodedBytes, 16, 0, 120, 18000);
  const ampEnvelope = buildStagesFromBytes(decodedBytes, 32, 0, 140, 14000);
  const detune = clamp(Math.round((decodedBytes[48] ?? 128) / 255 * 4095), 0, 4095);
  const ring = clamp(Math.round((decodedBytes[49] ?? 0) / 255 * 1200), 0, 4095);
  const noise = clamp(Math.round((decodedBytes[50] ?? 0) / 255 * 700), 0, 4095);

  return {
    name: `${baseName} draft`,
    wave,
    amp: ampEnvelope,
    pd: dcwEnvelope,
    sourceEnvelopes: {
      pitch: pitchEnvelope,
      dcw: dcwEnvelope,
      amp: ampEnvelope
    },
    performance: { detune, ring, noise },
    confidence: "medium"
  };
}

function formatStages(stages) {
  return stages.map((stage, index) => `${index + 1}. ${stage.level}, ${stage.time}`).join("\n");
}

function renderDraft(draft) {
  if (!draft) {
    el.decodedPatchBox.textContent = "No decoded patch yet.";
    el.mappedDraftBox.textContent = "Amplitude, PD, and 8-wave family mapping will appear here.";
    el.confidenceBox.textContent = "High / Medium / Low";
    el.supportedOutputBox.textContent = "Draft preset for Envelope Lab";
    el.draftNameBox.textContent = "No draft created yet.";
    el.waveBox.textContent = "No draft yet.";
    el.perfBox.textContent = "No draft yet.";
    el.ampDraftBox.textContent = "No draft yet.";
    el.pdDraftBox.textContent = "No draft yet.";
    return;
  }

  el.decodedPatchBox.textContent = `${draft.name} decoded and unpacked into a draft preset.`;
  el.mappedDraftBox.textContent = `8-wave family ${draft.wave.label}, CZ DCW envelope -> C1ZZL3 phase distortion, and CZ amplitude envelope -> C1ZZL3 amplitude are ready for review. Pitch envelope is decoded but not assigned yet.`;
  el.confidenceBox.textContent = draft.confidence;
  el.supportedOutputBox.textContent = "Envelope Lab draft handoff";
  el.draftNameBox.textContent = `${draft.name} (${draft.confidence} confidence)`;
  el.waveBox.textContent = `${draft.wave.label} -> ${draft.wave.hint}`;
  el.perfBox.textContent = `Wave family ${draft.wave.label}, detune ${draft.performance.detune}, ring ${draft.performance.ring}, noise ${draft.performance.noise}`;
  el.ampDraftBox.textContent = formatStages(draft.amp);
  el.pdDraftBox.textContent = formatStages(draft.pd);
}

function handoffDraft(draft) {
  if (!draft) return false;
  const payload = {
    source: "cz-import-lab",
    createdAt: new Date().toISOString(),
    draft
  };
  localStorage.setItem(HANDOFF_KEY, JSON.stringify(payload));
  return true;
}

function createEditorUrl() {
  const editorUrl = getEditorUrl();
  return editorUrl;
}

function createHandoffPayload() {
  if (!currentDraft) return null;
  return {
    source: "cz-import-lab",
    createdAt: new Date().toISOString(),
    draft: currentDraft
  };
}

function openEditorTab() {
  const editorUrl = createEditorUrl();
  const payload = createHandoffPayload();
  const handoffUrl = payload
    ? `${editorUrl}?cz-import=${encodeURIComponent(JSON.stringify(payload))}`
    : editorUrl;
  const tab = window.open(handoffUrl, ENVELOPE_LAB_WINDOW_NAME);
  if (tab && payload) {
    setTimeout(() => {
      try {
        tab.postMessage({ type: "cz-import-handoff", payload }, "*");
      } catch {
        /* If messaging fails, the editor can still fall back to local storage. */
      }
    }, 250);
  }
  return handoffUrl;
}

function decodePatch(buffer, fileName) {
  const data = new Uint8Array(buffer);
  const frame = findSysexFrame(data);
  if (!frame) {
    return {
      ok: false,
      tone: "bad",
      validation: "No complete SysEx frame found.",
      patchType: "Unsupported file",
      summary: `${fileName} does not contain a complete SysEx message.`,
      decoded: "The file could not be read as SysEx.",
      draft: null
    };
  }

  const czInfo = analyzeCzFrame(frame);
  const manufacturer = czInfo.manufacturer;
  const payload = czInfo.preferred.payload;
  const nibblePayload = czInfo.preferred.nibblePayload;
  const decodedBytes = czInfo.preferred.decodedBytes;
  const evenNibbleCount = czInfo.preferred.evenNibbleCount;
  const decodedLengthOk = czInfo.preferred.decodedLengthOk;
  const supportedCandidate =
    czInfo.looksCz &&
    czInfo.commandSupported &&
    czInfo.preferred.supported;
  const patchName = fileName.replace(/\.(syx|mid|sysex)$/i, "");
  const confidence = supportedCandidate ? "medium" : "low";
  const draft = supportedCandidate ? buildDraftPreset(decodedBytes, patchName) : null;

  const validationReasons = [];
  if (!czInfo.looksCasio) validationReasons.push("manufacturer was not Casio `0x44`");
  if (czInfo.looksCasio && !czInfo.looksCz) validationReasons.push("header did not match the common CZ family form `F0 44 00 00 7n`");
  if (czInfo.looksCz && !czInfo.commandSupported) validationReasons.push(`command was ${commandName(czInfo.command)}, not send patch data`);
  if (!nibblePayload) validationReasons.push("payload was not nibble-packed");
  if (nibblePayload && !evenNibbleCount) validationReasons.push("nibble payload length was uneven");
  if (nibblePayload && !decodedLengthOk) {
    validationReasons.push(`decoded size was ${decodedBytes.length} bytes, outside the expected draft range`);
  }

  return {
    ok: supportedCandidate,
    tone: supportedCandidate ? "ok" : "warn",
    validation: supportedCandidate
      ? `Casio CZ ${commandName(czInfo.command)} frame found (${frame.length} bytes, ${locationName(czInfo.location)}, data offset ${czInfo.preferred.offset}).`
      : `This file does not look like a supported Casio CZ single-patch draft candidate: ${validationReasons.join("; ")}.`,
    patchType: supportedCandidate
      ? "Casio CZ single-patch send frame"
      : "Unsupported or different synth family",
    summary: [
      `File: ${fileName}`,
      `Frame length: ${frame.length} bytes`,
      `Manufacturer: 0x${manufacturer.toString(16).padStart(2, "0")}`,
      `CZ family bytes: 0x${czInfo.familyA.toString(16).padStart(2, "0")} 0x${czInfo.familyB.toString(16).padStart(2, "0")}`,
      `MIDI channel byte: 0x${czInfo.channelByte.toString(16).padStart(2, "0")}${czInfo.channel ? ` (channel ${czInfo.channel})` : ""}`,
      `Command: ${commandName(czInfo.command)}`,
      `Location: ${locationName(czInfo.location)}`,
      `Selected data offset: ${czInfo.preferred.offset}`,
      `Payload bytes: ${payload.length}`,
      `Nibble-packed payload: ${nibblePayload ? "yes" : "no"}`,
      `Even nibble count: ${evenNibbleCount ? "yes" : "no"}`,
      `Decoded bytes: ${decodedBytes.length}`,
      `Draft name: ${patchName}`,
      `Confidence: ${confidence}`
    ].join("\n"),
    decoded: nibblePayload
      ? summarizeDecodedBytes(decodedBytes, czInfo)
      : [
          "Payload was not nibble-packed.",
          `Raw frame head: ${toHex(frame, 24)}`,
          `Raw payload head: ${toHex(payload, 48)}`
        ].join("\n"),
    draft
  };
}

async function handleFile(file) {
  if (!file) return;
  setStatus(`Reading ${file.name}...`, "warn");
  const buffer = await file.arrayBuffer();
  const result = decodePatch(buffer, file.name);
  currentDraft = result.draft;
  el.validationBox.textContent = result.validation;
  el.patchTypeBox.textContent = result.patchType;
  el.summaryBox.textContent = result.summary;
  el.decodedBox.textContent = result.decoded;
  renderDraft(currentDraft);
  setStatus(result.ok ? "Patch decoded. Draft preset created." : "Patch did not match a supported CZ import.", result.tone);
}

el.file.addEventListener("change", () => handleFile(el.file.files?.[0]));
el.themeToggle.addEventListener("click", () => {
  themeMode = themeMode === "light" ? "dark" : "light";
  saveThemeMode();
  renderThemeMode();
});
el.dropzone.addEventListener("dragover", (event) => {
  event.preventDefault();
});
el.dropzone.addEventListener("drop", (event) => {
  event.preventDefault();
  const file = event.dataTransfer.files?.[0];
  if (file) handleFile(file);
});
el.openDraft.addEventListener("click", () => {
  if (handoffDraft(currentDraft)) {
    setStatus("Draft handed off to Envelope Lab.", "ok");
    openEditorTab();
  } else {
    setStatus("Import a file first so there is a draft to open.", "warn");
  }
});
renderThemeMode();
renderDraft(null);
