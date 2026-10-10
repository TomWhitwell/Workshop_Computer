# Card 333 beta packaging checks

Recorded by Adrian Vos (soveda), 2026-10-10. Original documentation: MIT.

- Target branch verified: `Binaural-card`.
- Number 333 was unused in the local release directory and freshly fetched
  `upstream/main`. It represents the three spatial axes as a mnemonic;
  this firmware implements horizontal direction and distance, not elevation.
- Metadata: `Status: Beta`, `License: MIT`, version `0.1.0-alpha14`.
- Source snapshot: development commit `3a01fca91a527cf171efcacae5bc93760924d34e`.
  Copied source, editor, tests and generation tools match that snapshot.
- Strict `info.yaml` validation: one file, zero errors and zero warnings.
- Repository staged program-card validation passed against the branch baseline.
- Twelve C++ host suites passed with AddressSanitizer/UndefinedBehaviorSanitizer:
  startup_mode, modes, dsp, room, block_audio, mu_controls, mixer_mu_controls,
  midi_tx, settings, fig8, fig8_mu_controls and movements.
- Editor lifecycle, independent settings banks, Save and preset import/export
  tests passed (`tests/editor_test.cjs`).
- Clean out-of-tree build of the copied release source passed with Pico SDK
  2.3.0 and Arm GNU Toolchain 15.2.Rel1. Reported main RAM: 127,504 bytes;
  flash: 90,496 bytes; each scratch bank: 2,048 bytes.
- Packaged UF2 matches the user-tested alpha14 binary byte for byte. SHA-256:
  `9a9ad50c2719652c40bd4302dca882e2c9d7477e0da6f401a37e3d076978b3ec`.
  Its 354 UF2 blocks have RP2040 family headers and do not overlap the final
  flash settings sector. The verification build does not replace this binary.
- README and attribution document local file links resolve.
- Whitespace checks pass for original release files. Imported dependency headers,
  upstream directive and license/provenance texts retain their original whitespace.
- Original application/documentation remain MIT. Component and data notices
  are retained separately and collected in `uf2/NOTICE.txt`; see
  [build provenance](../licenses/BUILD_COMPONENTS.md).

Hardware evidence is the user's alpha14 pass report with peak callback 13 µs
and peak 64-frame block 997 µs. That report did not specify the final run duration
or USB role. Source compilation and host tests do not constitute a further
hardware test, and callback measurement excludes surrounding framework ISR work.
