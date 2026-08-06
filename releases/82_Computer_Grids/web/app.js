const SYSEX_START = 0xf0;
const SYSEX_END = 0xf7;
const MFR = 0x7d;
const DEVICE = 0x63;
const CMD_GET = 0x01;
const CMD_GET_RESP = 0x02;
const CMD_SET = 0x03;
const CMD_SAVE = 0x04;

const DEFAULTS = {
  magic: 0x47524453,
  bpm10: 1200,
  swing: 50,
  chaos: 10,
  cv1_mode: 2,
  cv2_mode: 0,
  cv1_amount: 64,
  cv2_amount: 64,
  lane1_fill_scale: 100,
  lane2_fill_scale: 85,
  lane3_fill_scale: 115,
  lane1_fill_offset: 0,
  lane2_fill_offset: 8,
  lane3_fill_offset: -10,
  aux_mode: 0,
  pulse_ms: 10,
};

const state = {
  midiAccess: null,
  input: null,
  output: null,
  monitorTimer: null,
};

let readPendingTimer = null;
let applyFlashTimer = null;
let saveFlashTimer = null;
let defaultsFlashTimer = null;

const fields = [
  "bpm10",
  "swing",
  "chaos",
  "cv1_mode",
  "cv1_amount",
  "cv2_amount",
  "lane1_fill_scale",
  "lane2_fill_scale",
  "lane3_fill_scale",
  "lane1_fill_offset",
  "lane2_fill_offset",
  "lane3_fill_offset",
  "aux_mode",
  "pulse_ms",
];

function byId(id) {
  return document.getElementById(id);
}

function setStatus(text) {
  byId("status").textContent = text;
}

function clearReadPending() {
  if (readPendingTimer !== null) {
    clearTimeout(readPendingTimer);
    readPendingTimer = null;
  }
  const readBtn = byId("readBtn");
  readBtn.classList.remove("is-pending");
  readBtn.textContent = "Read From Device";
}

function setReadPending() {
  clearReadPending();
  const readBtn = byId("readBtn");
  readBtn.classList.add("is-pending");
  readBtn.textContent = "Reading…";
  readPendingTimer = setTimeout(() => {
    readPendingTimer = null;
    readBtn.classList.remove("is-pending");
    readBtn.textContent = "Read From Device";
  }, 8000);
}

function flashApplyDone() {
  clearTimeout(applyFlashTimer);
  const btn = byId("applyBtn");
  btn.classList.add("is-done");
  applyFlashTimer = setTimeout(() => {
    btn.classList.remove("is-done");
    applyFlashTimer = null;
  }, 1600);
}

function flashSaveDone() {
  clearTimeout(saveFlashTimer);
  const btn = byId("saveBtn");
  btn.classList.add("is-done");
  saveFlashTimer = setTimeout(() => {
    btn.classList.remove("is-done");
    saveFlashTimer = null;
  }, 1600);
}

function flashDefaultsDone() {
  clearTimeout(defaultsFlashTimer);
  const btn = byId("defaultsBtn");
  btn.classList.add("is-done");
  defaultsFlashTimer = setTimeout(() => {
    btn.classList.remove("is-done");
    defaultsFlashTimer = null;
  }, 1600);
}

function notifyMonitorPoll() {
  const t = new Date().toLocaleTimeString();
  byId("lastRx").textContent = `Poll sent at ${t} — waiting for a reply from the device.`;
}

function applyIncomingConfig(cfg, sourceLabel) {
  setFormFromConfig(cfg);
  const t = new Date().toLocaleTimeString();
  byId("lastRx").textContent = `${sourceLabel} at ${t} — form updated.`;
  byId("monitorConfigPreview").textContent = JSON.stringify(cfg, null, 2);
  setStatus("Config received from device.");
  clearReadPending();
}

function clamp(val, min, max) {
  return Math.min(max, Math.max(min, val));
}

function asInt(v, dflt = 0) {
  const n = Number(v);
  return Number.isFinite(n) ? Math.trunc(n) : dflt;
}

function getConfigFromForm() {
  return {
    magic: DEFAULTS.magic,
    bpm10: clamp(asInt(byId("bpm10").value, DEFAULTS.bpm10), 400, 2600),
    swing: clamp(asInt(byId("swing").value, DEFAULTS.swing), 50, 75),
    chaos: clamp(asInt(byId("chaos").value, DEFAULTS.chaos), 0, 127),
    cv1_mode: clamp(asInt(byId("cv1_mode").value, DEFAULTS.cv1_mode), 0, 2),
    cv2_mode: DEFAULTS.cv2_mode,
    cv1_amount: clamp(asInt(byId("cv1_amount").value, DEFAULTS.cv1_amount), -127, 127),
    cv2_amount: clamp(asInt(byId("cv2_amount").value, DEFAULTS.cv2_amount), -127, 127),
    lane1_fill_scale: clamp(asInt(byId("lane1_fill_scale").value, DEFAULTS.lane1_fill_scale), 0, 200),
    lane2_fill_scale: clamp(asInt(byId("lane2_fill_scale").value, DEFAULTS.lane2_fill_scale), 0, 200),
    lane3_fill_scale: clamp(asInt(byId("lane3_fill_scale").value, DEFAULTS.lane3_fill_scale), 0, 200),
    lane1_fill_offset: clamp(asInt(byId("lane1_fill_offset").value, DEFAULTS.lane1_fill_offset), -127, 127),
    lane2_fill_offset: clamp(asInt(byId("lane2_fill_offset").value, DEFAULTS.lane2_fill_offset), -127, 127),
    lane3_fill_offset: clamp(asInt(byId("lane3_fill_offset").value, DEFAULTS.lane3_fill_offset), -127, 127),
    aux_mode: clamp(asInt(byId("aux_mode").value, DEFAULTS.aux_mode), 0, 2),
    pulse_ms: clamp(asInt(byId("pulse_ms").value, DEFAULTS.pulse_ms), 1, 40),
    reserved: [0, 0, 0, 0, 0, 0, 0, 0],
  };
}

function setFormFromConfig(cfg) {
  for (const k of fields) {
    if (k in cfg) byId(k).value = cfg[k];
    const rk = `${k}_range`;
    if (k in cfg && byId(rk)) byId(rk).value = cfg[k];
  }
}

function bindPairedInput(key) {
  const num = byId(key);
  const rng = byId(`${key}_range`);
  if (!num || !rng) return;
  rng.addEventListener("input", () => {
    num.value = rng.value;
  });
  num.addEventListener("input", () => {
    rng.value = num.value;
  });
}

function encodeStruct(cfg) {
  const bytes = [];
  const pushU8 = (v) => bytes.push(v & 0xff);
  const pushI8 = (v) => bytes.push((v + 256) & 0xff);
  const pushU16 = (v) => {
    pushU8(v & 0xff);
    pushU8((v >> 8) & 0xff);
  };
  const pushU32 = (v) => {
    pushU8(v & 0xff);
    pushU8((v >> 8) & 0xff);
    pushU8((v >> 16) & 0xff);
    pushU8((v >> 24) & 0xff);
  };

  pushU32(cfg.magic);
  pushU16(cfg.bpm10);
  pushU8(cfg.swing);
  pushU8(cfg.chaos);
  pushU8(cfg.cv1_mode);
  pushU8(cfg.cv2_mode);
  pushI8(cfg.cv1_amount);
  pushI8(cfg.cv2_amount);
  pushU8(cfg.lane1_fill_scale);
  pushU8(cfg.lane2_fill_scale);
  pushU8(cfg.lane3_fill_scale);
  pushI8(cfg.lane1_fill_offset);
  pushI8(cfg.lane2_fill_offset);
  pushI8(cfg.lane3_fill_offset);
  pushU8(cfg.aux_mode);
  pushU8(cfg.pulse_ms);
  for (const r of cfg.reserved || [0, 0, 0, 0, 0, 0, 0, 0]) pushU8(r);
  return bytes;
}

function decodeStruct(bytes) {
  let i = 0;
  const u8 = () => bytes[i++] ?? 0;
  const i8 = () => {
    const b = u8();
    return b > 127 ? b - 256 : b;
  };
  const u16 = () => u8() | (u8() << 8);
  const u32 = () => u8() | (u8() << 8) | (u8() << 16) | (u8() << 24);
  const cfg = {};
  cfg.magic = u32() >>> 0;
  cfg.bpm10 = u16();
  cfg.swing = u8();
  cfg.chaos = u8();
  cfg.cv1_mode = u8();
  cfg.cv2_mode = u8();
  cfg.cv1_amount = i8();
  cfg.cv2_amount = i8();
  cfg.lane1_fill_scale = u8();
  cfg.lane2_fill_scale = u8();
  cfg.lane3_fill_scale = u8();
  cfg.lane1_fill_offset = i8();
  cfg.lane2_fill_offset = i8();
  cfg.lane3_fill_offset = i8();
  cfg.aux_mode = u8();
  cfg.pulse_ms = u8();
  cfg.reserved = [u8(), u8(), u8(), u8(), u8(), u8(), u8(), u8()];
  return cfg;
}

function encode7Bit(raw) {
  const out = [];
  for (let i = 0; i < raw.length; i += 7) {
    const block = raw.slice(i, i + 7);
    let msb = 0;
    for (let j = 0; j < block.length; j++) {
      if (block[j] & 0x80) msb |= 1 << j;
    }
    out.push(msb);
    for (const b of block) out.push(b & 0x7f);
  }
  return out;
}

function decode7Bit(payload) {
  const out = [];
  let i = 0;
  while (i < payload.length) {
    const msb = payload[i++];
    for (let j = 0; j < 7 && i < payload.length; j++) {
      let b = payload[i++];
      if (msb & (1 << j)) b |= 0x80;
      out.push(b);
    }
  }
  return out;
}

function sendSysEx(cmd, payload = []) {
  if (!state.output) {
    setStatus("No MIDI output selected.");
    return;
  }
  const pl = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
  const msg = new Uint8Array(4 + pl.length + 1);
  msg[0] = SYSEX_START;
  msg[1] = MFR;
  msg[2] = DEVICE;
  msg[3] = cmd;
  msg.set(pl, 4);
  msg[msg.length - 1] = SYSEX_END;
  state.output.send(msg);
}

function refreshPortLists() {
  const inSel = byId("midiIn");
  const outSel = byId("midiOut");
  const prevIn = inSel.value;
  const prevOut = outSel.value;
  inSel.innerHTML = "";
  outSel.innerHTML = "";

  const inputs = [...state.midiAccess.inputs.values()];
  const outputs = [...state.midiAccess.outputs.values()];

  if (inputs.length === 0) {
    const opt = document.createElement("option");
    opt.value = "";
    opt.textContent = "No input ports";
    opt.disabled = true;
    inSel.appendChild(opt);
  } else {
    for (const input of inputs) {
      const opt = document.createElement("option");
      opt.value = input.id;
      opt.textContent = input.name || input.id;
      inSel.appendChild(opt);
    }
    if (prevIn && [...inSel.options].some((o) => o.value === prevIn)) {
      inSel.value = prevIn;
    } else {
      inSel.selectedIndex = 0;
    }
  }

  if (outputs.length === 0) {
    const opt = document.createElement("option");
    opt.value = "";
    opt.textContent = "No output ports";
    opt.disabled = true;
    outSel.appendChild(opt);
  } else {
    for (const output of outputs) {
      const opt = document.createElement("option");
      opt.value = output.id;
      opt.textContent = output.name || output.id;
      outSel.appendChild(opt);
    }
    if (prevOut && [...outSel.options].some((o) => o.value === prevOut)) {
      outSel.value = prevOut;
    } else {
      outSel.selectedIndex = 0;
    }
  }

  bindPorts();
}

function bindPorts() {
  const inId = byId("midiIn").value;
  const outId = byId("midiOut").value;
  state.input = state.midiAccess.inputs.get(inId) || null;
  state.output = state.midiAccess.outputs.get(outId) || null;

  const nIn = state.midiAccess.inputs.size;
  const nOut = state.midiAccess.outputs.size;
  if (nIn === 0 && nOut === 0) {
    setStatus(
      "Web MIDI connected, but no devices were reported. Plug in an interface or allow MIDI in the browser prompt, then try Connect again."
    );
  } else if (nIn === 0) {
    setStatus("Connected — no MIDI input ports. Check cabling and system MIDI settings.");
  } else if (nOut === 0) {
    setStatus("Connected — no MIDI output ports. Check cabling and system MIDI settings.");
  } else if (!state.input || !state.output) {
    setStatus("Select a MIDI input and output above.");
  } else {
    setStatus("MIDI ports selected.");
  }

  if (state.input) {
    state.input.onmidimessage = (ev) => {
      const data = Array.from(ev.data || []);
      if (data.length < 6) return;
      if (data[0] !== SYSEX_START || data[data.length - 1] !== SYSEX_END) return;
      if (data[1] !== MFR || data[2] !== DEVICE) return;
      if (data[3] !== CMD_GET_RESP) return;
      const payload = data.slice(4, -1);
      const raw = decode7Bit(payload);
      if (raw.length < 28) {
        setStatus("Received config payload too short.");
        clearReadPending();
        return;
      }
      const cfg = decodeStruct(raw);
      applyIncomingConfig(cfg, "Last config received");
    };
  }
}

async function connectMIDI() {
  if (!navigator.requestMIDIAccess) {
    setStatus("Web MIDI not available in this browser.");
    return;
  }
  try {
    state.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
    state.midiAccess.onstatechange = () => refreshPortLists();
    refreshPortLists();
  } catch (err) {
    setStatus(`MIDI connection failed: ${err.message}`);
  }
}

function exportJSON() {
  const cfg = getConfigFromForm();
  const blob = new Blob([JSON.stringify(cfg, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "82_Computer_Grids-config.json";
  a.click();
  URL.revokeObjectURL(url);
}

function loadDefaults() {
  setFormFromConfig(DEFAULTS);
  setStatus("Defaults loaded in UI.");
  flashDefaultsDone();
}

function monitorActive() {
  return state.monitorTimer !== null;
}

function refreshMonitorUi() {
  byId("monitorState").textContent = monitorActive() ? "Running" : "Stopped";
  byId("monitorToggleBtn").textContent = monitorActive() ? "Stop Monitor" : "Start Monitor";
}

function stopMonitor() {
  if (state.monitorTimer !== null) {
    clearInterval(state.monitorTimer);
    state.monitorTimer = null;
  }
  refreshMonitorUi();
}

function startMonitor() {
  stopMonitor();
  if (!state.output) {
    setStatus("No MIDI output selected — cannot poll device.");
    refreshMonitorUi();
    return;
  }
  const interval = clamp(asInt(byId("monitorIntervalMs").value, 1000), 100, 10000);
  byId("monitorIntervalMs").value = interval;
  state.monitorTimer = setInterval(() => {
    notifyMonitorPoll();
    sendSysEx(CMD_GET);
  }, interval);
  refreshMonitorUi();
  notifyMonitorPoll();
  sendSysEx(CMD_GET);
}

function importJSON(file) {
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const cfg = JSON.parse(reader.result);
      if ("swing" in cfg) {
        cfg.swing = clamp(asInt(cfg.swing, DEFAULTS.swing), 50, 75);
      }
      setFormFromConfig(cfg);
      setStatus("Imported config JSON.");
    } catch (e) {
      setStatus(`Import failed: ${e.message}`);
    }
  };
  reader.readAsText(file);
}

function init() {
  byId("connectBtn").addEventListener("click", connectMIDI);
  byId("midiIn").addEventListener("change", bindPorts);
  byId("midiOut").addEventListener("change", bindPorts);
  byId("readBtn").addEventListener("click", () => {
    if (!state.output) {
      setStatus("No MIDI output selected.");
      return;
    }
    setReadPending();
    sendSysEx(CMD_GET);
  });
  byId("applyBtn").addEventListener("click", () => {
    if (!state.output) {
      setStatus("No MIDI output selected.");
      return;
    }
    const raw = encodeStruct(getConfigFromForm());
    const payload = encode7Bit(raw);
    sendSysEx(CMD_SET, payload);
    setStatus("Config applied to running behavior (not yet persisted).");
    flashApplyDone();
  });
  byId("saveBtn").addEventListener("click", () => {
    if (!state.output) {
      setStatus("No MIDI output selected.");
      return;
    }
    sendSysEx(CMD_SAVE);
    setStatus("Save request sent to card.");
    flashSaveDone();
  });
  byId("exportBtn").addEventListener("click", exportJSON);
  byId("defaultsBtn").addEventListener("click", loadDefaults);
  byId("monitorToggleBtn").addEventListener("click", () => {
    if (monitorActive()) {
      stopMonitor();
    } else {
      startMonitor();
    }
  });
  byId("monitorIntervalMs").addEventListener("change", () => {
    if (monitorActive()) startMonitor();
  });
  byId("importFile").addEventListener("change", (ev) => {
    const f = ev.target.files?.[0];
    if (f) importJSON(f);
  });
  fields.forEach(bindPairedInput);
  refreshMonitorUi();
  loadDefaults();
}

init();

