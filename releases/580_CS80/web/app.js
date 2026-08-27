const statusEl = document.querySelector("#status");
const protocolEl = document.querySelector("#protocol");
const themeToggleEl = document.querySelector("#themeToggle");
const connectEl = document.querySelector("#connect");
const cardSlotEl = document.querySelector("#cardSlot");
const toneEls = Array.from(document.querySelectorAll("[data-tone]"));
const tabs = Array.from(document.querySelectorAll(".tab"));
const pages = Array.from(document.querySelectorAll(".page"));
const outputLanes = Array.from(document.querySelectorAll(".output-lane"));
const panelPageEl = document.querySelector("#panelPage");
const knobMapEl = document.querySelector("#knobMap");
const ledPreviewEls = Array.from(document.querySelectorAll("[data-led-index]"));
const envCanvas = document.querySelector("#envCanvas");
const presetGridEl = document.querySelector("#presetGrid");
const presetDetailEl = document.querySelector("#presetDetail");
const presetNameEl = document.querySelector("#presetName");
const readCardEl = document.querySelector("#readCard");
const applyPatchEl = document.querySelector("#applyPatch");
const savePatchEl = document.querySelector("#savePatch");
const deletePatchEl = document.querySelector("#deletePatch");
const setStartupSlotEl = document.querySelector("#setStartupSlot");
const developerToggleEl = document.querySelector("#developerToggle");
const developerPanelEl = document.querySelector("#developerPanel");
const developerLogEl = document.querySelector("#developerLog");
const clearDeveloperLogEl = document.querySelector("#clearDeveloperLog");
const THEME_KEY = "cs80-editor-theme";
const MAX_LOG_LINES = 120;
const SYSEX_MANUFACTURER = 0x7d;
const SYSEX_ID = [0x43, 0x53, 0x38, 0x30]; // CS80
const COMMAND_APPLY_PATCH = 0x01;
const COMMAND_SAVE_PATCH = 0x02;
const COMMAND_REQUEST_PATCH = 0x03;
const COMMAND_PATCH_RESPONSE = 0x04;
const COMMAND_SAVE_SLOT = 0x05;
const COMMAND_REQUEST_SLOTS = 0x06;
const COMMAND_SLOTS_RESPONSE = 0x07;
const COMMAND_REQUEST_SLOT = 0x08;
const COMMAND_SLOT_RESPONSE = 0x09;
const COMMAND_DELETE_SLOT = 0x0a;
const COMMAND_SET_STARTUP_SLOT = 0x0b;
const PATCH_PROTOCOL_VERSION = 5;
let themeMode = loadThemeMode();
let developerMode = false;
let developerLogLines = [];
let midiAccess = null;
let midiInput = null;
let midiOutput = null;
let savedSlotMask = 0;
let startupSlot = 0;
let pickupPreview = [false, false, false];
let activeTone = "all";

const defaultPatchParams = {
  pitch: 0,
  portamento: 2600,
  pulse: 1650,
  pwmAmount: 1450,
  sawLevel: 450,
  pulseLevel: 2500,
  sineLevel: 2300,
  noiseLevel: 220,
  level: 3600,
  spread: 0,
  hp: 1500,
  lp: 2450,
  res: 2650,
  expression: 1900,
  attack: 80,
  decay: 760,
  sustain: 3300,
  release: 1200,
  lfoRate: 1750,
  lfoPitchDepth: 980,
  lfoPwmDepth: 1550,
  vcfDepth: 1000,
  vcaDepth: 560,
  ring: 850,
  ringSpeed: 2250,
  pitchCvRange: 1,
};
let selectedPresetIndex = 0;

const knobMaps = {
  up: [
    ["MAIN", "VCO Pitch", "Pitch offset for the mono voice"],
    ["X", "Pulse Width", "Pulse shape for the mono voice"],
    ["Y", "PWM Amount", "Pulse-width modulation amount; LFO routing stays in the editor"],
  ],
  middle: [
    ["MAIN", "HP Cutoff", "High-pass filter movement"],
    ["X", "LP Cutoff", "Low-pass filter movement"],
    ["Y", "Resonance", "Brightness and filter emphasis"],
  ],
  down: [
    ["MAIN", "Performance Pitch", "Momentary pitch layer while Down is held"],
    ["X", "Ring Mod", "Metallic CS-style colour"],
    ["Y", "LFO to Pitch", "Vibrato depth; PWM, filter, and amp routings stay independent"],
  ],
};

const presets = [
  {
    name: "Doctor Who Theme",
    category: "funny",
    detail: "eerie pulse/sine lead",
    sources: "Sine and pulse high, saw low, slight noise edge",
    filter: "Resonant bandpass-like shape with raised HPF and open LPF",
    envelope: "Immediate attack, sustained note body, short/medium release",
    modulation: "Audible vibrato and PWM wobble, plus a little ring colour for tape-era shimmer",
    params: { ...defaultPatchParams, lfoPitchDepth: 560, lfoRate: 1620, lfoPwmDepth: 1480 },
  },
  {
    name: "Initial Brass",
    category: "brass",
    detail: "warm saw brass",
    sources: "Saw high, square medium, sine low, noise off",
    filter: "HPF low, LPF moderately open, LP resonance medium/high",
    envelope: "Medium attack, medium decay, medium release, expressive filter rise",
    modulation: "Expression CV opens brilliance; subtle LFO movement",
    params: { pitch: 0, pulse: 1900, mix: 2900, pwm: 520, sawLevel: 3000, pulseLevel: 1700, sineLevel: 500, noiseLevel: 0, hp: 280, lp: 2350, res: 1650, attack: 1550, decay: 1750, sustain: 3000, release: 1900, ring: 0, vcoDepth: 420, vcfDepth: 850, vcaDepth: 360, lfoRate: 980, ringSpeed: 900, expression: 2000 },
  },
  {
    name: "Muted Brass",
    category: "brass",
    detail: "Africa-style swell",
    sources: "Saw medium, square medium, sine low, noise off",
    filter: "Darker LPF, resonance boosted, performance brightness range reserved",
    envelope: "Moderate attack, medium release, filter opens into held notes",
    modulation: "Expression CV opens the filter for chorus/verse movement",
    params: { pitch: -12, pulse: 1780, mix: 2500, pwm: 420, sawLevel: 2300, pulseLevel: 1500, sineLevel: 400, noiseLevel: 90, hp: 220, lp: 1450, res: 2300, attack: 1700, decay: 1850, sustain: 2800, release: 2100, ring: 0, vcoDepth: 300, vcfDepth: 1100, vcaDepth: 300, lfoRate: 760, ringSpeed: 800, expression: 2300 },
  },
  {
    name: "Soft String",
    category: "string",
    detail: "wide PWM pad",
    sources: "Saw medium/high, square medium with PWM, sine low, noise off",
    filter: "HPF low, LPF fairly open, low resonance",
    envelope: "Slow attack, long release",
    modulation: "Subtle LFO to PWM and pitch for drift",
    params: { pitch: 0, pulse: 2100, mix: 2450, pwm: 2200, sawLevel: 2200, pulseLevel: 1700, sineLevel: 800, noiseLevel: 0, hp: 200, lp: 2650, res: 780, attack: 2650, decay: 2400, sustain: 3300, release: 3300, ring: 0, vcoDepth: 950, vcfDepth: 520, vcaDepth: 420, lfoRate: 620, ringSpeed: 700, expression: 1900 },
  },
  {
    name: "String Brass",
    category: "string",
    detail: "Human Nature-ish",
    sources: "Saw and square balanced, gentle sine support, noise off",
    filter: "Slightly dark brilliance, minimal resonance",
    envelope: "Medium attack, longer release for overlap",
    modulation: "Expression CV adds breathy brightness",
    params: { pitch: 0, pulse: 2000, mix: 2400, pwm: 1150, sawLevel: 2300, pulseLevel: 1750, sineLevel: 600, noiseLevel: 0, hp: 260, lp: 1900, res: 600, attack: 1900, decay: 2200, sustain: 3200, release: 2750, ring: 0, vcoDepth: 620, vcfDepth: 700, vcaDepth: 360, lfoRate: 720, ringSpeed: 780, expression: 2100 },
  },
  {
    name: "Christmas Bass",
    category: "funny",
    detail: "LFO filter bounce",
    sources: "Square/saw body, little sine, noise off",
    filter: "Resonance high, brilliance boosted",
    envelope: "Short-to-medium amp shape for stabs",
    modulation: "Saw-down LFO to VCF around a slow rhythmic rate",
    params: { pitch: -24, pulse: 1500, mix: 2600, pwm: 700, sawLevel: 1800, pulseLevel: 2400, sineLevel: 250, noiseLevel: 0, hp: 350, lp: 2300, res: 2800, attack: 450, decay: 1200, sustain: 1800, release: 1050, ring: 0, vcoDepth: 300, vcfDepth: 2100, vcaDepth: 520, lfoRate: 1550, ringSpeed: 900, expression: 2400 },
  },
  {
    name: "Warm Horn",
    category: "brass",
    detail: "French horn colour",
    sources: "Saw dominant, square supporting, sine low",
    filter: "LPF warm, resonance carefully raised",
    envelope: "Medium/slower attack, medium release",
    modulation: "Expression CV opens brightness like touch response",
    params: { pitch: 0, pulse: 1800, mix: 3100, pwm: 360, sawLevel: 2800, pulseLevel: 1200, sineLevel: 500, noiseLevel: 50, hp: 180, lp: 1750, res: 1450, attack: 2150, decay: 1850, sustain: 2900, release: 2200, ring: 0, vcoDepth: 260, vcfDepth: 760, vcaDepth: 300, lfoRate: 640, ringSpeed: 760, expression: 2250 },
  },
  {
    name: "Blade Runner Brass",
    category: "brass",
    detail: "Vangelis-style lead",
    sources: "Saw high, pulse/square medium, sine low, subtle noise breath",
    filter: "LPF fairly open, HPF low, low-to-medium resonance, expression opens brilliance",
    envelope: "Slow brass attack, long filter decay, medium/long release",
    modulation: "Slow PWM/LFO movement; CV expression should swell filter and level, add external reverb",
    params: { pitch: 0, pulse: 1850, mix: 3300, pwm: 1150, sawLevel: 3000, pulseLevel: 1600, sineLevel: 600, noiseLevel: 200, hp: 170, lp: 2550, res: 1350, attack: 2850, decay: 2600, sustain: 3150, release: 3000, ring: 180, vcoDepth: 650, vcfDepth: 1250, vcaDepth: 560, lfoRate: 520, ringSpeed: 620, expression: 2700 },
  },
  {
    name: "Organ Glow",
    category: "organ",
    detail: "Mr Crowley-ish",
    sources: "Sine and square higher, saw lower, noise off",
    filter: "Filters more open, brilliance and resonance raised",
    envelope: "Fast attack, sustained body, medium release",
    modulation: "Slow movement reserved for chorus-style animation",
    params: { pitch: 12, pulse: 2350, mix: 900, pwm: 300, sawLevel: 800, pulseLevel: 2500, sineLevel: 2600, noiseLevel: 0, hp: 500, lp: 3100, res: 1850, attack: 220, decay: 1100, sustain: 3600, release: 1700, ring: 0, vcoDepth: 180, vcfDepth: 260, vcaDepth: 320, lfoRate: 450, ringSpeed: 700, expression: 1600 },
  },
  {
    name: "Bandpass Dream",
    category: "clavi",
    detail: "Walking-style focus",
    sources: "Saw plus square/PWM, sine low, noise off",
    filter: "HPF raised and LPF lowered, both resonances boosted",
    envelope: "Soft attack, medium release",
    modulation: "LFO/PWM movement and expression into filter focus",
    params: { pitch: 0, pulse: 1950, mix: 2100, pwm: 1900, sawLevel: 2200, pulseLevel: 1900, sineLevel: 400, noiseLevel: 0, hp: 1550, lp: 1700, res: 2600, attack: 1650, decay: 1900, sustain: 2800, release: 2250, ring: 0, vcoDepth: 800, vcfDepth: 1300, vcaDepth: 420, lfoRate: 900, ringSpeed: 850, expression: 2300 },
  },
  {
    name: "Plucked Filter",
    category: "electric-piano",
    detail: "tight resonant hit",
    sources: "Saw dominant, square low, sine off, noise off",
    filter: "Narrow bandpass, high resonance",
    envelope: "Short filter decay with longer release tail",
    modulation: "Minimal LFO; expression controls resonance intensity",
    params: { pitch: 0, pulse: 1500, mix: 3200, pwm: 180, sawLevel: 2800, pulseLevel: 750, sineLevel: 0, noiseLevel: 100, hp: 1900, lp: 1500, res: 3200, attack: 120, decay: 720, sustain: 650, release: 1700, ring: 0, vcoDepth: 120, vcfDepth: 450, vcaDepth: 280, lfoRate: 380, ringSpeed: 900, expression: 2800 },
  },
  {
    name: "Hounds String 3",
    category: "string",
    detail: "bright warm chord",
    sources: "Saw high, square/PWM medium, sine low, noise off",
    filter: "LPF open and bright, HPF low, low/medium resonance",
    envelope: "Fast-to-medium attack, medium release, less pad-like than Soft String",
    modulation: "Subtle LFO/PWM movement for chorus-style life",
    params: { pitch: 0, pulse: 2200, mix: 2700, pwm: 1500, sawLevel: 2700, pulseLevel: 1600, sineLevel: 500, noiseLevel: 0, hp: 260, lp: 2900, res: 1100, attack: 1050, decay: 1700, sustain: 3300, release: 2050, ring: 0, vcoDepth: 560, vcfDepth: 520, vcaDepth: 350, lfoRate: 760, ringSpeed: 700, expression: 1900 },
  },
  {
    name: "PWM Flute Lead",
    category: "flute",
    detail: "hollow wind lead",
    sources: "Square high, saw low/off, sine low/medium, noise off",
    filter: "Moderate LPF, little resonance, brightness opened by expression",
    envelope: "Quick attack, medium release",
    modulation: "Gentle but audible LFO to pulse width",
    params: { pitch: 12, pulse: 2550, mix: 500, pwm: 1850, sawLevel: 300, pulseLevel: 2800, sineLevel: 1100, noiseLevel: 20, hp: 360, lp: 2050, res: 650, attack: 380, decay: 1350, sustain: 2500, release: 1750, ring: 0, vcoDepth: 480, vcfDepth: 420, vcaDepth: 300, lfoRate: 820, ringSpeed: 650, expression: 2200 },
  },
  {
    name: "Babooshka Guitar",
    category: "guitar",
    detail: "expressive pluck",
    sources: "Saw/square pluck, sine low, small noise transient reserved",
    filter: "Medium-bright filter, some resonance",
    envelope: "Fast attack, short decay, low sustain, short/medium release",
    modulation: "Expression CV adds touch-like brightness and level",
    params: { pitch: 0, pulse: 1350, mix: 2900, pwm: 260, sawLevel: 2300, pulseLevel: 1700, sineLevel: 300, noiseLevel: 150, hp: 700, lp: 2400, res: 1700, attack: 80, decay: 650, sustain: 900, release: 950, ring: 120, vcoDepth: 180, vcfDepth: 800, vcaDepth: 500, lfoRate: 420, ringSpeed: 1600, expression: 2450 },
  },
  {
    name: "String 1",
    category: "string",
    detail: "soft preset string",
    sources: "Saw medium/high, pulse medium, gentle sine support, noise off",
    filter: "Open LPF, low HPF, low resonance for broad ensemble colour",
    envelope: "Slow attack, long release, high sustain",
    modulation: "Subtle PWM drift with a little vibrato for movement",
    params: { pitch: 0, pulse: 2100, pwmAmount: 1750, sawLevel: 2500, pulseLevel: 1500, sineLevel: 700, noiseLevel: 0, hp: 180, lp: 2750, res: 700, attack: 2500, decay: 2100, sustain: 3400, release: 3200, ring: 0, lfoPitchDepth: 260, lfoPwmDepth: 1100, vcfDepth: 420, vcaDepth: 260, lfoRate: 620, ringSpeed: 700, expression: 1800 },
  },
  {
    name: "String 2",
    category: "string",
    detail: "brighter preset string",
    sources: "Saw high, pulse medium, sine low, faint noise sheen",
    filter: "Brighter LPF than String 1 with a little more presence",
    envelope: "Medium-slow attack, long release",
    modulation: "PWM chorus motion with restrained pitch drift",
    params: { pitch: 0, pulse: 2200, pwmAmount: 1650, sawLevel: 2850, pulseLevel: 1550, sineLevel: 450, noiseLevel: 80, hp: 240, lp: 2950, res: 950, attack: 1900, decay: 1800, sustain: 3320, release: 2850, ring: 0, lfoPitchDepth: 220, lfoPwmDepth: 980, vcfDepth: 380, vcaDepth: 240, lfoRate: 760, ringSpeed: 720, expression: 1950 },
  },
  {
    name: "Brass 1",
    category: "brass",
    detail: "factory-style warm brass",
    sources: "Saw dominant, pulse supporting, sine low, noise off",
    filter: "Low HPF, moderately open LPF, medium resonance",
    envelope: "Moderate attack, moderate decay, firm sustain",
    modulation: "Expression opens brilliance, light vibrato available",
    params: { pitch: 0, pulse: 1820, pwmAmount: 420, sawLevel: 2950, pulseLevel: 1450, sineLevel: 450, noiseLevel: 0, hp: 220, lp: 2280, res: 1500, attack: 1450, decay: 1680, sustain: 2950, release: 1850, ring: 0, lfoPitchDepth: 180, lfoPwmDepth: 260, vcfDepth: 760, vcaDepth: 240, lfoRate: 720, ringSpeed: 760, expression: 2250 },
  },
  {
    name: "Brass 2",
    category: "brass",
    detail: "brighter brass preset",
    sources: "Saw high, pulse medium, sine low, slight noise breath",
    filter: "Brighter LPF and stronger resonance than Brass 1",
    envelope: "Medium attack, stronger filter swell, medium release",
    modulation: "Expression gives the main tone lift",
    params: { pitch: 0, pulse: 1900, pwmAmount: 520, sawLevel: 3050, pulseLevel: 1650, sineLevel: 350, noiseLevel: 120, hp: 260, lp: 2480, res: 1900, attack: 1600, decay: 1820, sustain: 3020, release: 2050, ring: 0, lfoPitchDepth: 210, lfoPwmDepth: 340, vcfDepth: 980, vcaDepth: 260, lfoRate: 780, ringSpeed: 800, expression: 2400 },
  },
  {
    name: "Brass 3",
    category: "brass",
    detail: "sharper solo brass",
    sources: "Saw and pulse both high, sine low, tiny noise edge",
    filter: "Forward LPF, raised resonance, little HPF lift",
    envelope: "Faster attack than Brass 1/2, medium release",
    modulation: "A touch more vibrato for solo phrasing",
    params: { pitch: 12, pulse: 1750, pwmAmount: 650, sawLevel: 2800, pulseLevel: 2100, sineLevel: 300, noiseLevel: 100, hp: 340, lp: 2580, res: 2250, attack: 900, decay: 1550, sustain: 2750, release: 1750, ring: 90, lfoPitchDepth: 320, lfoPwmDepth: 420, vcfDepth: 820, vcaDepth: 260, lfoRate: 920, ringSpeed: 980, expression: 2350 },
  },
  {
    name: "Clavichord 1",
    category: "clavi",
    detail: "soft struck clavi",
    sources: "Saw medium, pulse low, sine off, tiny noise transient",
    filter: "Focused mid-bright filter with clear attack",
    envelope: "Fast attack, short decay, low sustain",
    modulation: "Minimal modulation, expression adds bite",
    params: { pitch: 0, pulse: 1450, pwmAmount: 180, sawLevel: 2500, pulseLevel: 900, sineLevel: 0, noiseLevel: 180, hp: 1050, lp: 2200, res: 1700, attack: 70, decay: 620, sustain: 700, release: 880, ring: 0, lfoPitchDepth: 60, lfoPwmDepth: 120, vcfDepth: 320, vcaDepth: 140, lfoRate: 420, ringSpeed: 900, expression: 2400 },
  },
  {
    name: "Clavichord 2",
    category: "clavi",
    detail: "brighter plucked clavi",
    sources: "Saw high, pulse medium, no sine, little noise snap",
    filter: "More open and resonant than Clavichord 1",
    envelope: "Very fast attack, quick decay, short release",
    modulation: "Little movement, meant to stay percussive",
    params: { pitch: 12, pulse: 1500, pwmAmount: 140, sawLevel: 2750, pulseLevel: 1200, sineLevel: 0, noiseLevel: 220, hp: 1320, lp: 2450, res: 2350, attack: 40, decay: 540, sustain: 520, release: 720, ring: 70, lfoPitchDepth: 40, lfoPwmDepth: 80, vcfDepth: 260, vcaDepth: 120, lfoRate: 520, ringSpeed: 1100, expression: 2550 },
  },
  {
    name: "Harpsichord 1",
    category: "clavi",
    detail: "thin bright pluck",
    sources: "Pulse high, saw medium, sine off, slight noise click",
    filter: "Bright LPF, moderate HPF, little resonance",
    envelope: "Immediate attack, very short decay, almost no sustain",
    modulation: "No intended motion, expression only adds presence",
    params: { pitch: 12, pulse: 1200, pwmAmount: 0, sawLevel: 1500, pulseLevel: 2500, sineLevel: 0, noiseLevel: 120, hp: 1500, lp: 2850, res: 600, attack: 10, decay: 380, sustain: 180, release: 380, ring: 0, lfoPitchDepth: 0, lfoPwmDepth: 0, vcfDepth: 120, vcaDepth: 0, lfoRate: 360, ringSpeed: 900, expression: 1800 },
  },
  {
    name: "Harpsichord 2",
    category: "clavi",
    detail: "fuller harpsichord preset",
    sources: "Pulse high, saw medium/high, little sine body, low noise",
    filter: "Slightly warmer than Harpsichord 1, still bright",
    envelope: "Immediate attack, short decay, tiny sustain trace",
    modulation: "Static by design",
    params: { pitch: 0, pulse: 1280, pwmAmount: 0, sawLevel: 2100, pulseLevel: 2350, sineLevel: 260, noiseLevel: 80, hp: 1280, lp: 2600, res: 900, attack: 10, decay: 460, sustain: 260, release: 460, ring: 0, lfoPitchDepth: 0, lfoPwmDepth: 0, vcfDepth: 150, vcaDepth: 0, lfoRate: 360, ringSpeed: 900, expression: 1750 },
  },
  {
    name: "Organ 1",
    category: "organ",
    detail: "compact preset organ",
    sources: "Sine and pulse dominant, saw low, no noise",
    filter: "Open filters, moderate brilliance, low resonance",
    envelope: "Fast attack, high sustain, medium release",
    modulation: "Very subtle PWM or chorus-style motion",
    params: { pitch: 0, pulse: 2400, pwmAmount: 220, sawLevel: 700, pulseLevel: 2200, sineLevel: 2500, noiseLevel: 0, hp: 420, lp: 3000, res: 520, attack: 50, decay: 900, sustain: 3600, release: 1500, ring: 0, lfoPitchDepth: 80, lfoPwmDepth: 180, vcfDepth: 120, vcaDepth: 180, lfoRate: 440, ringSpeed: 720, expression: 1500 },
  },
  {
    name: "Organ 2",
    category: "organ",
    detail: "brighter reed organ",
    sources: "Pulse high, sine medium, saw medium, no noise",
    filter: "Brighter than Organ 1 with a hint more edge",
    envelope: "Fast attack, strong sustain, medium release",
    modulation: "Still restrained, intended for held lines",
    params: { pitch: 12, pulse: 2300, pwmAmount: 260, sawLevel: 1200, pulseLevel: 2300, sineLevel: 1800, noiseLevel: 0, hp: 520, lp: 3150, res: 900, attack: 50, decay: 880, sustain: 3500, release: 1600, ring: 0, lfoPitchDepth: 120, lfoPwmDepth: 220, vcfDepth: 180, vcaDepth: 160, lfoRate: 480, ringSpeed: 760, expression: 1650 },
  },
  {
    name: "Guitar 1",
    category: "guitar",
    detail: "muted preset guitar",
    sources: "Saw and pulse pluck, tiny sine, slight noise pick",
    filter: "Mid-bright filter with some resonance",
    envelope: "Fast attack, short decay, low sustain, short release",
    modulation: "Minimal movement, expression adds edge",
    params: { pitch: 0, pulse: 1380, pwmAmount: 140, sawLevel: 2200, pulseLevel: 1600, sineLevel: 180, noiseLevel: 180, hp: 860, lp: 2280, res: 1550, attack: 60, decay: 760, sustain: 620, release: 820, ring: 100, lfoPitchDepth: 40, lfoPwmDepth: 80, vcfDepth: 360, vcaDepth: 120, lfoRate: 420, ringSpeed: 1450, expression: 2300 },
  },
  {
    name: "Guitar 2",
    category: "guitar",
    detail: "brighter picked guitar",
    sources: "Saw high, pulse medium, tiny sine, noise transient",
    filter: "Brighter LPF and more bite than Guitar 1",
    envelope: "Fast attack, short/medium decay, low sustain",
    modulation: "Very light movement, mostly static pluck tone",
    params: { pitch: 12, pulse: 1450, pwmAmount: 160, sawLevel: 2450, pulseLevel: 1450, sineLevel: 160, noiseLevel: 200, hp: 980, lp: 2520, res: 1750, attack: 45, decay: 700, sustain: 540, release: 760, ring: 140, lfoPitchDepth: 50, lfoPwmDepth: 90, vcfDepth: 420, vcaDepth: 120, lfoRate: 460, ringSpeed: 1520, expression: 2400 },
  },
  {
    name: "Funky 1",
    category: "funny",
    detail: "dry clipped funk stab",
    sources: "Pulse high, saw medium, no sine, low noise",
    filter: "Bright, nasal band-limited shape with medium resonance",
    envelope: "Fast attack, short decay, low sustain",
    modulation: "Little motion, meant for rhythmic chops",
    params: { pitch: 12, pulse: 1650, pwmAmount: 260, sawLevel: 1500, pulseLevel: 2600, sineLevel: 0, noiseLevel: 80, hp: 1150, lp: 2300, res: 2200, attack: 25, decay: 520, sustain: 700, release: 620, ring: 80, lfoPitchDepth: 30, lfoPwmDepth: 120, vcfDepth: 280, vcaDepth: 80, lfoRate: 540, ringSpeed: 980, expression: 2200 },
  },
  {
    name: "Funky 2",
    category: "funny",
    detail: "preset funky comp",
    sources: "Pulse and saw balanced, sine off, tiny noise",
    filter: "Sharper and more resonant than Funky 1",
    envelope: "Immediate attack, short decay, low release",
    modulation: "Slight PWM wobble if you hold notes longer",
    params: { pitch: 0, pulse: 1720, pwmAmount: 520, sawLevel: 1850, pulseLevel: 2350, sineLevel: 0, noiseLevel: 100, hp: 1260, lp: 2150, res: 2450, attack: 20, decay: 620, sustain: 820, release: 680, ring: 110, lfoPitchDepth: 40, lfoPwmDepth: 220, vcfDepth: 300, vcaDepth: 120, lfoRate: 700, ringSpeed: 1050, expression: 2300 },
  },
  {
    name: "Bass",
    category: "funny",
    detail: "factory-style mono bass",
    sources: "Saw and pulse strong, sine low, no noise",
    filter: "Low HPF, darker LPF, moderate resonance",
    envelope: "Fast attack, medium decay, medium sustain, short release",
    modulation: "A little more filter bounce and motion while keeping the note body solid",
    params: { pitch: -12, pulse: 1500, pwmAmount: 220, sawLevel: 2600, pulseLevel: 2200, sineLevel: 500, noiseLevel: 0, hp: 120, lp: 1580, res: 1520, attack: 20, decay: 720, sustain: 1500, release: 680, ring: 0, lfoPitchDepth: 30, lfoPwmDepth: 120, vcfDepth: 520, vcaDepth: 110, lfoRate: 680, ringSpeed: 720, expression: 1750 },
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

function getParam(name) {
  return document.querySelector(`[data-param='${name}']`);
}

function setParam(name, value) {
  const input = getParam(name);
  if (!input) return;
  input.value = String(value);
  updateOutput(input);
}

function paramValue(name, fallback = 0) {
  const input = getParam(name);
  return input ? Number(input.value) : fallback;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function clamp14(value) {
  return clamp(Math.round(value), 0, 4095);
}

function encodeUint14(value) {
  const clipped = clamp14(value);
  return [clipped & 0x7f, (clipped >> 7) & 0x7f];
}

function decodeUint14(data, offset) {
  return (data[offset] & 0x7f) | ((data[offset + 1] & 0x7f) << 7);
}

function pitchSliderToControl(value) {
  return clamp14(2048 + Number(value) * 64);
}

function controlToPitchSlider(value) {
  return clamp(Math.round((value - 2048) / 64), -24, 24);
}

function spreadSliderToControl(value) {
  return clamp14(2048 + Number(value) * 56);
}

function controlToSpreadSlider(value) {
  return clamp(Math.round((value - 2048) / 56), -36, 36);
}

function currentPatchPayload() {
  const pitch = Number(getParam("pitch").value);
  const payload = [PATCH_PROTOCOL_VERSION];
  [
    pitchSliderToControl(pitch),
    Number(getParam("portamento").value),
    Number(getParam("pitchCvRange").value),
    Number(getParam("pulse").value),
    Number(getParam("pwmAmount").value),
    Number(getParam("sawLevel").value),
    Number(getParam("pulseLevel").value),
    Number(getParam("sineLevel").value),
    Number(getParam("noiseLevel").value),
    Number(getParam("level").value),
    spreadSliderToControl(getParam("spread").value),
    Number(getParam("hp").value),
    Number(getParam("lp").value),
    Number(getParam("res").value),
    Number(getParam("expression").value),
    Number(getParam("attack").value),
    Number(getParam("decay").value),
    Number(getParam("sustain").value),
    Number(getParam("release").value),
    Number(getParam("lfoRate").value),
    Number(getParam("lfoPitchDepth").value),
    Number(getParam("lfoPwmDepth").value),
    Number(getParam("vcfDepth").value),
    Number(getParam("vcaDepth").value),
    Number(getParam("ring").value),
    Number(getParam("ringSpeed").value),
  ].forEach((value) => payload.push(...encodeUint14(value)));
  return payload;
}

function selectedCardSlot() {
  return clamp(Number(cardSlotEl.value), 0, 7);
}

function renderCardSlots() {
  Array.from(cardSlotEl.options).forEach((option, index) => {
    const saved = (savedSlotMask & (1 << index)) !== 0;
    const startup = saved && index === startupSlot;
    option.textContent = `Slot ${index + 1}${saved ? " saved" : " empty"}${startup ? " (startup)" : ""}`;
  });
  setStartupSlotEl.disabled = (savedSlotMask & (1 << selectedCardSlot())) === 0;
}

function resetPickupPreview() {
  pickupPreview = [false, false, false];
  renderPanelLeds();
}

function markPickupPreviewForParam(name) {
  const map = {
    pitch: ["up", 0],
    pulse: ["up", 1],
    pwmAmount: ["up", 2],
    hp: ["middle", 0],
    lp: ["middle", 1],
    res: ["middle", 2],
    spread: ["down", 0],
    ring: ["down", 1],
    lfoPitchDepth: ["down", 2],
  };
  const match = map[name];
  if (match && match[0] === panelPageEl.value) {
    pickupPreview[match[1]] = true;
  }
  renderPanelLeds();
}

function ledValuesForPanel() {
  const panel = panelPageEl.value;
  if (panel === "up") {
    return [
      pitchSliderToControl(paramValue("pitch")),
      pickupPreview[0] ? 4095 : 384,
      paramValue("pulse"),
      pickupPreview[1] ? 4095 : 384,
      paramValue("pwmAmount"),
      pickupPreview[2] ? 4095 : 384,
    ];
  }

  if (panel === "middle") {
    return [
      paramValue("hp"),
      pickupPreview[0] ? 4095 : 384,
      paramValue("lp"),
      pickupPreview[1] ? 4095 : 384,
      paramValue("res"),
      pickupPreview[2] ? 4095 : 384,
    ];
  }

  return [
    spreadSliderToControl(paramValue("spread")),
    pickupPreview[0] ? 4095 : 384,
    paramValue("ring"),
    pickupPreview[1] ? 4095 : 384,
    paramValue("lfoPitchDepth"),
    pickupPreview[2] ? 4095 : 384,
  ];
}

function renderPanelLeds() {
  const values = ledValuesForPanel();
  const labels = ["Main value", "Main pickup", "X value", "X pickup", "Y value", "Y pickup"];
  ledPreviewEls.forEach((led, index) => {
    const value = clamp14(values[index] || 0);
    const intensity = 0.42 + (value / 4095) * 0.58;
    const percent = Math.round((value / 4095) * 100);
    led.style.setProperty("--led-alpha", intensity.toFixed(2));
    led.classList.toggle("is-pickup", index % 2 === 1);
    led.classList.toggle("is-picked-up", index % 2 === 1 && value > 1000);
    led.querySelector("b").textContent = index % 2 === 0 ? `${percent}` : `${index}`;
    led.title = `LED ${index}: ${labels[index]} (${percent}%)`;
    led.setAttribute("aria-label", led.title);
  });
}

function frameFor(command, payload = []) {
  return [0xf0, SYSEX_MANUFACTURER, ...SYSEX_ID, command, ...payload, 0xf7];
}

function sendSysex(command, payload = []) {
  if (!midiOutput) {
    statusEl.textContent = "Connect MIDI before sending a patch.";
    protocolEl.textContent = "Disconnected";
    logDeveloper("midi send blocked", { reason: "no output" });
    return false;
  }

  const frame = frameFor(command, payload);
  midiOutput.send(frame);
  logDeveloper("sysex sent", { command, bytes: frame.length, output: midiOutput.name || "unnamed port" });
  return true;
}

function usePatchPayload(payload, sourceSlot = 0x7f) {
  if (payload[0] !== PATCH_PROTOCOL_VERSION || payload.length < 53) {
    logDeveloper("patch response ignored", { reason: "unsupported payload", version: payload[0], bytes: payload.length, slot: sourceSlot });
    return;
  }

  let offset = 1;
  setParam("pitch", controlToPitchSlider(decodeUint14(payload, offset))); offset += 2;
  setParam("portamento", decodeUint14(payload, offset)); offset += 2;
  setParam("pitchCvRange", decodeUint14(payload, offset)); offset += 2;
  setParam("pulse", decodeUint14(payload, offset)); offset += 2;
  setParam("pwmAmount", decodeUint14(payload, offset)); offset += 2;
  setParam("sawLevel", decodeUint14(payload, offset)); offset += 2;
  setParam("pulseLevel", decodeUint14(payload, offset)); offset += 2;
  setParam("sineLevel", decodeUint14(payload, offset)); offset += 2;
  setParam("noiseLevel", decodeUint14(payload, offset)); offset += 2;
  setParam("level", decodeUint14(payload, offset)); offset += 2;
  setParam("spread", controlToSpreadSlider(decodeUint14(payload, offset))); offset += 2;
  setParam("hp", decodeUint14(payload, offset)); offset += 2;
  setParam("lp", decodeUint14(payload, offset)); offset += 2;
  setParam("res", decodeUint14(payload, offset)); offset += 2;
  setParam("expression", decodeUint14(payload, offset)); offset += 2;
  setParam("attack", decodeUint14(payload, offset)); offset += 2;
  setParam("decay", decodeUint14(payload, offset)); offset += 2;
  setParam("sustain", decodeUint14(payload, offset)); offset += 2;
  setParam("release", decodeUint14(payload, offset)); offset += 2;
  setParam("lfoRate", decodeUint14(payload, offset)); offset += 2;
  setParam("lfoPitchDepth", decodeUint14(payload, offset)); offset += 2;
  setParam("lfoPwmDepth", decodeUint14(payload, offset)); offset += 2;
  setParam("vcfDepth", decodeUint14(payload, offset)); offset += 2;
  setParam("vcaDepth", decodeUint14(payload, offset)); offset += 2;
  setParam("ring", decodeUint14(payload, offset)); offset += 2;
  setParam("ringSpeed", decodeUint14(payload, offset));
  drawEnvelope();
  resetPickupPreview();
  statusEl.textContent = sourceSlot < 8
    ? `Patch read from card slot ${sourceSlot + 1}.`
    : "Patch read from card.";
  protocolEl.textContent = "CS80 v5";
  logDeveloper("patch response applied", { version: payload[0], slot: sourceSlot < 8 ? sourceSlot + 1 : null });
}

function handleMidiMessage(event) {
  const data = Array.from(event.data);
  if (data[0] !== 0xf0 || data[data.length - 1] !== 0xf7) return;
  if (data[1] !== SYSEX_MANUFACTURER) return;
  if (!SYSEX_ID.every((value, index) => data[2 + index] === value)) return;

  const command = data[6];
  const payload = data.slice(7, -1);
  logDeveloper("sysex received", { command, bytes: data.length, input: midiInput?.name || "unnamed port" });

  if (command === COMMAND_PATCH_RESPONSE) {
    const hasSlotByte = payload[0] !== PATCH_PROTOCOL_VERSION;
    const slot = hasSlotByte ? payload[0] & 0x7f : 0x7f;
    usePatchPayload(hasSlotByte ? payload.slice(1) : payload, slot);
  }

  if (command === COMMAND_SLOT_RESPONSE) {
    const slot = payload[0] & 0x07;
    savedSlotMask |= 1 << slot;
    renderCardSlots();
    cardSlotEl.value = String(slot);
    usePatchPayload(payload.slice(1), slot);
  }

  if (command === COMMAND_SLOTS_RESPONSE) {
    savedSlotMask = (payload[0] & 0x7f) | ((payload[1] & 0x7f) << 7);
    startupSlot = (payload[2] ?? 0) & 0x07;
    renderCardSlots();
    logDeveloper("card slot map received", { mask: savedSlotMask, startupSlot: startupSlot + 1 });
  }
}

function selectMidiPorts() {
  const outputs = Array.from(midiAccess.outputs.values());
  const inputs = Array.from(midiAccess.inputs.values());
  const match = (port) => /cs80|workshop|pico/i.test(port.name || "");

  midiOutput = outputs.find(match) || outputs[0] || null;
  midiInput = inputs.find(match) || inputs[0] || null;

  inputs.forEach((input) => {
    input.onmidimessage = input === midiInput ? handleMidiMessage : null;
  });

  protocolEl.textContent = midiOutput ? "CS80 v5" : "No MIDI Out";
  statusEl.textContent = midiOutput
    ? `MIDI connected: ${midiOutput.name || "unnamed output"}.`
    : "MIDI access granted, but no output port was found.";
  logDeveloper("midi ports selected", {
    input: midiInput?.name || null,
    output: midiOutput?.name || null,
  });

  if (midiOutput) {
    sendSysex(COMMAND_REQUEST_SLOTS);
  }
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
  pickupPreview = [false, false, false];
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
  renderPanelLeds();
  logDeveloper("panel page mapped", { switch: panelPageEl.value });
}

function updateOutput(range) {
  const output = range.parentElement.querySelector("output");
  if (!output) return;
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

function resolvedPresetParams(preset) {
  const legacy = preset.params || {};
  const { mix, pwm, vcoDepth, ...params } = legacy;
  return {
    ...defaultPatchParams,
    ...params,
    portamento: params.portamento ?? (preset.name === "Doctor Who Theme" ? defaultPatchParams.portamento : 0),
    // Preserve the previous preset character while separating its former
    // combined VCO modulation control into independent pitch and PWM routes.
    pwmAmount: params.pwmAmount ?? pwm ?? defaultPatchParams.pwmAmount,
    lfoPitchDepth: params.lfoPitchDepth ?? Math.round((vcoDepth || 0) / 4),
    lfoPwmDepth: params.lfoPwmDepth ?? vcoDepth ?? defaultPatchParams.lfoPwmDepth,
  };
}

function percent(value) {
  return `${Math.round((Number(value) / 4095) * 100)}%`;
}

function renderPresetDetail(index = 0) {
  const preset = presets[index] || presets[0];
  const params = resolvedPresetParams(preset);
  presetNameEl.value = preset.name;
  presetDetailEl.innerHTML = `
    <h3>${preset.name}</h3>
    <dl>
      <dt>Sources</dt><dd>${preset.sources}</dd>
      <dt>Source levels</dt><dd>Saw ${percent(params.sawLevel)}, square/pulse ${percent(params.pulseLevel)}, sine ${percent(params.sineLevel)}, noise ${percent(params.noiseLevel)}</dd>
      <dt>Filter</dt><dd>${preset.filter}</dd>
      <dt>Envelope</dt><dd>${preset.envelope}</dd>
      <dt>Modulation</dt><dd>${preset.modulation} Pitch ${percent(params.lfoPitchDepth)}, PWM ${percent(params.lfoPwmDepth)}.</dd>
    </dl>
  `;
}

function applyPreset(index) {
  const preset = presets[index] || presets[0];
  const params = resolvedPresetParams(preset);
  Object.entries(params).forEach(([name, value]) => setParam(name, value));
  presetNameEl.value = preset.name;
  drawEnvelope();
  resetPickupPreview();
  statusEl.textContent = `Loaded preset slot ${index + 1}: ${preset.name}. Press Apply to audition it or Save to write the selected card slot.`;
  logDeveloper("preset loaded", { slot: index + 1, name: preset.name, params });
}

function renderPresets() {
  const visiblePresets = presets
    .map((preset, index) => ({ preset, index }))
    .filter(({ preset }) => activeTone === "all" || preset.category === activeTone);

  presetGridEl.replaceChildren(...visiblePresets.map(({ preset, index }) => {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `preset-slot${index === selectedPresetIndex ? " is-active" : ""}`;
    button.innerHTML = `<strong>${index + 1}. ${preset.name}</strong><span>${preset.detail}</span>`;
    button.addEventListener("click", () => {
      selectedPresetIndex = index;
      document.querySelectorAll(".preset-slot").forEach((slot) => slot.classList.remove("is-active"));
      button.classList.add("is-active");
      renderPresetDetail(index);
      applyPreset(index);
    });
    return button;
  }));

  if (!visiblePresets.some(({ index }) => index === selectedPresetIndex)) {
    selectedPresetIndex = visiblePresets[0]?.index ?? 0;
  }
  renderPresetDetail(selectedPresetIndex);
}

function setToneFilter(tone) {
  activeTone = tone;
  toneEls.forEach((button) => {
    button.classList.toggle("is-active", button.dataset.tone === tone);
  });
  renderPresets();
  setPage("presets");
  const label = tone === "all" ? "all instruments" : tone.replace("-", " ");
  statusEl.textContent = `Showing ${label} presets.`;
  logDeveloper("instrument filter selected", { tone });
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
    midiAccess = access;
    midiAccess.onstatechange = selectMidiPorts;
    selectMidiPorts();
    logDeveloper("midi access granted", {
      inputs: Array.from(access.inputs.values()).map((input) => input.name || "unnamed port"),
      outputs: Array.from(access.outputs.values()).map((output) => output.name || "unnamed port"),
    });
  } catch (error) {
    statusEl.textContent = `MIDI connection failed: ${error.message}`;
    protocolEl.textContent = "Disconnected";
    logDeveloper("midi connection failed", { message: error.message });
  }
}

tabs.forEach((tab) => tab.addEventListener("click", () => setPage(tab.dataset.page)));
toneEls.forEach((button) => button.addEventListener("click", () => setToneFilter(button.dataset.tone)));
panelPageEl.addEventListener("change", renderKnobMap);
panelPageEl.addEventListener("input", renderKnobMap);
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
    markPickupPreviewForParam(range.dataset.param);
    logDeveloper("parameter changed", { parameter: range.dataset.param, value: Number(range.value) });
  });
});

readCardEl.addEventListener("click", () => {
  const slot = selectedCardSlot();
  if ((savedSlotMask & (1 << slot)) !== 0) {
    if (sendSysex(COMMAND_REQUEST_SLOT, [slot & 0x07])) {
      statusEl.textContent = `Card slot ${slot + 1} read requested.`;
      logDeveloper("slot read requested", { slot: slot + 1 });
    }
    return;
  }

  if (sendSysex(COMMAND_REQUEST_PATCH)) {
    statusEl.textContent = `Slot ${slot + 1} is empty; current card patch read requested.`;
    logDeveloper("current patch read requested");
  }
});

applyPatchEl.addEventListener("click", () => {
  if (sendSysex(COMMAND_APPLY_PATCH, currentPatchPayload())) {
    statusEl.textContent = "Patch sent to card.";
    logDeveloper("apply requested");
  }
});

savePatchEl.addEventListener("click", () => {
  const slot = selectedCardSlot();
  if (sendSysex(COMMAND_SAVE_SLOT, [slot & 0x07, ...currentPatchPayload()])) {
    savedSlotMask |= 1 << slot;
    renderCardSlots();
    statusEl.textContent = `Patch saved to card slot ${slot + 1}.`;
    logDeveloper("slot save requested", { slot: slot + 1 });
  }
});

setStartupSlotEl.addEventListener("click", () => {
  const slot = selectedCardSlot();
  if ((savedSlotMask & (1 << slot)) === 0) {
    statusEl.textContent = `Save card slot ${slot + 1} before making it the startup patch.`;
    return;
  }

  if (sendSysex(COMMAND_SET_STARTUP_SLOT, [slot & 0x07])) {
    startupSlot = slot;
    renderCardSlots();
    statusEl.textContent = `Card slot ${slot + 1} will load at startup.`;
    logDeveloper("startup slot set", { slot: slot + 1 });
  }
});

cardSlotEl.addEventListener("change", renderCardSlots);

deletePatchEl.addEventListener("click", () => {
  const slot = selectedCardSlot();
  if ((savedSlotMask & (1 << slot)) === 0) {
    statusEl.textContent = `Card slot ${slot + 1} is already empty.`;
    logDeveloper("slot delete skipped", { slot: slot + 1, reason: "empty" });
    return;
  }

  const confirmed = window.confirm(`Delete saved card slot ${slot + 1}?`);
  if (!confirmed) {
    statusEl.textContent = `Delete cancelled for card slot ${slot + 1}.`;
    logDeveloper("slot delete cancelled", { slot: slot + 1 });
    return;
  }

  if (sendSysex(COMMAND_DELETE_SLOT, [slot & 0x07])) {
    savedSlotMask &= ~(1 << slot);
    renderCardSlots();
    statusEl.textContent = `Delete sent for card slot ${slot + 1}.`;
    logDeveloper("slot delete requested", { slot: slot + 1 });
  }
});

renderKnobMap();
renderPresets();
renderCardSlots();
renderThemeMode();
renderDeveloperMode();
renderDeveloperLog();
drawEnvelope();
