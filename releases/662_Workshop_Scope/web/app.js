const NUM_CH = 4;
const SAMPLE_RATE = 48000;
const BLOCK_SAMPLES = 255;
const FRAME_CH = 3; // ADC0, ADC1, then whichever CV the block's tag selects
const HEADER = 4;
const FRAME_BYTES = HEADER + BLOCK_SAMPLES * 2;
const RING = 8192; // frames per channel kept for display
const FFT_N = 4096;

// Roll view keeps min/max per 5 ms bucket for up to 120 s so slow envelopes fit on screen.
const BUCKET = 240; // frames per bucket
const HIST_RATE = SAMPLE_RATE / BUCKET; // buckets per second
const HIST = HIST_RATE * 120;
const histMin = Array.from({ length: NUM_CH }, () => new Float32Array(HIST));
const histMax = Array.from({ length: NUM_CH }, () => new Float32Array(HIST));
const accMin = new Float32Array(NUM_CH).fill(Infinity);
const accMax = new Float32Array(NUM_CH).fill(-Infinity);
let accCount = 0;
let histHead = 0;

const ring = Array.from({ length: NUM_CH }, () => new Float32Array(RING));
let head = 0; // next write index
let totalFrames = 0;
let lastGapEnd = 0; // totalFrames at the latest dropped data, which would splatter the spectrum
let lastSeq = -1;
let dropped = 0;

const $ = (id) => document.getElementById(id);
const cv = $("cv");
const ctx = cv.getContext("2d");
const colors = ["#4fd1ff", "#ffb84f", "#7dff7d", "#ff6fb5"];
const names = ["Audio R", "Audio L", "CV 1", "CV 2"];
const kinds = Array(NUM_CH).fill("idle");
const lastVals = new Float32Array(NUM_CH);
const TAG_CV2 = 1;

const hidden = (c) => !$(`show${c}`).checked;

for (let c = 0; c < NUM_CH; c++) {
  const l = document.createElement("label");
  l.innerHTML = `<input type="checkbox" checked id="show${c}"><span id="lbl${c}" style="color:${colors[c]}">${names[c]}</span>`;
  $("shows").appendChild(l);
}

// Classify by fast-change energy: audio-rate content vs slow CV vs flat.
function classify() {
  const n = 2048;
  for (let c = 0; c < NUM_CH; c++) {
    const d = recent(c, n);
    let lo = Infinity, hi = -Infinity, diff = 0;
    for (let i = 0; i < n; i++) {
      lo = Math.min(lo, d[i]); hi = Math.max(hi, d[i]);
      if (i) diff += Math.abs(d[i] - d[i - 1]);
    }
    const pp = hi - lo;
    kinds[c] = pp < 0.02 ? "idle" : diff / n / pp > 0.003 ? "audio" : "CV";
    $(`lbl${c}`).textContent = `${names[c]} (${kinds[c]})`;
  }
}
setInterval(classify, 500);

// Full scale (+/-1) is +/-6 V. The CV not selected in a block, and the first frames after a mux switch, hold their last value.
function pushSamples(u16, tag) {
  const invert = $("invert").checked ? -1 : 1;
  const cvIdx = tag & TAG_CV2 ? 1 : 0;
  const frames = u16.length / FRAME_CH;
  const settleFrames = 2;

  for (let f = 0; f < frames; f++) {
    const i = f * FRAME_CH;
    lastVals[0] = invert * (u16[i] - 2048) / 2048;
    lastVals[1] = invert * (u16[i + 1] - 2048) / 2048;
    if (f >= settleFrames) lastVals[2 + cvIdx] = invert * (u16[i + 2] - 2048) / 2048;
    for (let c = 0; c < NUM_CH; c++) {
      const v = lastVals[c];
      ring[c][head] = v;
      if (v < accMin[c]) accMin[c] = v;
      if (v > accMax[c]) accMax[c] = v;
    }
    head = (head + 1) % RING;

    if (++accCount === BUCKET) {
      for (let c = 0; c < NUM_CH; c++) {
        histMin[c][histHead] = accMin[c];
        histMax[c][histHead] = accMax[c];
        accMin[c] = Infinity;
        accMax[c] = -Infinity;
      }
      histHead = (histHead + 1) % HIST;
      accCount = 0;
    }
  }
  totalFrames += frames;
}

// P/p switches the card's pass-through on/off.
let writer = null;
function sendSettings() {
  if (writer) writer.write(new Uint8Array([$("passThru").checked ? 0x50 : 0x70]));
}
$("passThru").onchange = sendSettings;

async function connect() {
  const port = await navigator.serial.requestPort();
  await port.open({ baudRate: 115200 });
  $("status").textContent = "Connected";
  writer = port.writable.getWriter();
  sendSettings();
  const reader = port.readable.getReader();
  let buf = new Uint8Array(0);

  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    const joined = new Uint8Array(buf.length + value.length);
    joined.set(buf);
    joined.set(value, buf.length);
    buf = joined;

    while (buf.length >= FRAME_BYTES) {
      if (buf[0] !== 0xa5 || buf[1] !== 0x5a) {
        const next = buf.indexOf(0xa5, 1);
        buf = next < 0 ? new Uint8Array(0) : buf.slice(next);
        continue;
      }
      const seq = buf[2];
      if (lastSeq >= 0 && seq !== ((lastSeq + 1) & 0xff)) {
        dropped++;
        lastGapEnd = totalFrames;
      }
      lastSeq = seq;
      const payload = buf.slice(HEADER, FRAME_BYTES);
      pushSamples(new Uint16Array(payload.buffer), buf[3]);
      buf = buf.slice(FRAME_BYTES);
    }
    $("status").textContent = `Connected, dropped blocks: ${dropped}`;
  }
  writer = null;
  $("status").textContent = "Disconnected";
}

function recent(ch, n) {
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = ring[ch][(head - n + i + RING * 2) % RING];
  return out;
}

function findTrigger(data, level, rising) {
  for (let i = 1; i < data.length; i++) {
    const crossed = rising ? data[i - 1] < level && data[i] >= level : data[i - 1] > level && data[i] <= level;
    if (crossed) return i;
  }
  return 0;
}

const FMIN = 20, FMAX = 24000; // log frequency axis for the spectrum view
const fftX = (f, w) => (Math.log(f / FMIN) / Math.log(FMAX / FMIN)) * w;
const fmtTime = (s) => (s >= 1 ? `${+s.toFixed(2)} s` : `${+(s * 1000).toFixed(2)} ms`);
const fmtHz = (f) => (f >= 1000 ? `${f / 1000}k` : `${f}`);
const VOLTS = [-6, -4, -2, 0, 2, 4, 6];

function graticule(view, w, h, dpr) {
  const gy = (v) => h / 2 - (v / 6) * (h / 2) * 0.9;
  const xyS = (Math.min(w, h) / 2) * 0.9;
  const vlines = [], hlines = [], labels = [];
  const volt = (v) => `${v > 0 ? "+" : ""}${v} V`;

  if (view === "scope" || view === "roll") {
    const span = +(view === "scope" ? $("timeDiv") : $("rollSpan")).value;
    for (let i = 1; i < 10; i++) vlines.push((w * i) / 10);
    for (const v of VOLTS) {
      hlines.push([0, w, gy(v)]);
      labels.push([volt(v), 4 * dpr, gy(v) - 3 * dpr, "left"]);
    }
    for (let i = 0; i <= 10; i++) {
      const t = view === "scope" ? fmtTime((span / SAMPLE_RATE) * (i / 10)) : i === 10 ? "now" : `${+(-span * (10 - i) / 10).toFixed(1)} s`;
      labels.push([t, (w * i) / 10, h - 4 * dpr, i === 0 ? "left" : i === 10 ? "right" : "center"]);
    }
    const perDiv = view === "scope" ? `${fmtTime(span / SAMPLE_RATE / 10)}/div` : `${span / 10} s/div`;
    labels.push([perDiv, w - 4 * dpr, 14 * dpr, "right"]);
  } else if (view === "xy") {
    for (const v of VOLTS) {
      vlines.push(w / 2 + (v / 6) * xyS);
      hlines.push([w / 2 - xyS, w / 2 + xyS, h / 2 - (v / 6) * xyS]);
    }
    labels.push([`+6 V  X: ${names[+$("xyX").value]}`, w / 2 + xyS, h / 2 - 4 * dpr, "right"]);
    labels.push([`+6 V  Y: ${names[+$("xyY").value]}`, w / 2 + 4 * dpr, h / 2 - xyS + 12 * dpr, "left"]);
  } else {
    for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
      vlines.push(fftX(f, w));
      labels.push([fmtHz(f), fftX(f, w), h - 4 * dpr, "center"]);
    }
    for (let db = 0; db >= -100; db -= 20) {
      hlines.push([0, w, h * (-db / 100)]);
      labels.push([`${db} dB`, 4 * dpr, h * (-db / 100) + 12 * dpr, "left"]);
    }
    labels.push(["Hz", w - 4 * dpr, h - 16 * dpr, "right"]);
  }

  ctx.strokeStyle = "#222";
  ctx.lineWidth = dpr;
  ctx.beginPath();
  for (const x of vlines) {
    const top = view === "xy" ? h / 2 - xyS : 0, bot = view === "xy" ? h / 2 + xyS : h;
    ctx.moveTo(x, top); ctx.lineTo(x, bot);
  }
  for (const [x0, x1, y] of hlines) { ctx.moveTo(x0, y); ctx.lineTo(x1, y); }
  ctx.stroke();

  ctx.fillStyle = "#8a94a0";
  ctx.font = `${11 * dpr}px system-ui, sans-serif`;
  for (const [t, x, y, align] of labels) {
    ctx.textAlign = align;
    ctx.fillText(t, x, y);
  }
}

function drawTriggerMarker(w, h, dpr) {
  const y = h / 2 - +$("trigLevel").value * (h / 2) * 0.9;
  ctx.strokeStyle = colors[+$("trigCh").value];
  ctx.lineWidth = dpr;
  ctx.setLineDash([6 * dpr, 6 * dpr]);
  ctx.beginPath();
  ctx.moveTo(0, y); ctx.lineTo(w, y);
  ctx.stroke();
  ctx.setLineDash([]);
}

function drawScope(w, h) {
  const span = +$("timeDiv").value;
  const window = span * 2; // search area for a trigger plus the displayed span
  const level = +$("trigLevel").value;
  const rising = $("trigEdge").value === "rise";
  const tc = +$("trigCh").value;
  const trigData = recent(tc, window);
  const start = findTrigger(trigData.subarray(0, window - span), level, rising);

  for (let c = 0; c < NUM_CH; c++) {
    if (hidden(c)) continue;
    const d = recent(c, window).subarray(start, start + span);
    ctx.strokeStyle = colors[c];
    ctx.beginPath();
    for (let i = 0; i < d.length; i++) {
      const x = (i / (span - 1)) * w;
      const y = h / 2 - d[i] * (h / 2) * 0.9;
      i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
    }
    ctx.stroke();
  }
}

function drawXY(w, h) {
  const n = 1024;
  const a = recent(+$("xyX").value, n), b = recent(+$("xyY").value, n);
  const s = Math.min(w, h) / 2 * 0.9;
  ctx.strokeStyle = "#7dff7d";
  ctx.beginPath();
  for (let i = 0; i < n; i++) {
    const x = w / 2 + a[i] * s, y = h / 2 - b[i] * s;
    i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
  }
  ctx.stroke();
}

// In-place radix-2 FFT
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const ur = re[i + k], ui = im[i + k];
        const vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
        const vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        [cr, ci] = [cr * wr - ci * wi, cr * wi + ci * wr];
      }
    }
  }
}

// a: weight of the previous spectrum (time averaging); k: octave fraction averaged across frequency.
const SMOOTH = [{ a: 0, k: 0 }, { a: 0.6, k: 1 / 24 }, { a: 0.8, k: 1 / 12 }, { a: 0.9, k: 1 / 6 }];
const fftAvg = Array.from({ length: NUM_CH }, () => new Float32Array(FFT_N / 2));

function drawFFT(w, h) {
  const { a, k } = SMOOTH[+$("smooth").value];
  const bins = FFT_N / 2;
  const binHz = SAMPLE_RATE / FFT_N;
  const norm = 1 / (FFT_N / 4) ** 2; // 0 dB = full-scale sine
  const half = Math.pow(2, k / 2);
  const cs = new Float64Array(bins + 1);
  const binAt = (px) => (FMIN * Math.pow(FMAX / FMIN, px / w)) / binHz;

  for (let c = 0; c < NUM_CH; c++) {
    if (hidden(c)) continue;
    const avg = fftAvg[c];
    if (totalFrames - lastGapEnd >= FFT_N) {
      const re = recent(c, FFT_N), im = new Float32Array(FFT_N);
      let mean = 0;
      for (let i = 0; i < FFT_N; i++) mean += re[i];
      mean /= FFT_N;
      for (let i = 0; i < FFT_N; i++) re[i] = (re[i] - mean) * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (FFT_N - 1)));
      fft(re, im);
      for (let i = 0; i < bins; i++) avg[i] = a * avg[i] + (1 - a) * (re[i] * re[i] + im[i] * im[i]) * norm;
    }
    for (let i = 0; i < bins; i++) cs[i + 1] = cs[i] + avg[i];

    ctx.strokeStyle = colors[c];
    ctx.beginPath();
    for (let px = 0; px < w; px++) {
      const b0 = binAt(px), b1 = binAt(px + 1), mid = (b0 + b1) / 2;
      const lo = Math.min(b0, mid / half), hi = Math.max(b1, mid * half);
      let p;
      if (hi - lo < 1) {
        // Narrower than one bin (low frequencies): interpolate between neighbours.
        const i0 = Math.min(Math.max(Math.floor(mid), 1), bins - 2);
        const fr = Math.min(Math.max(mid - i0, 0), 1);
        p = avg[i0] * (1 - fr) + avg[i0 + 1] * fr;
      } else {
        const i0 = Math.max(Math.ceil(lo), 1);
        const i1 = Math.max(Math.min(Math.floor(hi), bins - 1), i0);
        if (k === 0) {
          p = 0;
          for (let i = i0; i <= i1; i++) p = Math.max(p, avg[i]);
        } else {
          p = (cs[i1 + 1] - cs[i0]) / (i1 - i0 + 1);
        }
      }
      const db = 10 * Math.log10(p + 1e-12);
      const y = Math.min(Math.max(h * (-db / 100), 0), h);
      px ? ctx.lineTo(px, y) : ctx.moveTo(px, y);
    }
    ctx.stroke();
  }
}

function drawRoll(w, h, dpr) {
  const span = +$("rollSpan").value;
  const n = Math.min(span * HIST_RATE, HIST);
  const y = (v) => h / 2 - v * (h / 2) * 0.9;

  for (let c = 0; c < NUM_CH; c++) {
    if (hidden(c)) continue;
    const line = new Path2D(), band = new Path2D();
    for (let px = 0; px < w; px++) {
      const i0 = Math.floor((px * n) / w);
      const i1 = Math.max(i0 + 1, Math.floor(((px + 1) * n) / w));
      let lo = Infinity, hi = -Infinity;
      for (let i = i0; i < i1; i++) {
        const k = (histHead - n + i + HIST * 2) % HIST;
        lo = Math.min(lo, histMin[c][k]);
        hi = Math.max(hi, histMax[c][k]);
      }
      px ? line.lineTo(px, y((lo + hi) / 2)) : line.moveTo(px, y((lo + hi) / 2));
      band.moveTo(px, y(hi));
      band.lineTo(px, y(lo));
    }
    ctx.strokeStyle = colors[c];
    ctx.globalAlpha = 0.45;
    ctx.lineWidth = dpr;
    ctx.stroke(band);
    ctx.globalAlpha = 1;
    ctx.lineWidth = 2 * dpr;
    ctx.stroke(line);
  }
}

function frame() {
  const dpr = window.devicePixelRatio || 1;
  const pw = Math.round(cv.clientWidth * dpr), ph = Math.round(cv.clientHeight * dpr);
  let resized = false;
  if (cv.width !== pw || cv.height !== ph) {
    cv.width = pw;
    cv.height = ph;
    resized = true;
  }
  // Paused keeps the last drawn image; a resize clears the canvas so it must redraw once.
  if ($("pause").checked && !resized) {
    requestAnimationFrame(frame);
    return;
  }
  const w = cv.width, h = cv.height;
  ctx.clearRect(0, 0, w, h);
  const view = $("view").value;
  graticule(view, w, h, dpr);
  if (view === "scope") drawTriggerMarker(w, h, dpr);
  ctx.lineWidth = 2 * dpr;
  if (view === "scope") drawScope(w, h);
  else if (view === "xy") drawXY(w, h);
  else if (view === "roll") drawRoll(w, h, dpr);
  else drawFFT(w, h);
  requestAnimationFrame(frame);
}

$("view").onchange = () => (document.body.dataset.view = $("view").value);
$("connect").onclick = () => connect().catch((e) => ($("status").textContent = e.message));
if (!("serial" in navigator)) $("status").textContent = "Web Serial not supported; use Chrome or Edge";
frame();
