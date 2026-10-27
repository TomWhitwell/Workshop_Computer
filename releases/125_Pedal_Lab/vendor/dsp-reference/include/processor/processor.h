#ifndef FV1_FV1_H
#define FV1_FV1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FV1_PROGRAM_WORDS 128u
#define FV1_PROGRAM_BYTES 512u
#define FV1_REGISTER_COUNT 64u
#define FV1_DELAY_WORDS 32768u

/* Public register indices used by Spin FV-1 programs. */
enum {
    FV1_REG_SIN0_RATE  = 0x00,
    FV1_REG_SIN0_RANGE = 0x01,
    FV1_REG_SIN1_RATE  = 0x02,
    FV1_REG_SIN1_RANGE = 0x03,
    FV1_REG_RMP0_RATE  = 0x04,
    FV1_REG_RMP0_RANGE = 0x05,
    FV1_REG_RMP1_RATE  = 0x06,
    FV1_REG_RMP1_RANGE = 0x07,
    FV1_REG_POT0       = 0x10,
    FV1_REG_POT1       = 0x11,
    FV1_REG_POT2       = 0x12,
    FV1_REG_ADCL       = 0x14,
    FV1_REG_ADCR       = 0x15,
    FV1_REG_DACL       = 0x16,
    FV1_REG_DACR       = 0x17,
    FV1_REG_ADDR_PTR   = 0x18,
    FV1_REG0           = 0x20,
    FV1_REG31          = 0x3f
};

typedef enum fv1_result {
    FV1_OK = 0,
    FV1_ERROR_INVALID_ARGUMENT = -1,
    FV1_ERROR_INVALID_PROGRAM = -2,
    FV1_ERROR_BAD_STATE = -3,
    FV1_ERROR_IO = -4,
    FV1_ERROR_UNSUPPORTED = -5
} fv1_result;

typedef enum fv1_delay_model {
    /* Phase-1 reference model: delay samples retain the upper 16 bits of the
       24-bit datapath. This deliberately models the FV-1 delay RAM's reduced
       precision, while the exact proprietary floating representation remains
       a later hardware-validation target. */
    FV1_DELAY_REFERENCE_16 = 0,

    /* Diagnostic mode: preserve all 24 bits. Useful for isolating delay-RAM
       quantization artifacts when comparing algorithms. */
    FV1_DELAY_FULL_24 = 1
} fv1_delay_model;

typedef struct fv1_config {
    double virtual_sample_rate;
    fv1_delay_model delay_model;
} fv1_config;

typedef struct fv1_engine fv1_engine;

typedef struct fv1_snapshot {
    int32_t acc;
    int32_t pacc;
    int32_t lr;
    int32_t regs[FV1_REGISTER_COUNT];
    uint32_t delay_pointer;
    int32_t sin_lfo[2];
    int32_t cos_lfo[2];
    int32_t ramp_lfo[2];
    uint32_t program_counter;
    uint8_t first_run;
    uint8_t debug_sample_active;

    /* Phase-5C explicit simulated-time coordinates. sample_counter counts
       completed virtual FV-1 samples. instruction_counter is the number of
       instructions already executed in the currently active sample, or zero
       between samples. */
    uint64_t sample_counter;
    uint32_t instruction_counter;
} fv1_snapshot;

typedef struct fv1_state_digest {
    /* Canonical FNV-1a hashes used only for deterministic conformance/debug
       comparisons. They are not cryptographic identities. architectural_hash
       covers datapath/register/LFO/time state; delay_hash covers the semantic
       24-bit values visible when reading all 32768 delay words. */
    uint64_t architectural_hash;
    uint64_t delay_hash;
    uint64_t sample_counter;
} fv1_state_digest;

typedef struct fv1_trace {
    uint32_t pc_before;
    uint32_t pc_after;
    uint32_t raw_instruction;
    uint8_t opcode;
    int32_t acc_before;
    int32_t acc_after;
    int32_t pacc_after;
    int32_t lr_after;
    uint8_t skipped;
    uint8_t sample_finished;

    /* Explicit virtual-time position of this instruction. sample_index is the
       zero-based sample being executed; instruction_index is zero-based within
       that sample and counts actually executed instructions, not PC values. */
    uint64_t sample_index;
    uint32_t instruction_index;
} fv1_trace;

typedef struct fv1_resource_report {
    uint32_t used_instructions;
    uint32_t worst_case_path;
    uint32_t static_delay_reads;
    uint32_t static_delay_writes;
    uint32_t dynamic_delay_reads;
    uint32_t highest_static_delay_address;
    uint32_t general_registers_used;
    uint32_t pots_used;
    uint32_t sine_lfos_used;
    uint32_t ramp_lfos_used;
    uint32_t skip_instructions;
    uint32_t opcode_histogram[32];
} fv1_resource_report;

/* Lifetime and configuration. */
fv1_engine* fv1_create(const fv1_config* config);
void fv1_destroy(fv1_engine* engine);
void fv1_reset(fv1_engine* engine, int clear_delay_ram);

/* Program loading. Words use host byte order. Byte images use the standard
   big-endian 512-byte FV-1 EEPROM program representation. */
fv1_result fv1_load_words(fv1_engine* engine, const uint32_t* words, size_t count);
fv1_result fv1_load_bytes(fv1_engine* engine, const uint8_t* bytes, size_t size);
fv1_result fv1_get_program_words(const fv1_engine* engine, uint32_t* words, size_t count);

/* Control inputs. Values are clamped to 0..1 and quantized to the FV-1's
   9-bit POT representation before each sample executes. */
void fv1_set_pots(fv1_engine* engine, float pot0, float pot1, float pot2);

/* Normal execution. */
fv1_result fv1_process_sample(fv1_engine* engine,
                              float in_l, float in_r,
                              float* out_l, float* out_r);
fv1_result fv1_process_block(fv1_engine* engine,
                             const float* in_l, const float* in_r,
                             float* out_l, float* out_r,
                             size_t frames);

/* Instruction-level debug execution. Begin a virtual sample, then repeatedly
   call fv1_debug_step_instruction until trace.sample_finished is non-zero. */
fv1_result fv1_debug_begin_sample(fv1_engine* engine, float in_l, float in_r);
fv1_result fv1_debug_step_instruction(fv1_engine* engine, fv1_trace* trace);
fv1_result fv1_debug_finish_sample(fv1_engine* engine, float* out_l, float* out_r);

void fv1_get_snapshot(const fv1_engine* engine, fv1_snapshot* snapshot);

/* Deterministic diagnostic digest of complete architectural state and delay
   memory. Intended for differential/conformance testing, not persistence. */
fv1_result fv1_get_state_digest(const fv1_engine* engine, fv1_state_digest* digest);

/* Read one physical delay-RAM word by absolute 0..32767 address.  The value
   is returned in the same signed Q1.23 representation used by the datapath,
   after applying the configured delay-memory precision model.  This is a
   read-only inspection API intended for debugger/testbench visualizations. */
fv1_result fv1_read_delay_word(const fv1_engine* engine, uint32_t address, int32_t* value);

fv1_result fv1_analyze_program(const fv1_engine* engine, fv1_resource_report* report);

const char* fv1_opcode_name(uint8_t opcode);
const char* fv1_result_string(fv1_result result);

#ifdef __cplusplus
}
#endif

#endif /* FV1_FV1_H */
