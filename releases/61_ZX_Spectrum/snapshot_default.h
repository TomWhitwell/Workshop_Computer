// snapshot_default.h — fallback when no snapshot is baked in.
//
// A real baked snapshot lives in the git-ignored snapshot_data.h, generated from
// a .z80 via tools/bin2h.py (copyrighted game images are NOT committed to this
// public repo). If snapshot_data.h is absent, this empty default is used and the
// card simply boots to the 128K menu; load a game live over the USB Web UI.
//
// To bake in your own snapshot:
//   python tools/bin2h.py path/to/game.z80 snapshot_z80 snapshot_data.h
// and rebuild — main.cpp prefers snapshot_data.h when it exists.
#pragma once

#if __has_include("snapshot_data.h")
  #include "snapshot_data.h"
  #define ZX_HAVE_SNAPSHOT 1
#else
  static const unsigned char snapshot_z80[1] = {0};
  static const unsigned      snapshot_z80_len = 0;
  #define ZX_HAVE_SNAPSHOT 0
#endif
