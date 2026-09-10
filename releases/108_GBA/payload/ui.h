// ui.h — the screen: a PLAY readout and a paged editor.
#pragma once

#include <stdint.h>

// Page order is the order they appear on the tab bar. MEM COMES FIRST AND START LANDS ON IT,
// because recalling a patch is the one editor action that happens mid-performance and it used to
// be eight presses of SELECT+RIGHT away. The rest then run roughly in the order you build a
// sound: what the channels are, what triggers them, how they move, how they balance, then the
// mappings, then the housekeeping.
//
// These values ARE the index into page_tab[] in ui.c. Renumbering here without reordering that
// array gives a tab bar that lies rather than a compile error.
#define PAGE_MEM   0
#define PAGE_CHAN  1
#define PAGE_TRIG  2
#define PAGE_ENV   3
#define PAGE_MIX   4
#define PAGE_BTN   5
#define PAGE_MAP   6
#define PAGE_ORN   7
#define PAGE_DRUM  8
#define PAGE_CAL   9
#define PAGE_SET   10
#define PAGE_COUNT 11

void ui_init(void);
void ui_frame(void);      // one frame: handle buttons, redraw what changed

extern uint8_t g_page;
