const TARGET_SAMPLE_RATE = 24000;
const BANK_MAGIC = 0x4b4e5550; // PUNK
const BANK_VERSION = 1;
const FORMAT_MULAW_8 = 1;
const SAMPLE_COUNT = 4;
const LOADER_MAGIC = 0x444c4350; // PCLD
const RESTORE_MAGIC = 0x524c4350; // PCLR
const HEADER_BYTES = 32 + SAMPLE_COUNT * 8;

const targets = {
  "2mb": { label: "standard 2 MB card", flashBytes: 2 * 1024 * 1024, bankBytes: 1024 * 1024 },
  "16mb": { label: "large 16 MB card", flashBytes: 16 * 1024 * 1024, bankBytes: 14 * 1024 * 1024 }
};

const slots = [
  { venue: "Marquee", phrase: "Sample One" },
  { venue: "CBGB", phrase: "Sample Two" },
  { venue: "100 Club", phrase: "Sample Three" },
  { venue: "Whisky a Go Go", phrase: "Sample Four" }
];

const state = slots.map(() => null);
const slotsEl = document.querySelector("#slots");
const template = document.querySelector("#slotTemplate");
const connectButton = document.querySelector("#connect");
const uploadButton = document.querySelector("#upload");
const restoreButton = document.querySelector("#restore");
const downloadButton = document.querySelector("#download");
const buildSelect = document.querySelector("#buildSelect");
const bankLimitEl = document.querySelector("#bankLimit");
const payloadSizeEl = document.querySelector("#payloadSize");
const durationEl = document.querySelector("#duration");
const remainingEl = document.querySelector("#remaining");
const logEl = document.querySelector("#log");
const firmwareNoticeEl = document.querySelector("#firmwareNotice");

let audioContext;
let port;
let serialText = "";
let uploadInProgress = false;
let loaderGreetingSeen = false;
let firmwareCheckTimer;
const serialWaiters = [];

function log(message) {
  serialText = message;
  logEl.textContent = serialText;
}

function appendLog(message) {
  serialText = `${serialText}${serialText ? "\n" : ""}${message}`.slice(-3000);
  logEl.textContent = serialText;
  notifySerialWaiters(serialText);
}

function setFirmwareNotice(message, kind = "") {
  firmwareNoticeEl.textContent = message;
  firmwareNoticeEl.className = `notice ${kind}`.trim();
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function getAudioContext() {
  audioContext ||= new AudioContext();
  return audioContext;
}

async function decodeAudio(file) {
  return getAudioContext().decodeAudioData(await file.arrayBuffer());
}

async function resampleMono(buffer) {
  const frameCount = Math.max(1, Math.ceil(buffer.duration * TARGET_SAMPLE_RATE));
  const offline = new OfflineAudioContext(1, frameCount, TARGET_SAMPLE_RATE);
  const source = offline.createBufferSource();
  const gain = offline.createGain();
  source.buffer = buffer;
  gain.gain.value = 1 / Math.max(1, buffer.numberOfChannels);
  source.connect(gain);
  gain.connect(offline.destination);
  source.start();
  return (await offline.startRendering()).getChannelData(0);
}

function linearToMuLaw(sample) {
  const clipped = Math.max(-1, Math.min(1, sample));
  const sign = clipped < 0 ? 0x80 : 0;
  let magnitude = Math.round(Math.abs(clipped) * 32767);
  magnitude = Math.min(32635, magnitude + 0x84);

  let exponent = 7;
  for (let mask = 0x4000; exponent > 0 && (magnitude & mask) === 0; exponent--, mask >>= 1) {}

  const mantissa = (magnitude >> (exponent + 3)) & 0x0f;
  return ~(sign | (exponent << 4) | mantissa) & 0xff;
}

function normalise(samples) {
  let peak = 0;
  for (const sample of samples) peak = Math.max(peak, Math.abs(sample));
  const gain = peak > 0 ? (10 ** (-6 / 20)) / peak : 1;
  const encoded = new Uint8Array(samples.length);
  for (let i = 0; i < samples.length; i++) encoded[i] = linearToMuLaw(samples[i] * gain);
  return { encoded, gain, peak };
}

function writeU32(view, offset, value) {
  view.setUint32(offset, value >>> 0, true);
}

function checksum(bytes) {
  let sum = 2166136261;
  for (const byte of bytes) {
    sum ^= byte;
    sum = Math.imul(sum, 16777619) >>> 0;
  }
  return sum >>> 0;
}

function selectedTarget() {
  return targets[buildSelect.value] || targets["2mb"];
}

function buildBank() {
  if (state.some((item) => !item)) return null;

  const payloadBytes = state.reduce((sum, item) => sum + item.encoded.length, 0);
  const totalBytes = HEADER_BYTES + payloadBytes;
  const target = selectedTarget();
  if (totalBytes > target.bankBytes) {
    throw new Error(`These sounds need more room than the ${target.label} can use here. Shorten the audio or choose the 16 MB build.`);
  }

  const bank = new Uint8Array(totalBytes);
  const view = new DataView(bank.buffer);
  writeU32(view, 0, BANK_MAGIC);
  writeU32(view, 4, BANK_VERSION);
  writeU32(view, 8, FORMAT_MULAW_8);
  writeU32(view, 12, TARGET_SAMPLE_RATE);
  writeU32(view, 16, SAMPLE_COUNT);
  writeU32(view, 20, payloadBytes);
  writeU32(view, 24, checksum(new Uint8Array(bank.buffer, HEADER_BYTES, payloadBytes)));
  writeU32(view, 28, 0);

  let payloadOffset = 0;
  for (let i = 0; i < SAMPLE_COUNT; i++) {
    writeU32(view, 32 + i * 8, payloadOffset);
    writeU32(view, 36 + i * 8, state[i].encoded.length);
    bank.set(state[i].encoded, HEADER_BYTES + payloadOffset);
    payloadOffset += state[i].encoded.length;
  }

  writeU32(view, 24, checksum(bank.slice(HEADER_BYTES)));
  return bank;
}

function updateSampleStatus(ready, totalBytes, target) {
  if (port || uploadInProgress) return;

  if (ready.length === 0) {
    log("Waiting for samples. Add four short sounds, one for each slot.");
  } else if (totalBytes > target.bankBytes) {
    log(`Your sounds are too large for the ${target.label}. Shorten them or choose the 16 MB build.`);
  } else if (ready.length < SAMPLE_COUNT) {
    log(`${ready.length} of ${SAMPLE_COUNT} samples ready. Add ${SAMPLE_COUNT - ready.length} more.`);
  } else {
    log("All four samples are ready. Connect the card when LEDs 1, 3, and 5 are lit.");
  }
}

function updateSummary() {
  const ready = state.filter(Boolean);
  const payloadBytes = ready.reduce((sum, item) => sum + item.encoded.length, 0);
  const totalBytes = HEADER_BYTES + payloadBytes;
  const duration = ready.reduce((sum, item) => sum + item.encoded.length / TARGET_SAMPLE_RATE, 0);
  const target = selectedTarget();
  const remaining = Math.max(0, target.bankBytes - totalBytes);
  bankLimitEl.textContent = formatBytes(target.bankBytes);
  payloadSizeEl.textContent = formatBytes(payloadBytes);
  durationEl.textContent = `${duration.toFixed(3)} s`;
  remainingEl.textContent = formatBytes(remaining);
  uploadButton.disabled = ready.length !== SAMPLE_COUNT || !port || totalBytes > target.bankBytes;
  restoreButton.disabled = !port;
  downloadButton.disabled = ready.length !== SAMPLE_COUNT || totalBytes > target.bankBytes;
  if (uploadInProgress) {
    uploadButton.disabled = true;
    restoreButton.disabled = true;
  }
  updateSampleStatus(ready, totalBytes, target);
}

function notifySerialWaiters(text) {
  for (let i = serialWaiters.length - 1; i >= 0; i--) {
    const waiter = serialWaiters[i];
    const match = text.slice(waiter.from).match(waiter.pattern);
    if (match) {
      clearTimeout(waiter.timer);
      serialWaiters.splice(i, 1);
      waiter.resolve({ text, match });
    }
  }
}

function waitForSerial(pattern, timeoutMs = 15000) {
  return new Promise((resolve, reject) => {
    const waiter = {
      pattern,
      from: serialText.length,
      resolve,
      timer: setTimeout(() => {
        const index = serialWaiters.indexOf(waiter);
        if (index >= 0) serialWaiters.splice(index, 1);
        reject(new Error("The card did not answer in time. Check that LEDs 1, 3, and 5 are lit. If they are not, flash the latest Punk Confusion WebSerial UF2 and enter the loader again."));
      }, timeoutMs)
    };
    serialWaiters.push(waiter);
  });
}

async function writeToCard(bytes) {
  const writer = port.writable.getWriter();
  try {
    await writer.write(bytes);
  } finally {
    writer.releaseLock();
  }
}

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = filename;
  document.body.append(link);
  link.click();
  link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

async function handleFile(index, file, node) {
  const info = node.querySelector(".info");
  const audio = node.querySelector("audio");
  info.textContent = "Decoding...";
  if (!port) log(`Preparing ${slots[index].phrase}...`);

  const decoded = await decodeAudio(file);
  const mono = await resampleMono(decoded);
  const { encoded, gain, peak } = normalise(mono);
  state[index] = { encoded, sourceName: file.name };

  const preview = new Blob([await file.arrayBuffer()], { type: file.type || "audio/*" });
  audio.src = URL.createObjectURL(preview);
  info.innerHTML = `${file.name}<br>${(encoded.length / TARGET_SAMPLE_RATE).toFixed(3)} s, ${formatBytes(encoded.length)} µ-law<br>source peak ${(peak * 100).toFixed(1)}%, gain ${gain.toFixed(2)}x`;
  updateSummary();
}

function renderSlot(index) {
  const node = template.content.firstElementChild.cloneNode(true);
  const input = node.querySelector("input");
  const drop = node.querySelector(".drop");
  node.querySelector("h2").textContent = slots[index].phrase;
  node.querySelector(".venue").textContent = slots[index].venue;

  input.addEventListener("change", () => {
    if (input.files?.[0]) handleFile(index, input.files[0], node);
  });

  ["dragenter", "dragover"].forEach((name) => {
    drop.addEventListener(name, (event) => {
      event.preventDefault();
      drop.classList.add("dragover");
    });
  });

  ["dragleave", "drop"].forEach((name) => {
    drop.addEventListener(name, (event) => {
      event.preventDefault();
      drop.classList.remove("dragover");
    });
  });

  drop.addEventListener("drop", (event) => {
    const file = event.dataTransfer?.files?.[0];
    if (file) handleFile(index, file, node);
  });

  return node;
}

function matchTargetFromBankSize(bankBytes) {
  for (const [key, target] of Object.entries(targets)) {
    if (target.bankBytes === bankBytes) return key;
  }
  return null;
}

connectButton.addEventListener("click", async () => {
  if (!("serial" in navigator)) {
    log("This browser cannot talk to the card. Use Chrome or Edge.");
    return;
  }

  port = await navigator.serial.requestPort();
  await port.open({ baudRate: 115200 });
  loaderGreetingSeen = false;
  clearTimeout(firmwareCheckTimer);
  log("Connected. Checking the card firmware...");
  setFirmwareNotice("Connected. Waiting for the Punk Confusion sample loader to identify itself...", "warn");
  readSerial(port);
  firmwareCheckTimer = setTimeout(() => {
    if (!loaderGreetingSeen) {
      setFirmwareNotice(
        "The card connected, but this page has not seen the Punk Confusion WebSerial loader. Flash the latest WebSerial UF2, then hold the switch down while resetting so LEDs 1, 3, and 5 stay lit.",
        "warn"
      );
      appendLog("Firmware check: loader greeting not seen. If uploads do not start, update the card with the latest Punk Confusion WebSerial UF2.");
    }
  }, 4500);
  updateSummary();
});

buildSelect.addEventListener("change", updateSummary);

async function readSerial(serialPort) {
  const decoder = new TextDecoder();
  while (serialPort.readable) {
    const reader = serialPort.readable.getReader();
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) {
          const text = decoder.decode(value, { stream: true }).trimEnd();
          appendLog(text);
          if (text.includes("PUNKCONF LOADER READY")) {
            loaderGreetingSeen = true;
            clearTimeout(firmwareCheckTimer);
            setFirmwareNotice("Punk Confusion WebSerial loader detected. You can send sounds when all four slots are ready.", "ok");
          }
          const match = text.match(/SAMPLE_BANK_BYTES\s+(\d+)/);
          if (match) {
            const targetKey = matchTargetFromBankSize(Number(match[1]));
            if (targetKey) {
              buildSelect.value = targetKey;
              updateSummary();
            }
          }
        }
      }
    } catch (error) {
      appendLog(error.message || String(error));
    } finally {
      reader.releaseLock();
    }
  }
}

uploadButton.addEventListener("click", async () => {
  if (uploadInProgress) return;
  uploadInProgress = true;
  updateSummary();
  try {
    const bank = buildBank();
    const command = new Uint8Array(8);
    const view = new DataView(command.buffer);
    writeU32(view, 0, LOADER_MAGIC);
    writeU32(view, 4, bank.length);

    appendLog(`Starting upload of ${formatBytes(bank.length)}. Keep the card connected.`);
    if (!loaderGreetingSeen) {
      appendLog("I have not seen the WebSerial loader greeting yet. If this does not continue, flash the latest Punk Confusion WebSerial UF2 and enter the loader again.");
    }
    const ready = waitForSerial(/OK SEND\s+\d+|ERR\s+\w+/, 10000);
    await writeToCard(command);
    const readyReply = await ready;
    if (/ERR/.test(readyReply.match[0])) throw new Error(readyReply.match[0]);

    appendLog("Card is ready. Sending sounds now...");
    const done = waitForSerial(/OK DONE|ERR\s+\w+/, 30000);
    await writeToCard(bank);
    const doneReply = await done;
    if (/ERR/.test(doneReply.match[0])) throw new Error(doneReply.match[0]);

    appendLog("Upload complete. Restart the card now to use the new shouts.");
  } catch (error) {
    appendLog(error.message || String(error));
  } finally {
    uploadInProgress = false;
    updateSummary();
  }
});

restoreButton.addEventListener("click", async () => {
  if (uploadInProgress) return;
  try {
    const packet = new Uint8Array(4);
    writeU32(new DataView(packet.buffer), 0, RESTORE_MAGIC);
    await writeToCard(packet);
    appendLog("Built-in sounds command sent. Wait for OK FACTORY_DONE, then restart the card.");
  } catch (error) {
    appendLog(error.message || String(error));
  }
});

downloadButton.addEventListener("click", () => {
  const bank = buildBank();
  downloadBlob(new Blob([bank], { type: "application/octet-stream" }), "punk_confusion_samples.pbank");
});

slots.forEach((_, index) => slotsEl.append(renderSlot(index)));
updateSummary();
