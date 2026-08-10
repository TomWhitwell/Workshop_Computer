#define COMPUTERCARD_NOIMPL
#include "resonator.h"

// Mark settings dirty so Core 1's idle loop performs one debounced flash save.
// Called from the serial command handlers (Core 1) instead of writing immediately,
// so a burst of commands collapses into a single flash erase/program.
void ResonatingStrings::markFlashDirty() {
    flashDirtyTime = to_ms_since_boot(get_absolute_time());
    flashDirty = true;
}

// Check and perform deferred flash save (called from Core 1 on serial idle)
void ResonatingStrings::checkPendingFlashSave() {
    if (pendingFlashSave) {
        // Reset-to-defaults (single event from Core 0): save immediately
        pendingFlashSave = false;
        flashDirty = false;
        saveProgressionToFlash();
    } else if (flashDirty) {
        // Debounced settings save: write once the changes have settled
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - flashDirtyTime >= FLASH_DEBOUNCE_MS) {
            flashDirty = false;
            saveProgressionToFlash();
        }
    }
}

// Serial command handler (called from Core 1)
void ResonatingStrings::handleSerialCommand(const char* cmd) {
    if (strncmp(cmd, "SET ", 4) == 0) {
        handleSet(cmd + 4);
    } else if (strcmp(cmd, "GET") == 0) {
        handleGet();
    } else if (strncmp(cmd, "SETARP ", 7) == 0) {
        handleSetArp(cmd + 7);
    } else if (strcmp(cmd, "GETARP") == 0) {
        handleGetArp();
    } else if (strncmp(cmd, "SETPAT ", 7) == 0) {
        handleSetPat(cmd + 7);
    } else if (strcmp(cmd, "GETPAT") == 0) {
        handleGetPat();
    } else if (strncmp(cmd, "SETLOOP ", 8) == 0) {
        handleSetLoop(cmd + 8);
    } else if (strcmp(cmd, "GETLOOP") == 0) {
        handleGetLoop();
    } else if (strncmp(cmd, "SETROOT ", 8) == 0) {
        handleSetRoot(cmd + 8);
    } else if (strcmp(cmd, "GETROOT") == 0) {
        handleGetRoot();
    } else if (strncmp(cmd, "SETOUT ", 7) == 0) {
        handleSetOut(cmd + 7);
    } else if (strcmp(cmd, "GETOUT") == 0) {
        handleGetOut();
    } else if (strncmp(cmd, "SETDIV ", 7) == 0) {
        handleSetDiv(cmd + 7);
    } else if (strcmp(cmd, "GETDIV") == 0) {
        handleGetDiv();
    } else {
        printf("ERR unknown_command\n");
    }
}

void ResonatingStrings::handleSet(const char* args) {
    ChordMode newChords[MAX_PROGRESSION_LENGTH];
    int count = 0;
    const char* p = args;

    while (*p && count < MAX_PROGRESSION_LENGTH) {
        int val = 0;
        bool hasDigit = false;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
            hasDigit = true;
        }
        if (!hasDigit) break;
        if (val < 0 || val >= NUM_MODES) {
            printf("ERR invalid_id\n");
            return;
        }
        newChords[count++] = (ChordMode)val;
        if (*p == ',') p++;
    }

    if (count == 0) {
        printf("ERR empty_progression\n");
        return;
    }

    // Write to inactive buffer, then swap
    int writeIdx = 1 - activeBuffer;
    for (int i = 0; i < count; i++) {
        progressionBuffers[writeIdx].chords[i] = newChords[i];
    }
    progressionBuffers[writeIdx].length = count;
    __dmb();  // ARM data memory barrier
    activeBuffer = writeIdx;
    progressionChanged = true;

    handleGet();

    // Persist to flash (debounced)
    markFlashDirty();
}

void ResonatingStrings::handleGet() {
    int bufIdx = activeBuffer;
    printf("PROG ");
    for (int i = 0; i < progressionBuffers[bufIdx].length; i++) {
        if (i > 0) printf(",");
        printf("%d", (int)progressionBuffers[bufIdx].chords[i]);
    }
    printf("\n");
}

void ResonatingStrings::handleGetArp() {
    printf("ARP %d\n", arpDivision);
}

void ResonatingStrings::handleSetArp(const char* args) {
    int val = 0;
    const char* p = args;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (val != 1 && val != 2 && val != 4 && val != 8) {
        printf("ERR invalid_arp_division\n");
        return;
    }
    arpDivision = val;
    arpSettingsChanged = true;
    printf("ARP %d\n", arpDivision);
    markFlashDirty();
}

void ResonatingStrings::handleGetPat() {
    printf("PAT %d\n", arpPattern);
}

void ResonatingStrings::handleSetPat(const char* args) {
    int val = 0;
    const char* p = args;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (val < 0 || val > 5) {
        printf("ERR invalid_arp_pattern\n");
        return;
    }
    arpPattern = val;
    arpSettingsChanged = true;
    printf("PAT %d\n", arpPattern);
    markFlashDirty();
}

void ResonatingStrings::handleGetLoop() {
    printf("LOOP %d\n", arpLoop ? 1 : 0);
}

void ResonatingStrings::handleSetLoop(const char* args) {
    int val = 0;
    const char* p = args;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (val < 0 || val > 1) {
        printf("ERR invalid_arp_loop\n");
        return;
    }
    arpLoop = (val != 0);
    printf("LOOP %d\n", arpLoop ? 1 : 0);
    markFlashDirty();
}

void ResonatingStrings::handleGetRoot() {
    printf("ROOT %d\n", rootString);
}

void ResonatingStrings::handleSetRoot(const char* args) {
    int val = 0;
    const char* p = args;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (val < 0 || val > 3) {
        printf("ERR invalid_root_string\n");
        return;
    }
    rootString = val;
    printf("ROOT %d\n", rootString);
    markFlashDirty();
}

void ResonatingStrings::handleGetOut() {
    printf("OUT %d,%d,%d,%d,%d,%d,%d,%d,%d\n", (int)cv1Mode, (int)cv2Mode, (int)p1Mode, (int)p2Mode, (int)pi1Mode, (int)pi2Mode, (int)ao2Mode, (int)ci1Mode, (int)ci2Mode);
}

void ResonatingStrings::handleSetOut(const char* args) {
    int vals[9];
    int count = 0;
    const char* p = args;
    while (*p && count < 9) {
        int val = 0;
        bool hasDigit = false;
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
            hasDigit = true;
        }
        if (!hasDigit) break;
        vals[count++] = val;
        if (*p == ',') p++;
    }
    if (count < 6 || count > 9) {
        printf("ERR invalid_out_args\n");
        return;
    }
    // Validate ranges
    if (vals[0] < 0 || vals[0] > 6 ||
        vals[1] < 0 || vals[1] > 6 ||
        vals[2] < 0 || vals[2] > 3 ||
        vals[3] < 0 || vals[3] > 5 ||
        vals[4] < 0 || vals[4] > 2 ||
        vals[5] < 0 || vals[5] > 3 ||
        (count >= 7 && (vals[6] < 0 || vals[6] > 2)) ||
        (count >= 8 && (vals[7] < 0 || vals[7] > 1)) ||
        (count >= 9 && (vals[8] < 0 || vals[8] > 1))) {
        printf("ERR invalid_out_mode\n");
        return;
    }
    cv1Mode = vals[0];
    cv2Mode = vals[1];
    p1Mode = vals[2];
    p2Mode = vals[3];
    pi1Mode = vals[4];
    pi2Mode = vals[5];
    if (count >= 7) ao2Mode = vals[6];
    if (count >= 8) ci1Mode = vals[7];
    if (count >= 9) ci2Mode = vals[8];
    outputModesChanged = true;
    handleGetOut();
    markFlashDirty();
}

void ResonatingStrings::handleGetDiv() {
    printf("DIV %d\n", (int)clockDivRatio);
}

void ResonatingStrings::handleSetDiv(const char* args) {
    int val = 0;
    const char* p = args;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (val != 2 && val != 3 && val != 4 && val != 8) {
        printf("ERR invalid_div_ratio\n");
        return;
    }
    clockDivRatio = val;
    printf("DIV %d\n", (int)clockDivRatio);
    markFlashDirty();
}

// Flash erase+program callback for flash_safe_execute. Runs in RAM with the other core
// locked out and interrupts disabled (guaranteed by flash_safe_execute). param = data page.
static void __not_in_flash_func(doFlashProgram)(void* param) {
    const uint8_t* data = (const uint8_t*)param;
    flash_range_erase(FLASH_PROG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_PROG_OFFSET, data, FLASH_PAGE_SIZE);
}

// Save current progression to flash (must be called from Core 1)
void ResonatingStrings::saveProgressionToFlash() {
    int bufIdx = activeBuffer;
    int len = progressionBuffers[bufIdx].length;

    // Prepare data buffer (must be 256-byte aligned for flash write)
    uint8_t data[FLASH_PAGE_SIZE] = {0};
    data[0] = FLASH_PROG_MAGIC;
    data[1] = (uint8_t)len;
    for (int i = 0; i < len && i < MAX_PROGRESSION_LENGTH; i++) {
        data[2 + i] = (uint8_t)progressionBuffers[bufIdx].chords[i];
    }
    data[20] = (uint8_t)arpDivision;
    data[21] = (uint8_t)arpPattern;
    data[22] = (uint8_t)cv1Mode;
    data[23] = (uint8_t)cv2Mode;
    data[24] = (uint8_t)p1Mode;
    data[25] = (uint8_t)p2Mode;
    data[26] = (uint8_t)pi1Mode;
    data[27] = (uint8_t)pi2Mode;
    data[28] = (uint8_t)clockDivRatio;
    data[29] = (uint8_t)ao2Mode;
    data[30] = (uint8_t)ci1Mode;
    data[31] = (uint8_t)ci2Mode;
    data[32] = (uint8_t)(arpLoop ? 1 : 0);
    data[33] = (uint8_t)rootString;

    // Write flash safely. flash_safe_execute locks out Core 0 and disables interrupts,
    // then runs doFlashProgram in that safe window. The timeout is for *engaging* the
    // lockout handshake (a healthy one completes in microseconds); keep it short so a
    // struggling handshake under heavy Core 1 load (pitch tracking) blips briefly instead
    // of freezing both cores for a full second. Retry a bounded number of times to catch
    // a good window; never re-arm (avoids a retry storm that would stall Core 1's YIN).
    for (int attempt = 0; attempt < 8; attempt++) {
        if (flash_safe_execute(doFlashProgram, data, 20) == PICO_OK) break;
        sleep_ms(10);
    }
}

// Load progression from flash, returns true if valid data found
bool ResonatingStrings::loadProgressionFromFlash() {
    // Read from flash (XIP address space)
    const uint8_t* flash_data = (const uint8_t*)(XIP_BASE + FLASH_PROG_OFFSET);

    // Check magic byte
    if (flash_data[0] != FLASH_PROG_MAGIC) {
        return false;
    }

    int len = flash_data[1];
    if (len < 1 || len > MAX_PROGRESSION_LENGTH) {
        return false;
    }

    // Load into both buffers
    for (int i = 0; i < len; i++) {
        uint8_t modeVal = flash_data[2 + i];
        if (modeVal >= NUM_MODES) {
            return false;  // Invalid chord ID
        }
        ChordMode mode = (ChordMode)modeVal;
        progressionBuffers[0].chords[i] = mode;
        progressionBuffers[1].chords[i] = mode;
    }
    progressionBuffers[0].length = len;
    progressionBuffers[1].length = len;

    // Detect pre-v1.2 flash: v1.2 always writes a valid arp division (1/2/4/8) at byte 20,
    // while v1.1 zero-fills bytes 20+. A zero-filled settings block would otherwise decode
    // to non-factory values (notably CV Out 2 = arpeggio instead of input envelope), so on
    // migration apply the full factory I/O defaults instead — the progression is kept.
    uint8_t arpVal = flash_data[20];
    bool v2format = (arpVal == 1 || arpVal == 2 || arpVal == 4 || arpVal == 8);

    if (!v2format) {
        // Migrating from v1.1 (or unrecognised settings): factory I/O defaults
        arpDivision = 4;
        arpPattern = 0;
        arpLoop = false;
        rootString = 0;
        cv1Mode = CVOUT_ARP;
        cv2Mode = CVOUT_IN_ENV;
        p1Mode = P1_AUDIO_TRIG;
        p2Mode = P2_CHORD_TRIG;
        pi1Mode = PI1_PLUCK;
        pi2Mode = PI2_ADVANCE;
        clockDivRatio = 2;
        ao2Mode = AO2_AUDIO;
        ci1Mode = CI1_VOCT;
        ci2Mode = CI2_DAMPING;
        return true;
    }

    // v1.2 settings block: decode each field (clamp invalid values to a safe default)
    arpDivision = arpVal;

    uint8_t patVal = flash_data[21];
    arpPattern = (patVal <= 5) ? patVal : 0;

    arpLoop = (flash_data[32] == 1);

    uint8_t rootVal = flash_data[33];
    rootString = (rootVal <= 3) ? rootVal : 0;

    uint8_t cv1Val = flash_data[22];
    cv1Mode = (cv1Val <= 6) ? cv1Val : 0;
    uint8_t cv2Val = flash_data[23];
    cv2Mode = (cv2Val <= 6) ? cv2Val : 0;
    uint8_t p1Val = flash_data[24];
    p1Mode = (p1Val <= 3) ? p1Val : 0;
    uint8_t p2Val = flash_data[25];
    p2Mode = (p2Val <= 5) ? p2Val : 0;
    uint8_t pi1Val = flash_data[26];
    pi1Mode = (pi1Val <= 2) ? pi1Val : 0;
    uint8_t pi2Val = flash_data[27];
    pi2Mode = (pi2Val <= 3) ? pi2Val : 0;

    uint8_t divVal = flash_data[28];
    clockDivRatio = (divVal == 2 || divVal == 3 || divVal == 4 || divVal == 8) ? divVal : 2;

    uint8_t ao2Val = flash_data[29];
    ao2Mode = (ao2Val <= 2) ? ao2Val : 0;
    uint8_t ci1Val = flash_data[30];
    ci1Mode = (ci1Val <= 1) ? ci1Val : 0;
    uint8_t ci2Val = flash_data[31];
    ci2Mode = (ci2Val <= 1) ? ci2Val : 0;

    return true;
}

// Reset progression to factory defaults and save to flash
void ResonatingStrings::resetToDefaults() {
    const ChordMode allChords[] = {
        HARMONIC, FIFTH, MAJOR7, MINOR7, DIM, SUS4, ADD9, MAJOR10,
        SUS2, MAJOR, MINOR, MAJOR6, DOM7, MIN9,
        TANPURA_PA, TANPURA_MA, TANPURA_NI, TANPURA_NI_KOMAL
    };

    // Write to inactive buffer, then swap
    int writeIdx = 1 - activeBuffer;
    for (int i = 0; i < NUM_MODES; i++) {
        progressionBuffers[writeIdx].chords[i] = allChords[i];
    }
    progressionBuffers[writeIdx].length = NUM_MODES;
    __dmb();
    activeBuffer = writeIdx;

    // Reset to first chord
    progressionIndex = 0;
    currentMode = progressionBuffers[activeBuffer].chords[0];
    arpDivision = 4;
    arpPattern = 0;
    arpLoop = false;
    rootString = 0;
    cv1Mode = CVOUT_ARP;
    cv2Mode = CVOUT_IN_ENV;
    p1Mode = P1_AUDIO_TRIG;
    p2Mode = P2_CHORD_TRIG;
    pi1Mode = PI1_PLUCK;
    pi2Mode = PI2_ADVANCE;
    clockDivRatio = 2;
    ao2Mode = AO2_AUDIO;
    ci1Mode = CI1_VOCT;
    ci2Mode = CI2_DAMPING;
    outputModesChanged = true;

    // Defer flash save to Core 1 (Core 0 can't lock out Core 1)
    pendingFlashSave = true;
}
