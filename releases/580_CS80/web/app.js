const statusEl = document.querySelector("#status");
const protocolEl = document.querySelector("#protocol");
const themeToggleEl = document.querySelector("#themeToggle");
const connectEl = document.querySelector("#connect");
const tabs = Array.from(document.querySelectorAll(".tab"));
const pages = Array.from(document.querySelectorAll(".page"));
const outputLanes = Array.from(document.querySelectorAll(".output-lane"));
const panelPageEl = document.querySelector("#panelPage");
const knobMapEl = document.querySelector("#knobMap");
const envCanvas = document.querySelector("#envCanvas");
const presetGridEl = document.querySelector("#presetGrid");
const presetDetailEl = document.querySelector("#presetDetail");
const presetNameEl = document.querySelector("#presetName");
const readCardEl = document.querySelector("#readCard");
const applyPatchEl = document.querySelector("#applyPatch");
const savePatchEl = document.querySelector("#savePatch");
const developerToggleEl = document.querySelector("#developerToggle");
const developerPanelEl = document.querySelector("#developerPanel");
const developerLogEl = document.querySelector("#developerLog");
const clearDeveloperLogEl = document.querySelector("#clearDeveloperLog");
const THEME_KEY = "cs80-editor-theme";
const MAX_LOG_LINES = 120;
let themeMode = loadThemeMode();
let developerMode = false;
let developerLogLines = [];

const knobMaps = {
  up: [
    ["MAIN", "Pitch Offset", "Pitch offset for the mono voice"],
    ["X", "Pulse Width", "Pulse shape for the mono voice"],
    ["Y", "Saw / Pulse", "Blend and PWM depth for the mono voice"],
  ],
  middle: [
    ["MAIN", "HP Cutoff", "High-pass filter movement"],
    ["X", "LP Cutoff", "Low-pass filter movement"],
    ["Y", "Resonance", "Brightness and filter emphasis"],
  ],
  down: [
    ["MAIN", "Pitch Offset", "Temporary performance layer while held"],
    ["X", "Ring Mod", "Metallic CS-style colour"],
    ["Y", "LFO Depth", "Vibrato, PWM, and filter animation"],
  ],
};

const presets = [
  {
    name: "Initial Brass",
    detail: "warm saw brass",
    sources: "Saw high, square medium, sine low, noise off",
    filter: "HPF low, LPF moderately open, LP resonance medium/high",
    envelope: "Medium attack, medium decay, medium release, expressive filter rise",
    modulation: "Expression CV opens brilliance; subtle LFO movement",
  },
  {
    name: "Muted Brass",
    detail: "Africa-style swell",
    sources: "Saw medium, square medium, sine low, noise off",
    filter: "Darker LPF, resonance boosted, performance brightness range reserved",
    envelope: "Moderate attack, medium release, filter opens into held notes",
    modulation: "Expression CV opens the filter for chorus/verse movement",
  },
  {
    name: "Soft String",
    detail: "wide PWM pad",
    sources: "Saw medium/high, square medium with PWM, sine low, noise off",
    filter: "HPF low, LPF fairly open, low resonance",
    envelope: "Slow attack, long release",
    modulation: "Subtle LFO to PWM and pitch for drift",
  },
  {
    name: "String Brass",
    detail: "Human Nature-ish",
    sources: "Saw and square balanced, gentle sine support, noise off",
    filter: "Slightly dark brilliance, minimal resonance",
    envelope: "Medium attack, longer release for overlap",
    modulation: "Expression CV adds breathy brightness",
  },
  {
    name: "Christmas Bass",
    detail: "LFO filter bounce",
    sources: "Square/saw body, little sine, noise off",
    filter: "Resonance high, brilliance boosted",
    envelope: "Short-to-medium amp shape for stabs",
    modulation: "Saw-down LFO to VCF around a slow rhythmic rate",
  },
  {
    name: "Warm Horn",
    detail: "French horn colour",
    sources: "Saw dominant, square supporting, sine low",
    filter: "LPF warm, resonance carefully raised",
    envelope: "Medium/slower attack, medium release",
    modulation: "Expression CV opens brightness like touch response",
  },
  {
    name: "Blade Runner Brass",
    detail: "Vangelis-style lead",
    sources: "Saw high, pulse/square medium, sine low, subtle noise breath",
    filter: "LPF fairly open, HPF low, low-to-medium resonance, expression opens brilliance",
    envelope: "Slow brass attack, long filter decay, medium/long release",
    modulation: "Slow PWM/LFO movement; CV expression should swell filter and level, add external reverb",
  },
  {
    name: "Organ Glow",
    detail: "Mr Crowley-ish",
    sources: "Sine and square higher, saw lower, noise off",
    filter: "Filters more open, brilliance and resonance raised",
    envelope: "Fast attack, sustained body, medium release",
    modulation: "Slow movement reserved for chorus-style animation",
  },
  {
    name: "Bandpass Dream",
    detail: "Walking-style focus",
    sources: "Saw plus square/PWM, sine low, noise off",
    filter: "HPF raised and LPF lowered, both resonances boosted",
    envelope: "Soft attack, medium release",
    modulation: "LFO/PWM movement and expression into filter focus",
  },
  {
    name: "Plucked Filter",
    detail: "tight resonant hit",
    sources: "Saw dominant, square low, sine off, noise off",
    filter: "Narrow bandpass, high resonance",
    envelope: "Short filter decay with longer release tail",
    modulation: "Minimal LFO; expression controls resonance intensity",
  },
  {
    name: "Hounds String 3",
    detail: "bright warm chord",
    sources: "Saw high, square/PWM medium, sine low, noise off",
    filter: "LPF open and bright, HPF low, low/medium resonance",
    envelope: "Fast-to-medium attack, medium release, less pad-like than Soft String",
    modulation: "Subtle LFO/PWM movement for chorus-style life",
  },
  {
    name: "PWM Flute Lead",
    detail: "hollow wind lead",
    sources: "Square high, saw low/off, sine low/medium, noise off",
    filter: "Moderate LPF, little resonance, brightness opened by expression",
    envelope: "Quick attack, medium release",
    modulation: "Gentle but audible LFO to pulse width",
  },
  {
    name: "Babooshka Guitar",
    detail: "expressive pluck",
    sources: "Saw/square pluck, sine low, small noise transient reserved",
    filter: "Medium-bright filter, some resonance",
    envelope: "Fast attack, short decay, low sustain, short/medium release",
    modulation: "Expression CV adds touch-like brightness and level",
  },
];

function loadThemeMode() {
  try {
    const saved = localStorage.getItem(THEME_KEY);
    if (saved === "light" || saved === "dark") return saved;
  } catch (error) {
    /* Keep default theme if saved data is unavailable. */
  }
  return "dark";
}

function saveThemeMode() {
  try {
    localStorage.setItem(THEME_KEY, themeMode);
  } catch (error) {
    // Storage is optional; the editor still works when embedded or sandboxed.
  }
}

function renderThemeMode() {
  document.documentElement.dataset.theme = themeMode;
  document.body.classList.toggle("theme-light", themeMode === "light");
  themeToggleEl.classList.toggle("is-active", themeMode === "light");
  themeToggleEl.textContent = themeMode === "light" ? "Light" : "Dark";
  themeToggleEl.setAttribute("aria-checked", String(themeMode === "light"));
  themeToggleEl.setAttribute("title", `Toggle ${themeMode === "light" ? "dark" : "light"} mode`);
  drawEnvelope();
}

function renderDeveloperMode() {
  developerPanelEl.classList.toggle("is-hidden", !developerMode);
  developerToggleEl.classList.toggle("is-active", developerMode);
  developerToggleEl.textContent = developerMode ? "Dev On" : "Dev";
  developerToggleEl.setAttribute("aria-checked", String(developerMode));
}

function renderDeveloperLog() {
  developerLogEl.textContent = developerLogLines.length
    ? developerLogLines.join("\n")
    : "Developer log is empty.";
  developerLogEl.scrollTop = developerLogEl.scrollHeight;
}

function logDeveloper(message, detail = null) {
  const stamp = new Date().toISOString().slice(11, 19);
  const suffix = detail == null ? "" : ` ${JSON.stringify(detail)}`;
  developerLogLines.push(`[${stamp}] ${message}${suffix}`);
  if (developerLogLines.length > MAX_LOG_LINES) {
    developerLogLines = developerLogLines.slice(-MAX_LOG_LINES);
  }
  renderDeveloperLog();
}

function setPage(pageName) {
  tabs.forEach((tab) => tab.classList.toggle("is-active", tab.dataset.page === pageName));
  pages.forEach((page) => page.classList.toggle("is-active", page.dataset.pagePanel === pageName));
  logDeveloper("page selected", { page: pageName });
  drawEnvelope();
}

function renderKnobMap() {
  const rows = knobMaps[panelPageEl.value] || knobMaps.middle;
  knobMapEl.replaceChildren(...rows.map(([knob, name, detail]) => {
    const row = document.createElement("div");
    row.className = "knob-row";
    row.innerHTML = `<div class="knob-icon">${knob}</div><div><strong>${name}</strong><span>${detail}</span></div>`;
    return row;
  }));

  document.querySelectorAll("[data-switch-led]").forEach((led) => {
    led.classList.toggle("is-lit", led.dataset.switchLed === panelPageEl.value);
  });
  logDeveloper("panel page mapped", { switch: panelPageEl.value });
}

function updateOutput(range) {
  const output = range.parentElement.querySelector("output");
  const min = Number(range.min);
  const max = Number(range.max);
  const value = Number(range.value);

  if (min < 0) {
    output.value = `${value} st`;
    return;
  }

  output.value = `${Math.round((value / max) * 100)}%`;
}

function drawEnvelope() {
  if (!envCanvas) return;

  const ctx = envCanvas.getContext("2d");
  const styles = getComputedStyle(document.documentElement);
  const w = envCanvas.width;
  const h = envCanvas.height;
  const attack = Number(document.querySelector("[data-param='attack']").value);
  const decay = Number(document.querySelector("[data-param='decay']").value);
  const sustain = Number(document.querySelector("[data-param='sustain']").value);
  const release = Number(document.querySelector("[data-param='release']").value);

  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = styles.getPropertyValue("--canvas-bg").trim() || "#171b1c";
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = styles.getPropertyValue("--canvas-grid").trim() || "#343b3d";
  ctx.lineWidth = 1;

  for (let i = 1; i < 4; i += 1) {
    const y = (h * i) / 4;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  const aX = 40 + (attack / 4095) * 170;
  const dX = aX + 50 + (decay / 4095) * 120;
  const sY = h - 24 - (sustain / 4095) * (h - 48);
  const rX = w - 40 - (release / 4095) * 140;

  ctx.strokeStyle = styles.getPropertyValue("--teal-strong").trim() || "#48b6a7";
  ctx.lineWidth = 4;
  ctx.beginPath();
  ctx.moveTo(28, h - 24);
  ctx.lineTo(aX, 28);
  ctx.lineTo(dX, sY);
  ctx.lineTo(rX, sY);
  ctx.lineTo(w - 28, h - 24);
  ctx.stroke();

  ctx.fillStyle = styles.getPropertyValue("--yellow").trim() || "#e0c443";
  for (const [x, y] of [[aX, 28], [dX, sY], [rX, sY]]) {
    ctx.beginPath();
    ctx.arc(x, y, 6, 0, Math.PI * 2);
    ctx.fill();
  }
}

function renderPresetDetail(index = 0) {
  const preset = presets[index] || presets[0];
  presetNameEl.value = preset.name;
  presetDetailEl.innerHTML = `
    <h3>${preset.name}</h3>
    <dl>
      <dt>Sources</dt><dd>${preset.sources}</dd>
      <dt>Filter</dt><dd>${preset.filter}</dd>
      <dt>Envelope</dt><dd>${preset.envelope}</dd>
      <dt>Modulation</dt><dd>${preset.modulation}</dd>
    </dl>
  `;
}

function renderPresets() {
  presetGridEl.replaceChildren(...presets.map((preset, index) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `preset-slot${index === 0 ? " is-active" : ""}`;
    button.innerHTML = `<strong>${index + 1}. ${preset.name}</strong><span>${preset.detail}</span>`;
    button.addEventListener("click", () => {
      document.querySelectorAll(".preset-slot").forEach((slot) => slot.classList.remove("is-active"));
      button.classList.add("is-active");
      renderPresetDetail(index);
      statusEl.textContent = `Selected preset slot ${index + 1}: ${preset.name}.`;
      logDeveloper("preset selected", { slot: index + 1, name: preset.name });
    });
    return button;
  }));
  renderPresetDetail(0);
}

async function connectMidi() {
  if (!navigator.requestMIDIAccess) {
    statusEl.textContent = "Web MIDI is not available in this browser.";
    protocolEl.textContent = "No Web MIDI";
    logDeveloper("midi unavailable");
    return;
  }

  try {
    const access = await navigator.requestMIDIAccess({ sysex: true });
    const outputs = Array.from(access.outputs.values());
    protocolEl.textContent = "Protocol draft";
    statusEl.textContent = outputs.length
      ? `MIDI ready. First matching output: ${outputs[0].name || "unnamed port"}.`
      : "MIDI ready, but no output port was found.";
    logDeveloper("midi access granted", { outputs: outputs.map((output) => output.name || "unnamed port") });
  } catch (error) {
    statusEl.textContent = `MIDI connection failed: ${error.message}`;
    protocolEl.textContent = "Disconnected";
    logDeveloper("midi connection failed", { message: error.message });
  }
}

tabs.forEach((tab) => tab.addEventListener("click", () => setPage(tab.dataset.page)));
panelPageEl.addEventListener("change", renderKnobMap);
connectEl.addEventListener("click", connectMidi);
themeToggleEl.addEventListener("click", () => {
  themeMode = themeMode === "light" ? "dark" : "light";
  saveThemeMode();
  renderThemeMode();
  logDeveloper("theme changed", { theme: themeMode });
});

developerToggleEl.addEventListener("click", () => {
  developerMode = !developerMode;
  renderDeveloperMode();
  logDeveloper("developer tools toggled", { enabled: developerMode });
});

clearDeveloperLogEl.addEventListener("click", () => {
  developerLogLines = [];
  renderDeveloperLog();
});

document.querySelectorAll("input[type='range']").forEach((range) => {
  updateOutput(range);
  range.addEventListener("input", () => {
    updateOutput(range);
    drawEnvelope();
    logDeveloper("parameter changed", { parameter: range.dataset.param, value: Number(range.value) });
  });
});

readCardEl.addEventListener("click", () => {
  statusEl.textContent = "Read will request patch data once the firmware SysEx protocol is assigned.";
  logDeveloper("read requested");
});

applyPatchEl.addEventListener("click", () => {
  statusEl.textContent = "Apply will send a non-persistent patch preview once the firmware protocol is assigned.";
  logDeveloper("apply requested");
});

savePatchEl.addEventListener("click", () => {
  statusEl.textContent = "Save will write to card flash after readback and confirmation are implemented.";
  logDeveloper("save requested");
});

renderKnobMap();
renderPresets();
renderThemeMode();
renderDeveloperMode();
renderDeveloperLog();
drawEnvelope();
