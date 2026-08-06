import { describe, expect, it } from 'vitest';
import { buildBankBlob, parseBankBlob } from './bank';
import { estimateBpmFromFrames, inferBpmFromName } from './audio';

describe('stretchcore bank format', () => {
  it('round-trips sample metadata and raw pcm', () => {
    const pcm = new Uint8Array([0, 1, 255, 128]);
    const blob = buildBankBlob(
      [
        {
          id: 'a',
          name: 'Amen',
          bpm: 170,
          peak: 127,
          pcm,
          cropStart: 1,
          cropEnd: 3,
        },
      ],
      1024,
    );

    const parsed = parseBankBlob(blob);
    expect(parsed.samples).toHaveLength(1);
    expect(parsed.samples[0].name).toBe('Amen');
    expect(parsed.samples[0].bpm).toBe(170);
    expect(Array.from(parsed.samples[0].pcm)).toEqual([1, 255]);
  });
});

describe('BPM detection', () => {
  it('prefers filename bpm', () => {
    expect(inferBpmFromName('break_bpm170.wav')).toBe(170);
  });

  it('estimates loop bpm from duration', () => {
    expect(estimateBpmFromFrames(526629)).toBe(175);
  });
});
