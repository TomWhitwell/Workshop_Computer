// Panel-label coordinates for the Workshop Computer front panel diagram.
// Ported from MTM_Newsite_2022 _data/program_cards/panel_positions.yml.
// Values are percentages relative to the panel SVG (left/top).

export const panelPositions = {
  controls: [
    { key: 'main', name: 'Main knob', left: 48.9, top: 15.1 },
    { key: 'x', name: 'X knob', left: 15.7, top: 32.6 },
    { key: 'y', name: 'Y knob', left: 48.7, top: 32.6 },
    { key: 'z', name: 'Z switch', left: 81.6, top: 32.6 },
  ],
  inputs: [
    { key: 'audio_l', name: 'Audio 1', left: 10.9, top: 46.1 },
    { key: 'audio_r', name: 'Audio 2', left: 36.1, top: 46.1 },
    { key: 'cv_1', name: 'CV 1', left: 10.9, top: 57.0 },
    { key: 'cv_2', name: 'CV 2', left: 36.1, top: 57.0 },
    { key: 'pulse_1', name: 'Pulse 1', left: 10.9, top: 67.9 },
    { key: 'pulse_2', name: 'Pulse 2', left: 36.1, top: 67.9 },
  ],
  outputs: [
    { key: 'audio_out_l', name: 'Audio 1', left: 61.3, top: 46.1 },
    { key: 'audio_out_r', name: 'Audio 2', left: 86.5, top: 46.1 },
    { key: 'cv_out_1', name: 'CV 1', left: 61.3, top: 57.0 },
    { key: 'cv_out_2', name: 'CV 2', left: 86.5, top: 57.0 },
    { key: 'pulse_out_1', name: 'Pulse 1', left: 61.3, top: 67.9 },
    { key: 'pulse_out_2', name: 'Pulse 2', left: 86.5, top: 67.9 },
  ],
};
