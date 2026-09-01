    const MFR = 0x7D;
    const DEVICE = 79;
    const CMD_PREVIEW = 0x01;
    const CMD_SAVE = 0x02;
    const CMD_READ = 0x03;
    const CMD_CARD_ID = 0x04;
    const CMD_PANEL_STATE = 0x05;
    const CMD_PANEL_STREAM = 0x06;
    const CMD_READ_MAPS = 0x07;
    const CMD_WRITE_MAPS = 0x08;
    const CMD_SET_PERF = 0x09;
    const CMD_LEARN_NOTIFY = 0x0A;
    const CMD_READ_PROFILE = 0x0B;
    const CONFIG_LEN = 8;
    const EXT_MARKER = 0x59;
    const NUM_SLOTS = 13;
    const SRC_NONE = 0;
    const SRC_CC = 1;
    const SRC_NOTE = 2;
    const SRC_KNOB_X = 3;
    const SRC_KNOB_Y = 4;
    const CHAN_OMNI = 16;
    const SWITCH_NAMES = ['Down', 'Middle', 'Up'];
    const VOICE_MATRIX_ROWS = 11;
    const VOICE_MATRIX_COLS = 11;
    const VOICE_MATRIX_MAX = 120;
    const VOICE_ENGINE_MAX = VOICE_MATRIX_MAX;
    const VOICE_ROW_NAMES = [
      'Pulse', 'Square', 'Sine', 'Saw', 'Triangle', 'Narrow', 'Bright saw',
      'Hollow', 'FM bell', 'Organ', 'Noise'
    ];
    const VOICE_COL_NAMES = [
      'Pure', 'Dual', 'Triple', 'Detune', 'Sub', 'Octave', 'Unison',
      'LP', 'Chorus', 'Glide', 'Sync/FM'
    ];
    const VOICE_NAMES = VOICE_ROW_NAMES;
    const SLOT_NAMES = [
      'Reserved',
      'Voice A channel',
      'Voice B channel',
      'Pitch bend range',
      'Audio engine (CC 0–120)',
      'Reserved',
      'Reserved',
      'Attack',
      'Decay',
      'Sustain',
      'Release',
      'Cutoff',
      'PWM'
    ];
    const FACTORY_SLOT_CCS = [null, null, null, null, 24, null, null, null, null, null, null, null, null];
    const FACTORY_SLOT_SRC = [
      SRC_NONE, SRC_NONE, SRC_NONE, SRC_NONE,
      SRC_CC, SRC_NONE, SRC_NONE,
      SRC_KNOB_X, SRC_NONE, SRC_NONE, SRC_KNOB_Y,
      SRC_NONE, SRC_NONE
    ];

    let midiAccess = null;
    let cardOutput = null;
    let cardInput = null;
    let monitorInput = null;
    let monitorHandler = null;
    let controllerInput = null;
    let controllerHandler = null;
    let relayMsgCount = 0;
    let relayLastBendAt = 0;
    const relayHeldNotes = new Set(); // "ch:note"
    let panelStreaming = false;
    let panelStreamKey = '';
    let lastPanelAt = 0;
    let sysexBuf = [];
    let sysexActive = false;
    let sysexStartedAt = 0;
    let midiFaultCount = 0;
    let midiLastResyncAt = 0;
    let midiHealthVisibleUntil = 0;
    let midiHealthDismissed = false;
    let setupModeActive = false;
    let setupSlotActive = 0;
    let setupSlotPending = 0;
    let setupLearnFlashSlot = -1;
    let setupLearnFlashUntil = 0;
    let mapsState = factoryMapsState();
    let perfUiFromPanel = false;
    let perfSendTimer = 0;
    let profilePollTimer = 0;
    let profilePeakUs = 0;
    let profileBudgetUs = 20;
    let profileOverrun = false;

    const heldNotes = new Map(); // note -> channel used for note-on

    function $(id) { return document.getElementById(id); }

    function factoryMapsState() {
      const slots = [];
      for (let i = 0; i < NUM_SLOTS; i++) {
        const cc = FACTORY_SLOT_CCS[i];
        const src = FACTORY_SLOT_SRC[i];
        slots.push({
          sourceType: src,
          channel: CHAN_OMNI,
          ccOrNote: cc == null ? 0 : cc,
          pad: 0
        });
      }
      return {
        marker: EXT_MARKER,
        audioVoice: 0,
        attack: 0,
        decay: 0,
        sustain: 127,
        releaseAmp: 0,
        cutoff: 127,
        pwmWidth: 0,
        slots
      };
    }

    function formatMapSource(slotObj) {
      if (!slotObj || slotObj.sourceType === SRC_NONE)
        return 'None (unmapped)';
      if (slotObj.sourceType === SRC_CC) {
        const ch = slotObj.channel === CHAN_OMNI
          ? 'Omni'
          : ('Ch ' + ((slotObj.channel & 0x0F) + 1));
        return 'CC ' + slotObj.ccOrNote + ' · ' + ch;
      }
      if (slotObj.sourceType === SRC_NOTE) {
        const ch = slotObj.channel === CHAN_OMNI
          ? 'Omni'
          : ('Ch ' + ((slotObj.channel & 0x0F) + 1));
        return 'Note ' + slotObj.ccOrNote + ' · ' + ch;
      }
      if (slotObj.sourceType === SRC_KNOB_X) return 'Knob X';
      if (slotObj.sourceType === SRC_KNOB_Y) return 'Knob Y';
      return 'Source ' + slotObj.sourceType;
    }

    function appendSetupLog(line) {
      const el = $('setupLearnLog');
      if (!el) return;
      const t = new Date().toLocaleTimeString();
      el.textContent = '[' + t + '] ' + line + '\n' + el.textContent;
    }

    function renderSetupLeds(slot) {
      const host = $('setupLeds');
      if (!host) return;
      host.innerHTML = '';
      for (let i = 0; i < 4; i++) {
        const d = document.createElement('div');
        d.className = 'setup-led' + (((slot >> i) & 1) ? ' on' : '');
        d.textContent = String(i);
        d.title = 'LED' + i;
        host.appendChild(d);
      }
    }

    function updateSetupMonitor(ext) {
      const entering = ext && ext.mode === 1;
      if (entering && !setupModeActive) {
        appendSetupLog('Entered SETUP — turn Main to choose a learn slot');
        // Sync RAM maps so the monitor matches the card.
        sendSysEx([CMD_READ_MAPS], { quiet: true });
      } else if (!entering && setupModeActive) {
        appendSetupLog('Left SETUP (PLAY)');
        setupLearnFlashSlot = -1;
      }
      setupModeActive = !!entering;
      document.body.classList.toggle('setup-active', setupModeActive);

      if (!setupModeActive) {
        highlightMapsSlot(-1);
        return;
      }

      const slot = ext.slot != null ? (ext.slot & 0x7F) : 0;
      const pending = ext.slotPending != null ? (ext.slotPending & 0x7F) : slot;
      setupSlotActive = slot;
      setupSlotPending = pending;

      if ($('setupSlotNum')) $('setupSlotNum').textContent = String(slot);
      if ($('setupSlotName'))
        $('setupSlotName').textContent = SLOT_NAMES[slot] || ('Slot ' + slot);
      let meta = 'Confirmed learn slot · LED0–3 show binary ' + slot;
      if (pending !== slot)
        meta = 'Main selecting slot ' + pending + '… (hold briefly to confirm ' + slot + ')';
      if ($('setupSlotMeta')) $('setupSlotMeta').textContent = meta;
      renderSetupLeds(slot);

      const map = mapsState.slots[slot];
      if ($('setupMapValue'))
        $('setupMapValue').textContent = formatMapSource(map);

      highlightMapsSlot(slot);
    }

    function highlightMapsSlot(slot) {
      const body = $('mapsBody');
      if (!body) return;
      const rows = body.querySelectorAll('tr');
      const flashOn = setupLearnFlashSlot >= 0 && performance.now() < setupLearnFlashUntil;
      rows.forEach((tr, i) => {
        tr.classList.toggle('setup-selected', setupModeActive && i === slot);
        tr.classList.toggle('setup-learned', flashOn && i === setupLearnFlashSlot);
      });
    }

    function applyLearnNotify(slot, src, chan, id) {
      if (slot < 0 || slot >= NUM_SLOTS) return;
      mapsState.slots[slot] = {
        sourceType: src,
        channel: chan,
        ccOrNote: id,
        pad: 0
      };
      setupLearnFlashSlot = slot;
      setupLearnFlashUntil = performance.now() + 1600;
      renderMapsTable({ syncLive: false });
      const card = $('setupMapCard');
      if (card) {
        card.classList.remove('flash');
        void card.offsetWidth;
        card.classList.add('flash');
      }
      if ($('setupMapValue'))
        $('setupMapValue').textContent = formatMapSource(mapsState.slots[slot]);
      appendSetupLog(
        'Learned slot ' + slot + ' (' + (SLOT_NAMES[slot] || '?') + ') ← ' +
        formatMapSource(mapsState.slots[slot])
      );
      if ($('setupHeard'))
        $('setupHeard').textContent =
          'Last learn: slot ' + slot + ' ← ' + formatMapSource(mapsState.slots[slot]);
      setMapsStatus('SETUP learned slot ' + slot, true);
    }

    function noteSetupMidiHeard(kind, detail) {
      if (!setupModeActive) return;
      const slot = setupSlotActive;
      if ($('setupHeard'))
        $('setupHeard').textContent =
          'Heard ' + kind + ' ' + detail + ' → learning to slot ' + slot +
          ' (' + (SLOT_NAMES[slot] || '') + ')';
    }

    function renderMapsTable(opts) {
      const body = $('mapsBody');
      if (!body) return;
      body.innerHTML = '';
      for (let i = 0; i < NUM_SLOTS; i++) {
        const s = mapsState.slots[i];
        const tr = document.createElement('tr');
        tr.style.borderTop = '1px solid var(--border)';
        tr.innerHTML =
          '<td style="padding:6px 8px">' + i + '</td>' +
          '<td style="padding:6px 8px">' + SLOT_NAMES[i] + '</td>' +
          '<td style="padding:6px 8px"><select data-slot="' + i + '" data-field="sourceType">' +
            '<option value="0">None</option>' +
            '<option value="1">CC</option>' +
            '<option value="2">Note</option>' +
            '<option value="3">Knob X</option>' +
            '<option value="4">Knob Y</option>' +
          '</select></td>' +
          '<td style="padding:6px 8px"><select data-slot="' + i + '" data-field="channel"></select></td>' +
          '<td style="padding:6px 8px"><input data-slot="' + i + '" data-field="ccOrNote" type="number" min="0" max="127" style="width:72px"></td>';
        body.appendChild(tr);
        const src = tr.querySelector('[data-field="sourceType"]');
        const ch = tr.querySelector('[data-field="channel"]');
        const id = tr.querySelector('[data-field="ccOrNote"]');
        for (let c = 0; c < 16; c++) {
          const o = document.createElement('option');
          o.value = String(c);
          o.textContent = 'Ch ' + (c + 1);
          ch.appendChild(o);
        }
        const omni = document.createElement('option');
        omni.value = String(CHAN_OMNI);
        omni.textContent = 'Omni';
        ch.appendChild(omni);
        src.value = String(s.sourceType);
        ch.value = String(s.channel);
        id.value = String(s.ccOrNote);
        src.addEventListener('change', onMapFieldChange);
        ch.addEventListener('change', onMapFieldChange);
        id.addEventListener('change', onMapFieldChange);
      }
      // Read-maps must not overwrite Live engine (RAM SetPerf) controls
      if (!opts || opts.syncLive !== false)
        applyPerfToLiveUI(mapsState);
      if (setupModeActive)
        highlightMapsSlot(setupSlotActive);
    }

    function onMapFieldChange(e) {
      const el = e.target;
      const slot = parseInt(el.dataset.slot, 10);
      const field = el.dataset.field;
      mapsState.slots[slot][field] = parseInt(el.value, 10) || 0;
    }

    function voiceNameFromId(voiceId) {
      const cc = Math.max(0, Math.min(VOICE_MATRIX_MAX, voiceId | 0));
      const row = Math.floor(cc / VOICE_MATRIX_COLS);
      const col = cc % VOICE_MATRIX_COLS;
      return VOICE_ROW_NAMES[row] + ' + ' + VOICE_COL_NAMES[col] + ' (CC' + cc + ')';
    }

    function voiceCcFromRowCol(row, col) {
      return Math.max(0, Math.min(VOICE_MATRIX_MAX, row * VOICE_MATRIX_COLS + col));
    }

    function syncLiveVoiceUiFromCc(cc, opts) {
      const quiet = opts && opts.quiet;
      cc = Math.max(0, Math.min(VOICE_MATRIX_MAX, cc | 0));
      const row = Math.floor(cc / VOICE_MATRIX_COLS);
      const col = cc % VOICE_MATRIX_COLS;
      if ($('liveVoiceRow')) $('liveVoiceRow').value = String(row);
      if ($('liveVoiceCol')) $('liveVoiceCol').value = String(col);
      if ($('liveVoiceCc')) $('liveVoiceCc').value = String(cc);
      if ($('liveVoiceLabel')) $('liveVoiceLabel').textContent = voiceNameFromId(cc);
      document.querySelectorAll('#voiceMatrixGrid button').forEach((btn) => {
        btn.classList.toggle('is-active', parseInt(btn.dataset.cc, 10) === cc);
      });
      if (!quiet)
        schedulePerfSend();
    }

    function initVoiceMatrixUi() {
      const rowSel = $('liveVoiceRow');
      const colSel = $('liveVoiceCol');
      if (rowSel && !rowSel.options.length) {
        for (let r = 0; r < VOICE_MATRIX_ROWS; r++) {
          const o = document.createElement('option');
          o.value = String(r);
          o.textContent = 'R' + r + ' — ' + VOICE_ROW_NAMES[r];
          rowSel.appendChild(o);
        }
      }
      if (colSel && !colSel.options.length) {
        for (let c = 0; c < VOICE_MATRIX_COLS; c++) {
          const o = document.createElement('option');
          o.value = String(c);
          o.textContent = 'C' + c + ' — ' + VOICE_COL_NAMES[c];
          colSel.appendChild(o);
        }
      }
      const grid = $('voiceMatrixGrid');
      if (grid && !grid.childElementCount) {
        for (let cc = 0; cc <= VOICE_MATRIX_MAX; cc++) {
          const r = Math.floor(cc / VOICE_MATRIX_COLS);
          const c = cc % VOICE_MATRIX_COLS;
          const btn = document.createElement('button');
          btn.type = 'button';
          btn.dataset.cc = String(cc);
          btn.title = voiceNameFromId(cc);
          btn.textContent = String(cc);
          btn.addEventListener('click', () => {
            mapsState.audioVoice = cc;
            syncLiveVoiceUiFromCc(cc);
            sendPerfToCard({ quiet: false });
          });
          grid.appendChild(btn);
        }
      }
    }

    function clamp7(v) {
      return Math.max(0, Math.min(127, v | 0));
    }

    function applyPerfToLiveUI(state) {
      const s = state || mapsState;
      perfUiFromPanel = true;
      if ($('liveVoiceRow') && $('liveVoiceCol'))
        syncLiveVoiceUiFromCc(s.audioVoice | 0, { quiet: true });
      const pairs = [
        ['liveAttack', 'liveAttackVal', s.attack],
        ['liveDecay', 'liveDecayVal', s.decay],
        ['liveSustain', 'liveSustainVal', s.sustain],
        ['liveRelease', 'liveReleaseVal', s.releaseAmp],
        ['liveCutoff', 'liveCutoffVal', s.cutoff],
        ['livePwm', 'livePwmVal', s.pwmWidth]
      ];
      for (const [id, vid, val] of pairs) {
        const v = clamp7(val);
        if ($(id)) $(id).value = String(v);
        if ($(vid)) $(vid).textContent = String(v);
      }
      perfUiFromPanel = false;
    }

    function syncPerfFromLiveUI() {
      mapsState.audioVoice = voiceCcFromRowCol(
        parseInt($('liveVoiceRow').value, 10) || 0,
        parseInt($('liveVoiceCol').value, 10) || 0
      );
      if ($('liveVoiceCc'))
        mapsState.audioVoice = Math.max(0, Math.min(VOICE_MATRIX_MAX,
          parseInt($('liveVoiceCc').value, 10) || mapsState.audioVoice));
      mapsState.attack = clamp7(parseInt($('liveAttack').value, 10) || 0);
      mapsState.decay = clamp7(parseInt($('liveDecay').value, 10) || 0);
      mapsState.sustain = clamp7(parseInt($('liveSustain').value, 10) || 0);
      mapsState.releaseAmp = clamp7(parseInt($('liveRelease').value, 10) || 0);
      mapsState.cutoff = clamp7(parseInt($('liveCutoff').value, 10) || 0);
      mapsState.pwmWidth = clamp7(parseInt($('livePwm').value, 10) || 0);
      mapsState.marker = EXT_MARKER;
      if ($('liveAttackVal')) $('liveAttackVal').textContent = String(mapsState.attack);
      if ($('liveDecayVal')) $('liveDecayVal').textContent = String(mapsState.decay);
      if ($('liveSustainVal')) $('liveSustainVal').textContent = String(mapsState.sustain);
      if ($('liveReleaseVal')) $('liveReleaseVal').textContent = String(mapsState.releaseAmp);
      if ($('liveCutoffVal')) $('liveCutoffVal').textContent = String(mapsState.cutoff);
      if ($('livePwmVal')) $('livePwmVal').textContent = String(mapsState.pwmWidth);
    }

    function setLiveEngStatus(text, ok) {
      const el = $('liveEngStatus');
      if (!el) return;
      el.textContent = text;
      el.className = 'status ' + (ok ? 'ok' : 'warn');
    }

    function sendPerfToCard(opts) {
      syncPerfFromLiveUI();
      const quiet = !opts || opts.quiet !== false;
      const ok = sendSysEx(
        [
          CMD_SET_PERF,
          mapsState.audioVoice,
          mapsState.attack,
          mapsState.decay,
          mapsState.sustain,
          mapsState.releaseAmp,
          mapsState.cutoff,
          mapsState.pwmWidth
        ],
        { quiet: quiet }
      );
      if (ok)
        setLiveEngStatus('Applied live', true);
      else
        setLiveEngStatus('Select card MIDI out', false);
    }

    function schedulePerfSend() {
      if (perfUiFromPanel) return;
      syncPerfFromLiveUI();
      if (perfSendTimer) clearTimeout(perfSendTimer);
      perfSendTimer = setTimeout(() => {
        perfSendTimer = 0;
        sendPerfToCard({ quiet: true });
      }, 40);
    }

    function syncMapsHeaderFromUI() {
      syncPerfFromLiveUI();
    }

    function extConfigToBytes(state) {
      const out = [
        state.marker & 0x7F,
        state.audioVoice & 0x7F,
        (state.attack || 0) & 0x7F,
        (state.decay || 0) & 0x7F,
        (state.sustain || 0) & 0x7F,
        (state.releaseAmp || 0) & 0x7F,
        (state.cutoff != null ? state.cutoff : 127) & 0x7F,
        (state.pwmWidth != null ? state.pwmWidth : 0) & 0x7F
      ];
      for (let i = 0; i < NUM_SLOTS; i++) {
        const s = state.slots[i];
        out.push(s.sourceType & 0x7F, s.channel & 0x7F, s.ccOrNote & 0x7F, (s.pad || 0) & 0x7F);
      }
      return out;
    }

    function bytesToExtConfig(bytes) {
      if (!bytes || bytes.length < 8 + NUM_SLOTS * 4) return null;
      if (bytes[0] !== EXT_MARKER) return null;
      const state = {
        marker: bytes[0],
        audioVoice: bytes[1],
        attack: bytes[2],
        decay: bytes[3],
        sustain: bytes[4],
        releaseAmp: bytes[5],
        cutoff: bytes[6],
        pwmWidth: bytes[7],
        slots: []
      };
      for (let i = 0; i < NUM_SLOTS; i++) {
        const o = 8 + i * 4;
        state.slots.push({
          sourceType: bytes[o],
          channel: bytes[o + 1],
          ccOrNote: bytes[o + 2],
          pad: bytes[o + 3]
        });
      }
      return state;
    }

    function fillChannelSelect(sel, selected) {
      sel.innerHTML = '';
      for (let i = 0; i < 16; i++) {
        const o = document.createElement('option');
        o.value = String(i);
        o.textContent = 'Channel ' + (i + 1);
        if (i === selected) o.selected = true;
        sel.appendChild(o);
      }
    }

    fillChannelSelect($('chA'), 0);
    fillChannelSelect($('chB'), 1);
    fillChannelSelect($('vkChannel'), 0);

    function setCardStatus(text, ok) {
      const el = $('cardStatus');
      el.textContent = text;
      el.className = 'status ' + (ok ? 'ok' : 'warn');
    }

    function setMapsStatus(text, ok) {
      const el = $('mapsStatus');
      if (!el) return;
      el.textContent = text;
      el.className = 'status ' + (ok ? 'ok' : 'warn');
    }

    function portLabel(port) {
      return (port.name || 'MIDI') + (port.manufacturer ? ' — ' + port.manufacturer : '');
    }

    // Prefer the Workshop Computer card: match name or manufacturer.
    function preferCardPort(ports) {
      const connected = ports.filter((p) => !p.state || p.state === 'connected');
      const hay = (p) =>
        ((p.name || '') + ' ' + (p.manufacturer || '')).toLowerCase();
      const ranked = [
        (p) => /music thing/i.test(hay(p)),
        (p) => /usb midi host/i.test(hay(p)),
        (p) => /mtmcomputer/i.test(hay(p)),
        (p) => /workshop/i.test(hay(p)),
      ];
      for (const test of ranked) {
        const hit = connected.find(test);
        if (hit) return hit;
      }
      return null;
    }

    // First non–Music Thing input (for controller relay), optionally skipping card in.
    function preferControllerPort(ports, excludeId) {
      const connected = ports.filter((p) => !p.state || p.state === 'connected');
      for (const p of connected) {
        if (excludeId && p.id === excludeId) continue;
        const hay = ((p.name || '') + ' ' + (p.manufacturer || '')).toLowerCase();
        if (hay.includes('music thing')) continue;
        return p;
      }
      return null;
    }

    function noteName(n) {
      const names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
      return names[n % 12] + (Math.floor(n / 12) - 1);
    }

    function refreshPortLists() {
      const outs = [...midiAccess.outputs.values()];
      const ins = [...midiAccess.inputs.values()];

      function fill(sel, ports, keepId) {
        const prev = keepId || sel.value;
        sel.innerHTML = '';
        const none = document.createElement('option');
        none.value = '';
        none.textContent = '(none)';
        sel.appendChild(none);
        for (const p of ports) {
          if (p.state && p.state !== 'connected') continue;
          const o = document.createElement('option');
          o.value = p.id;
          o.textContent = portLabel(p);
          sel.appendChild(o);
        }
        if (prev && [...sel.options].some(o => o.value === prev))
          sel.value = prev;
        else
          sel.value = '';
      }

      fill($('cardOut'), outs, cardOutput && cardOutput.id);
      fill($('cardIn'), ins, cardInput && cardInput.id);
      fill($('controllerIn'), ins, controllerInput && controllerInput.id);
      fill($('monitorIn'), ins, monitorInput && monitorInput.id);

      // Auto-pick Music Thing card ports when nothing selected yet.
      if (!$('cardOut').value) {
        const prefer = preferCardPort(outs);
        if (prefer) $('cardOut').value = prefer.id;
      }
      if (!$('cardIn').value) {
        const prefer = preferCardPort(ins);
        if (prefer) $('cardIn').value = prefer.id;
      }

      // Auto-pick first non–Music Thing controller and enable relay.
      if (!$('controllerIn').value) {
        const prefer = preferControllerPort(ins, $('cardIn').value);
        if (prefer) {
          $('controllerIn').value = prefer.id;
          if ($('relayEnable')) $('relayEnable').checked = true;
        }
      }

      // Inputs first so cardOutput bind sees current cardInput
      bindCardIn();
      bindCardOut();
      bindController();
      bindMonitor();

      updateRelayStatus();
    }

    function updateRelayStatus() {
      if (!$('relayStatus')) return;
      if (!$('relayEnable') || !$('relayEnable').checked) {
        setRelayStatus('Relay off', false);
        return;
      }
      if (!controllerInput)
        setRelayStatus('Pick a controller MIDI in', false);
      else if (!cardOutput)
        setRelayStatus('Pick card MIDI out', false);
      else
        setRelayStatus('Relaying ' + portLabel(controllerInput) + ' → card', true);
    }

    function setRelayStatus(text, ok) {
      const el = $('relayStatus');
      if (!el) return;
      el.textContent = text;
      el.className = 'status ' + (ok ? 'ok' : 'warn');
    }

    function vkPanic() {
      if (heldNotes.size === 0) return;
      if (cardOutput) {
        for (const [note, ch] of [...heldNotes]) {
          try {
            cardOutput.send([0x80 | (ch & 0x0F), note & 0x7F, 0]);
          } catch (_) { /* ignore */ }
        }
      }
      heldNotes.clear();
      document.querySelectorAll('.key.active').forEach(el => el.classList.remove('active'));
    }

    function resetSysexParser() {
      sysexActive = false;
      sysexBuf = [];
      sysexStartedAt = 0;
    }

    function setMidiHealth(visible, msg, recovering) {
      const bar = $('midiHealth');
      const text = $('midiHealthMsg');
      if (!bar) return;
      if (visible) {
        midiHealthDismissed = false;
        bar.hidden = false;
        bar.classList.add('is-visible');
        bar.classList.toggle('is-recovering', !!recovering);
        if (text && msg) text.textContent = msg;
        midiHealthVisibleUntil = performance.now() + (recovering ? 4000 : 20000);
      } else {
        bar.hidden = true;
        bar.classList.remove('is-visible', 'is-recovering');
      }
    }

    function noteMidiFault(reason) {
      midiFaultCount++;
      if (midiHealthDismissed && performance.now() < midiHealthVisibleUntil)
        return;
      setMidiHealth(
        true,
        'MIDI link glitch (' + reason + '). Panel/SysEx may be desynced — Resync recommended.',
        false
      );
      // Auto-resync at most once every 3s when faults pile up.
      if (midiFaultCount >= 2 && performance.now() - midiLastResyncAt > 3000)
        resyncMidiLink({ quiet: true, reason: reason });
    }

    /** Reset browser SysEx parser and re-enable panel stream on the card. */
    function resyncMidiLink(opts) {
      const quiet = !!(opts && opts.quiet);
      const reason = (opts && opts.reason) || 'manual';
      midiLastResyncAt = performance.now();
      midiFaultCount = 0;
      resetSysexParser();
      // Ask the card to stop then start stream so TinyUSB TX state settles.
      if (cardOutput) {
        sendSysEx([CMD_PANEL_STREAM, 0], { quiet: true });
        sendSysEx([CMD_PANEL_STREAM, 1], { quiet: true });
        sendSysEx([CMD_CARD_ID], { quiet: true });
      }
      panelStreaming = !!(cardOutput && cardInput);
      if (panelStreaming)
        panelStreamKey = cardOutput.id + '|' + cardInput.id;
      lastPanelAt = 0;
      setMidiHealth(
        true,
        'Resyncing MIDI link (' + reason + ')… waiting for panel telemetry.',
        true
      );
      setPanelStatus('Resyncing panel stream…', false);
      if (!quiet)
        setCardStatus('MIDI resync sent', true);
      document.querySelectorAll('.eng-knob').forEach((el) => el.classList.add('is-stale'));
      if ($('engStateHint'))
        $('engStateHint').textContent = 'Resyncing…';
    }

    function relayPanic() {
      if (!cardOutput || relayHeldNotes.size === 0) {
        relayHeldNotes.clear();
        return;
      }
      for (const key of [...relayHeldNotes]) {
        const [ch, note] = key.split(':').map(Number);
        try {
          cardOutput.send([0x80 | (ch & 0x0F), note & 0x7F, 0]);
        } catch (_) { /* ignore */ }
      }
      relayHeldNotes.clear();
    }

    function trackRelayNote(data) {
      const status = data[0] & 0xF0;
      const ch = data[0] & 0x0F;
      if (status === 0x90 && data.length >= 3) {
        const key = ch + ':' + data[1];
        if (data[2] > 0) relayHeldNotes.add(key);
        else relayHeldNotes.delete(key);
      } else if (status === 0x80 && data.length >= 2) {
        relayHeldNotes.delete(ch + ':' + data[1]);
      } else if (status === 0xB0 && data.length >= 3 &&
                 (data[1] === 120 || data[1] === 123)) {
        // Match exact channel ("1:60") — not prefix ("1:" matching "10:…")
        const chStr = String(ch);
        for (const key of [...relayHeldNotes]) {
          const colon = key.indexOf(':');
          if (colon >= 0 && key.slice(0, colon) === chStr)
            relayHeldNotes.delete(key);
        }
      }
    }

    function onControllerMidi(event) {
      if (!$('relayEnable') || !$('relayEnable').checked) return;
      if (!cardOutput || !controllerInput) return;
      if (cardInput && controllerInput.id === cardInput.id) return;
      const data = event.data;
      if (!data || data.length < 1) return;
      const status = data[0];
      if (status >= 0xF0) return; // no SysEx / clock / realtime
      // Pitch-bend floods (~1 kHz) used to stall device SysEx TX mid-message.
      // Cap bend relay rate; notes/CCs stay immediate.
      if ((status & 0xF0) === 0xE0) {
        const now = performance.now();
        if (now - relayLastBendAt < 8) return;
        relayLastBendAt = now;
      }
      try {
        cardOutput.send(data);
      } catch (err) {
        setRelayStatus('Send failed: ' + err, false);
        return;
      }
      trackRelayNote(data);
      relayMsgCount++;
      if ($('relayCount')) $('relayCount').textContent = String(relayMsgCount);
      if ($('relayLog') && $('relayLog').checked) {
        logTx(Array.from(data), 'Relay ' + fmtMidi(data));
      }
      if (setupModeActive && data.length >= 2) {
        const st = data[0] & 0xF0;
        const ch = (data[0] & 0x0F) + 1;
        if (st === 0xB0 && data.length >= 3)
          noteSetupMidiHeard('CC', data[1] + ' value ' + data[2] + ' on ch ' + ch);
        else if (st === 0x90 && data.length >= 3 && data[2] > 0)
          noteSetupMidiHeard('Note', data[1] + ' vel ' + data[2] + ' on ch ' + ch);
      }
    }

    function sendPanelStreamOff(port) {
      if (!port) return;
      try {
        port.send([0xF0, MFR, DEVICE, CMD_PANEL_STREAM, 0, 0xF7]);
      } catch (_) { /* ignore */ }
    }

    function bindController() {
      const prev = controllerInput;
      if (controllerInput && controllerHandler) {
        controllerInput.removeEventListener('midimessage', controllerHandler);
      }
      const id = $('controllerIn').value;
      const next = id && midiAccess ? midiAccess.inputs.get(id) : null;
      const connected = (next && next.state === 'connected') ? next : null;

      if (prev && prev !== connected) {
        if ($('relayEnable') && $('relayEnable').checked)
          relayPanic();
      }

      if (connected) {
        if (cardInput && connected.id === cardInput.id) {
          if ($('relayEnable') && $('relayEnable').checked)
            relayPanic();
          setRelayStatus('Controller can’t be the card port', false);
          $('controllerIn').value = '';
          controllerInput = null;
          controllerHandler = null;
          updateRelayStatus();
          return;
        }
        controllerInput = connected;
        controllerHandler = onControllerMidi;
        controllerInput.addEventListener('midimessage', controllerHandler);
      } else {
        controllerInput = null;
        controllerHandler = null;
      }
      updateRelayStatus();
    }

    function bindCardOut() {
      const prev = cardOutput;
      const id = $('cardOut').value;
      const next = id && midiAccess ? midiAccess.outputs.get(id) : null;
      const connected = next && next.state === 'connected' ? next : null;
      if (prev && prev !== connected) {
        relayPanic();
        vkPanic();
        sendPanelStreamOff(prev);
        panelStreaming = false;
        panelStreamKey = '';
        lastPanelAt = 0;
        stopProfilePoll();
      }
      cardOutput = connected;
      if (cardOutput && cardInput) startPanelStream({ force: !lastPanelAt });
      else if (!cardOutput) {
        panelStreaming = false;
        panelStreamKey = '';
        lastPanelAt = 0;
        stopProfilePoll();
        setPanelStatus('Panel stream off', false);
      }
      updateRelayStatus();
    }

    function bindCardIn() {
      const prev = cardInput;
      if (cardInput) {
        cardInput.removeEventListener('midimessage', onCardMidiMessage);
      }
      const id = $('cardIn').value;
      const next = id && midiAccess ? midiAccess.inputs.get(id) : null;
      cardInput = (next && next.state === 'connected') ? next : null;
      if (prev !== cardInput) {
        resetSysexParser();
        if (!cardInput || (prev && cardInput && prev.id !== cardInput.id))
          lastPanelAt = 0;
      }
      if (cardInput)
        cardInput.addEventListener('midimessage', onCardMidiMessage);
      if (cardOutput && cardInput) startPanelStream({ force: !lastPanelAt });
      else stopPanelStream(!!cardOutput);
      if (controllerInput && cardInput && controllerInput.id === cardInput.id) {
        $('controllerIn').value = '';
        bindController();
      }
      updateRelayStatus();
    }

    function bindMonitor() {
      if (monitorInput && monitorHandler) {
        monitorInput.removeEventListener('midimessage', monitorHandler);
      }
      const id = $('monitorIn').value;
      const next = id && midiAccess ? midiAccess.inputs.get(id) : null;
      monitorInput = (next && next.state === 'connected') ? next : null;
      if (monitorInput) {
        monitorHandler = (e) => logMonitor(e.data);
        monitorInput.addEventListener('midimessage', monitorHandler);
      } else {
        monitorHandler = null;
      }
    }

    function logTx(bytes, detail) {
      const log = $('midiTxLog');
      if (!log) return;
      const t = new Date().toISOString().slice(11, 23);
      const rawOn = !$('txLogRaw') || $('txLogRaw').checked;
      const hex = Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join(' ');
      let line = t + '  → ' + detail;
      if (rawOn) line += '\n         [' + hex + ']';
      log.textContent += line + '\n';
      log.scrollTop = log.scrollHeight;
      const lines = log.textContent.split('\n');
      if (lines.length > 400)
        log.textContent = lines.slice(-400).join('\n');
    }

    function sendSysEx(payload, opts) {
      if (!cardOutput) {
        setCardStatus('No card MIDI out selected', false);
        return false;
      }
      const quiet = opts && opts.quiet;
      const msg = [0xF0, MFR, DEVICE, ...payload, 0xF7];
      try {
        cardOutput.send(msg);
        if (!quiet) {
          const names = {
            1: 'Preview', 2: 'Save flash', 3: 'Read config', 4: 'Card ID',
            5: 'Panel snapshot', 6: 'Panel stream',
            7: 'Read maps', 8: 'Write maps', 9: 'Set engine',
            0x0A: 'Learn notify', 0x0B: 'Read profile'
          };
          logTx(msg, 'SysEx ' + (names[payload[0]] || ('cmd 0x' + payload[0].toString(16))));
        }
        return true;
      } catch (err) {
        setCardStatus('MIDI send failed: ' + err, false);
        return false;
      }
    }

    function startPanelStream(opts) {
      if (!cardOutput || !cardInput) return false;
      const force = !!(opts && opts.force);
      const key = cardOutput.id + '|' + cardInput.id;
      // Always (re)enable when forced, or when we have never received a packet
      // for this port pair — otherwise a premature enable during USB settle
      // leaves panelStreaming true and never retries.
      if (!force && panelStreaming && panelStreamKey === key && lastPanelAt)
        return true;
      panelStreaming = true;
      panelStreamKey = key;
      const ok = sendSysEx([CMD_PANEL_STREAM, 1], { quiet: true });
      if (ok)
        startProfilePoll();
      if (!lastPanelAt)
        setPanelStatus('Listening for panel…', false);
      return ok;
    }

    function stopPanelStream(sendOff) {
      panelStreaming = false;
      panelStreamKey = '';
      lastPanelAt = 0;
      stopProfilePoll();
      if (sendOff && cardOutput)
        sendSysEx([CMD_PANEL_STREAM, 0], { quiet: true });
      setPanelStatus('Panel stream off', false);
    }

    /** Identify + force panel stream — used by Connect / Identify buttons. */
    function connectCard(opts) {
      const quiet = !(opts && opts.loud);
      if (!cardOutput || !cardInput) {
        setPanelStatus('Pick card MIDI in + out in Settings', false);
        setCardStatus('Open Settings and select the Workshop Computer ports', false);
        return false;
      }
      sendSysEx([CMD_CARD_ID], { quiet: quiet });
      startPanelStream({ force: true });
      sendSysEx([CMD_READ_MAPS], { quiet: true });
      return true;
    }

    function setPanelStatus(text, live) {
      const el = $('panelStatus');
      const textEl = $('panelStatusText');
      const dot = $('panelLiveDot');
      if (textEl) textEl.textContent = text;
      else if (el) el.textContent = text;
      if (dot) dot.classList.toggle('on', !!live);
      if (el) el.className = 'status ' + (live ? 'ok' : 'warn');
    }

    function setProfileDisplay(peakUs, budgetUs, overrun) {
      profilePeakUs = peakUs;
      profileBudgetUs = budgetUs;
      profileOverrun = overrun;
      const el = $('profileStatus');
      const peakEl = $('profilePeak');
      const overrunEl = $('profileOverrun');
      if (!el || !peakEl) return;
      peakEl.textContent = String(peakUs);
      if (overrunEl) overrunEl.hidden = !overrun;
      let cls = 'status ';
      if (overrun || peakUs > budgetUs)
        cls += 'warn';
      else if (peakUs > Math.floor(budgetUs * 0.75))
        cls += 'warn';
      else
        cls += 'ok';
      el.className = cls;
      el.title =
        'ProcessSample peak over ~170 ms (budget ' + budgetUs + ' µs)' +
        (overrun ? ' — overrun latched on card' : '');
    }

    function resetProfileDisplay() {
      profilePeakUs = 0;
      profileOverrun = false;
      const peakEl = $('profilePeak');
      const overrunEl = $('profileOverrun');
      const el = $('profileStatus');
      if (peakEl) peakEl.textContent = '—';
      if (overrunEl) overrunEl.hidden = true;
      if (el) {
        el.className = 'status';
        el.title = 'ProcessSample peak over ~170 ms (target < 20 µs)';
      }
    }

    function requestProfileSample() {
      if (!cardOutput) return;
      sendSysEx([CMD_READ_PROFILE], { quiet: true });
    }

    function startProfilePoll() {
      stopProfilePoll();
      requestProfileSample();
      profilePollTimer = setInterval(requestProfileSample, 500);
    }

    function stopProfilePoll() {
      if (profilePollTimer) {
        clearInterval(profilePollTimer);
        profilePollTimer = 0;
      }
      resetProfileDisplay();
    }

    function knobAngle(v) {
      // Speedometer: 0% → SW (−135°), 50% → north, 100% → SE (+135°).
      // Artwork pointers point north at rotate(0).
      return -135 + (Math.max(0, Math.min(4095, v)) / 4095) * 270;
    }

    // Centres from Standalone_computer_rev1.svg (Music Thing Modular)
    const PANEL_PIVOTS = {
      main: { id: 'hw-main', cx: 56.2031, cy: 54.847 },
      x: { id: 'hw-x', cx: 17.9617, cy: 118.7145 },
      y: { id: 'hw-y', cx: 55.6915, cy: 118.431 },
      sw: { id: 'hw-switch-lever', cx: 93.2929, cy: 118.3302 },
    };

    let panelSvgRoot = null;

    function mountOfficialPanel() {
      const host = $('wcPanelHost');
      panelSvgRoot = host ? host.querySelector('svg') : null;
      // Rest pose until telemetry arrives: all knobs at 0% (SW).
      if (panelSvgRoot) {
        setSvgRotate('main', knobAngle(0));
        setSvgRotate('x', knobAngle(0));
        setSvgRotate('y', knobAngle(0));
      }
    }

    function setSvgRotate(key, degrees) {
      if (!panelSvgRoot) mountOfficialPanel();
      if (!panelSvgRoot) return;
      const p = PANEL_PIVOTS[key];
      if (!p) return;
      const el =
        (typeof panelSvgRoot.getElementById === 'function' &&
          panelSvgRoot.getElementById(p.id)) ||
        panelSvgRoot.querySelector('#' + p.id) ||
        document.getElementById(p.id);
      if (!el) return;
      el.setAttribute(
        'transform',
        'rotate(' + degrees.toFixed(2) + ' ' + p.cx + ' ' + p.cy + ')'
      );
    }

    function midiToKnobAngle(v07, vmax) {
      const max = vmax > 0 ? vmax : 127;
      const t = Math.max(0, Math.min(max, v07 | 0)) / max;
      return -135 + t * 270;
    }

    function setEngKnob(dialId, valId, value, label, vmax) {
      const dial = $(dialId);
      const valEl = $(valId);
      if (dial)
        dial.style.setProperty('--angle', midiToKnobAngle(value, vmax) + 'deg');
      if (valEl) valEl.textContent = label;
    }

    function applyEngineState(ext) {
      if (!ext) return;
      const pill = $('engModePill');
      if (pill) {
        const setup = ext.mode === 1;
        pill.textContent = setup ? 'SETUP' : 'PLAY';
        pill.classList.toggle('setup', setup);
      }
      const atk = ext.attack != null ? ext.attack : 0;
      const dcy = ext.decay != null ? ext.decay : 0;
      const sus = ext.sustain != null ? ext.sustain : 0;
      const rel = ext.releaseAmp != null ? ext.releaseAmp : 0;
      const ctf = ext.cutoff != null ? ext.cutoff : 0;
      const pwm = ext.pwmWidth != null ? ext.pwmWidth : 0;

      setEngKnob('engDialAtk', 'engValAtk', atk, String(atk), 127);
      setEngKnob('engDialDcy', 'engValDcy', dcy, String(dcy), 127);
      setEngKnob('engDialSus', 'engValSus', sus, String(sus), 127);
      setEngKnob('engDialRel', 'engValRel', rel, String(rel), 127);
      setEngKnob('engDialCtf', 'engValCtf', ctf, String(ctf), 127);
      setEngKnob('engDialPwm', 'engValPwm', pwm, String(pwm), 127);

      const voice = voiceNameFromId(ext.voice != null ? ext.voice : 0);
      if ($('engStateHint')) {
        let hint = 'Live from card · ' + voice;
        if (ext.cvCalibrated === false)
          hint += ' · CV pitch not calibrated (EEPROM)';
        $('engStateHint').textContent = hint;
      }
      // Mark knobs fresh
      document.querySelectorAll('.eng-knob').forEach((el) => el.classList.remove('is-stale'));
    }

    function applyPanelState(main, x, y, sw, ext) {
      lastPanelAt = performance.now();
      midiFaultCount = 0;
      // Clear recovering banner once live packets resume.
      const bar = $('midiHealth');
      if (bar && bar.classList.contains('is-recovering'))
        setMidiHealth(false);
      try {
        const pct = (v) => Math.round((v / 4095) * 100) + '%';
        const inSetup = ext && ext.mode === 1;
        if ($('valMain')) {
          if (inSetup) {
            const slot = ext.slot != null ? ext.slot : 0;
            const name = SLOT_NAMES[slot] || ('slot ' + slot);
            $('valMain').textContent = 'Slot ' + slot + ' — ' + name;
            if ($('roleMain')) $('roleMain').textContent = 'SETUP learn slot (Main)';
          } else {
            $('valMain').textContent = main + '  (' + pct(main) + ')';
            if ($('roleMain')) $('roleMain').textContent = 'Unused in PLAY (SETUP slot select)';
          }
        }
        if ($('valX')) $('valX').textContent = x + '  (' + pct(x) + ')';
        if ($('valY')) $('valY').textContent = y + '  (' + pct(y) + ')';
        if ($('valSwitch')) $('valSwitch').textContent = SWITCH_NAMES[sw] || String(sw);

        // Quantize SVG angles so ADC noise doesn't vibrate the graphic
        const q = (v) => Math.round(v / 32) * 32;
        setSvgRotate('main', knobAngle(q(main)));
        setSvgRotate('x', knobAngle(q(x)));
        setSvgRotate('y', knobAngle(q(y)));
        const swDeg = sw === 2 ? 0 : (sw === 1 ? 90 : 180);
        setSvgRotate('sw', swDeg);

        if ($('swUp')) $('swUp').classList.toggle('on', sw === 2);
        if ($('swMid')) $('swMid').classList.toggle('on', sw === 1);
        if ($('swDown')) $('swDown').classList.toggle('on', sw === 0);

        setPanelStatus('Live — hardware Main / X / Y / Z', true);
      } catch (err) {
        setPanelStatus('Panel UI error: ' + err, false);
      }

      // Engine state is independent so a panel graphic glitch can't block it.
      try {
        if (ext) applyEngineState(ext);
        updateSetupMonitor(ext || { mode: 0 });
      } catch (err) {
        if ($('engStateHint'))
          $('engStateHint').textContent = 'Engine monitor error: ' + err;
      }
    }

    function configBytesFromUI() {
      const bend = Math.max(1, Math.min(12, parseInt($('bend').value, 10) || 2));
      const flags = $('activityLed').checked ? 1 : 0;
      return [
        parseInt($('chA').value, 10),
        parseInt($('chB').value, 10),
        bend,
        flags,
        0, 0, 0,
        0x4D
      ];
    }

    function applyConfigToUI(bytes) {
      if (!bytes || bytes.length < CONFIG_LEN) return;
      const chA = bytes[0] & 0x0F;
      $('chA').value = String(chA);
      $('chB').value = String(bytes[1] & 0x0F);
      setVkChannel(chA);
      $('bend').value = String(Math.max(1, Math.min(12, bytes[2] || 2)));
      $('activityLed').checked = (bytes[3] & 1) !== 0;
    }

    function processIncomingSysEx(data) {
      if (!data.length) return;
      const cmd = data[0];
      if (cmd === CMD_CARD_ID && data.length >= 5) {
        $('fwStatus').textContent =
          'Firmware ' + data[2] + '.' + data[3] + '.' + data[4] +
          ' (device ' + data[1] + ')';
        setCardStatus('Card identified', true);
        startPanelStream({ force: true });
      } else if (cmd === CMD_READ && data.length >= 1 + CONFIG_LEN) {
        applyConfigToUI(data.slice(1, 1 + CONFIG_LEN));
        setCardStatus('Config loaded from card', true);
      } else if (cmd === CMD_SAVE && data.length >= 2) {
        setCardStatus('Config saved to flash', true);
      } else if (cmd === CMD_PANEL_STATE) {
        // Accept known payload sizes only — garbage after a desync used to
        // thrash the front-panel / engine knobs.
        const okLen =
          data.length === 8 || data.length === 11 || data.length === 15 ||
          data.length === 17 || data.length === 18 || data.length === 19;
        if (!okLen) {
          noteMidiFault('bad panel packet len ' + data.length);
          return;
        }
        const main = ((data[1] & 0x7F) << 7) | (data[2] & 0x7F);
        const x = ((data[3] & 0x7F) << 7) | (data[4] & 0x7F);
        const y = ((data[5] & 0x7F) << 7) | (data[6] & 0x7F);
        if (main > 4095 || x > 4095 || y > 4095) {
          noteMidiFault('panel ADC out of range');
          return;
        }
        const sw = data[7] & 0x03;
        let ext = null;
        if (data.length >= 11) {
          ext = {
            mode: data[8] & 0x7F,
            slot: data[9] & 0x7F,
            voice: data[10] & 0x7F
          };
          if (data.length >= 15) {
            ext.attack = data[11] & 0x7F;
            ext.decay = data[12] & 0x7F;
            ext.sustain = data[13] & 0x7F;
            ext.releaseAmp = data[14] & 0x7F;
          }
          if (data.length >= 17) {
            ext.cutoff = data[15] & 0x7F;
            ext.pwmWidth = data[16] & 0x7F;
          }
          if (data.length >= 18)
            ext.slotPending = data[17] & 0x7F;
          if (data.length >= 19)
            ext.cvCalibrated = (data[18] & 0x7F) !== 0;
        }
        applyPanelState(main, x, y, sw, ext);
      } else if (cmd === CMD_READ_PROFILE && data.length >= 6) {
        const peak = ((data[1] & 0x7F) << 7) | (data[2] & 0x7F);
        const overrun = (data[3] & 0x7F) !== 0;
        const budget = ((data[4] & 0x7F) << 7) | (data[5] & 0x7F);
        setProfileDisplay(peak, budget || 20, overrun);
      } else if (cmd === CMD_LEARN_NOTIFY && data.length >= 5) {
        applyLearnNotify(
          data[1] & 0x7F,
          data[2] & 0x7F,
          data[3] & 0x7F,
          data[4] & 0x7F
        );
      } else if (cmd === CMD_READ_MAPS) {
        const parsed = bytesToExtConfig(data.slice(1));
        if (parsed) {
          mapsState = parsed;
          renderMapsTable({ syncLive: false });
          setMapsStatus('Maps loaded from card (Live engine unchanged)', true);
          if (setupModeActive && $('setupMapValue'))
            $('setupMapValue').textContent =
              formatMapSource(mapsState.slots[setupSlotActive]);
        } else {
          setMapsStatus('Invalid maps payload', false);
        }
      } else if (cmd === CMD_WRITE_MAPS && data.length >= 2) {
        setMapsStatus('Maps saved to flash', true);
        setCardStatus('Maps saved to flash', true);
      }
    }

    function onCardMidiMessage(event) {
      const data = event.data;
      for (let i = 0; i < data.length; i++) {
        const b = data[i];
        if (b === 0xF0) {
          if (sysexActive)
            noteMidiFault('nested SysEx start');
          sysexActive = true;
          sysexBuf = [b];
          sysexStartedAt = performance.now();
          continue;
        }
        if (b >= 0xF8) continue; // realtime — ignore while buffering
        if (!sysexActive) continue;
        // Other status bytes abort a wedged/incomplete SysEx
        if (b >= 0x80 && b !== 0xF7) {
          noteMidiFault('SysEx aborted by status 0x' + b.toString(16));
          resetSysexParser();
          continue;
        }
        sysexBuf.push(b);
        if (b === 0xF7) {
          if (sysexBuf.length >= 5 &&
              sysexBuf[1] === MFR &&
              sysexBuf[2] === DEVICE) {
            try {
              processIncomingSysEx(sysexBuf.slice(3, -1));
            } catch (err) {
              console.error('SysEx handler error:', err);
              noteMidiFault('handler error');
              setCardStatus('SysEx handler error: ' + err, false);
            }
          } else if (sysexBuf.length >= 3) {
            noteMidiFault('foreign/corrupt SysEx');
          }
          resetSysexParser();
        } else if (sysexBuf.length > 256) {
          noteMidiFault('SysEx overflow');
          resetSysexParser();
        }
      }
    }

    function sendNote(note, velocity, on, chOverride) {
      if (!cardOutput) {
        $('vkHint').textContent = 'Select card MIDI out first.';
        $('vkHint').className = 'status warn';
        return false;
      }
      const ch = (chOverride != null
        ? chOverride
        : parseInt($('vkChannel').value, 10)) & 0x0F;
      const status = (on ? 0x90 : 0x80) | ch;
      const msg = [status, note & 0x7F, velocity & 0x7F];
      try {
        cardOutput.send(msg);
        logTx(
          msg,
          (on ? 'Note On' : 'Note Off') +
            ` ch${ch + 1} note=${note} (${noteName(note)}) vel=${velocity}`
        );
        return true;
      } catch (err) {
        setCardStatus('MIDI send failed: ' + err, false);
        return false;
      }
    }

    function buildKeyboard() {
      const root = $('keyboard');
      root.innerHTML = '';
      const low = 24, high = 72;
      const isWhite = (n) => {
        const pc = n % 12;
        return pc === 0 || pc === 2 || pc === 4 || pc === 5 ||
               pc === 7 || pc === 9 || pc === 11;
      };
      const whiteByNote = new Map();
      for (let n = low; n <= high; n++) {
        if (!isWhite(n)) continue;
        const el = document.createElement('div');
        el.className = 'key';
        el.dataset.note = String(n);
        if (n % 12 === 0) {
          el.textContent = noteName(n);
          if (n === 48) el.classList.add('c3-mark');
        }
        wireKey(el);
        root.appendChild(el);
        whiteByNote.set(n, el);
      }
      requestAnimationFrame(() => {
        for (let n = low; n <= high; n++) {
          if (isWhite(n)) continue;
          const leftWhite = whiteByNote.get(n - 1);
          if (!leftWhite) continue;
          const el = document.createElement('div');
          el.className = 'key black';
          el.dataset.note = String(n);
          el.style.left =
            (leftWhite.offsetLeft + leftWhite.offsetWidth - 7) + 'px';
          wireKey(el);
          root.appendChild(el);
        }
      });
    }

    function wireKey(el) {
      const noteOf = () => parseInt(el.dataset.note, 10);
      const down = (e) => {
        e.preventDefault();
        try { el.setPointerCapture(e.pointerId); } catch (_) { /* ignore */ }
        const note = noteOf();
        if (heldNotes.has(note)) return;
        const ch = parseInt($('vkChannel').value, 10) & 0x0F;
        if (!sendNote(note, 100, true, ch)) return;
        heldNotes.set(note, ch);
        el.classList.add('active');
        $('vkNote').textContent = `MIDI ${note} (${noteName(note)})`;
      };
      const up = (e) => {
        e.preventDefault();
        const note = noteOf();
        if (!heldNotes.has(note)) return;
        const ch = heldNotes.get(note);
        heldNotes.delete(note);
        el.classList.remove('active');
        sendNote(note, 0, false, ch);
      };
      el.addEventListener('pointerdown', down);
      el.addEventListener('pointerup', up);
      el.addEventListener('pointercancel', up);
      el.addEventListener('lostpointercapture', up);
    }

    function fmtMidi(data) {
      if (!data || !data.length) return '';
      const status = data[0];
      const hi = status & 0xF0;
      const ch = (status & 0x0F) + 1;
      if (hi === 0x90 && data[2] > 0)
        return `Ch${ch} Note On  ${data[1]} (${noteName(data[1])}) vel ${data[2]}`;
      if (hi === 0x80 || (hi === 0x90 && data[2] === 0))
        return `Ch${ch} Note Off ${data[1]}`;
      if (hi === 0xB0)
        return `Ch${ch} CC ${data[1]} = ${data[2]}`;
      if (hi === 0xE0) {
        const bend = data[1] | (data[2] << 7);
        return `Ch${ch} Pitch bend ${bend} (${bend - 8192})`;
      }
      if (status === 0xF0)
        return `SysEx (${data.length} bytes)`;
      return Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' ');
    }

    function logMonitor(data) {
      const log = $('monitorLog');
      const line = new Date().toISOString().slice(11, 23) + '  ' + fmtMidi(data) + '\n';
      log.textContent += line;
      log.scrollTop = log.scrollHeight;
      const lines = log.textContent.split('\n');
      if (lines.length > 200)
        log.textContent = lines.slice(-200).join('\n');
    }

    function setVkChannel(ch) {
      vkPanic();
      $('vkChannel').value = String(ch & 0x0F);
    }

    $('cardOut').addEventListener('change', bindCardOut);
    $('cardIn').addEventListener('change', bindCardIn);
    $('controllerIn').addEventListener('change', bindController);
    $('relayEnable').addEventListener('change', () => {
      if (!$('relayEnable').checked) relayPanic();
      updateRelayStatus();
    });
    $('monitorIn').addEventListener('change', bindMonitor);
    $('chA').addEventListener('change', () => {
      setVkChannel(parseInt($('chA').value, 10) || 0);
    });
    $('vkChannel').addEventListener('change', () => {
      // Panic notes that were sent on the previous channel
      vkPanic();
    });

    $('btnIdentify').addEventListener('click', () => connectCard({ loud: true }));
    if ($('btnPanelConnect'))
      $('btnPanelConnect').addEventListener('click', () => connectCard({ loud: true }));
    function wireResync(btn) {
      if (btn) btn.addEventListener('click', () => resyncMidiLink({ quiet: false, reason: 'manual' }));
    }
    wireResync($('btnPanelResync'));
    wireResync($('btnMidiResync'));
    if ($('btnMidiHealthDismiss'))
      $('btnMidiHealthDismiss').addEventListener('click', () => {
        midiHealthDismissed = true;
        setMidiHealth(false);
      });
    $('btnRead').addEventListener('click', () => sendSysEx([CMD_READ]));
    $('btnPreview').addEventListener('click', () => {
      sendSysEx([CMD_PREVIEW, ...configBytesFromUI()]);
      setCardStatus('Preview sent (RAM only)', true);
    });
    $('btnSave').addEventListener('click', () => {
      sendSysEx([CMD_SAVE, ...configBytesFromUI()]);
      setCardStatus('Save to flash sent', true);
    });
    $('btnReadMaps').addEventListener('click', () => {
      sendSysEx([CMD_READ_MAPS]);
      setMapsStatus('Read maps sent…', true);
    });
    $('btnWriteMaps').addEventListener('click', () => {
      syncMapsHeaderFromUI();
      sendSysEx([CMD_WRITE_MAPS, ...extConfigToBytes(mapsState)]);
      setMapsStatus('Write maps sent…', true);
    });
    $('btnMapsDefaults').addEventListener('click', () => {
      mapsState = factoryMapsState();
      renderMapsTable();
      sendPerfToCard({ quiet: true });
      setMapsStatus('UI reset to factory maps (not written yet)', true);
    });
    initVoiceMatrixUi();
    applyPerfToLiveUI(mapsState);
    if ($('liveVoiceRow')) {
      $('liveVoiceRow').addEventListener('change', () => {
        syncLiveVoiceUiFromCc(voiceCcFromRowCol(
          parseInt($('liveVoiceRow').value, 10) || 0,
          parseInt($('liveVoiceCol').value, 10) || 0));
        sendPerfToCard({ quiet: false });
      });
    }
    if ($('liveVoiceCol')) {
      $('liveVoiceCol').addEventListener('change', () => {
        syncLiveVoiceUiFromCc(voiceCcFromRowCol(
          parseInt($('liveVoiceRow').value, 10) || 0,
          parseInt($('liveVoiceCol').value, 10) || 0));
        sendPerfToCard({ quiet: false });
      });
    }
    if ($('liveVoiceCc')) {
      $('liveVoiceCc').addEventListener('change', () => {
        syncLiveVoiceUiFromCc(parseInt($('liveVoiceCc').value, 10) || 0);
        sendPerfToCard({ quiet: false });
      });
    }
    for (const id of ['liveAttack', 'liveDecay', 'liveSustain', 'liveRelease', 'liveCutoff', 'livePwm']) {
      $(id).addEventListener('input', schedulePerfSend);
      $(id).addEventListener('change', () => sendPerfToCard({ quiet: true }));
    }
    $('btnClearLog').addEventListener('click', () => { $('monitorLog').textContent = ''; });
    $('btnClearTxLog').addEventListener('click', () => { $('midiTxLog').textContent = ''; });
    if ($('btnClearSetupLog'))
      $('btnClearSetupLog').addEventListener('click', () => {
        if ($('setupLearnLog')) $('setupLearnLog').textContent = '';
      });

    window.addEventListener('beforeunload', () => {
      relayPanic();
      vkPanic();
      stopPanelStream(true);
    });
    window.addEventListener('pagehide', () => {
      relayPanic();
      vkPanic();
      stopPanelStream(true);
    });
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') {
        relayPanic();
        vkPanic();
      }
    });

    setInterval(() => {
      if (!cardOutput || !cardInput) return;

      // Stuck open SysEx (no F7) — classic mid-message desync symptom.
      if (sysexActive && sysexStartedAt &&
          performance.now() - sysexStartedAt > 120) {
        noteMidiFault('SysEx timeout (no F7)');
        resetSysexParser();
      }

      if (!panelStreaming) return;
      const age = lastPanelAt ? performance.now() - lastPanelAt : Infinity;
      if (!lastPanelAt || age > 2000) {
        setPanelStatus(
          lastPanelAt
            ? 'No panel data — link may be wedged'
            : 'Waiting for panel — click Connect / Resync',
          false
        );
        document.querySelectorAll('.eng-knob').forEach((el) => el.classList.add('is-stale'));
        if ($('engStateHint'))
          $('engStateHint').textContent = lastPanelAt
            ? 'Stale — no panel packets for 2s'
            : 'Waiting for panel stream…';
        if (lastPanelAt && age > 2500)
          noteMidiFault('panel stream stalled');
        else
          startPanelStream({ force: true });
      }

      // Auto-hide recovering banner after grace period with no new faults.
      const bar = $('midiHealth');
      if (bar && bar.classList.contains('is-visible') &&
          !bar.classList.contains('is-recovering') &&
          midiHealthVisibleUntil && performance.now() > midiHealthVisibleUntil &&
          midiFaultCount === 0) {
        setMidiHealth(false);
      }
    }, 500);

    renderMapsTable();
    buildKeyboard();
    mountOfficialPanel();

    async function init() {
      if (!navigator.requestMIDIAccess) {
        setCardStatus('WebMIDI not supported — use Chrome or Edge', false);
        return;
      }
      try {
        midiAccess = await navigator.requestMIDIAccess({ sysex: true });
        setCardStatus('WebMIDI ready — select the Workshop Computer ports', true);
        refreshPortLists();
        midiAccess.addEventListener('statechange', refreshPortLists);
        // After ports auto-bind, Identify so firmware/version status fills in.
        if (cardOutput && cardInput)
          connectCard({ loud: false });
      } catch (err) {
        setCardStatus('WebMIDI denied: ' + err, false);
      }
    }

    /* ---- App shell: tab switching + settings modal ---- */
    (function appShell() {
      const tabButtons = Array.from(document.querySelectorAll('.tab-btn[data-tab]'));
      const tabPanels = Array.from(document.querySelectorAll('.tab-panel[data-tab-panel]'));

      function selectTab(name) {
        tabButtons.forEach((btn) => {
          const on = btn.dataset.tab === name;
          btn.classList.toggle('active', on);
          btn.setAttribute('aria-selected', on ? 'true' : 'false');
        });
        tabPanels.forEach((p) => p.classList.toggle('active', p.dataset.tabPanel === name));
      }
      tabButtons.forEach((btn) => {
        btn.addEventListener('click', () => selectTab(btn.dataset.tab));
      });

      const modal = $('settingsModal');
      const openBtn = $('btnSettings');
      const closeBtn = $('btnSettingsClose');

      function setModal(open) {
        if (!modal) return;
        modal.classList.toggle('open', open);
        if (open && closeBtn) closeBtn.focus();
        else if (!open && openBtn) openBtn.focus();
      }
      if (openBtn) openBtn.addEventListener('click', () => setModal(true));
      if (closeBtn) closeBtn.addEventListener('click', () => setModal(false));
      if (modal) {
        modal.addEventListener('click', (ev) => {
          if (ev.target.dataset.modalClose !== undefined) setModal(false);
        });
      }
      document.addEventListener('keydown', (ev) => {
        if (ev.key === 'Escape' && modal && modal.classList.contains('open')) setModal(false);
      });
    })();

    init();
