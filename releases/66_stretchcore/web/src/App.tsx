import {
  ArrowDown,
  ArrowUp,
  BookOpen,
  Cable,
  Download,
  Eraser,
  FileDown,
  FolderOpen,
  HelpCircle,
  Pause,
  Play,
  Plus,
  Scissors,
  Moon,
  Sun,
  Terminal,
  Trash2,
  Upload,
  X,
} from 'lucide-react';
import { driver, type Driver } from 'driver.js';
import 'driver.js/dist/driver.css';
import { MouseEvent, useEffect, useMemo, useRef, useState } from 'react';
import { decodeAndEncodeFile, makePreviewBuffer } from './audio';
import {
  BANK_SAMPLE_RATE,
  BANK_MAX_SAMPLES,
  BankSample,
  buildBankBlob,
  croppedPcm,
  parseBankBlob,
  usedAudioBytes,
} from './bank';
import { DeviceInfo, StretchcoreSerial } from './serial';

const serial = new StretchcoreSerial();
const stretchcoreImageUrl = `${import.meta.env.BASE_URL}stretchcore.png`;
const stretchcoreUf2Url = `${import.meta.env.BASE_URL}stretchcore.uf2`;

type StatusKind = 'idle' | 'good' | 'warn' | 'bad';
type Theme = 'light' | 'dark';

interface Status {
  text: string;
  kind: StatusKind;
}

interface TransferState {
  label: string;
  ratio: number;
  etaSeconds: number | null;
}

export function App() {
  const [samples, setSamples] = useState<BankSample[]>([]);
  const [device, setDevice] = useState<DeviceInfo | null>(null);
  const [connected, setConnected] = useState(false);
  const [bankDirty, setBankDirty] = useState(false);
  const [status, setStatus] = useState<Status>({ text: 'Disconnected', kind: 'idle' });
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState(0);
  const [transfer, setTransfer] = useState<TransferState | null>(null);
  const [playingId, setPlayingId] = useState<string | null>(null);
  const [playheadFrame, setPlayheadFrame] = useState(0);
  const [debugLog, setDebugLog] = useState<string[]>([]);
  const [debugOpen, setDebugOpen] = useState(false);
  const [imageOpen, setImageOpen] = useState(false);
  const [theme, setTheme] = useState<Theme>(() => loadTheme());
  const debugOpenRef = useRef(false);
  const debugEntriesRef = useRef<string[]>([]);
  const debugFlushTimerRef = useRef<number | null>(null);
  const debugInteractionRef = useRef(false);
  const transferStartRef = useRef(0);
  const audioRef = useRef<{
    context: AudioContext;
    source: AudioBufferSourceNode;
    startedAt: number;
    frameCount: number;
  } | null>(null);

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    window.localStorage.setItem('stretchcore-theme', theme);
  }, [theme]);

  useEffect(() => {
    debugOpenRef.current = debugOpen;
    if (debugOpen) flushDebugLog(true);
  }, [debugOpen]);

  useEffect(() => {
    const closeSerial = () => {
      void serial.disconnect();
    };
    window.addEventListener('pagehide', closeSerial);
    window.addEventListener('beforeunload', closeSerial);
    return () => {
      window.removeEventListener('pagehide', closeSerial);
      window.removeEventListener('beforeunload', closeSerial);
      void serial.disconnect();
    };
  }, []);

  useEffect(() => {
    if (!imageOpen) return;
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setImageOpen(false);
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [imageOpen]);

  useEffect(() => {
    if (!playingId || !audioRef.current) {
      setPlayheadFrame(0);
      return;
    }

    let raf = 0;
    const tick = () => {
      const active = audioRef.current;
      if (!active) return;
      const elapsed = active.context.currentTime - active.startedAt;
      const frame = Math.floor((elapsed * BANK_SAMPLE_RATE) % Math.max(1, active.frameCount));
      setPlayheadFrame(frame);
      raf = window.requestAnimationFrame(tick);
    };
    raf = window.requestAnimationFrame(tick);
    return () => window.cancelAnimationFrame(raf);
  }, [playingId]);

  const capacity = device?.capacityBytes ?? null;
  const used = useMemo(() => usedAudioBytes(samples), [samples]);
  const overCapacity = capacity != null && used > capacity;
  const usageRatio = capacity != null && capacity > 0 ? Math.min(1, used / capacity) : 0;
  const showStatusSpinner = busy && status.kind === 'idle';
  const bankEditingDisabled = !connected || busy;
  const uploadNeedsSync = connected && bankDirty;
  const uploadDisabled = !connected || busy || overCapacity || (!uploadNeedsSync && samples.length === 0);

  async function connect() {
    if (connected) {
      stopPreview();
      await serial.disconnect();
      setConnected(false);
      setDevice(null);
      setBankDirty(false);
      clearTransfer();
      setStatus({ text: 'Disconnected', kind: 'idle' });
      return;
    }

    try {
      setBusy(true);
      clearDebugLog();
      serial.setLogger(appendDebugLog);
      setStatus({ text: 'Connecting', kind: 'idle' });
      await serial.connect();
      const info = await initialiseWithRetry();
      setDevice(info);
      setConnected(true);
      startTransfer('Downloading');
      const bank = await serial.readBank((ratio) => updateTransfer('Downloading', ratio));
      if (bank) {
        const parsed = parseBankBlob(bank);
        setSamples(parsed.samples);
        setBankDirty(false);
        setStatus({ text: `Loaded ${parsed.samples.length} samples from device`, kind: 'good' });
      } else {
        setSamples([]);
        setBankDirty(false);
        setStatus({ text: 'Connected: device bank is empty', kind: 'good' });
      }
      clearTransfer();
    } catch (error) {
      clearTransfer();
      setStatus({ text: errorMessage(error), kind: 'bad' });
      try {
        await serial.disconnect();
      } catch (_) {}
      serial.setLogger(null);
      setConnected(false);
      setBankDirty(false);
    } finally {
      setBusy(false);
    }
  }

  async function initialiseWithRetry(): Promise<DeviceInfo> {
    let lastError: unknown = null;
    for (let attempt = 0; attempt < 4; attempt++) {
      try {
        setStatus({ text: attempt === 0 ? 'Connecting' : 'Retrying connection', kind: 'idle' });
        await serial.sync();
        await new Promise((resolve) => setTimeout(resolve, 120));
        return await serial.info(true);
      } catch (error) {
        lastError = error;
        await new Promise((resolve) => setTimeout(resolve, 250 * (attempt + 1)));
      }
    }
    throw lastError instanceof Error ? lastError : new Error(String(lastError));
  }

  function clearDebugLog() {
    debugEntriesRef.current = [];
    setDebugLog([]);
    if (debugFlushTimerRef.current != null) {
      window.clearTimeout(debugFlushTimerRef.current);
      debugFlushTimerRef.current = null;
    }
  }

  function appendDebugLog(message: string) {
    debugEntriesRef.current.push(`${new Date().toLocaleTimeString()} ${message}`);
    if (debugEntriesRef.current.length > 80) debugEntriesRef.current.splice(0, debugEntriesRef.current.length - 80);
    if (!debugOpenRef.current || debugInteractionRef.current || debugFlushTimerRef.current != null) return;
    debugFlushTimerRef.current = window.setTimeout(() => {
      debugFlushTimerRef.current = null;
      flushDebugLog();
    }, 250);
  }

  function flushDebugLog(force = false) {
    if (debugFlushTimerRef.current != null) {
      window.clearTimeout(debugFlushTimerRef.current);
      debugFlushTimerRef.current = null;
    }
    if (!force && debugInteractionRef.current) return;
    setDebugLog([...debugEntriesRef.current]);
  }

  function toggleDebug() {
    const next = !debugOpenRef.current;
    debugOpenRef.current = next;
    setDebugOpen(next);
    if (next) flushDebugLog(true);
  }

  function pauseDebugLog() {
    debugInteractionRef.current = true;
    if (debugFlushTimerRef.current != null) {
      window.clearTimeout(debugFlushTimerRef.current);
      debugFlushTimerRef.current = null;
    }
  }

  function resumeDebugLog() {
    debugInteractionRef.current = false;
    flushDebugLog(true);
  }

  function startTransfer(label: string) {
    transferStartRef.current = performance.now();
    setProgress(0);
    setTransfer({ label, ratio: 0, etaSeconds: null });
    setStatus({ text: label, kind: 'idle' });
  }

  function updateTransfer(label: string, ratio: number) {
    const elapsedSeconds = Math.max(0.001, (performance.now() - transferStartRef.current) / 1000);
    const etaSeconds = ratio > 0 && ratio < 1 ? Math.max(0, (elapsedSeconds / ratio) * (1 - ratio)) : null;
    setProgress(ratio);
    setTransfer({ label, ratio, etaSeconds });
    const percent = Math.round(ratio * 100);
    setStatus({
      text: `${label} ${percent}%${etaSeconds == null ? '' : ` · ${formatEta(etaSeconds)} left`}`,
      kind: 'idle',
    });
  }

  function clearTransfer() {
    setProgress(0);
    setTransfer(null);
  }

  async function addFiles(fileList: FileList | File[]) {
    if (!connected) {
      setStatus({ text: 'Connect to a stretchcore before adding audio', kind: 'warn' });
      return;
    }
    const files = Array.from(fileList).filter((file) => file.type.startsWith('audio/') || /\.(aif|aiff|wav|mp3|flac|ogg)$/i.test(file.name));
    if (files.length === 0) return;
    setBusy(true);
    try {
      const next: BankSample[] = [];
      for (const file of files) {
        setStatus({ text: `Processing ${file.name}`, kind: 'idle' });
        next.push(await decodeAndEncodeFile(file));
      }
      setSamples((current) => [...current, ...next].slice(0, BANK_MAX_SAMPLES));
      setBankDirty(true);
      setStatus({ text: `Added ${next.length} sample${next.length === 1 ? '' : 's'}`, kind: 'good' });
    } catch (error) {
      setStatus({ text: errorMessage(error), kind: 'bad' });
    } finally {
      setBusy(false);
    }
  }

  async function uploadBank() {
    if (!connected) {
      setStatus({ text: 'Connect to a stretchcore before uploading', kind: 'warn' });
      return;
    }
    try {
      if (!device) throw new Error('Connect to a stretchcore before uploading');
      setBusy(true);
      stopPreview();
      startTransfer('Uploading');
      const blob = buildBankBlob(samples, device.capacityBytes);
      await serial.writeBank(blob, (ratio) => updateTransfer('Uploading', ratio));
      const info = await serial.info(true);
      setDevice(info);
      setBankDirty(false);
      clearTransfer();
      setStatus({ text: 'Upload complete', kind: 'good' });
    } catch (error) {
      clearTransfer();
      setStatus({ text: errorMessage(error), kind: 'bad' });
    } finally {
      setBusy(false);
    }
  }

  async function readDevice() {
    if (!connected) return;
    try {
      setBusy(true);
      stopPreview();
      startTransfer('Downloading');
      const bank = await serial.readBank((ratio) => updateTransfer('Downloading', ratio));
      const info = await serial.info(true);
      setDevice(info);
      if (bank) {
        const parsed = parseBankBlob(bank);
        setSamples(parsed.samples);
        setBankDirty(false);
        setStatus({ text: `Read ${parsed.samples.length} samples`, kind: 'good' });
      } else {
        setSamples([]);
        setBankDirty(false);
        setStatus({ text: 'Device bank is empty', kind: 'good' });
      }
      clearTransfer();
    } catch (error) {
      clearTransfer();
      setStatus({ text: errorMessage(error), kind: 'bad' });
    } finally {
      setBusy(false);
    }
  }

  async function eraseDevice() {
    if (!connected) return;
    try {
      setBusy(true);
      stopPreview();
      setStatus({ text: 'Erasing', kind: 'idle' });
      await serial.erase();
      const info = await serial.info(true);
      setDevice(info);
      setSamples([]);
      setBankDirty(false);
      setStatus({ text: 'Device bank erased', kind: 'good' });
    } catch (error) {
      setStatus({ text: errorMessage(error), kind: 'bad' });
    } finally {
      setBusy(false);
    }
  }

  function updateSample(id: string, patch: Partial<BankSample>) {
    setSamples((current) => current.map((sample) => (sample.id === id ? { ...sample, ...patch } : sample)));
    if (connected) setBankDirty(true);
  }

  function removeSample(id: string) {
    if (playingId === id) stopPreview();
    setSamples((current) => current.filter((sample) => sample.id !== id));
    if (connected) setBankDirty(true);
  }

  function moveSample(index: number, direction: -1 | 1) {
    const target = index + direction;
    const changed = target >= 0 && target < samples.length;
    setSamples((current) => {
      const next = [...current];
      if (target < 0 || target >= next.length) return current;
      [next[index], next[target]] = [next[target], next[index]];
      return next;
    });
    if (connected && changed) setBankDirty(true);
  }

  function cropSample(id: string) {
    setSamples((current) =>
      current.map((sample) => {
        if (sample.id !== id) return sample;
        const pcm = croppedPcm(sample);
        return { ...sample, pcm, cropStart: 0, cropEnd: pcm.length };
      }),
    );
    if (connected) setBankDirty(true);
  }

  function stopPreview() {
    audioRef.current?.source.stop();
    audioRef.current?.context.close();
    audioRef.current = null;
    setPlayingId(null);
  }

  async function togglePreview(sample: BankSample) {
    if (playingId === sample.id) {
      stopPreview();
      return;
    }
    stopPreview();
    const context = new AudioContext();
    const pcm = croppedPcm(sample);
    const source = context.createBufferSource();
    source.buffer = makePreviewBuffer(context, pcm);
    source.loop = true;
    source.connect(context.destination);
    const startedAt = context.currentTime;
    source.start();
    source.onended = () => {
      if (audioRef.current?.source === source) setPlayingId(null);
    };
    audioRef.current = { context, source, startedAt, frameCount: pcm.length };
    setPlayingId(sample.id);
  }

  function startTour() {
    let tour: Driver;
    tour = driver({
      animate: true,
      smoothScroll: true,
      overlayOpacity: 0.62,
      stagePadding: 6,
      stageRadius: 7,
      popoverClass: `stretchcore-tour stretchcore-tour-${theme}`,
      showProgress: true,
      progressText: '{{current}} / {{total}}',
      nextBtnText: 'Next',
      prevBtnText: 'Back',
      doneBtnText: 'Done',
      showButtons: ['previous', 'next', 'close'],
      overlayClickBehavior: () => undefined,
      steps: [
        {
          popover: {
            title: 'Welcome to stretchcore loader',
            description: `<img class="tour-stretchcore" src="${stretchcoreImageUrl}" alt="stretchcore front panel" /><p>Use this page to update firmware and manage the samples on your stretchcore.</p>`,
            side: 'over',
          },
        },
        {
          element: '[data-tour="uf2"]',
          popover: {
            title: 'Download firmware',
            description: 'Download the latest UF2 firmware, then copy it to your stretchcore in bootloader mode.',
            side: 'bottom',
            align: 'center',
          },
        },
        {
          element: '[data-tour="connect"]',
          popover: {
            title: 'Connect',
            description: 'Connect over USB serial so the loader can read capacity and download the current sample bank.',
            side: 'bottom',
            align: 'center',
          },
        },
        {
          element: '[data-tour="add"]',
          popover: {
            title: 'Add samples',
            description: 'After connecting, add audio files or drag them into the sample area. The loader converts them for stretchcore.',
            side: 'bottom',
            align: 'center',
          },
        },
        {
          element: '[data-tour="upload"]',
          popover: {
            title: 'Upload samples',
            description: 'When the sample list is ready, upload it to write the bank back to your stretchcore.',
            side: 'bottom',
            align: 'center',
          },
        },
        {
          element: '[data-tour="read"]',
          popover: {
            title: 'Read from device',
            description: 'Reload the sample bank from the device if you want to inspect or restore what is currently on your stretchcore.',
            side: 'bottom',
            align: 'center',
          },
        },
        {
          element: '[data-tour="erase"]',
          popover: {
            title: 'Erase the bank',
            description: 'Erase clears the sample bank on the device.',
            side: 'bottom',
            align: 'center',
          },
        },
      ],
      onNextClick: (_, __, { driver: activeTour }) => {
        if (activeTour.isLastStep()) {
          activeTour.destroy();
          return;
        }
        activeTour.moveNext();
      },
      onCloseClick: (_, __, { driver: activeTour }) => {
        activeTour.destroy();
      },
    });
    tour.drive();
  }

  return (
    <main className="app">
      <header className="topbar">
        <div>
          <h1>stretchcore loader</h1>
          <div className={`status ${status.kind}`}>
            {showStatusSpinner ? <span className="spinner" aria-hidden="true" /> : null}
            {status.text}
          </div>
        </div>
        <div className="toolbar">
          <button
            className="primary"
            data-tour="connect"
            onClick={connect}
            disabled={busy}
            title={connected ? 'Disconnect from stretchcore' : 'Connect to stretchcore over USB serial'}
          >
            <Cable size={18} />
            {connected ? 'Disconnect' : 'Connect'}
          </button>
          <label
            className={`button ${bankEditingDisabled ? 'disabled' : ''}`}
            data-tour="add"
            title={connected ? 'Add audio files to the sample list' : 'Connect to a stretchcore before adding audio'}
          >
            <Plus size={18} />
            Add
            <input
              type="file"
              multiple
              accept="audio/*,.aif,.aiff"
              disabled={bankEditingDisabled}
              onChange={(event) => {
                if (event.target.files) void addFiles(event.target.files);
                event.currentTarget.value = '';
              }}
            />
          </label>
          <button
            data-tour="upload"
            onClick={uploadBank}
            disabled={uploadDisabled}
            className={uploadNeedsSync ? 'needs-sync' : undefined}
            title={uploadNeedsSync ? 'Upload changes to sync stretchcore' : 'Upload the current sample bank to the device'}
          >
            <Upload size={18} />
            Upload
          </button>
          <button data-tour="read" onClick={readDevice} disabled={!connected || busy} title="Download samples from device">
            <Download size={18} />
            Read
          </button>
          <button data-tour="erase" onClick={eraseDevice} disabled={!connected || busy} title="Erase samples on device">
            <Eraser size={18} />
            Erase
          </button>
          <a className="button" data-tour="uf2" href={stretchcoreUf2Url} download="stretchcore.uf2" title="Download firmware UF2">
            <FileDown size={18} />
            Firmware
          </a>
          <div className="toolbar-spacer" />
          <button className="icon-button" onClick={startTour} title="Show stretchcore tour" aria-label="Show stretchcore tour">
            <HelpCircle size={18} />
          </button>
          <button
            className="icon-button"
            onClick={() => setImageOpen(true)}
            title="Show module image"
            aria-label="Show module image"
          >
            <BookOpen size={18} />
          </button>
          <button
            className="icon-button"
            onClick={toggleDebug}
            title={debugOpen ? 'Hide serial debug log' : 'Show serial debug log'}
            aria-label={debugOpen ? 'Hide serial debug log' : 'Show serial debug log'}
            aria-pressed={debugOpen}
          >
            <Terminal size={18} />
          </button>
          <button
            className="icon-button"
            onClick={() => setTheme((current) => (current === 'dark' ? 'light' : 'dark'))}
            title={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}
            aria-label={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}
          >
            {theme === 'dark' ? <Sun size={18} /> : <Moon size={18} />}
          </button>
        </div>
      </header>

      {imageOpen ? (
        <div className="modal-backdrop" role="presentation" onMouseDown={() => setImageOpen(false)}>
          <div
            className="image-modal"
            role="dialog"
            aria-modal="true"
            aria-label="stretchcore image"
            onMouseDown={(event) => event.stopPropagation()}
          >
            <button className="icon-button modal-close" onClick={() => setImageOpen(false)} title="Close" aria-label="Close">
              <X size={18} />
            </button>
            <img src={stretchcoreImageUrl} alt="stretchcore" />
          </div>
        </div>
      ) : null}

      <section className="capacity">
        <div className="capacity-line">
          <span>{formatBytes(used)} used</span>
          <span>{capacity == null ? 'Capacity unknown' : `${formatBytes(Math.max(0, capacity - used))} free`}</span>
          <span>
            {samples.length} sample{samples.length === 1 ? '' : 's'}
          </span>
          <span>{device ? `FW ${device.firmware}` : 'No device'}</span>
        </div>
        <div className="meter">
          <div className={overCapacity ? 'over' : ''} style={{ width: `${usageRatio * 100}%` }} />
        </div>
        {transfer ? (
          <div className="transfer-label">
            {transfer.label} {Math.round(transfer.ratio * 100)}%
            {transfer.etaSeconds == null ? '' : ` · ${formatEta(transfer.etaSeconds)} left`}
          </div>
        ) : null}
        {busy && progress > 0 ? <div className="transfer" style={{ width: `${progress * 100}%` }} /> : null}
      </section>

      {debugOpen ? (
        <section className="debug-log">
          <div className="debug-title">Serial debug</div>
          {debugLog.length > 0 ? (
            <textarea
              className="debug-output"
              readOnly
              spellCheck={false}
              value={debugLog.join('\n')}
              onFocus={pauseDebugLog}
              onMouseDown={pauseDebugLog}
              onTouchStart={pauseDebugLog}
              onBlur={resumeDebugLog}
              title="Serial debug messages"
              aria-label="Serial debug messages"
            />
          ) : (
            <div className="debug-empty">No serial messages yet</div>
          )}
        </section>
      ) : null}

      <section
        className="sample-list"
        onDragOver={(event) => event.preventDefault()}
        onDrop={(event) => {
          event.preventDefault();
          if (!connected) {
            setStatus({ text: 'Connect to a stretchcore before adding audio', kind: 'warn' });
            return;
          }
          void addFiles(event.dataTransfer.files);
        }}
      >
        {samples.length === 0 ? (
          <div className="empty">
            <FolderOpen size={28} />
            <span>No samples loaded</span>
          </div>
        ) : (
          samples.map((sample, index) => (
            <SampleRow
              key={sample.id}
              sample={sample}
              index={index}
              playing={playingId === sample.id}
              playheadFrame={playingId === sample.id ? playheadFrame : null}
              theme={theme}
              onUpdate={(patch) => updateSample(sample.id, patch)}
              onRemove={() => removeSample(sample.id)}
              onMove={(direction) => moveSample(index, direction)}
              onCrop={() => cropSample(sample.id)}
              onPreview={() => void togglePreview(sample)}
            />
          ))
        )}
      </section>
    </main>
  );
}

function SampleRow({
  sample,
  index,
  playing,
  playheadFrame,
  theme,
  onUpdate,
  onRemove,
  onMove,
  onCrop,
  onPreview,
}: {
  sample: BankSample;
  index: number;
  playing: boolean;
  playheadFrame: number | null;
  theme: Theme;
  onUpdate: (patch: Partial<BankSample>) => void;
  onRemove: () => void;
  onMove: (direction: -1 | 1) => void;
  onCrop: () => void;
  onPreview: () => void;
}) {
  const length = croppedPcm(sample).length;
  return (
    <article className="sample-row">
      <div className="sample-meta">
        <span className="slot">{index + 1}</span>
        <input
          className="name"
          value={sample.name}
          maxLength={47}
          onChange={(event) => onUpdate({ name: event.target.value })}
        />
        <label>
          BPM
          <input
            className="bpm"
            type="number"
            min={1}
            max={65535}
            value={sample.bpm}
            onChange={(event) => onUpdate({ bpm: Number(event.target.value) || 1 })}
          />
        </label>
        <span>{formatDuration(length)}</span>
        <span>{formatBytes(length)}</span>
      </div>
      <Waveform sample={sample} playheadFrame={playheadFrame} theme={theme} onUpdate={onUpdate} />
      <div className="row-actions">
        <button onClick={onPreview} title={playing ? 'Stop sample preview' : 'Play sample preview'}>
          {playing ? <Pause size={16} /> : <Play size={16} />}
        </button>
        <button onClick={onCrop} disabled={sample.cropStart === 0 && sample.cropEnd === sample.pcm.length} title="Crop sample to selection">
          <Scissors size={16} />
        </button>
        <button onClick={() => onMove(-1)} disabled={index === 0} title="Move sample up">
          <ArrowUp size={16} />
        </button>
        <button onClick={() => onMove(1)} title="Move sample down">
          <ArrowDown size={16} />
        </button>
        <button onClick={onRemove} title="Remove sample">
          <Trash2 size={16} />
        </button>
      </div>
    </article>
  );
}

function Waveform({
  sample,
  playheadFrame,
  theme,
  onUpdate,
}: {
  sample: BankSample;
  playheadFrame: number | null;
  theme: Theme;
  onUpdate: (patch: Partial<BankSample>) => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const dragStart = useRef<number | null>(null);

  function draw(canvas: HTMLCanvasElement) {
    const rect = canvas.getBoundingClientRect();
    const scale = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.floor(rect.width * scale));
    const height = Math.max(1, Math.floor(rect.height * scale));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const waveBackground = theme === 'dark' ? '#191918' : '#f7f7f4';
    const waveLine = theme === 'dark' ? '#f1f0ea' : '#202020';
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = waveBackground;
    ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = waveLine;
    ctx.lineWidth = Math.max(1, scale);
    ctx.beginPath();
    const mid = height / 2;
    for (let x = 0; x < width; x++) {
      const start = Math.floor((x / width) * sample.pcm.length);
      const end = Math.max(start + 1, Math.floor(((x + 1) / width) * sample.pcm.length));
      let min = 0;
      let max = 0;
      for (let i = start; i < end; i++) {
        const value = sample.pcm[i] ? ((sample.pcm[i] << 24) >> 24) / 128 : 0;
        min = Math.min(min, value);
        max = Math.max(max, value);
      }
      ctx.moveTo(x, mid + min * mid * 0.9);
      ctx.lineTo(x, mid + max * mid * 0.9);
    }
    ctx.stroke();

    const left = (sample.cropStart / sample.pcm.length) * width;
    const right = (sample.cropEnd / sample.pcm.length) * width;
    const isPartialSelection = sample.cropStart > 0 || sample.cropEnd < sample.pcm.length;
    if (isPartialSelection) {
      ctx.fillStyle = colorWithAlpha(waveLine, 0.14);
      ctx.fillRect(0, 0, left, height);
      ctx.fillRect(right, 0, Math.max(0, width - right), height);
      ctx.fillStyle = waveLine;
      ctx.fillRect(left, 0, Math.max(2, 2 * scale), height);
      ctx.fillRect(right, 0, Math.max(2, 2 * scale), height);
    }

    if (playheadFrame != null) {
      const playhead = ((sample.cropStart + playheadFrame) / sample.pcm.length) * width;
      ctx.fillStyle = cssVar('--playhead', '#d23b2a');
      ctx.fillRect(playhead, 0, Math.max(2, 2 * scale), height);
    }
  }

  useEffect(() => {
    if (canvasRef.current) draw(canvasRef.current);
  }, [sample, playheadFrame, theme]);

  function frameFromEvent(event: MouseEvent<HTMLCanvasElement>): number {
    const rect = event.currentTarget.getBoundingClientRect();
    const ratio = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width));
    return Math.round(ratio * sample.pcm.length);
  }

  function setSelection(start: number, end: number) {
    const cropStart = Math.max(0, Math.min(sample.pcm.length - 1, Math.min(start, end)));
    const cropEnd = Math.max(cropStart + 1, Math.min(sample.pcm.length, Math.max(start, end)));
    onUpdate({ cropStart, cropEnd });
  }

  return (
    <canvas
      ref={(node) => {
        canvasRef.current = node;
        if (node) draw(node);
      }}
      className="waveform"
      onMouseDown={(event) => {
        dragStart.current = frameFromEvent(event);
        setSelection(dragStart.current, dragStart.current + 1);
      }}
      onDoubleClick={() => {
        dragStart.current = null;
        onUpdate({ cropStart: 0, cropEnd: sample.pcm.length });
      }}
      onMouseMove={(event) => {
        if (dragStart.current == null) return;
        setSelection(dragStart.current, frameFromEvent(event));
      }}
      onMouseUp={() => {
        dragStart.current = null;
      }}
      onMouseLeave={() => {
        dragStart.current = null;
      }}
    />
  );
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function formatDuration(frames: number): string {
  return `${(frames / BANK_SAMPLE_RATE).toFixed(2)} s`;
}

function formatEta(seconds: number): string {
  const rounded = Math.max(0, Math.ceil(seconds));
  const minutes = Math.floor(rounded / 60);
  const remainder = rounded % 60;
  return minutes > 0 ? `${minutes}:${remainder.toString().padStart(2, '0')}` : `${remainder}s`;
}

function loadTheme(): Theme {
  const stored =
    window.localStorage.getItem('stretchcore-theme') ?? window.localStorage.getItem('breaky-theme');
  if (stored === 'light' || stored === 'dark') return stored;
  return window.matchMedia?.('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

function cssVar(name: string, fallback: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
}

function colorWithAlpha(color: string, alpha: number): string {
  if (!color.startsWith('#')) return color;
  const hex = color.slice(1);
  const full =
    hex.length === 3
      ? hex
          .split('')
          .map((part) => part + part)
          .join('')
      : hex;
  const value = Number.parseInt(full, 16);
  if (!Number.isFinite(value)) return color;
  const r = (value >> 16) & 0xff;
  const g = (value >> 8) & 0xff;
  const b = value & 0xff;
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}
