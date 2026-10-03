const root = document.documentElement;
const storageKey = "cosmik-m1n1-presets-v1";
const midi = { access: null, input: null, output: null, log: [], responseCommand: null, responseValues: [], voicePoll: null, localEdits: false };
const preset = { baseline: null, active: null, current: null, userSlots: [] };
const midiElements = {
  connect: document.querySelector("#midiConnect"), refresh: document.querySelector("#midiRefresh"),
  input: document.querySelector("#midiInput"), output: document.querySelector("#midiOutput"),
  status: document.querySelector("#midiStatus"), log: document.querySelector("#midiLog"),
  clearLog: document.querySelector("#midiClearLog"), showAll: document.querySelector("#midiShowAll"),
  sysex: document.querySelector("#midiSysex"), sendSysex: document.querySelector("#midiSendSysex"), probe: document.querySelector("#midiProbe"), apply: document.querySelector("#midiApply")
};
const presetElements = Object.fromEntries(["name:presetName", "slot:userSlot", "status:presetStatus", "factory:factoryPresets", "user:userPresets", "new:presetNew", "duplicate:presetDuplicate", "save:presetSave", "delete:presetDelete", "reset:presetReset"].map((pair) => { const [key, id] = pair.split(":"); return [key, document.querySelector(`#${id}`)]; }));

const clone = (value) => JSON.parse(JSON.stringify(value));
function merge(base, patch) { Object.entries(patch).forEach(([key, value]) => { if (value && typeof value === "object" && !Array.isArray(value)) merge(base[key], value); else base[key] = value; }); return base; }
function setPath(object, path, value) { const parts = path.split("."); const key = parts.pop(); parts.reduce((target, part) => target[part], object)[key] = value; }
function valueFor(input) { if (input.type === "range") return Number(input.value); if (input.type === "checkbox") return input.checked; return input.value; }
function updateReadout(input) { const readout = input.nextElementSibling; if (readout?.classList.contains("range-readout")) readout.value = `${input.value}${input.dataset.unit ? ` ${input.dataset.unit}` : ""}`; }
function setTheme(theme) { root.dataset.theme = theme; localStorage.setItem("minimoog-theme", theme); document.querySelectorAll("[data-theme]").forEach((button) => button.classList.toggle("is-active", button.dataset.theme === theme)); }
function readControls() {
  const sound = { name: "Init Voice" };
  document.querySelectorAll("[data-param]").forEach((input) => { const parts = input.dataset.param.split("."); let target = sound; parts.slice(0, -1).forEach((part) => { target[part] ||= {}; target = target[part]; }); target[parts.at(-1)] = valueFor(input); });
  return sound;
}
function applySound(sound) { document.querySelectorAll("[data-param]").forEach((input) => { const value = input.dataset.param.split(".").reduce((target, part) => target?.[part], sound); if (value !== undefined) { if (input.type === "checkbox") input.checked = Boolean(value); else input.value = value; } if (input.type === "range") updateReadout(input); }); presetElements.name.value = sound.name; }
function voice(name, changes) { return merge(merge(clone(preset.baseline), { name }), changes); }
function factoryVoices() {
  return [
    voice("Init Voice", { modulation: { depth: 0 } }),
    voice("Funk Glide Bass", { osc1: { range: "16'", waveform: "Sawtooth", level: 92 }, osc2: { range: "16'", waveform: "Square", level: 78 }, mixer: { osc1Level: 92, osc2Level: 78, drive: 26 }, filter: { cutoff: 720, emphasis: 34, contour: 72 }, filterAds: { attack: 5, decay: 310, sustain: 18 }, ampAds: { attack: 4, decay: 260, sustain: 72 }, modulation: { glide: 105 } }),
    voice("Three Saw Bass", { osc1: { range: "16'", waveform: "Sawtooth", level: 86 }, osc2: { range: "16'", waveform: "Sawtooth", interval: 7, level: 78 }, osc3: { returnLevel: 70 }, mixer: { osc1Level: 86, osc2Level: 78, osc3Return: 70, drive: 22 }, filter: { cutoff: 540, emphasis: 16, contour: 44 }, filterAds: { attack: 5, decay: 410, sustain: 28 }, ampAds: { attack: 4, decay: 350, sustain: 78 } }),
    voice("West Coast Whistle", { osc1: { range: "4'", waveform: "Triangle", level: 72 }, osc2: { range: "2'", waveform: "Triangle / sawtooth", interval: 0, level: 42 }, mixer: { osc1Level: 72, osc2Level: 42, drive: 4 }, filter: { cutoff: 4260, emphasis: 48, contour: 36, keyboardTracking: "Full" }, filterAds: { attack: 90, decay: 820, sustain: 0 }, ampAds: { attack: 38, decay: 720, sustain: 0 }, modulation: { secondarySource: "Internal LFO", noiseBlend: 100, lfoRate: 5.8, lfoSquare: false, depth: 18, blend: 80 } }),
    voice("Glide Mod Arp", { osc1: { waveform: "Sawtooth", level: 80 }, osc2: { range: "4'", waveform: "Narrow rectangle", interval: 0, level: 55 }, mixer: { osc1Level: 80, osc2Level: 55, drive: 14 }, filter: { cutoff: 1880, emphasis: 38, contour: 74, keyboardTracking: "Full" }, filterAds: { attack: 12, decay: 480, sustain: 24 }, ampAds: { attack: 9, decay: 360, sustain: 58 }, modulation: { secondarySource: "Internal LFO", noiseBlend: 100, lfoRate: 7.2, lfoSquare: false, depth: 31, blend: 64, glide: 180 } }),
    voice("Slow Brass Lead", { osc1: { waveform: "Sawtooth", level: 82 }, osc2: { waveform: "Wide rectangle", level: 66 }, mixer: { osc1Level: 82, osc2Level: 66, drive: 18 }, filter: { cutoff: 1320, emphasis: 22, contour: 68 }, filterAds: { attack: 220, decay: 980, sustain: 46 }, ampAds: { attack: 180, decay: 860, sustain: 76 }, modulation: { glide: 130 } }),
    voice("Sub Pulse Bass", { osc1: { range: "32'", waveform: "Square", level: 84 }, osc2: { range: "16'", waveform: "Wide rectangle", level: 63 }, mixer: { osc1Level: 84, osc2Level: 63, drive: 30 }, filter: { cutoff: 410, emphasis: 12, contour: 52, keyboardTracking: "None" }, filterAds: { attack: 3, decay: 250, sustain: 34 }, ampAds: { attack: 3, decay: 230, sustain: 82 } }),
    voice("Resonant Pulse Lead", { osc1: { waveform: "Narrow rectangle", level: 74 }, osc2: { range: "4'", waveform: "Square", interval: 0, level: 67 }, mixer: { osc1Level: 74, osc2Level: 67, drive: 12 }, filter: { cutoff: 2460, emphasis: 56, contour: 77, keyboardTracking: "Full" }, filterAds: { attack: 18, decay: 640, sustain: 20 }, ampAds: { attack: 11, decay: 520, sustain: 62 }, modulation: { secondarySource: "Internal LFO", noiseBlend: 100, lfoRate: 4.4, lfoSquare: true, depth: 8, blend: 60 } })
  ];
}
function loadUserSlots() { try { const stored = JSON.parse(localStorage.getItem(storageKey)); if (Array.isArray(stored) && stored.length === 8) return stored; } catch {} return Array(8).fill(null); }
function isDirty() { return JSON.stringify(preset.current) !== JSON.stringify(preset.active?.sound); }
function updatePresetStatus() { const origin = preset.active?.kind === "factory" ? "Factory preset" : preset.active?.kind === "user" ? `User slot ${preset.active.slot + 1}` : "Unsaved voice"; presetElements.status.value = `${origin}${isDirty() ? " · modified" : ""}`; }
function activate(sound, active) { preset.active = { ...active, sound: clone(sound) }; preset.current = clone(sound); applySound(preset.current); renderPresetBank(); updatePresetStatus(); }
function button(name, selected, action, empty = false) { const element = document.createElement("button"); element.type = "button"; element.className = `preset-button${selected ? " is-active" : ""}${empty ? " is-empty" : ""}`; element.textContent = name; element.addEventListener("click", action); return element; }
function sendCardCommand(command, payload = []) { if (!midi.output) return false; const bytes = [0xf0, 0x7d, 0x4d, 0x4e, 0x56, 0x31, command, ...payload.map((value) => value & 0x7f), 0xf7]; midi.output.send(bytes); logMidi("OUT", bytes); return true; }
function encodeName(name) { return Array.from({ length: 16 }, (_, index) => name.charCodeAt(index) || 0); }
function control(value, minimum, maximum) { return Math.round(Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum))) * 4095); }
function lfoRateControl(value) { return Math.round(Math.sqrt(Math.max(0, Math.min(1, (value - 0.05) / 13.95))) * 4095); }
function lfoRateFromControl(value) { return Number((0.05 + (value * value * 13.95) / (4095 * 4095)).toFixed(2)); }
function selectedIndex(path) { const input = document.querySelector(`[data-param="${path}"]`); return [...input.options].indexOf(input.selectedOptions[0]); }
function append14(values, value) { values.push(value & 0x7f, (value >> 7) & 0x7f); }
function sendCurrentVoice() {
  const values = [];
  const sound = readControls();
  const range = { "32'": 0, "16'": 1024, "8'": 2048, "4'": 3072, "2'": 4095 };
  const tracking = [0, 1365, 2730, 4095];
  const voice = [
    control(Number(sound.osc1.tune), -7, 7), control(Number(sound.osc2.interval), -7, 7),
    control(selectedIndex("osc1.waveform"), 0, 5), control(selectedIndex("osc2.waveform"), 0, 5),
    control(Number(sound.mixer.osc1Level), 0, 100), control(Number(sound.mixer.osc2Level), 0, 100),
    control(Number(sound.mixer.osc3Return), 0, 100), range[sound.osc3.range] ?? 2048,
    control(Number(sound.filter.cutoff), 20, 12000), 2048, control(Number(sound.filter.contour), 0, 100),
    control(Number(sound.filter.emphasis), 0, 100), control(Number(sound.osc3.offset), -7, 7),
    control(Number(sound.modulation.depth), 0, 100), control(Number(sound.modulation.blend), 0, 100),
    range[sound.osc1.range] ?? 2048, range[sound.osc2.range] ?? 2048,
    control(Number(sound.mixer.noise), 0, 100), control(selectedIndex("mixer.noiseColour"), 0, 1),
    control(Number(sound.modulation.noiseBlend), 0, 100), control(selectedIndex("filter.range"), 0, 1),
    sound.modulation.filterEnabled === "On" ? 4095 : 0, sound.modulation.oscillatorEnabled === "On" ? 4095 : 0,
    control(selectedIndex("modulation.primarySource"), 0, 1), control(selectedIndex("modulation.secondarySource"), 0, 2),
    lfoRateControl(Number(sound.modulation.lfoRate)), sound.modulation.lfoSquare ? 4095 : 0,
    control(Number(sound.modulation.glide), 0, 2000),
    control(Number(sound.ampAds.attack), 1, 10000), control(Number(sound.ampAds.decay), 4, 10000),
    control(Number(sound.ampAds.sustain), 0, 100), control(Number(sound.filterAds.attack), 1, 10000),
    control(Number(sound.filterAds.decay), 4, 10000), control(Number(sound.filterAds.sustain), 0, 100),
    tracking[selectedIndex("filter.keyboardTracking")] ?? 2730,
    sound.ampAds.decayRelease === "true" ? 4095 : 0, sound.filterAds.decayRelease === "true" ? 4095 : 0
  ];
  voice.forEach((value) => append14(values, value));
  if (sendCardCommand(12, values)) { midi.localEdits = false; setMidiStatus("Sending voice to card..."); }
}
function setControl(path, value) { const input = document.querySelector(`[data-param="${path}"]`); if (!input) return; if (input.type === "checkbox") input.checked = Boolean(value); else if (input.tagName === "SELECT" && Number.isInteger(value)) input.selectedIndex = value; else input.value = value; if (input.type === "range") updateReadout(input); setPath(preset.current, path, valueFor(input)); }
function applyCardVoice(values) {
  const value = (index) => values[index];
  const range = ["32'", "16'", "8'", "4'", "2'"];
  setControl("osc1.tune", Math.round(value(0) * 14 / 4095 - 7)); setControl("osc2.interval", Math.round(value(1) * 14 / 4095 - 7));
  setControl("osc1.waveform", Math.max(0, Math.min(5, Math.round(value(2) * 5 / 4095)))); setControl("osc2.waveform", Math.max(0, Math.min(5, Math.round(value(3) * 5 / 4095))));
  setControl("mixer.osc1Level", Math.round(value(4) * 100 / 4095)); setControl("mixer.osc2Level", Math.round(value(5) * 100 / 4095)); setControl("mixer.osc3Return", Math.round(value(6) * 100 / 4095));
  setControl("osc3.range", range[Math.max(0, Math.min(4, Math.round(value(7) / 1024)))]); setControl("filter.cutoff", Math.round(20 + value(8) * 11980 / 4095));
  setControl("filter.contour", Math.round(value(10) * 100 / 4095)); setControl("filter.emphasis", Math.round(value(11) * 100 / 4095)); setControl("osc3.offset", Math.round(value(12) * 14 / 4095 - 7));
  setControl("modulation.depth", Math.round(value(13) * 100 / 4095)); setControl("modulation.blend", Math.round(value(14) * 100 / 4095));
  setControl("osc1.range", range[Math.max(0, Math.min(4, Math.round(value(15) / 1024)))]); setControl("osc2.range", range[Math.max(0, Math.min(4, Math.round(value(16) / 1024)))]);
  setControl("mixer.noise", Math.round(value(17) * 100 / 4095)); setControl("mixer.noiseColour", value(18) >= 2048 ? 1 : 0); setControl("modulation.noiseBlend", Math.round(value(19) * 100 / 4095));
  setControl("filter.range", value(20) >= 2048 ? 1 : 0); setControl("modulation.filterEnabled", value(21) >= 2048 ? "On" : "Off"); setControl("modulation.oscillatorEnabled", value(22) >= 2048 ? "On" : "Off");
  setControl("modulation.primarySource", value(23) >= 2048 ? 1 : 0); setControl("modulation.secondarySource", Math.max(0, Math.min(2, Math.round(value(24) * 2 / 4095))));
  setControl("modulation.lfoRate", lfoRateFromControl(value(25))); setControl("modulation.lfoSquare", value(26) >= 2048); setControl("modulation.glide", Math.round(value(27) * 2000 / 4095));
  setControl("ampAds.attack", Math.round(1 + value(28) * 9999 / 4095)); setControl("ampAds.decay", Math.round(4 + value(29) * 9996 / 4095)); setControl("ampAds.sustain", Math.round(value(30) * 100 / 4095));
  setControl("filterAds.attack", Math.round(1 + value(31) * 9999 / 4095)); setControl("filterAds.decay", Math.round(4 + value(32) * 9996 / 4095)); setControl("filterAds.sustain", Math.round(value(33) * 100 / 4095));
  setControl("filter.keyboardTracking", Math.max(0, Math.min(3, Math.round(value(34) * 3 / 4095))));
  setControl("ampAds.decayRelease", value(35) >= 2048 ? "true" : "false"); setControl("filterAds.decayRelease", value(36) >= 2048 ? "true" : "false");
  midi.localEdits = false; updatePresetStatus();
}
function requestCardVoice() { if (!midi.output || midi.responseCommand !== null || midi.localEdits) return; const bytes = [0xf0, 0x7d, 0x4d, 0x4e, 0x56, 0x31, 0x0d, 0xf7]; midi.output.send(bytes); }
function startVoicePolling() { if (midi.voicePoll) return; midi.voicePoll = window.setInterval(requestCardVoice, 750); requestCardVoice(); }
function renderPresetBank() {
  const factory = factoryVoices();
  presetElements.factory.replaceChildren(...factory.map((sound, index) => button(sound.name, preset.active?.kind === "factory" && preset.active.index === index, () => { sendCardCommand(8, [0, index]); activate(sound, { kind: "factory", index }); })));
  presetElements.user.replaceChildren(...preset.userSlots.map((sound, index) => button(sound ? sound.name : `User ${index + 1} · empty`, preset.active?.kind === "user" && preset.active.slot === index, () => { if (!sound) return; sendCardCommand(8, [1, index]); activate(preset.current, { kind: "user", slot: index }); }, !sound)));
  presetElements.slot.replaceChildren(...preset.userSlots.map((sound, index) => new Option(sound ? `${index + 1}: ${sound.name}` : `${index + 1}: Empty`, index)));
  if (preset.active?.kind === "user") presetElements.slot.value = preset.active.slot;
}
function updateCurrent(input) { setPath(preset.current, input.dataset.param, valueFor(input)); midi.localEdits = true; updateReadout(input); updatePresetStatus(); }
function saveUserSlot() { const slot = Number(presetElements.slot.value); const name = presetElements.name.value.trim().slice(0, 16) || "Untitled Voice"; if (midi.output) { sendCurrentVoice(); if (sendCardCommand(11, [slot, ...encodeName(name)])) { setMidiStatus(`Saving current voice to User ${slot + 1}...`); return; } } preset.current.name = name; preset.userSlots[slot] = clone(preset.current); localStorage.setItem(storageKey, JSON.stringify(preset.userSlots)); activate(preset.userSlots[slot], { kind: "user", slot }); }
function deleteUserSlot() { const slot = Number(presetElements.slot.value); const sound = preset.userSlots[slot]; if (!sound || !window.confirm(`Delete user voice ${slot + 1}: ${sound.name}?`)) return; if (sendCardCommand(9, [slot])) { setMidiStatus(`Deleting User ${slot + 1}...`); return; } preset.userSlots[slot] = null; localStorage.setItem(storageKey, JSON.stringify(preset.userSlots)); activate(factoryVoices()[0], { kind: "factory", index: 0 }); }

document.querySelectorAll("[data-theme]").forEach((button) => button.addEventListener("click", () => setTheme(button.dataset.theme)));
document.querySelectorAll('input[type="range"]').forEach((input) => { if (!input.hasAttribute("data-no-readout")) { const readout = document.createElement("output"); readout.className = "range-readout"; input.insertAdjacentElement("afterend", readout); updateReadout(input); } input.addEventListener("input", () => updateCurrent(input)); });
document.querySelectorAll('input[type="checkbox"][data-param]').forEach((input) => input.addEventListener("change", () => updateCurrent(input)));
document.querySelectorAll("select[data-param]").forEach((input) => input.addEventListener("change", () => updateCurrent(input)));
presetElements.name.addEventListener("input", () => { preset.current.name = presetElements.name.value.slice(0, 16); updatePresetStatus(); });
presetElements.new.addEventListener("click", () => activate(merge(clone(preset.baseline), { name: "New Voice" }), { kind: "new" }));
presetElements.duplicate.addEventListener("click", () => { const copy = clone(preset.current); copy.name = `${copy.name.slice(0, 11)} Copy`; activate(copy, { kind: "new" }); });
presetElements.save.addEventListener("click", saveUserSlot);
presetElements.delete.addEventListener("click", deleteUserSlot);
presetElements.reset.addEventListener("click", () => activate(preset.active.sound, preset.active));

function midiPortName(port) { return port.name || port.manufacturer || port.id; }
function setMidiStatus(message) { midiElements.status.value = message; }
function renderMidiLog() { midiElements.log.textContent = midi.log.length ? midi.log.join("\n") : "Waiting for MIDI traffic."; midiElements.log.scrollTop = midiElements.log.scrollHeight; }
function appendMidiLog(message) { midi.log.push(`${new Date().toLocaleTimeString()} ${message}`); if (midi.log.length > 200) midi.log.shift(); renderMidiLog(); }
function logMidi(direction, data) { const sysex = data[0] === 0xf0; const cardResponse = data[0] === 0xbf && data[1] >= 117 && data[1] <= 119; if (cardResponse || (!sysex && !midiElements.showAll.checked)) return; const bytes = Array.from(data, (byte) => byte.toString(16).padStart(2, "0").toUpperCase()).join(" "); appendMidiLog(`${direction} ${sysex ? "SysEx" : "Performance MIDI"}: ${bytes}`); }
function populate(select, ports, selected) { const previous = selected?.id || select.value; select.replaceChildren(); if (!ports.length) { select.add(new Option("No ports available", "")); return null; } ports.forEach((port) => select.add(new Option(midiPortName(port), port.id))); const preferred = ports.find((port) => /cosmik m1n1|minimoog voice/i.test(midiPortName(port))); select.value = ports.some((port) => port.id === previous) ? previous : (preferred || ports[0]).id; return ports.find((port) => port.id === select.value) || null; }
function detachMidiInput() { if (!midi.access) return; midi.access.inputs.forEach((input) => { input.onmidimessage = null; }); }
function handleCardResponse(data) { if (data[0] !== 0xbf) return; if (data[1] === 119) { midi.responseCommand = data[2]; midi.responseValues = []; return; } if (data[1] === 118 && midi.responseCommand !== null) { midi.responseValues.push(data[2]); return; } if (data[1] !== 117 || midi.responseCommand === null) return; const command = midi.responseCommand, values = midi.responseValues; midi.responseCommand = null; if (command === 2 && values.length === 4) { const [version, userMask, bank, slot] = values; appendMidiLog(`IN Card identity: MNV1 v${version}, ${userMask.toString(2).padStart(8, "0")} user slots, ${bank ? "User" : "Factory"} ${slot + 1}`); setMidiStatus(`Card reply: MNV1 v${version} | User slots: ${userMask.toString(2).padStart(8, "0")} | Active: ${bank ? "User" : "Factory"} ${slot + 1}`); sendCardCommand(3); return; } if (command === 4 && values.length === 129) { const mask = values[0]; preset.userSlots = Array.from({ length: 8 }, (_, slot) => { if (!(mask & (1 << slot))) return null; const name = String.fromCharCode(...values.slice(1 + slot * 16, 17 + slot * 16)).replace(/\0/g, "").trim() || `User ${slot + 1}`; return { name }; }); renderPresetBank(); appendMidiLog(`IN Card user-preset list: ${preset.userSlots.filter(Boolean).length} stored`); setMidiStatus(`Loaded ${preset.userSlots.filter(Boolean).length} user voices from card.`); return; } if (command === 14 && values.length === 74) { const voice = Array.from({ length: 37 }, (_, index) => values[index * 2] | (values[index * 2 + 1] << 7)); applyCardVoice(voice); return; } if (command === 10 && values.length >= 3) { appendMidiLog("IN Card preset bank updated"); sendCardCommand(3); setMidiStatus("Card preset bank updated."); } }
function monitorMidiInput(input) { input.onmidimessage = (event) => { logMidi("IN", event.data); handleCardResponse(event.data); }; }
function selectMidiPorts() { if (!midi.access) return; const input = populate(midiElements.input, [...midi.access.inputs.values()], midi.input); const output = populate(midiElements.output, [...midi.access.outputs.values()], midi.output); detachMidiInput(); midi.access.inputs.forEach(monitorMidiInput); midi.input = input || null; midi.output = output || null; if (!midi.input && !midi.output) { setMidiStatus("No MIDI ports found"); return; } const inputState = midi.input ? `${midiPortName(midi.input)} (${midi.input.connection || "unknown"})` : "none"; const outputState = midi.output ? midiPortName(midi.output) : "none"; const sysexState = midi.access.sysexEnabled === false ? "blocked" : "enabled"; setMidiStatus(`Voice: ${outputState} | Monitor input: ${inputState} | SysEx: ${sysexState}`); }
async function openMidiPort(port) { if (!port || typeof port.open !== "function" || port.connection === "open") return; await port.open(); }
async function prepareMidiPorts() { if (!midi.access) return; const ports = [...midi.access.inputs.values(), ...midi.access.outputs.values()]; await Promise.allSettled(ports.map(openMidiPort)); selectMidiPorts(); }
async function connectMidi() { if (!navigator.requestMIDIAccess) { setMidiStatus("WebMIDI is unavailable in this browser"); return; } try { midiElements.showAll.checked = false; midi.access = await navigator.requestMIDIAccess({ sysex: true }); midi.access.onstatechange = prepareMidiPorts; await prepareMidiPorts(); startVoicePolling(); midiElements.connect.textContent = "Reconnect MIDI"; } catch (error) { setMidiStatus(`MIDI connection failed: ${error.message}`); } }
function parseSysEx(value) { const bytes = value.trim().split(/\s+/).filter(Boolean).map((token) => { if (!/^[0-9a-f]{1,2}$/i.test(token)) throw new Error(`Invalid byte: ${token}`); return Number.parseInt(token, 16); }); if (bytes.length < 2 || bytes[0] !== 0xf0 || bytes.at(-1) !== 0xf7) throw new Error("SysEx must start with F0 and end with F7"); return bytes; }
midiElements.connect.addEventListener("click", connectMidi); midiElements.refresh.addEventListener("click", prepareMidiPorts);
midiElements.input.addEventListener("change", () => { if (!midi.access) return; midi.input = midi.access.inputs.get(midiElements.input.value) || null; selectMidiPorts(); });
midiElements.output.addEventListener("change", () => { if (!midi.access) return; midi.output = midi.access.outputs.get(midiElements.output.value) || null; selectMidiPorts(); });
midiElements.clearLog.addEventListener("click", () => { midi.log = []; renderMidiLog(); });
midiElements.sendSysex.addEventListener("click", () => { try { if (!midi.output) throw new Error("Choose a MIDI output first"); const bytes = parseSysEx(midiElements.sysex.value); midi.output.send(bytes); logMidi("OUT", bytes); } catch (error) { setMidiStatus(error.message); } });
midiElements.probe.addEventListener("click", () => { try { if (!midi.output) throw new Error("Choose a MIDI output first"); const bytes = [0xf0, 0x7d, 0x4d, 0x4e, 0x56, 0x31, 0x01, 0xf7]; midi.output.send(bytes); logMidi("OUT", bytes); setMidiStatus("Waiting for card reply..."); } catch (error) { setMidiStatus(error.message); } });
midiElements.apply.addEventListener("click", sendCurrentVoice);

setTheme(localStorage.getItem("minimoog-theme") || (window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light"));
preset.baseline = readControls(); preset.userSlots = loadUserSlots(); activate(factoryVoices()[0], { kind: "factory", index: 0 });
