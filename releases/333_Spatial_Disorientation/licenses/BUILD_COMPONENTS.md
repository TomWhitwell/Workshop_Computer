# Firmware build and component provenance

Original record: Adrian Vos (soveda), 2026, MIT. Third-party notices retain their
own copyright/license terms. Recorded 2026-10-10 for card 333 beta packaging.

- Firmware: user-tested 0.1.0-alpha14, preserved byte for byte (not rebuilt for distribution).
- Source snapshot: Spatial Disorientation commit `3a01fca91a527cf171efcacae5bc93760924d34e`.
- ComputerCard: unmodified 0.4.0, MIT (vendor/ComputerCard).
- CPU/audio/FIR: 192 MHz / 1.15 V, 48 kHz, 32 taps; 64-frame block worker.
- Pico SDK: revision `98a542c1a62fb549ffb5d66a3e5892b06276b670`; BSD-3-Clause and component terms.
- TinyUSB: revision `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`; MIT, contributor notices in vendor/TinyUSB.
- Compiler: Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86), GCC 15.2.1 20251203.
- GNU toolchain manifest: ARM_TOOLCHAIN_MANIFEST.txt (preserved installed manifest).
- Link map identifies Newlib libg.a (C runtime) and GCC libgcc.a. Retained runtime
  notices: TOOLCHAIN_NOTICES.txt, copied unchanged from card 122's collection
  derived from card 369. GCC runtime exception is included; this does not change
  the application MIT license. The reference notice collection is broader than
  the linked runtime and is not itself an exact source-version inventory.
- Linked Pico SDK printf preserves Marco Paland's 2014–2019 MIT notice in
  PICO_PRINTF_LICENSE.txt, copied from the actual SDK source header.
- KEMAR measurement authors/terms: Bill Gardner and Keith Martin, MIT Media
  Laboratory, 1994; vendor/KEMAR/SOURCE_TERMS.md. Not an MIT software license.

SHA-256: `9a9ad50c2719652c40bd4302dca882e2c9d7477e0da6f401a37e3d076978b3ec`.
User reports alpha14 tests pass, peak callback 13 us / block 997 us. No run duration
or USB role supplied for those readings; callback is not the complete ISR.

Runtime archive members listed by the original link map (includes discarded
sections; not every listed member necessarily contributes to final machine code):

- `_dvmd_tls.o`
- `libc_a-calloc.o`
- `libc_a-callocr.o`
- `libc_a-categories.o`
- `libc_a-closer.o`
- `libc_a-ctype_.o`
- `libc_a-div.o`
- `libc_a-environ.o`
- `libc_a-envlock.o`
- `libc_a-errno.o`
- `libc_a-fclose.o`
- `libc_a-fflush.o`
- `libc_a-findfp.o`
- `libc_a-freer.o`
- `libc_a-fwalk.o`
- `libc_a-getenv_r.o`
- `libc_a-gettzinfo.o`
- `libc_a-gmtime_r.o`
- `libc_a-impure.o`
- `libc_a-iswspace.o`
- `libc_a-iswspace_l.o`
- `libc_a-jp2uc.o`
- `libc_a-lcltime_r.o`
- `libc_a-locale.o`
- `libc_a-lock.o`
- `libc_a-lseekr.o`
- `libc_a-malloc.o`
- `libc_a-mallocr.o`
- `libc_a-mbrtowc.o`
- `libc_a-mbtowc_r.o`
- `libc_a-memcmp.o`
- `libc_a-memmove.o`
- `libc_a-mktime.o`
- `libc_a-mlock.o`
- `libc_a-month_lengths.o`
- `libc_a-readr.o`
- `libc_a-realloc.o`
- `libc_a-reallocr.o`
- `libc_a-reent.o`
- `libc_a-sbrkr.o`
- `libc_a-sccl.o`
- `libc_a-siscanf.o`
- `libc_a-stdio.o`
- `libc_a-strcasecmp.o`
- `libc_a-strcat.o`
- `libc_a-strchr.o`
- `libc_a-strcmp.o`
- `libc_a-strcpy.o`
- `libc_a-strlcpy.o`
- `libc_a-strlen-stub.o`
- `libc_a-strncasecmp.o`
- `libc_a-strncmp.o`
- `libc_a-strncpy.o`
- `libc_a-strtol.o`
- `libc_a-strtoll.o`
- `libc_a-strtoul.o`
- `libc_a-strtoull.o`
- `libc_a-svfiscanf.o`
- `libc_a-sysconf.o`
- `libc_a-tzcalc_limits.o`
- `libc_a-tzlock.o`
- `libc_a-tzset.o`
- `libc_a-tzset_r.o`
- `libc_a-tzvars.o`
- `libc_a-ungetc.o`
- `libc_a-wctomb_r.o`
- `libc_a-writer.o`
