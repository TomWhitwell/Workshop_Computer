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
const slotNodes = [];
const slotsEl = document.querySelector("#slots");
const template = document.querySelector("#slotTemplate");
const bankFileInput = document.querySelector("#bankFile");
const bankDrop = document.querySelector("#bankDrop");
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

function muLawToLinear(byte) {
  const decoded = (~byte) & 0xff;
  const sign = decoded & 0x80;
  const exponent = (decoded >> 4) & 0x07;
  const mantissa = decoded & 0x0f;
  const magnitude = (((mantissa << 3) + 0x84) << exponent) - 0x84;
  return (sign ? -magnitude : magnitude) / 32768;
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

function readU32(view, offset) {
  return view.getUint32(offset, true);
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

function wavPreviewFromMuLaw(encoded) {
  const dataBytes = encoded.length * 2;
  const wav = new ArrayBuffer(44 + dataBytes);
  const view = new DataView(wav);
  const writeText = (offset, text) => {
    for (let i = 0; i < text.length; i++) view.setUint8(offset + i, text.charCodeAt(i));
  };

  writeText(0, "RIFF");
  writeU32(view, 4, 36 + dataBytes);
  writeText(8, "WAVE");
  writeText(12, "fmt ");
  writeU32(view, 16, 16);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  writeU32(view, 24, TARGET_SAMPLE_RATE);
  writeU32(view, 28, TARGET_SAMPLE_RATE * 2);
  view.setUint16(32, 2, true);
  view.setUint16(34, 16, true);
  writeText(36, "data");
  writeU32(view, 40, dataBytes);

  for (let i = 0; i < encoded.length; i++) {
    const sample = Math.max(-1, Math.min(1, muLawToLinear(encoded[i])));
    view.setInt16(44 + i * 2, Math.round(sample * 32767), true);
  }

  return new Blob([wav], { type: "audio/wav" });
}

function setSlotFromEncoded(index, encoded, sourceName) {
  state[index] = { encoded, sourceName };
  const node = slotNodes[index];
  if (!node) return;

  const info = node.querySelector(".info");
  const audio = node.querySelector("audio");
  audio.src = URL.createObjectURL(wavPreviewFromMuLaw(encoded));
  info.innerHTML = `${sourceName}<br>${(encoded.length / TARGET_SAMPLE_RATE).toFixed(3)} s, ${formatBytes(encoded.length)} µ-law`;
}

function parseBank(bytes) {
  if (bytes.length < HEADER_BYTES) throw new Error("That prepared sounds file is too small to be a Punk Confusion bank.");

  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (readU32(view, 0) !== BANK_MAGIC) throw new Error("That is not a Punk Confusion prepared sounds file.");
  if (readU32(view, 4) !== BANK_VERSION) throw new Error("That prepared sounds file uses an unsupported version.");
  if (readU32(view, 8) !== FORMAT_MULAW_8) throw new Error("That prepared sounds file uses an unsupported audio format.");
  if (readU32(view, 12) !== TARGET_SAMPLE_RATE) throw new Error("That prepared sounds file uses the wrong sample rate.");
  if (readU32(view, 16) !== SAMPLE_COUNT) throw new Error("That prepared sounds file does not contain four sounds.");

  const payloadBytes = readU32(view, 20);
  const expectedChecksum = readU32(view, 24);
  if (HEADER_BYTES + payloadBytes !== bytes.length) throw new Error("That prepared sounds file appears to be incomplete.");
  if (checksum(bytes.slice(HEADER_BYTES)) !== expectedChecksum) throw new Error("That prepared sounds file did not pass its safety check.");

  return slots.map((slot, index) => {
    const offset = readU32(view, 32 + index * 8);
    const length = readU32(view, 36 + index * 8);
    if (offset + length > payloadBytes) throw new Error("That prepared sounds file has a damaged slot table.");
    return {
      encoded: bytes.slice(HEADER_BYTES + offset, HEADER_BYTES + offset + length),
      sourceName: `${slot.phrase} from prepared sounds`
    };
  });
}

async function handleBankFile(file) {
  try {
    log(`Loading prepared sounds from ${file.name}...`);
    const bytes = new Uint8Array(await file.arrayBuffer());
    const samples = parseBank(bytes);
    samples.forEach((sample, index) => setSlotFromEncoded(index, sample.encoded, sample.sourceName));
    updateSummary();
    log(`Prepared sounds loaded from ${file.name}. All four slots are ready.`);
  } catch (error) {
    log(error.message || String(error));
  }
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
  setSlotFromEncoded(index, encoded, file.name);

  const preview = new Blob([await file.arrayBuffer()], { type: file.type || "audio/*" });
  audio.src = URL.createObjectURL(preview);
  info.innerHTML = `${file.name}<br>${(encoded.length / TARGET_SAMPLE_RATE).toFixed(3)} s, ${formatBytes(encoded.length)} µ-law<br>source peak ${(peak * 100).toFixed(1)}%, gain ${gain.toFixed(2)}x`;
  updateSummary();
}

function addDropHandlers(drop, onFile) {
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
    if (file) onFile(file);
  });
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

  addDropHandlers(drop, (file) => handleFile(index, file, node));

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

    appendLog(`Stage 1 of 4: asking the card if it is ready for ${formatBytes(bank.length)}.`);
    if (!loaderGreetingSeen) {
      appendLog("I have not seen the WebSerial loader greeting yet. If this does not continue, flash the latest Punk Confusion WebSerial UF2 and enter the loader again.");
    }
    const ready = waitForSerial(/OK SEND\s+\d+|ERR\s+\w+/, 10000);
    await writeToCard(command);
    const readyReply = await ready;
    if (/ERR/.test(readyReply.match[0])) throw new Error(readyReply.match[0]);

    appendLog("Stage 2 of 4: card is ready. Sending sounds now...");
    const done = waitForSerial(/OK DONE|ERR\s+\w+/, 30000);
    await writeToCard(bank);
    appendLog("Stage 3 of 4: sounds sent. Waiting for the card to finish saving them...");
    const doneReply = await done;
    if (/ERR/.test(doneReply.match[0])) throw new Error(doneReply.match[0]);

    appendLog("Stage 4 of 4: upload complete. Restart the card now to use the new shouts.");
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

bankFileInput.addEventListener("change", () => {
  if (bankFileInput.files?.[0]) handleBankFile(bankFileInput.files[0]);
});
addDropHandlers(bankDrop, handleBankFile);

slots.forEach((_, index) => {
  const node = renderSlot(index);
  slotNodes[index] = node;
  slotsEl.append(node);
});
updateSummary();
