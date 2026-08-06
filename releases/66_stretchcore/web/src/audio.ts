import { BANK_SAMPLE_RATE, BankSample, signedByteToFloat } from './bank';

const LOOP_BEAT_COUNTS = [4, 8, 16, 32, 48, 64, 72, 96];
const BPM_MIN = 100;
const BPM_MAX = 200;
const TARGET_PEAK = Math.pow(10, -1 / 20);

export function inferBpmFromName(name: string): number | null {
  const match = /bpm[_-]?(\d+(?:\.\d+)?)/i.exec(name);
  if (!match) return null;
  const bpm = Math.round(Number(match[1]));
  return bpm > 0 ? bpm : null;
}

export function estimateBpmFromFrames(frameCount: number): number {
  const seconds = frameCount / BANK_SAMPLE_RATE;
  let best: { rangeError: number; integerError: number; beats: number; bpm: number } | null = null;
  for (const beats of LOOP_BEAT_COUNTS) {
    const exact = (beats * 60) / seconds;
    const clamped = Math.min(Math.max(exact, BPM_MIN), BPM_MAX);
    const rounded = Math.round(clamped);
    const candidate = {
      rangeError: Math.abs(exact - clamped),
      integerError: Math.abs(clamped - rounded),
      beats,
      bpm: rounded,
    };
    if (
      !best ||
      candidate.rangeError < best.rangeError ||
      (candidate.rangeError === best.rangeError && candidate.integerError < best.integerError) ||
      (candidate.rangeError === best.rangeError &&
        candidate.integerError === best.integerError &&
        candidate.beats > best.beats)
    ) {
      best = candidate;
    }
  }
  return best?.bpm ?? 120;
}

export async function decodeAndEncodeFile(file: File): Promise<BankSample> {
  const context = new AudioContext();
  const buffer = await context.decodeAudioData(await file.arrayBuffer());
  await context.close();

  const mono = new Float32Array(buffer.length);
  for (let channel = 0; channel < buffer.numberOfChannels; channel++) {
    const data = buffer.getChannelData(channel);
    for (let i = 0; i < data.length; i++) mono[i] += data[i] / buffer.numberOfChannels;
  }

  const monoBuffer = new AudioBuffer({
    length: mono.length,
    numberOfChannels: 1,
    sampleRate: buffer.sampleRate,
  });
  monoBuffer.copyToChannel(mono, 0);

  const outLength = Math.max(1, Math.floor((mono.length / buffer.sampleRate) * BANK_SAMPLE_RATE));
  const offline = new OfflineAudioContext(1, outLength, BANK_SAMPLE_RATE);
  const source = offline.createBufferSource();
  const lowpass = offline.createBiquadFilter();
  lowpass.type = 'lowpass';
  lowpass.frequency.value = 20000;
  lowpass.Q.value = 0.707;
  source.buffer = monoBuffer;
  source.connect(lowpass).connect(offline.destination);
  source.start();
  const rendered = await offline.startRendering();
  const data = rendered.getChannelData(0);

  let peakFloat = 0;
  for (const value of data) peakFloat = Math.max(peakFloat, Math.abs(value));
  const gain = peakFloat > 0 ? TARGET_PEAK / peakFloat : 1;

  const pcm = new Uint8Array(data.length);
  let peak = 0;
  for (let i = 0; i < data.length; i++) {
    const quantized = Math.max(-128, Math.min(127, Math.round(data[i] * gain * 127)));
    peak = Math.max(peak, Math.abs(quantized));
    pcm[i] = quantized & 0xff;
  }

  const bpm = inferBpmFromName(file.name) ?? estimateBpmFromFrames(pcm.length);
  return {
    id: crypto.randomUUID(),
    name: file.name.replace(/\.[^.]+$/, ''),
    bpm,
    peak,
    pcm,
    cropStart: 0,
    cropEnd: pcm.length,
  };
}

export function makePreviewBuffer(context: AudioContext, pcm: Uint8Array): AudioBuffer {
  const buffer = context.createBuffer(1, pcm.length, BANK_SAMPLE_RATE);
  const channel = buffer.getChannelData(0);
  for (let i = 0; i < pcm.length; i++) {
    channel[i] = signedByteToFloat(pcm[i]) / 128;
  }
  return buffer;
}

