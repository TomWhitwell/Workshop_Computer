// WS-DOOM companion display.
// The Workshop System Computer runs the whole game and is played from its
// panel; this page renders the streamed video and offers a few settings.
//
// Device -> host packets:  AB CD <type u8> <len u16le> <payload>
//   0x02 PALETTE   : 768 bytes, 256 x RGB
//   0x03 STATUS    : health i16, armor i16, ammo i16, weapon u8, level u8
//   0x04 TEXT      : utf-8 log line
//   0x05 LINE      : line u8, then RLE pairs (count u8, palette-index u8)
//   0x06 FRAME_END : frame counter u8 -> blit the assembled frame
//   0x07 SETTINGS  : music u8, sfx u8, turn u8, move u8 (current device settings)
// Host -> device packets:  BA <type u8> <a u8> <b u8>
//   0x01 KEY       : doom keycode, 1 down / 0 up
//   0x02 MUSIC_VOL : a = 0..255
//   0x03 SFX_VOL   : a = 0..255
//   0x04 WARP      : a = episode, b = map
//   0x05 SAVE      : commit current settings to the card's flash
//   0x06 TURN_SENS : a = 32..255 (128 = nominal)
//   0x07 MOVE_SENS : a = 32..255 (128 = nominal)
//   0x08 REFRESH   : resend palette + settings

'use strict';

const W = 320, H = 200;

const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
const crt = document.getElementById('crt');
const overlay = document.getElementById('overlay');
const connBtn = document.getElementById('connect');
const connStatus = document.getElementById('conn-status');
const fpsEl = document.getElementById('fps');
const kbpsEl = document.getElementById('kbps');
const levelSel = document.getElementById('level');
const musicVol = document.getElementById('music-vol');
const sfxVol = document.getElementById('sfx-vol');
const turnSens = document.getElementById('turn-sens');
const moveSens = document.getElementById('move-sens');

const image = ctx.createImageData(W, H);
const pixels32 = new Uint32Array(image.data.buffer);
const frame = new Uint8Array(W * H);
const palette = new Uint32Array(256);

// Grey ramp until the device sends its real palette
for (let i = 0; i < 256; i++) palette[i] = 0xff000000 | (i << 16) | (i << 8) | i;

let port = null, writer = null, currentReader = null;
let framesThisSecond = 0, bytesThisSecond = 0;
let wantConnection = false;   // user has connected once and not clicked Disconnect
let retryTimer = null;

setInterval(() => {
  fpsEl.textContent = `${framesThisSecond} fps`;
  kbpsEl.textContent = `${(bytesThisSecond / 1024).toFixed(0)} KB/s`;
  framesThisSecond = 0;
  bytesThisSecond = 0;
}, 1000);

crt.addEventListener('dblclick', () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else crt.requestFullscreen?.();
});

// ---------------------------------------------------------------- controls

function sendCmd(type, a, b = 0) {
  writer?.write(new Uint8Array([0xba, type, a & 0xff, b & 0xff])).catch(() => {});
}

levelSel.addEventListener('change', () => {
  sendCmd(0x04, 1, parseInt(levelSel.value, 10));
});

function throttled(fn, ms) {
  let t = null, lastArgs = null;
  return (...args) => {
    lastArgs = args;
    if (t) return;
    t = setTimeout(() => { t = null; fn(...lastArgs); }, ms);
  };
}

musicVol.addEventListener('input', throttled(() => {
  sendCmd(0x02, parseInt(musicVol.value, 10));
}, 120));

sfxVol.addEventListener('input', throttled(() => {
  sendCmd(0x03, parseInt(sfxVol.value, 10));
}, 120));

turnSens.addEventListener('input', throttled(() => {
  sendCmd(0x06, parseInt(turnSens.value, 10));
}, 120));

moveSens.addEventListener('input', throttled(() => {
  sendCmd(0x07, parseInt(moveSens.value, 10));
}, 120));

document.getElementById('save-settings').addEventListener('click', () => {
  sendCmd(0x05, 0); // commit current mix to the card's flash
});

document.getElementById('refresh').addEventListener('click', () => {
  sendCmd(0x08, 0); // device resends palette + settings
});

// Cheat codes: typed into doom as plain key taps — the engine's own cheat
// detector (IDDQD & friends) does the rest. Staggered a couple of game
// tics apart so the event queue never overflows. Only the classics are
// let through; anything else stays in the field.
const CHEATS = new Set([
  'iddqd', 'idkfa', 'idfa', 'idchoppers', 'idspispopd',
  ...'irsalv'.split('').map((c) => 'idbehold' + c),
]);

document.getElementById('cheat').addEventListener('keydown', (e) => {
  if (e.key !== 'Enter') return;
  const field = e.target;
  const code = field.value.toLowerCase().replace(/[^a-z0-9]/g, '');
  if (!CHEATS.has(code) || !writer) return;
  field.value = '';
  [...code].forEach((ch, i) => {
    const k = ch.charCodeAt(0);
    setTimeout(() => {
      sendCmd(0x01, k, 1);
      setTimeout(() => sendCmd(0x01, k, 0), 15);
    }, i * 60);
  });
});

document.getElementById('pause').addEventListener('click', () => {
  // tap doom's pause key
  sendCmd(0x01, 0xff, 1);
  setTimeout(() => sendCmd(0x01, 0xff, 0), 80);
});

// ---------------------------------------------------------------- sidebar

function setCollapsed(on) {
  document.body.classList.toggle('collapsed', on);
  try { localStorage.setItem('ws-doom-collapsed', on ? '1' : '0'); } catch (e) { /* private mode */ }
}
document.getElementById('collapse').addEventListener('click', () => setCollapsed(true));
document.getElementById('expand').addEventListener('click', () => setCollapsed(false));
try {
  if (localStorage.getItem('ws-doom-collapsed') === '1') setCollapsed(true);
} catch (e) { /* private mode */ }

// ---------------------------------------------------------------- intro popup

const intro = document.getElementById('intro');
function showIntro() { intro.classList.remove('hidden'); }
document.getElementById('intro-close').addEventListener('click', () => {
  intro.classList.add('hidden');
  try { localStorage.setItem('ws-doom-intro-seen', '1'); } catch (e) { /* private mode */ }
});
// optional footer link — reopens the intro when present
document.getElementById('about')?.addEventListener('click', (e) => {
  e.preventDefault();
  showIntro();
});
try {
  if (localStorage.getItem('ws-doom-intro-seen') !== '1') showIntro();
} catch (e) { showIntro(); }

// ---------------------------------------------------------------- serial

connBtn.addEventListener('click', async () => {
  if (wantConnection || port) { await disconnect(true); return; }
  if (!('serial' in navigator)) {
    alert('WebSerial is not available in this browser. Use Chrome or Edge.');
    return;
  }
  let p;
  try {
    p = await navigator.serial.requestPort({
      filters: [{ usbVendorId: 0x2e8a }], // Raspberry Pi
    });
  } catch (e) {
    return; // user dismissed the picker
  }
  wantConnection = true;
  if (!(await openPort(p))) scheduleRetry();
});

async function openPort(p) {
  try {
    await p.open({ baudRate: 921600 }); // rate is ignored by USB CDC
  } catch (e) {
    return false;
  }
  port = p;
  writer = port.writable.getWriter();
  setConnected('connected');
  readLoop();
  return true;
}

// The module vanished (power cycle, unplug): keep trying previously-granted
// ports until it comes back. No permission prompt is needed for those.
function scheduleRetry() {
  if (!wantConnection || retryTimer) return;
  setConnected('reconnecting');
  retryTimer = setTimeout(async () => {
    retryTimer = null;
    if (!wantConnection || port) return;
    const grants = await navigator.serial.getPorts().catch(() => []);
    for (const p of grants) {
      if (await openPort(p)) return;
    }
    scheduleRetry();
  }, 1500);
}

// Fires when a granted device re-enumerates — reconnect immediately.
if ('serial' in navigator) {
  navigator.serial.addEventListener('connect', (e) => {
    if (wantConnection && !port) openPort(e.target || e.port);
  });
}

async function disconnect(userAsked = false) {
  if (userAsked) {
    wantConnection = false;
    clearTimeout(retryTimer);
    retryTimer = null;
  }
  // cancel the read stream first: while it holds its lock, port.close()
  // rejects and the port would keep streaming behind the overlay
  try { await currentReader?.cancel(); } catch (e) { /* already gone */ }
  try { writer?.releaseLock(); } catch (e) { /* already gone */ }
  try { await port?.close(); } catch (e) { /* already gone */ }
  port = null;
  writer = null;
  setConnected(wantConnection ? 'reconnecting' : 'disconnected');
  if (wantConnection) scheduleRetry();
}

function setConnected(state) {
  const on = state === 'connected';
  connStatus.textContent = state === 'reconnecting' ? 'reconnecting…' : state;
  connStatus.className = `badge ${on ? 'on' : 'off'}`;
  connBtn.textContent = on || state === 'reconnecting' ? 'Disconnect' : 'Connect';
  overlay.classList.toggle('hidden', on);
}

async function readLoop() {
  const parser = makeParser();
  while (port?.readable) {
    const reader = port.readable.getReader();
    currentReader = reader;
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;    // cancelled by disconnect()
        bytesThisSecond += value.length;
        parser(value);
      }
      break;
    } catch (e) {
      break; // device unplugged
    } finally {
      reader.releaseLock();
      currentReader = null;
    }
  }
  disconnect();
}

// Incremental packet parser over arbitrary chunk boundaries.
// Per-type payload caps: a header that claims more is stream garbage
// (magic bytes appearing inside payload data) — skip it and resync
// instead of swallowing kilobytes of real packets.
const MAX_PAYLOAD = { 2: 768, 3: 16, 4: 256, 5: 1 + 2 * W, 6: 4, 7: 8 };

function makeParser() {
  let buf = new Uint8Array(0);
  return (chunk) => {
    const merged = new Uint8Array(buf.length + chunk.length);
    merged.set(buf); merged.set(chunk, buf.length);
    buf = merged;
    for (;;) {
      // resync to AB CD
      let start = 0;
      while (start + 1 < buf.length && !(buf[start] === 0xab && buf[start + 1] === 0xcd)) start++;
      if (start > 0) buf = buf.subarray(start);
      if (buf.length < 5) return;
      const type = buf[2];
      const len = buf[3] | (buf[4] << 8);
      const max = MAX_PAYLOAD[type];
      if (max === undefined || len > max) {
        buf = buf.subarray(2); // false header: skip the magic, rescan
        continue;
      }
      if (buf.length < 5 + len) return;
      handlePacket(type, buf.subarray(5, 5 + len));
      buf = buf.subarray(5 + len);
    }
  };
}

function handlePacket(type, payload) {
  switch (type) {
    case 0x05: { // one RLE scanline
      const y = payload[0];
      if (y >= H) break;
      let out = y * W;
      const end = out + W;
      for (let i = 1; i + 1 < payload.length && out < end; i += 2) {
        const count = payload[i], value = payload[i + 1];
        frame.fill(value, out, Math.min(out + count, end));
        out += count;
      }
      break;
    }
    case 0x06: { // frame complete: blit
      for (let i = 0; i < frame.length; i++) pixels32[i] = palette[frame[i]];
      ctx.putImageData(image, 0, 0);
      framesThisSecond++;
      break;
    }
    case 0x02: { // palette (exactly 768 bytes; anything else is a torn packet)
      if (payload.length !== 768) break;
      for (let i = 0; i < 256 && i * 3 + 2 < payload.length; i++) {
        palette[i] = 0xff000000 | (payload[i * 3 + 2] << 16) | (payload[i * 3 + 1] << 8) | payload[i * 3];
      }
      break;
    }
    case 0x07: { // device settings -> reflect in the sliders
      if (payload.length >= 2) {
        musicVol.value = payload[0];
        sfxVol.value = payload[1];
      }
      if (payload.length >= 4) {
        turnSens.value = payload[2];
        moveSens.value = payload[3];
      }
      break;
    }
    case 0x04: { // device message (panics show up here)
      const msg = new TextDecoder().decode(payload);
      console.log('[ws-doom]', msg);
      document.getElementById('devmsg').textContent = msg;
      break;
    }
    case 0x03: { // status: keep the level dropdown in sync
      if (payload.length >= 8) {
        const map = payload[7] & 15;
        if (map >= 1 && map <= 9 && levelSel.value !== String(map)) {
          levelSel.value = String(map);
        }
      }
      break;
    }
  }
}
