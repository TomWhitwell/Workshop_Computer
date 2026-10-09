# Firmware license packaging

Run from the releases directory:

```sh
python3 license_tools/package_uf2.py --refresh
python3 license_tools/package_uf2.py path/to/card.uf2 --output /tmp/card-with-notices.zip
```

Refresh writes a complete NOTICE.txt beside the checked-in UF2 files, scoped to
the owning card or audio variant. CMake copies the card notice into its build
output directory. ZIP packaging preserves the UF2 bytes and includes the complete
project/dependency/runtime notice collection. Existing output files are never
overwritten. GPL card bundles also include the current release sources; verify
that those are the corresponding sources for the binary being distributed.

BBC-derived Sense of Space files remain in place for the uses permitted by BBC
terms. The packager uses the verified CC0 audio variant for public distribution
and refuses BBC audio bundles while separate sharing permission is unestablished.

## Build records

The existing UF2 SDK, dependency and compiler versions were not reconstructed.
BUILD_PROVENANCE.json in a package records its firmware SHA-256 and says explicitly
that those versions are unknown. The notice-packaging Git revision is not presented
as the firmware's source revision. CMake sdkVersion variables are IDE preferences,
not proof of a historical build.

For each new build, record the source revision and modifications, SDK commit and
version, SDK submodule revisions, compiler version, full CMake options, linker map,
and firmware SHA-256. Check linked components against the version-specific license
files. The Newlib/GCC reference collection is not a substitute for that check.

## Notice sources

- ComputerCard: upstream Workshop Computer ComputerCard LICENSE (Chris Johnson).
- SDK BSD, TinyUSB MIT and printf MIT: installed Pico SDK license files, inspected
  at SDK tag 2.3.0. Component notices remain scoped to their own code.
- Newlib/GCC runtime texts: the existing 369_Asterisk reference notice collection,
  including the GCC runtime exception. No application is relicensed by that text.
- Buddies: unchanged upstream LICENSE.md plus its declared MIT software terms.
- Reverb+: Chris Johnson's MIT declaration in 20_reverb/info.yaml; the referenced
  el-visio/dattorro-verb MIT license by Pauli Pölkki is also preserved.

Wild Pebble metadata now agrees with the author's deliberate MIT LICENSE change
in commit 24811e2498e7369f7cc1780b299ee01495c11ef8. XHT and Turing Matrix retain
their existing GPL-3.0-or-later choices and now include the complete license text.
