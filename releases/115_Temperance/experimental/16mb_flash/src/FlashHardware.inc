/*
 * Temperance 16MB flash experiment, 2026-11-04.
 * QSPI helpers and streaming approach adapted from Goldfish 2.0 by
 * Dune Desormeaux: releases/11_goldfish/goldfish_stream.c and .h.
 * Uses Dune Desormeaux's MLRws ADPCM implementation via Goldfish (GPL-3.0).
 * Per-page format, independent queues and cache integration generated with
 * OpenAI Codex at the user's direction; upstream authors retain their credit.
 * Overall experiment: GNU GPL version 3. See ../LICENSE and ../ATTRIBUTION.md.
 */
// QSPI command/ROM helpers adapted from Goldfish 2.0 goldfish_stream.c,
// by Dune Desormeaux. Included only by FlashStore.cpp (core 1, RAM-resident).
typedef void (*flash_rom_fn)(void);
static flash_rom_fn s_rom_connect;   /* connect_internal_flash */
static flash_rom_fn s_rom_exit_xip;  /* flash_exit_xip         */
static flash_rom_fn s_rom_flush;     /* flash_flush_cache      */
static flash_rom_fn s_rom_enter_xip; /* flash_enter_cmd_xip    */

static void qspi_rom_init(void)
{
	s_rom_connect   = (flash_rom_fn)rom_func_lookup(rom_table_code('I', 'F'));
	s_rom_exit_xip  = (flash_rom_fn)rom_func_lookup(rom_table_code('E', 'X'));
	s_rom_flush     = (flash_rom_fn)rom_func_lookup(rom_table_code('F', 'C'));
	s_rom_enter_xip = (flash_rom_fn)rom_func_lookup(rom_table_code('C', 'X'));
}

/* Drive the QSPI chip-select via the pad override (SDK does the same). */
static void __not_in_flash_func(qspi_cs)(bool high)
{
	uint32_t v = high ? IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_HIGH
	                  : IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_VALUE_LOW;
	hw_write_masked(&ioqspi_hw->io[1].ctrl,
	                v << IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_LSB,
	                IO_QSPI_GPIO_QSPI_SS_CTRL_OUTOVER_BITS);
}

/* One command-mode transaction over the SSI in single-bit (0x03-style) mode.
 * tx may be NULL (send zeros); rx may be NULL (discard). Mirrors the inner loop
 * of the SDK's flash_do_cmd, keeping <=14 bytes in flight. */
static void __not_in_flash_func(qspi_xfer)(const uint8_t *tx, uint8_t *rx, size_t n)
{
	qspi_cs(false);
	size_t tx_rem = n, rx_rem = n;
	while (tx_rem || rx_rem) {
		uint32_t sr = ssi_hw->sr;
		if ((sr & SSI_SR_TFNF_BITS) && tx_rem && (rx_rem - tx_rem) < 14u) {
			ssi_hw->dr0 = tx ? (uint32_t)*tx++ : 0u;
			--tx_rem;
		}
		if ((sr & SSI_SR_RFNE_BITS) && rx_rem) {
			uint8_t b = (uint8_t)ssi_hw->dr0;
			if (rx) *rx++ = b;
			--rx_rem;
		}
	}
	qspi_cs(true);
}

/* Read status register 1 (WIP = bit 0). */
static uint8_t __not_in_flash_func(qspi_status)(void)
{
	uint8_t tx[2] = { 0x05u, 0x00u };
	uint8_t rx[2] = { 0u, 0u };
	qspi_xfer(tx, rx, 2);
	return rx[1];
}

static void __not_in_flash_func(qspi_write_enable)(void)
{
	uint8_t c = 0x06u;
	qspi_xfer(&c, NULL, 1);
}

/* Program one 256-byte page in command mode (XIP must already be exited).
 * Blocks on WIP so the caller may safely resume an erase afterwards. */
static void __not_in_flash_func(qspi_program_page)(uint32_t off, const uint8_t *data)
{
	qspi_write_enable();
	uint8_t hdr[4] = { 0x02u, (uint8_t)(off >> 16), (uint8_t)(off >> 8), (uint8_t)off };
	uint32_t total = 4u + kPageBytes;
	qspi_cs(false);
	uint32_t sent = 0u, got = 0u;
	while (sent < total || got < total) {
		uint32_t sr = ssi_hw->sr;
		if ((sr & SSI_SR_TFNF_BITS) && sent < total && (sent - got) < 14u) {
			uint8_t b = (sent < 4u) ? hdr[sent] : data[sent - 4u];
			ssi_hw->dr0 = (uint32_t)b;
			++sent;
		}
		if ((sr & SSI_SR_RFNE_BITS) && got < total) {
			(void)ssi_hw->dr0;
			++got;
		}
	}
	qspi_cs(true);
	uint32_t start = time_us_32();
    while (qspi_status() & 0x01u) {
        if (time_us_32() - start > 10000u) {
            diagnostics.hardwareFault = 1;
            return;
        }
    }
}

/* Program one page with interrupts masked on the calling (core 1) core.
 * Uses the raw QSPI command path so runtime-detected 16 MB cards are not capped
 * by the SDK's compile-time PICO_FLASH_SIZE_BYTES assertion for the pico board. */
static void __not_in_flash_func(flash_program_page)(uint32_t off, const uint8_t *data)
{
	uint32_t ints = save_and_disable_interrupts();
	s_rom_connect();
	s_rom_exit_xip();

	qspi_program_page(off, data);
	if (!diagnostics.hardwareFault) { s_rom_flush(); s_rom_enter_xip(); }
	restore_interrupts(ints);
}

