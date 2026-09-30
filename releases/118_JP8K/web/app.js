const SYSEX = [0xF0, 0x7D, 0x4A, 0x34, 0x4B, 0x53];
const CMD_PATTERN = 0x01;
const CMD_CONTROLS = 0x02;
const REST = 127;

const noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const notes = [
  REST, 57, 59, 62, 64, 69, 71, 74, 76, 81, 83, 86, 88
];

let midiAccess = null;
let output = null;
let pattern = defaultPattern();

const el = {
  connect: document.querySelector("#connect"),
  output: document.querySelector("#output"),
  tempo: document.querySelector("#tempo"),
  tempoValue: document.querySelector("#tempoValue"),
  length: document.querySelector("#length"),
  steps: document.querySelector("#steps"),
  sendPattern: document.querySelector("#sendPattern"),
  sendControls: document.querySelector("#sendControls"),
  resetSandstorm: document.querySelector("#resetSandstorm"),
  spread: document.querySelector("#spread"),
  brightness: document.querySelector("#brightness"),
  volume: document.querySelector("#volume"),
  status: document.querySelector("#status")
};

function defaultPattern() {
  const midi = [
    71, 71, 71, 71, 71, REST, 71, 71,
    71, 71, 71, 71, 71, REST, 76, 76,
    76, 76, 76, 76, 76, REST, 74, 74,
    74, 74, 74, 74, 74, REST, 69, 69
  ];
  return midi.map((note, index) => ({
    note,
    gate: note === REST ? 0 : ((index % 8) === 4 ? 80 : 45),
    accent: [0, 6, 8, 14, 16, 22, 24, 30].includes(index)
  }));
}

function noteLabel(note) {
  if (note === REST) return "Rest";
  const octave = Math.floor(note / 12) - 1;
  return `${noteNames[note % 12]}${octave}`;
}

function renderSteps() {
  el.steps.innerHTML = "";
  pattern.forEach((step, index) => {
    const row = document.createElement("div");
    row.className = "step";

    const number = document.createElement("span");
    number.className = "step-number";
    number.textContent = String(index + 1).padStart(2, "0");

    const select = document.createElement("select");
    notes.forEach((note) => {
      const option = document.createElement("option");
      option.value = String(note);
      option.textContent = noteLabel(note);
      select.append(option);
    });
    select.value = String(step.note);
    select.addEventListener("change", () => {
      step.note = Number(select.value);
      if (step.note === REST) step.gate = 0;
      else if (step.gate === 0) step.gate = 45;
      renderSteps();
    });

    const gate = document.createElement("input");
    gate.type = "range";
    gate.min = "5";
    gate.max = "100";
    gate.value = String(Math.max(5, step.gate || 5));
    gate.disabled = step.note === REST;
    gate.addEventListener("input", () => {
      step.gate = Number(gate.value);
    });

    const accentWrap = document.createElement("label");
    accentWrap.className = "accent-cell";
    const accent = document.createElement("input");
    accent.type = "checkbox";
    accent.checked = step.accent;
    accent.disabled = step.note === REST;
    accent.addEventListener("change", () => {
      step.accent = accent.checked;
    });
    accentWrap.append(accent);

    row.append(number, select, gate, accentWrap);
    el.steps.append(row);
  });
}

async function connectMidi() {
  if (!navigator.requestMIDIAccess) {
    setStatus("Web MIDI is not available in this browser");
    return;
  }
  midiAccess = await navigator.requestMIDIAccess({ sysex: true });
  midiAccess.addEventListener("statechange", refreshOutputs);
  refreshOutputs();
  setStatus("MIDI ready");
}

function refreshOutputs() {
  el.output.innerHTML = "";
  const outputs = [...midiAccess.outputs.values()];
  outputs.forEach((port) => {
    const option = document.createElement("option");
    option.value = port.id;
    option.textContent = port.name || port.manufacturer || port.id;
    el.output.append(option);
  });
  output = outputs[0] || null;
  if (output) el.output.value = output.id;
}

function selectedOutput() {
  if (!midiAccess) return null;
  return midiAccess.outputs.get(el.output.value) || output;
}

function send(bytes) {
  const port = selectedOutput();
  if (!port) {
    setStatus("No MIDI output selected");
    return;
  }
  port.send(bytes);
  setStatus(`Sent to ${port.name || "MIDI output"}`);
}

function sendPattern() {
  const tempo = clamp(Number(el.tempo.value), 30, 240);
  const length = clamp(Number(el.length.value), 1, 32);
  let accentMask = 0;
  pattern.slice(0, length).forEach((step, index) => {
    if (step.accent && step.note !== REST) accentMask |= (1 << index);
  });
  const payload = [
    tempo & 0x7F,
    (tempo >> 7) & 0x7F,
    length,
    accentMask & 0x7F,
    (accentMask >> 7) & 0x7F,
    (accentMask >> 14) & 0x7F,
    (accentMask >> 21) & 0x7F,
    (accentMask >> 28) & 0x0F,
    ...pattern.slice(0, length).map((step) => step.note),
    ...pattern.slice(0, length).map((step) => clamp(step.gate || 5, 5, 100))
  ];
  send([...SYSEX, CMD_PATTERN, ...payload, 0xF7]);
}

function sendControls() {
  send([
    ...SYSEX,
    CMD_CONTROLS,
    clamp(Number(el.spread.value), 0, 127),
    clamp(Number(el.brightness.value), 0, 127),
    clamp(Number(el.volume.value), 0, 127),
    0xF7
  ]);
}

function clamp(value, min, max) {
  if (!Number.isFinite(value)) return min;
  return Math.max(min, Math.min(max, Math.round(value)));
}

function setStatus(text) {
  el.status.textContent = text;
}

el.connect.addEventListener("click", connectMidi);
el.output.addEventListener("change", () => {
  output = selectedOutput();
});
el.tempo.addEventListener("input", () => {
  el.tempoValue.textContent = `${el.tempo.value} BPM`;
});
el.sendPattern.addEventListener("click", sendPattern);
el.sendControls.addEventListener("click", sendControls);
el.resetSandstorm.addEventListener("click", () => {
  pattern = defaultPattern();
  el.tempo.value = "136";
  el.tempoValue.textContent = "136 BPM";
  el.length.value = "32";
  renderSteps();
});

renderSteps();
