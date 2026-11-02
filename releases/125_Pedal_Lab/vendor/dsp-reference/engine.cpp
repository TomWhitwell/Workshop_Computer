#include <processor/processor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

namespace {

constexpr int32_t Q23_ONE = 1 << 23;
constexpr int32_t Q23_MAX = Q23_ONE - 1;
constexpr int32_t Q23_MIN = -Q23_ONE;
constexpr uint32_t DELAY_MASK = FV1_DELAY_WORDS - 1;
constexpr uint64_t FNV_OFFSET = UINT64_C(14695981039346656037);
constexpr uint64_t FNV_PRIME  = UINT64_C(1099511628211);

inline void hash_u8(uint64_t& h, uint8_t value) {
    h ^= value;
    h *= FNV_PRIME;
}

template <typename T>
inline void hash_integer_le(uint64_t& h, T value) {
    using U = std::make_unsigned_t<T>;
    U u = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(U); ++i) {
        hash_u8(h, static_cast<uint8_t>(u & static_cast<U>(0xffu)));
        u >>= 8u;
    }
}

constexpr uint8_t OP_RDA  = 0x00;
constexpr uint8_t OP_RMPA = 0x01;
constexpr uint8_t OP_WRA  = 0x02;
constexpr uint8_t OP_WRAP = 0x03;
constexpr uint8_t OP_RDAX = 0x04;
constexpr uint8_t OP_RDFX = 0x05;
constexpr uint8_t OP_WRAX = 0x06;
constexpr uint8_t OP_WRHX = 0x07;
constexpr uint8_t OP_WRLX = 0x08;
constexpr uint8_t OP_MAXX = 0x09;
constexpr uint8_t OP_MULX = 0x0a;
constexpr uint8_t OP_LOG  = 0x0b;
constexpr uint8_t OP_EXP  = 0x0c;
constexpr uint8_t OP_SOF  = 0x0d;
constexpr uint8_t OP_AND  = 0x0e;
constexpr uint8_t OP_OR   = 0x0f;
constexpr uint8_t OP_XOR  = 0x10;
constexpr uint8_t OP_SKP  = 0x11;
constexpr uint8_t OP_WLDS = 0x12;
constexpr uint8_t OP_JAM  = 0x13;
constexpr uint8_t OP_CHO  = 0x14;

constexpr uint8_t SKP_NEG = 0x01;
constexpr uint8_t SKP_GEZ = 0x02;
constexpr uint8_t SKP_ZRO = 0x04;
constexpr uint8_t SKP_ZRC = 0x08;
constexpr uint8_t SKP_RUN = 0x10;

constexpr uint8_t CHO_COS   = 0x01;
constexpr uint8_t CHO_COMPC = 0x04;
constexpr uint8_t CHO_COMPA = 0x08;
constexpr uint8_t CHO_RPTR2 = 0x10;
constexpr uint8_t CHO_NA    = 0x20;

constexpr uint8_t CHO_TYPE_RDA  = 0;
constexpr uint8_t CHO_TYPE_SOF  = 2;
constexpr uint8_t CHO_TYPE_RDAL = 3;

inline int32_t sat24(int64_t x) {
    if (x > Q23_MAX) return Q23_MAX;
    if (x < Q23_MIN) return Q23_MIN;
    return static_cast<int32_t>(x);
}

inline int32_t sign_extend(uint32_t x, unsigned bits) {
    const uint32_t m = 1u << (bits - 1u);
    x &= (1u << bits) - 1u;
    return static_cast<int32_t>((x ^ m) - m);
}

inline int32_t mul_q(int32_t value, int32_t coefficient, unsigned frac_bits) {
    return sat24((static_cast<int64_t>(value) * coefficient) >> frac_bits);
}

inline int32_t mul_q23(int32_t a, int32_t b) {
    return sat24((static_cast<int64_t>(a) * b) >> 23);
}

inline int32_t float_to_q23(float x) {
    // Host audio is not supposed to contain NaN/Inf, but a public emulator SDK
    // must not feed non-finite values into llround(). Define deterministic
    // behavior instead: NaN becomes digital silence and infinities saturate.
    if (std::isnan(x)) return 0;
    if (std::isinf(x)) return x > 0.0f ? Q23_MAX : Q23_MIN;
    const double c = std::clamp(static_cast<double>(x), -1.0, std::nextafter(1.0, 0.0));
    return sat24(static_cast<int64_t>(std::llround(c * static_cast<double>(Q23_ONE))));
}

inline float q23_to_float(int32_t x) {
    return static_cast<float>(static_cast<double>(x) / static_cast<double>(Q23_ONE));
}

inline int32_t quantize_pot(float x) {
    if (std::isnan(x)) x = 0.0f;
    else if (std::isinf(x)) x = x > 0.0f ? 1.0f : 0.0f;
    const float c = std::clamp(x, 0.0f, 1.0f);
    const int code = std::clamp(static_cast<int>(std::lround(c * 511.0f)), 0, 511);
    return code << 14; // code / 512 in Q1.23, max = 0.998046875
}

struct DecodedInstruction {
    uint32_t raw{};
    uint8_t opcode{};
    uint8_t reg{};
    uint8_t flags{};
    uint8_t lfo{};
    uint8_t cho_type{};
    uint8_t skip_cond{};
    uint8_t skip_count{};
    uint16_t u16{};
    int32_t a{};
    int32_t b{};
};

DecodedInstruction decode(uint32_t raw) {
    DecodedInstruction d{};
    d.raw = raw;
    d.opcode = static_cast<uint8_t>(raw & 0x1fu);

    switch (d.opcode) {
        case OP_RDA:
        case OP_WRA:
        case OP_WRAP:
            d.u16 = static_cast<uint16_t>((raw >> 5) & 0x7fff);
            d.a = sign_extend((raw >> 21) & 0x7ff, 11); // Q1.9
            break;
        case OP_RMPA:
            d.a = sign_extend((raw >> 21) & 0x7ff, 11);
            break;
        case OP_RDAX:
        case OP_RDFX:
        case OP_WRAX:
        case OP_WRHX:
        case OP_WRLX:
        case OP_MAXX:
            d.reg = static_cast<uint8_t>((raw >> 5) & 0x3f);
            d.a = sign_extend((raw >> 16) & 0xffff, 16); // Q1.14
            break;
        case OP_MULX:
            d.reg = static_cast<uint8_t>((raw >> 5) & 0x3f);
            break;
        case OP_LOG:
        case OP_EXP:
        case OP_SOF:
            d.a = sign_extend((raw >> 16) & 0xffff, 16); // Q1.14
            d.b = sign_extend((raw >> 5) & 0x7ff, 11);  // Q0.10
            break;
        case OP_AND:
        case OP_OR:
        case OP_XOR:
            d.a = static_cast<int32_t>((raw >> 8) & 0xffffff);
            break;
        case OP_SKP:
            d.skip_cond = static_cast<uint8_t>((raw >> 27) & 0x1f);
            d.skip_count = static_cast<uint8_t>((raw >> 21) & 0x3f);
            break;
        case OP_WLDS:
            if (raw & 0x40000000u) { // WLDR shares opcode 0x12
                d.flags = 1; // ramp form
                d.lfo = static_cast<uint8_t>((raw >> 29) & 0x1);
                d.a = sign_extend((raw >> 13) & 0xffff, 16);
                d.b = static_cast<int32_t>((raw >> 5) & 0x3);
            } else {
                d.flags = 0; // sine form
                d.lfo = static_cast<uint8_t>((raw >> 29) & 0x1);
                d.a = static_cast<int32_t>((raw >> 20) & 0x1ff);
                d.b = static_cast<int32_t>((raw >> 5) & 0x7fff);
            }
            break;
        case OP_JAM:
            d.lfo = static_cast<uint8_t>((raw >> 6) & 0x1);
            break;
        case OP_CHO:
            d.cho_type = static_cast<uint8_t>((raw >> 30) & 0x3);
            d.flags = static_cast<uint8_t>((raw >> 24) & 0x3f);
            d.lfo = static_cast<uint8_t>((raw >> 21) & 0x3);
            d.u16 = static_cast<uint16_t>((raw >> 5) & 0xffff);
            break;
        default:
            break;
    }
    return d;
}

struct SineLfo {
    int32_t sin = 0;
    int32_t cos = -Q23_MAX;

    void jam() {
        sin = 0;
        cos = -Q23_MAX;
    }

    void tick(int32_t rate_raw) {
        // Spin's sine generator is conveniently modeled as a coupled
        // fixed-point integrator. The RATE register's upper bits define the
        // recurrence coefficient.
        const int32_t k = rate_raw >> 8;
        const int32_t new_cos = sat24(static_cast<int64_t>(cos) + mul_q23(sin, k));
        const int32_t new_sin = sat24(static_cast<int64_t>(sin) - mul_q23(new_cos, k));
        cos = new_cos;
        sin = new_sin;
    }
};

struct RampLfo {
    int32_t pos = 0;

    void jam() { pos = 0; }

    static uint32_t range_mask(int32_t range_raw) {
        const unsigned shift = static_cast<unsigned>((static_cast<uint32_t>(range_raw) >> 21) & 0x3);
        return 0x3fffffu >> shift;
    }

    void tick(int32_t rate_raw, int32_t range_raw) {
        const int32_t step = rate_raw >> 12;
        const uint32_t mask = range_mask(range_raw);
        pos = static_cast<int32_t>((static_cast<uint32_t>(pos - step)) & mask);
    }

    int32_t value(int32_t range_raw, bool second_pointer) const {
        const uint32_t mask = range_mask(range_raw);
        uint32_t v = static_cast<uint32_t>(pos) & mask;
        if (second_pointer) v = (v + ((mask + 1u) >> 1u)) & mask;
        return static_cast<int32_t>(v);
    }

    int32_t crossfade(int32_t range_raw) const {
        const uint32_t mask = range_mask(range_raw);
        const uint32_t v = static_cast<uint32_t>(pos) & mask;
        const uint32_t half = (mask + 1u) >> 1u;
        uint32_t tri = v < half ? v : (mask - v);
        const unsigned shift = static_cast<unsigned>((static_cast<uint32_t>(range_raw) >> 21) & 0x3);
        // Normalize the triangle to approximately 0..1 in Q1.23.
        uint64_t scaled = static_cast<uint64_t>(tri) << (shift + 2u);
        if (scaled > static_cast<uint64_t>(Q23_MAX)) scaled = Q23_MAX;
        return static_cast<int32_t>(scaled);
    }
};

} // namespace

struct fv1_engine {
    fv1_config config{32768.0, FV1_DELAY_REFERENCE_16};
    std::array<uint32_t, FV1_PROGRAM_WORDS> words{};
    std::array<DecodedInstruction, FV1_PROGRAM_WORDS> program{};
    std::array<int32_t, FV1_REGISTER_COUNT> regs{};
    std::array<int16_t, FV1_DELAY_WORDS> delay16{};
    std::array<int32_t, FV1_DELAY_WORDS> delay24{};
    int32_t acc = 0;
    int32_t pacc = 0;
    int32_t lr = 0;
    uint32_t delay_ptr = 0;
    SineLfo sine[2]{};
    RampLfo ramp[2]{};
    float pot[3]{0.0f, 0.0f, 0.0f};
    bool first_run = true;
    bool program_loaded = false;

    bool debug_sample_active = false;
    bool debug_sample_finished = false;
    uint32_t debug_pc = 0;
    uint64_t sample_counter = 0;
    uint32_t instruction_counter = 0;

    int32_t read_delay(int32_t relative) {
        const uint32_t idx = (delay_ptr + static_cast<uint32_t>(relative)) & DELAY_MASK;
        int32_t v;
        if (config.delay_model == FV1_DELAY_FULL_24) {
            v = delay24[idx];
        } else {
            v = static_cast<int32_t>(delay16[idx]) * 256;
        }
        lr = sat24(v);
        return lr;
    }

    void write_delay(int32_t relative, int32_t value) {
        const uint32_t idx = (delay_ptr + static_cast<uint32_t>(relative)) & DELAY_MASK;
        const int32_t v = sat24(value);
        if (config.delay_model == FV1_DELAY_FULL_24) {
            delay24[idx] = v;
        } else {
            delay16[idx] = static_cast<int16_t>(v >> 8);
        }
    }

    void acc_to_pacc() { pacc = acc; }

    void begin_sample(float in_l, float in_r) {
        regs[FV1_REG_ADCL] = float_to_q23(in_l);
        regs[FV1_REG_ADCR] = float_to_q23(in_r);
        regs[FV1_REG_POT0] = quantize_pot(pot[0]);
        regs[FV1_REG_POT1] = quantize_pot(pot[1]);
        regs[FV1_REG_POT2] = quantize_pot(pot[2]);
        debug_pc = 0;
        instruction_counter = 0;
        debug_sample_active = true;
        debug_sample_finished = false;
    }

    void end_sample() {
        first_run = false;
        delay_ptr = (delay_ptr - 1u) & DELAY_MASK;
        sine[0].tick(regs[FV1_REG_SIN0_RATE]);
        sine[1].tick(regs[FV1_REG_SIN1_RATE]);
        ramp[0].tick(regs[FV1_REG_RMP0_RATE], regs[FV1_REG_RMP0_RANGE]);
        ramp[1].tick(regs[FV1_REG_RMP1_RATE], regs[FV1_REG_RMP1_RANGE]);
        ++sample_counter;
        debug_sample_finished = true;
        debug_sample_active = false;
        instruction_counter = 0;
    }

    int32_t sine_wide(uint8_t idx, bool cosine) const {
        const int32_t base = cosine ? sine[idx].cos : sine[idx].sin;
        const int32_t range = regs[idx == 0 ? FV1_REG_SIN0_RANGE : FV1_REG_SIN1_RANGE];
        return mul_q23(base, range);
    }

    struct ChoValues {
        int32_t wide = 0;
        int32_t fraction = 0;
        int32_t xfade = 0;
    };

    ChoValues cho_values(uint8_t lfo, uint8_t flags) const {
        ChoValues v{};
        if (lfo <= 1) {
            v.wide = sine_wide(lfo, (flags & CHO_COS) != 0);
            if (flags & CHO_COMPA) v.wide = -v.wide;
            // FV-1 CHO splits a wide LFO result into an integer delay-address
            // component and fractional interpolation component. The exact
            // bit partition is hardware-specific; this reference split follows
            // the public address/interpolation relationship and is validated
            // later against hardware test vectors.
            const uint32_t frac10 = static_cast<uint32_t>(v.wide) & 0x3ffu;
            v.fraction = static_cast<int32_t>(frac10 << 13); // 10-bit -> Q1.23
        } else {
            const uint8_t ri = static_cast<uint8_t>(lfo - 2u);
            const int32_t range_reg = regs[ri == 0 ? FV1_REG_RMP0_RANGE : FV1_REG_RMP1_RANGE];
            v.wide = ramp[ri].value(range_reg, (flags & CHO_RPTR2) != 0);
            const uint32_t frac10 = static_cast<uint32_t>(v.wide) & 0x3ffu;
            v.fraction = static_cast<int32_t>(frac10 << 13);
            v.xfade = ramp[ri].crossfade(range_reg);
            if (flags & CHO_COMPA) {
                const uint32_t mask = RampLfo::range_mask(range_reg);
                v.wide = static_cast<int32_t>(mask - (static_cast<uint32_t>(v.wide) & mask));
            }
        }
        return v;
    }

    void execute_instruction(const DecodedInstruction& d, uint32_t& pc, bool& skipped) {
        skipped = false;
        switch (d.opcode) {
            case OP_RDA: {
                acc_to_pacc();
                const int32_t mem = read_delay(static_cast<int32_t>(d.u16));
                acc = sat24(static_cast<int64_t>(acc) + mul_q(mem, d.a, 9));
                break;
            }
            case OP_RMPA: {
                acc_to_pacc();
                const int32_t relative = regs[FV1_REG_ADDR_PTR] >> 8;
                const int32_t mem = read_delay(relative);
                acc = sat24(static_cast<int64_t>(acc) + mul_q(mem, d.a, 9));
                break;
            }
            case OP_WRA: {
                acc_to_pacc();
                write_delay(static_cast<int32_t>(d.u16), acc);
                acc = mul_q(acc, d.a, 9);
                break;
            }
            case OP_WRAP: {
                acc_to_pacc();
                write_delay(static_cast<int32_t>(d.u16), acc);
                acc = sat24(static_cast<int64_t>(mul_q(acc, d.a, 9)) + lr);
                break;
            }
            case OP_RDAX: {
                acc_to_pacc();
                acc = sat24(static_cast<int64_t>(acc) + mul_q(regs[d.reg], d.a, 14));
                break;
            }
            case OP_RDFX: {
                acc_to_pacc();
                const int32_t r = regs[d.reg];
                acc = sat24(static_cast<int64_t>(r) + mul_q(sat24(static_cast<int64_t>(acc) - r), d.a, 14));
                break;
            }
            case OP_WRAX: {
                acc_to_pacc();
                regs[d.reg] = acc;
                acc = mul_q(acc, d.a, 14);
                break;
            }
            case OP_WRHX: {
                const int32_t old_pacc = pacc;
                acc_to_pacc();
                regs[d.reg] = acc;
                acc = sat24(static_cast<int64_t>(mul_q(acc, d.a, 14)) + old_pacc);
                break;
            }
            case OP_WRLX: {
                const int32_t old_pacc = pacc;
                acc_to_pacc();
                regs[d.reg] = acc;
                acc = sat24(static_cast<int64_t>(old_pacc) + mul_q(sat24(static_cast<int64_t>(old_pacc) - acc), d.a, 14));
                break;
            }
            case OP_MAXX: {
                acc_to_pacc();
                const int32_t scaled = mul_q(regs[d.reg], d.a, 14);
                const int64_t aa = std::llabs(static_cast<long long>(acc));
                const int64_t bb = std::llabs(static_cast<long long>(scaled));
                acc = sat24(std::max(aa, bb));
                break;
            }
            case OP_MULX: {
                acc_to_pacc();
                acc = mul_q23(acc, regs[d.reg]);
                break;
            }
            case OP_LOG: {
                acc_to_pacc();
                double y;
                if (acc == 0) {
                    y = -1.0;
                } else {
                    const double mag = std::abs(static_cast<double>(acc) / Q23_ONE);
                    y = std::log2(std::max(mag, std::numeric_limits<double>::min())) / 16.0;
                }
                const int32_t log_q = sat24(static_cast<int64_t>(std::llround(y * Q23_ONE)));
                acc = sat24(static_cast<int64_t>(mul_q(log_q, d.a, 14)) + (static_cast<int64_t>(d.b) * 8192));
                break;
            }
            case OP_EXP: {
                acc_to_pacc();
                const double x = static_cast<double>(acc) / Q23_ONE;
                const double e = x >= 0.0 ? 1.0 : std::exp2(x * 16.0);
                const int32_t exp_q = sat24(static_cast<int64_t>(std::llround(std::min(e, std::nextafter(1.0, 0.0)) * Q23_ONE)));
                acc = sat24(static_cast<int64_t>(mul_q(exp_q, d.a, 14)) + (static_cast<int64_t>(d.b) * 8192));
                break;
            }
            case OP_SOF: {
                acc_to_pacc();
                acc = sat24(static_cast<int64_t>(mul_q(acc, d.a, 14)) + (static_cast<int64_t>(d.b) * 8192));
                break;
            }
            case OP_AND: {
                acc_to_pacc();
                const uint32_t a24 = static_cast<uint32_t>(acc) & 0xffffffu;
                acc = sat24(sign_extend(a24 & static_cast<uint32_t>(d.a), 24));
                break;
            }
            case OP_OR: {
                acc_to_pacc();
                const uint32_t a24 = static_cast<uint32_t>(acc) & 0xffffffu;
                acc = sat24(sign_extend(a24 | static_cast<uint32_t>(d.a), 24));
                break;
            }
            case OP_XOR: {
                acc_to_pacc();
                const uint32_t a24 = static_cast<uint32_t>(acc) & 0xffffffu;
                acc = sat24(sign_extend(a24 ^ static_cast<uint32_t>(d.a), 24));
                break;
            }
            case OP_SKP: {
                // A zero SKP condition mask is an unconditional forward skip.
                // The canonical NOP is the same operation with a skip count of 0.
                bool take = (d.skip_cond == 0);
                if (d.skip_cond & SKP_RUN) take |= !first_run;
                if (d.skip_cond & SKP_ZRO) take |= (acc == 0);
                if (d.skip_cond & SKP_GEZ) take |= (acc >= 0);
                if (d.skip_cond & SKP_NEG) take |= (acc < 0);
                if (d.skip_cond & SKP_ZRC) take |= ((acc < 0) != (pacc < 0));
                if (take) {
                    pc = std::min<uint32_t>(FV1_PROGRAM_WORDS, pc + static_cast<uint32_t>(d.skip_count) + 1u);
                    skipped = true;
                    return;
                }
                break;
            }
            case OP_WLDS: {
                if (d.flags == 0) {
                    const uint8_t i = d.lfo & 1u;
                    regs[i == 0 ? FV1_REG_SIN0_RATE : FV1_REG_SIN1_RATE] = sat24(static_cast<int64_t>(d.a) * 16384);
                    regs[i == 0 ? FV1_REG_SIN0_RANGE : FV1_REG_SIN1_RANGE] = sat24(static_cast<int64_t>(d.b) * 256);
                    sine[i].jam();
                } else {
                    const uint8_t i = d.lfo & 1u;
                    regs[i == 0 ? FV1_REG_RMP0_RATE : FV1_REG_RMP1_RATE] = sat24(static_cast<int64_t>(d.a) * 256);
                    regs[i == 0 ? FV1_REG_RMP0_RANGE : FV1_REG_RMP1_RANGE] = static_cast<int32_t>((d.b & 0x3) << 21);
                    ramp[i].jam();
                }
                break;
            }
            case OP_JAM: {
                ramp[d.lfo & 1u].jam();
                break;
            }
            case OP_CHO: {
                const ChoValues cv = cho_values(d.lfo, d.flags);
                int32_t coeff;
                if ((d.flags & CHO_NA) && d.lfo >= 2) {
                    coeff = cv.xfade;
                } else {
                    coeff = cv.fraction;
                }
                if (d.flags & CHO_COMPC) coeff = Q23_MAX - coeff;

                if (d.cho_type == CHO_TYPE_RDA) {
                    acc_to_pacc();
                    int32_t address = static_cast<int32_t>(d.u16);
                    if (!(d.flags & CHO_NA)) {
                        address += cv.wide >> 10;
                    }
                    const int32_t mem = read_delay(address);
                    acc = sat24(static_cast<int64_t>(acc) + mul_q23(mem, coeff));
                } else if (d.cho_type == CHO_TYPE_SOF) {
                    acc_to_pacc();
                    const int32_t offset_q23 = sign_extend(d.u16, 16) * 256;
                    acc = sat24(static_cast<int64_t>(mul_q23(acc, coeff)) + offset_q23);
                } else if (d.cho_type == CHO_TYPE_RDAL) {
                    acc_to_pacc();
                    if (d.lfo <= 1) {
                        acc = sine_wide(d.lfo, (d.flags & CHO_COS) != 0);
                    } else {
                        const uint8_t i = static_cast<uint8_t>(d.lfo - 2u);
                        const int32_t range_reg = regs[i == 0 ? FV1_REG_RMP0_RANGE : FV1_REG_RMP1_RANGE];
                        acc = sat24(static_cast<int64_t>(ramp[i].value(range_reg, false)) * 2);
                    }
                }
                break;
            }
            default:
                break;
        }
        ++pc;
    }

    fv1_result step_one(fv1_trace* trace) {
        if (!debug_sample_active || debug_sample_finished) return FV1_ERROR_BAD_STATE;

        if (trace) std::memset(trace, 0, sizeof(*trace));
        if (debug_pc >= FV1_PROGRAM_WORDS) {
            const uint64_t sample_index = sample_counter;
            const uint32_t instruction_index = instruction_counter;
            end_sample();
            if (trace) {
                trace->sample_finished = 1;
                trace->sample_index = sample_index;
                trace->instruction_index = instruction_index;
            }
            return FV1_OK;
        }

        const uint64_t sample_index = sample_counter;
        const uint32_t instruction_index = instruction_counter;
        const uint32_t pc_before = debug_pc;
        const DecodedInstruction& d = program[pc_before];

        if (trace) {
            trace->pc_before = pc_before;
            trace->raw_instruction = d.raw;
            trace->opcode = d.opcode;
            trace->acc_before = acc;
            trace->sample_index = sample_index;
            trace->instruction_index = instruction_index;
        }

        bool skipped = false;
        execute_instruction(d, debug_pc, skipped);
        ++instruction_counter;

        if (trace) {
            trace->pc_after = debug_pc;
            trace->acc_after = acc;
            trace->pacc_after = pacc;
            trace->lr_after = lr;
            trace->skipped = skipped ? 1 : 0;
        }

        if (debug_pc >= FV1_PROGRAM_WORDS) {
            end_sample();
            if (trace) trace->sample_finished = 1;
        }
        return FV1_OK;
    }
};

extern "C" {

fv1_engine* fv1_create(const fv1_config* config) {
    fv1_engine* e = new (std::nothrow) fv1_engine();
    if (!e) return nullptr;
    if (config) {
        e->config = *config;
        if (!(e->config.virtual_sample_rate > 0.0)) e->config.virtual_sample_rate = 32768.0;
    }
    fv1_reset(e, 1);
    return e;
}

void fv1_destroy(fv1_engine* engine) {
    delete engine;
}

void fv1_reset(fv1_engine* e, int clear_delay_ram) {
    if (!e) return;
    e->regs.fill(0);
    e->acc = 0;
    e->pacc = 0;
    e->lr = 0;
    e->delay_ptr = 0;
    e->sine[0].jam();
    e->sine[1].jam();
    e->ramp[0].jam();
    e->ramp[1].jam();
    e->first_run = true;
    e->debug_sample_active = false;
    e->debug_sample_finished = false;
    e->debug_pc = 0;
    e->sample_counter = 0;
    e->instruction_counter = 0;
    if (clear_delay_ram) {
        e->delay16.fill(0);
        e->delay24.fill(0);
    }
}

fv1_result fv1_load_words(fv1_engine* e, const uint32_t* words, size_t count) {
    if (!e || !words || count != FV1_PROGRAM_WORDS) return FV1_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < FV1_PROGRAM_WORDS; ++i) {
        e->words[i] = words[i];
        e->program[i] = decode(words[i]);
        if (e->program[i].opcode > OP_CHO) return FV1_ERROR_INVALID_PROGRAM;
    }
    e->program_loaded = true;
    fv1_reset(e, 1);
    return FV1_OK;
}

fv1_result fv1_load_bytes(fv1_engine* e, const uint8_t* bytes, size_t size) {
    if (!e || !bytes || size != FV1_PROGRAM_BYTES) return FV1_ERROR_INVALID_ARGUMENT;
    std::array<uint32_t, FV1_PROGRAM_WORDS> words{};
    for (size_t i = 0; i < FV1_PROGRAM_WORDS; ++i) {
        const size_t o = i * 4;
        words[i] = (static_cast<uint32_t>(bytes[o]) << 24) |
                   (static_cast<uint32_t>(bytes[o + 1]) << 16) |
                   (static_cast<uint32_t>(bytes[o + 2]) << 8) |
                   static_cast<uint32_t>(bytes[o + 3]);
    }
    return fv1_load_words(e, words.data(), words.size());
}

fv1_result fv1_get_program_words(const fv1_engine* e, uint32_t* words, size_t count) {
    if (!e || !words || count < FV1_PROGRAM_WORDS) return FV1_ERROR_INVALID_ARGUMENT;
    std::copy(e->words.begin(), e->words.end(), words);
    return FV1_OK;
}

void fv1_set_pots(fv1_engine* e, float pot0, float pot1, float pot2) {
    if (!e) return;
    const auto sanitize = [](float value) noexcept {
        if (std::isnan(value)) return 0.0f;
        if (std::isinf(value)) return value > 0.0f ? 1.0f : 0.0f;
        return std::clamp(value, 0.0f, 1.0f);
    };
    e->pot[0] = sanitize(pot0);
    e->pot[1] = sanitize(pot1);
    e->pot[2] = sanitize(pot2);
}

fv1_result fv1_debug_begin_sample(fv1_engine* e, float in_l, float in_r) {
    if (!e) return FV1_ERROR_INVALID_ARGUMENT;
    if (!e->program_loaded) return FV1_ERROR_BAD_STATE;
    if (e->debug_sample_active) return FV1_ERROR_BAD_STATE;
    e->begin_sample(in_l, in_r);
    return FV1_OK;
}

fv1_result fv1_debug_step_instruction(fv1_engine* e, fv1_trace* trace) {
    if (!e || !trace) return FV1_ERROR_INVALID_ARGUMENT;
    return e->step_one(trace);
}

fv1_result fv1_debug_finish_sample(fv1_engine* e, float* out_l, float* out_r) {
    if (!e || !out_l || !out_r) return FV1_ERROR_INVALID_ARGUMENT;
    if (e->debug_sample_active) return FV1_ERROR_BAD_STATE;
    if (!e->debug_sample_finished) return FV1_ERROR_BAD_STATE;
    *out_l = q23_to_float(e->regs[FV1_REG_DACL]);
    *out_r = q23_to_float(e->regs[FV1_REG_DACR]);
    e->debug_sample_finished = false;
    return FV1_OK;
}

fv1_result fv1_process_sample(fv1_engine* e, float in_l, float in_r, float* out_l, float* out_r) {
    if (!e || !out_l || !out_r) return FV1_ERROR_INVALID_ARGUMENT;
    if (!e->program_loaded) return FV1_ERROR_BAD_STATE;
    if (e->debug_sample_active) return FV1_ERROR_BAD_STATE;

    e->begin_sample(in_l, in_r);
    while (e->debug_sample_active) {
        const fv1_result step_result = e->step_one(nullptr);
        if (step_result != FV1_OK) return step_result;
    }
    *out_l = q23_to_float(e->regs[FV1_REG_DACL]);
    *out_r = q23_to_float(e->regs[FV1_REG_DACR]);
    e->debug_sample_finished = false;
    return FV1_OK;
}

fv1_result fv1_process_block(fv1_engine* e,
                             const float* in_l, const float* in_r,
                             float* out_l, float* out_r,
                             size_t frames) {
    if (!e || !in_l || !in_r || !out_l || !out_r) return FV1_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < frames; ++i) {
        const fv1_result r = fv1_process_sample(e, in_l[i], in_r[i], &out_l[i], &out_r[i]);
        if (r != FV1_OK) return r;
    }
    return FV1_OK;
}

void fv1_get_snapshot(const fv1_engine* e, fv1_snapshot* s) {
    if (!e || !s) return;
    std::memset(s, 0, sizeof(*s));
    s->acc = e->acc;
    s->pacc = e->pacc;
    s->lr = e->lr;
    std::copy(e->regs.begin(), e->regs.end(), s->regs);
    s->delay_pointer = e->delay_ptr;
    s->sin_lfo[0] = e->sine_wide(0, false);
    s->sin_lfo[1] = e->sine_wide(1, false);
    s->cos_lfo[0] = e->sine_wide(0, true);
    s->cos_lfo[1] = e->sine_wide(1, true);
    s->ramp_lfo[0] = e->ramp[0].value(e->regs[FV1_REG_RMP0_RANGE], false);
    s->ramp_lfo[1] = e->ramp[1].value(e->regs[FV1_REG_RMP1_RANGE], false);
    s->program_counter = e->debug_pc;
    s->first_run = e->first_run ? 1 : 0;
    s->debug_sample_active = e->debug_sample_active ? 1 : 0;
    s->sample_counter = e->sample_counter;
    s->instruction_counter = e->instruction_counter;
}

fv1_result fv1_get_state_digest(const fv1_engine* e, fv1_state_digest* digest) {
    if (!e || !digest) return FV1_ERROR_INVALID_ARGUMENT;

    uint64_t arch = FNV_OFFSET;
    hash_integer_le(arch, e->acc);
    hash_integer_le(arch, e->pacc);
    hash_integer_le(arch, e->lr);
    for (const int32_t reg : e->regs) hash_integer_le(arch, reg);
    hash_integer_le(arch, e->delay_ptr);
    for (const auto& lfo : e->sine) {
        hash_integer_le(arch, lfo.sin);
        hash_integer_le(arch, lfo.cos);
    }
    for (const auto& lfo : e->ramp) hash_integer_le(arch, lfo.pos);
    hash_u8(arch, e->first_run ? 1u : 0u);
    hash_u8(arch, e->program_loaded ? 1u : 0u);
    hash_u8(arch, e->debug_sample_active ? 1u : 0u);
    hash_u8(arch, e->debug_sample_finished ? 1u : 0u);
    hash_integer_le(arch, e->debug_pc);
    hash_integer_le(arch, e->sample_counter);
    hash_integer_le(arch, e->instruction_counter);

    uint64_t delay = FNV_OFFSET;
    for (uint32_t i = 0; i < FV1_DELAY_WORDS; ++i) {
        const int32_t value = e->config.delay_model == FV1_DELAY_FULL_24
            ? sat24(e->delay24[i])
            : sat24(static_cast<int32_t>(e->delay16[i]) * 256);
        hash_integer_le(delay, value);
    }

    digest->architectural_hash = arch;
    digest->delay_hash = delay;
    digest->sample_counter = e->sample_counter;
    return FV1_OK;
}

fv1_result fv1_read_delay_word(const fv1_engine* e, uint32_t address, int32_t* value) {
    if (!e || !value || address >= FV1_DELAY_WORDS) return FV1_ERROR_INVALID_ARGUMENT;
    if (e->config.delay_model == FV1_DELAY_FULL_24)
        *value = sat24(e->delay24[address]);
    else
        *value = sat24(static_cast<int32_t>(e->delay16[address]) * 256);
    return FV1_OK;
}

fv1_result fv1_analyze_program(const fv1_engine* e, fv1_resource_report* r) {
    if (!e || !r) return FV1_ERROR_INVALID_ARGUMENT;
    if (!e->program_loaded) return FV1_ERROR_BAD_STATE;
    std::memset(r, 0, sizeof(*r));

    bool regs_used[32]{};
    bool pots_used[3]{};
    bool sine_used[2]{};
    bool ramp_used[2]{};
    int last_used = -1;

    auto note_reg = [&](uint8_t reg) {
        if (reg >= FV1_REG0 && reg <= FV1_REG31) regs_used[reg - FV1_REG0] = true;
        if (reg >= FV1_REG_POT0 && reg <= FV1_REG_POT2) pots_used[reg - FV1_REG_POT0] = true;
        if (reg == FV1_REG_SIN0_RATE || reg == FV1_REG_SIN0_RANGE) sine_used[0] = true;
        if (reg == FV1_REG_SIN1_RATE || reg == FV1_REG_SIN1_RANGE) sine_used[1] = true;
        if (reg == FV1_REG_RMP0_RATE || reg == FV1_REG_RMP0_RANGE) ramp_used[0] = true;
        if (reg == FV1_REG_RMP1_RATE || reg == FV1_REG_RMP1_RANGE) ramp_used[1] = true;
    };

    for (size_t i = 0; i < FV1_PROGRAM_WORDS; ++i) {
        const auto& d = e->program[i];
        const bool canonical_nop = d.raw == 0x00000011u;
        if (!canonical_nop) last_used = static_cast<int>(i);
        if (canonical_nop) continue;
        if (d.opcode < 32) r->opcode_histogram[d.opcode]++;

        switch (d.opcode) {
            case OP_RDA:
                r->static_delay_reads++;
                r->highest_static_delay_address = std::max<uint32_t>(r->highest_static_delay_address, d.u16 & 0x7fff);
                break;
            case OP_RMPA:
                r->dynamic_delay_reads++;
                break;
            case OP_WRA:
            case OP_WRAP:
                r->static_delay_writes++;
                r->highest_static_delay_address = std::max<uint32_t>(r->highest_static_delay_address, d.u16 & 0x7fff);
                break;
            case OP_RDAX:
            case OP_RDFX:
            case OP_WRAX:
            case OP_WRHX:
            case OP_WRLX:
            case OP_MAXX:
            case OP_MULX:
                note_reg(d.reg);
                break;
            case OP_SKP:
                r->skip_instructions++;
                break;
            case OP_WLDS:
                if (d.flags == 0) sine_used[d.lfo & 1u] = true;
                else ramp_used[d.lfo & 1u] = true;
                break;
            case OP_JAM:
                ramp_used[d.lfo & 1u] = true;
                break;
            case OP_CHO:
                if (d.lfo <= 1) sine_used[d.lfo] = true;
                else ramp_used[d.lfo - 2u] = true;
                if (d.cho_type == CHO_TYPE_RDA) {
                    r->static_delay_reads++;
                    r->highest_static_delay_address = std::max<uint32_t>(r->highest_static_delay_address, d.u16 & 0x7fff);
                }
                break;
            default:
                break;
        }
    }

    r->used_instructions = last_used < 0 ? 0u : static_cast<uint32_t>(last_used + 1);
    for (bool v : regs_used) r->general_registers_used += v ? 1u : 0u;
    for (bool v : pots_used) r->pots_used += v ? 1u : 0u;
    for (bool v : sine_used) r->sine_lfos_used += v ? 1u : 0u;
    for (bool v : ramp_used) r->ramp_lfos_used += v ? 1u : 0u;

    // All FV-1 SKP offsets are forward-only. Compute the longest possible
    // path through the meaningful portion of the program as a DAG.
    const uint32_t n = r->used_instructions;
    std::array<uint32_t, FV1_PROGRAM_WORDS + 1> dp{};
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        const auto& d = e->program[static_cast<size_t>(i)];
        uint32_t best_next = dp[static_cast<size_t>(i + 1)];
        if (d.opcode == OP_SKP && d.skip_cond != 0) {
            const uint32_t target = std::min<uint32_t>(n, static_cast<uint32_t>(i) + d.skip_count + 1u);
            best_next = std::max(best_next, dp[target]);
        } else if (d.opcode == OP_SKP && d.skip_cond == 0 && d.skip_count > 0) {
            const uint32_t target = std::min<uint32_t>(n, static_cast<uint32_t>(i) + d.skip_count + 1u);
            best_next = dp[target];
        }
        dp[static_cast<size_t>(i)] = 1u + best_next;
    }
    r->worst_case_path = n ? dp[0] : 0;

    return FV1_OK;
}

const char* fv1_opcode_name(uint8_t opcode) {
    switch (opcode) {
        case OP_RDA: return "RDA";
        case OP_RMPA: return "RMPA";
        case OP_WRA: return "WRA";
        case OP_WRAP: return "WRAP";
        case OP_RDAX: return "RDAX";
        case OP_RDFX: return "RDFX/LDAX";
        case OP_WRAX: return "WRAX";
        case OP_WRHX: return "WRHX";
        case OP_WRLX: return "WRLX";
        case OP_MAXX: return "MAXX/ABSA";
        case OP_MULX: return "MULX";
        case OP_LOG: return "LOG";
        case OP_EXP: return "EXP";
        case OP_SOF: return "SOF";
        case OP_AND: return "AND/CLR";
        case OP_OR: return "OR";
        case OP_XOR: return "XOR/NOT";
        case OP_SKP: return "SKP/NOP";
        case OP_WLDS: return "WLDS/WLDR";
        case OP_JAM: return "JAM";
        case OP_CHO: return "CHO";
        default: return "UNKNOWN";
    }
}

const char* fv1_result_string(fv1_result result) {
    switch (result) {
        case FV1_OK: return "ok";
        case FV1_ERROR_INVALID_ARGUMENT: return "invalid argument";
        case FV1_ERROR_INVALID_PROGRAM: return "invalid program";
        case FV1_ERROR_BAD_STATE: return "bad engine state";
        case FV1_ERROR_IO: return "I/O error";
        case FV1_ERROR_UNSUPPORTED: return "unsupported";
        default: return "unknown error";
    }
}

} // extern "C"
