# Firmware build and component provenance — beta3

Original record ©2026 Adrian Vos(soveda), MIT. Third-party notices retain their
own ownership/terms. Recorded2026-10-15 for0.3.0-beta3.

- Distributed firmware: user-tested0.4.0-alpha2-roll-polarity, preserved byte-for-byte; root
  rebuild matches. Source checkpoint67228dd, with packaging/path/doc changes only.
- SHA256: `ff641228bbca88632c47de0419f0194238a98026c79164e5411022e0fdc92149`.
- ComputerCard0.4.0, Chris Johnson MIT, with local Adrian Vos MIT flash-pause/
  restart additions; vendor/ComputerCard/PATCHES.md records the adaptation.
- CPU192MHz/1.15V,48kHz, two64tap sources,64frame worker,128frame transport.
- PicoSDK2.3.0 revision`98a542c1a62fb549ffb5d66a3e5892b06276b670`;
  BSD-3-Clause plus component terms in vendor/PicoSDK/LICENSE.TXT.
- TinyUSB revision`86ad6e56c1700e85f1c5678607a762cfe3aa2f47`;
  MIT/contributor notices in vendor/TinyUSB.
- Compiler ArmGNU15.2.Rel1, GCC15.2.1; installed ARM_TOOLCHAIN_MANIFEST.txt retained.
- Linked C/runtime archives Newlib libg.a and GCC libgcc.a; TOOLCHAIN_NOTICES.txt
  preserves the full notice collection derived from card369 via card122, including
  the GCC runtime exception. This broad collection is not an exact version inventory.
- Pico printf Marco Paland2014–2019 MIT notice preserved from SDK source in
  PICO_PRINTF_LICENSE.txt. ComputerCard/EightMU/rppicomidi/Workshop_BlockAudioCard
  and related adaptation notices remain in repository and firmware notice bundle.
- KEMAR measurements Bill Gardner/Keith Martin, MIT Media Laboratory1994:
  vendor/KEMAR/SOURCE_TERMS.md. Separate measurement terms, not MIT software.
- User reports pitch/roll alpha2 tests pass; earlier save-fix/pages passed. No new pitch/roll peak or duration
  supplied; do not present older horizontal/alpha26 timings as beta3 profiling.

Runtime archive members named in the current link map follow. Inclusion in the
map can include discarded sections and is not proof that every member contributes
instructions to the final image:

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
