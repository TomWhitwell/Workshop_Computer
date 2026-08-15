const TARGET_SAMPLE_RATE = 24000;
const TARGET_PEAK = 10 ** (-6 / 20);
const INT16_MAX = 32767;
const TWO_MB_CARD_HEADER_LIMIT = 5 * 1024 * 1024;
const SIXTEEN_MB_CARD_HEADER_LIMIT = 45 * 1024 * 1024;

const sampleSlots = [
  {
    venue: "Marquee",
    phrase: "Oi",
    filename: "marquee_oi.wav",
    symbol: "kVocalMarqueeOi",
    label: "Marquee: Oi"
  },
  {
    venue: "CBGB",
    phrase: "Hey Ho",
    filename: "cbgb_hey_ho.wav",
    symbol: "kVocalCbgbHeyHo",
    label: "CBGB: Hey Ho"
  },
  {
    venue: "100 Club",
    phrase: "No Future",
    filename: "club100_no_future.wav",
    symbol: "kVocalClub100NoFuture",
    label: "100 Club: No Future"
  },
  {
    venue: "Whisky a Go Go",
    phrase: "Let's Go",
    filename: "whisky_lets_go.wav",
    symbol: "kVocalWhiskyLetsGo",
    label: "Whisky a Go Go: Let's Go"
  }
];

const state = sampleSlots.map(() => null);
const slotsEl = document.querySelector("#slots");
const template = document.querySelector("#slotTemplate");
const downloadHeaderButton = document.querySelector("#downloadHeader");
const downloadWavsButton = document.querySelector("#downloadWavs");
const buildUf2Button = document.querySelector("#buildUf2");
const clearAllButton = document.querySelector("#clearAll");
const totalDurationEl = document.querySelector("#totalDuration");
const headerSizeEl = document.querySelector("#headerSize");
const sizeWarningEl = document.querySelector("#sizeWarning");
const sizeWarningTitleEl = document.querySelector("#sizeWarningTitle");
const sizeWarningTextEl = document.querySelector("#sizeWarningText");
const builderMessageEl = document.querySelector("#builderMessage");

let audioContext;

function getAudioContext() {
  audioContext ||= new AudioContext();
  return audioContext;
}

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
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

function arrayBufferToBase64(buffer) {
  const bytes = new Uint8Array(buffer);
  let binary = "";
  for (let offset = 0; offset < bytes.length; offset += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
  }
  return btoa(binary);
}

async function decodeAudioFile(file) {
  const arrayBuffer = await file.arrayBuffer();
  return getAudioContext().decodeAudioData(arrayBuffer);
}

async function resampleToMono(audioBuffer) {
  const frameCount = Math.max(1, Math.ceil(audioBuffer.duration * TARGET_SAMPLE_RATE));
  const offline = new OfflineAudioContext(1, frameCount, TARGET_SAMPLE_RATE);
  const source = offline.createBufferSource();
  source.buffer = audioBuffer;

  const merger = offline.createGain();
  merger.gain.value = 1 / Math.max(1, audioBuffer.numberOfChannels);
  source.connect(merger);
  merger.connect(offline.destination);
  source.start();

  const rendered = await offline.startRendering();
  return rendered.getChannelData(0);
}

function normaliseAndConvert(floatSamples) {
  let peak = 0;
  for (const sample of floatSamples) {
    const abs = Math.abs(sample);
    if (abs > peak) peak = abs;
  }

  const gain = peak > 0 ? TARGET_PEAK / peak : 1;
  const intSamples = new Int16Array(floatSamples.length);

  for (let index = 0; index < floatSamples.length; index++) {
    const clamped = Math.max(-1, Math.min(1, floatSamples[index] * gain));
    intSamples[index] = Math.round(clamped * INT16_MAX);
  }

  return { intSamples, sourcePeak: peak, gain };
}

function writeAscii(view, offset, text) {
  for (let index = 0; index < text.length; index++) {
    view.setUint8(offset + index, text.charCodeAt(index));
  }
}

function encodeWav(intSamples) {
  const byteLength = 44 + intSamples.length * 2;
  const buffer = new ArrayBuffer(byteLength);
  const view = new DataView(buffer);

  writeAscii(view, 0, "RIFF");
  view.setUint32(4, byteLength - 8, true);
  writeAscii(view, 8, "WAVE");
  writeAscii(view, 12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, TARGET_SAMPLE_RATE, true);
  view.setUint32(28, TARGET_SAMPLE_RATE * 2, true);
  view.setUint16(32, 2, true);
  view.setUint16(34, 16, true);
  writeAscii(view, 36, "data");
  view.setUint32(40, intSamples.length * 2, true);

  let offset = 44;
  for (const sample of intSamples) {
    view.setInt16(offset, sample, true);
    offset += 2;
  }

  return new Blob([buffer], { type: "audio/wav" });
}

function emitArray(slot, intSamples) {
  const seconds = intSamples.length / TARGET_SAMPLE_RATE;
  const lines = [
    `// ${slot.label}, ${seconds.toFixed(3)} seconds.`,
    `constexpr int16_t ${slot.symbol}[] = {`
  ];

  for (let offset = 0; offset < intSamples.length; offset += 12) {
    const chunk = Array.from(intSamples.slice(offset, offset + 12)).join(", ");
    lines.push(`    ${chunk},`);
  }

  lines.push("};", "");
  return lines.join("\n");
}

function generateHeader() {
  const body = [
    "#ifndef PUNK_CONFUSION_VOCAL_SAMPLES_H",
    "#define PUNK_CONFUSION_VOCAL_SAMPLES_H",
    "",
    "#include <cstdint>",
    "",
    "// Generated with web/index.html.",
    "// Format: 24 kHz mono signed 16-bit PCM, peak-normalised to about -6 dBFS.",
    ""
  ];

  sampleSlots.forEach((slot, index) => {
    body.push(emitArray(slot, state[index].intSamples));
  });

  body.push("#endif // PUNK_CONFUSION_VOCAL_SAMPLES_H", "");
  return body.join("\n");
}

function updateSummary() {
  const ready = state.filter(Boolean);
  const duration = ready.reduce((sum, item) => sum + item.intSamples.length / TARGET_SAMPLE_RATE, 0);
  const header = ready.length === sampleSlots.length ? generateHeader() : "";
  const headerBytes = header ? new TextEncoder().encode(header).length : 0;
  totalDurationEl.textContent = `${duration.toFixed(3)} s`;
  headerSizeEl.textContent = header ? formatBytes(headerBytes) : "0 KB";
  updateSizeWarning(ready.length, headerBytes);
  buildUf2Button.disabled = ready.length !== sampleSlots.length;
  downloadHeaderButton.disabled = ready.length !== sampleSlots.length;
  downloadWavsButton.disabled = ready.length !== sampleSlots.length;
}

function updateSizeWarning(readyCount, headerBytes) {
  sizeWarningEl.classList.remove("ok", "warning", "danger");

  if (readyCount !== sampleSlots.length) {
    sizeWarningTitleEl.textContent = "Waiting for samples";
    sizeWarningTextEl.textContent = "Add all four shouts to estimate whether the build should fit.";
    return;
  }

  if (headerBytes > SIXTEEN_MB_CARD_HEADER_LIMIT) {
    sizeWarningEl.classList.add("danger");
    sizeWarningTitleEl.textContent = "Too large";
    sizeWarningTextEl.textContent = "This is over the suggested 16 MB-card limit. Shorten the samples before building.";
    return;
  }

  if (headerBytes > TWO_MB_CARD_HEADER_LIMIT) {
    sizeWarningEl.classList.add("warning");
    sizeWarningTitleEl.textContent = "Large for 2 MB cards";
    sizeWarningTextEl.textContent = "This may be too large for a standard 2 MB card. Use shorter samples, or build for a 16 MB card.";
    return;
  }

  sizeWarningEl.classList.add("ok");
  sizeWarningTitleEl.textContent = "Good for 2 MB cards";
  sizeWarningTextEl.textContent = "This is under the suggested 2 MB-card header-size limit.";
}

function renderSlot(index) {
  const slot = sampleSlots[index];
  const node = template.content.firstElementChild.cloneNode(true);
  const input = node.querySelector("input");
  const dropZone = node.querySelector(".drop-zone");
  const status = node.querySelector(".status");
  const info = node.querySelector(".sample-info");
  const audio = node.querySelector("audio");
  const downloadWavButton = node.querySelector(".download-wav");

  node.querySelector(".venue").textContent = slot.venue;
  node.querySelector("h2").textContent = slot.phrase;

  async function handleFile(file) {
    status.textContent = "working";
    status.classList.remove("ready");
    info.textContent = "Decoding and converting...";
    downloadWavButton.disabled = true;

    try {
      const decoded = await decodeAudioFile(file);
      const mono = await resampleToMono(decoded);
      const { intSamples, sourcePeak, gain } = normaliseAndConvert(mono);
      const wavBlob = encodeWav(intSamples);
      const objectUrl = URL.createObjectURL(wavBlob);

      if (state[index]?.objectUrl) URL.revokeObjectURL(state[index].objectUrl);
      state[index] = {
        intSamples,
        wavBlob,
        objectUrl,
        sourceName: file.name,
        sourcePeak,
        gain
      };

      audio.src = objectUrl;
      status.textContent = "ready";
      status.classList.add("ready");
      info.innerHTML = [
        `<strong>${file.name}</strong>`,
        `${(intSamples.length / TARGET_SAMPLE_RATE).toFixed(3)} s at 24 kHz`,
        `source peak ${(sourcePeak * 100).toFixed(1)}%, gain ${gain.toFixed(2)}x`
      ].join("<br>");
      downloadWavButton.disabled = false;
    } catch (error) {
      console.error(error);
      status.textContent = "error";
      status.classList.remove("ready");
      info.textContent = error.message || "Could not decode this file.";
      state[index] = null;
      audio.removeAttribute("src");
    }

    updateSummary();
  }

  input.addEventListener("change", () => {
    const file = input.files?.[0];
    if (file) handleFile(file);
  });

  ["dragenter", "dragover"].forEach((eventName) => {
    dropZone.addEventListener(eventName, (event) => {
      event.preventDefault();
      dropZone.classList.add("dragover");
    });
  });

  ["dragleave", "drop"].forEach((eventName) => {
    dropZone.addEventListener(eventName, (event) => {
      event.preventDefault();
      dropZone.classList.remove("dragover");
    });
  });

  dropZone.addEventListener("drop", (event) => {
    const file = event.dataTransfer?.files?.[0];
    if (file) handleFile(file);
  });

  downloadWavButton.addEventListener("click", () => {
    const item = state[index];
    if (item) downloadBlob(item.wavBlob, slot.filename);
  });

  return node;
}

sampleSlots.forEach((_, index) => {
  slotsEl.append(renderSlot(index));
});

downloadHeaderButton.addEventListener("click", () => {
  const header = generateHeader();
  downloadBlob(new Blob([header], { type: "text/x-c++hdr" }), "VocalSamples.h");
});

downloadWavsButton.addEventListener("click", () => {
  state.forEach((item, index) => {
    if (!item) return;
    setTimeout(() => {
      downloadBlob(item.wavBlob, sampleSlots[index].filename);
    }, index * 180);
  });
});

buildUf2Button.addEventListener("click", async () => {
  const ready = state.filter(Boolean);
  if (ready.length !== sampleSlots.length) return;

  buildUf2Button.disabled = true;
  builderMessageEl.textContent = "Building UF2 locally. This can take a minute or two...";

  try {
    const samples = await Promise.all(
      sampleSlots.map(async (slot, index) => ({
        filename: slot.filename,
        data: arrayBufferToBase64(await state[index].wavBlob.arrayBuffer())
      }))
    );

    const response = await fetch("/api/build", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ samples })
    });

    if (!response.ok) {
      const error = await response.json().catch(() => ({}));
      throw new Error(error.error || `Build failed with HTTP ${response.status}`);
    }

    const uf2 = await response.blob();
    downloadBlob(uf2, "punk_confusion_custom.uf2");
    builderMessageEl.textContent = "Built punk_confusion_custom.uf2. Flash it to test your shouts.";
  } catch (error) {
    console.error(error);
    builderMessageEl.textContent = error.message || "Build failed.";
  } finally {
    buildUf2Button.disabled = state.filter(Boolean).length !== sampleSlots.length;
  }
});

clearAllButton.addEventListener("click", () => {
  state.forEach((item) => {
    if (item?.objectUrl) URL.revokeObjectURL(item.objectUrl);
  });
  window.location.reload();
});

updateSummary();
