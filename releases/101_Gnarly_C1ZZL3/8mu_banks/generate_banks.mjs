import { mkdirSync, writeFileSync } from "node:fs";

const output = new URL("./", import.meta.url);
const offGesture = () => Array.from({ length: 8 }, () => ({ channel: 0, cc: 0, lsb: 0, highResolution: false }));
const controls = (ccs) => [
  ...ccs.map((cc) => ({ channel: 1, cc, lsb: 0, highResolution: false })),
  ...offGesture(),
];
const buttons = (edit = false) => edit
  ? [120, 121, 122, 123].map((paramA) => ({ channel: 1, mode: 0, paramA, paramB: 127, pressed: false }))
  : [36, 48, 60, 72].map((paramA) => ({ channel: 1, mode: 1, paramA, paramB: 127, pressed: false }));
const config = (ccs, edit = false) => ({
  ledFlash: true, ledFlashAccel: false, controllerFlip: false, midiThru: false,
  deviceId: 6, faderMin: 0, faderMax: 16383, trsMode: 0, firmwareVersion: "1.0.0",
  usbControls: controls(ccs), trsControls: controls(ccs),
  usbButtonControls: buttons(edit), trsButtonControls: buttons(edit),
});
const banks = [
  ["01-performance", [20, 21, 22, 23, 24, 25, 26, 27], false],
  ["02-amp1-stages", [64, 65, 66, 67, 68, 69, 70, 71], true],
  ["03-amp2-stages", [72, 73, 74, 75, 76, 77, 78, 79], true],
  ["04-pd1-stages", [80, 81, 82, 83, 84, 85, 86, 87], true],
  ["05-pd2-stages", [88, 89, 90, 91, 92, 93, 94, 95], true],
  ["06-pitch1-stages", [96, 97, 98, 99, 100, 101, 102, 103], true],
  ["07-pitch2-stages", [104, 105, 106, 107, 108, 109, 110, 111], true],
  ["08-envelope-utility", [112, 113, 114, 115, 116, 117, 118, 119], true],
];
mkdirSync(output, { recursive: true });
for (const [name, ccs, edit] of banks)
  writeFileSync(new URL(`${name}.json`, output), `${JSON.stringify(config(ccs, edit), null, 2)}\n`);
