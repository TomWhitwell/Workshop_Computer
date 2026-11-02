// SPDX-License-Identifier: MPL-2.0
// Derived from Roth-Amplification-Ltd/Spin-FV-1-Emulator src/core/engine.cpp
// commit 4319ab42af0c9cb3a8d5c3881d60d60041982d30. See vendor/dsp-reference/LICENSE.
// Adaptations: static state, Q23 I/O, AOT constant folding, integer LOG/EXP LUTs,
// one 16-bit delay bank; no allocator, decoder loop, host floats, or debug API.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <processor/processor.h>
#include "math_tables.hpp"
namespace spin {
constexpr int32_t Q23_ONE = 1 << 23;
constexpr int32_t Q23_MAX = Q23_ONE - 1;
constexpr int32_t Q23_MIN = -Q23_ONE;
constexpr uint32_t DELAY_MASK = FV1_DELAY_WORDS - 1;
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

inline int32_t sat24(int32_t x) {
    if(x>Q23_MAX)return Q23_MAX;
    if(x<Q23_MIN)return Q23_MIN;
    return x;
}


constexpr int32_t sign_extend(uint32_t x, unsigned bits) {
    const uint32_t m = 1u << (bits - 1u);
    x &= (1u << bits) - 1u;
    return static_cast<int32_t>((x ^ m) - m);
}

// Exact decomposed multiplies: RP2040 has fast 32-bit MUL, no 64-bit MUL.
// Avoid libgcc __aeabi_lmul while preserving every truncated fractional bit.
inline int32_t mul_q(int32_t value, int32_t coefficient, unsigned bits) {
    // Operand invariant: all semantic machine values are already saturated Q23.
    if(coefficient==0)return 0;
    if(coefficient==(1<<bits))return value;
    if(coefficient==-(1<<bits))return sat24(-value);
    const int32_t mask=(1<<bits)-1;
    const int32_t product=(value>>bits)*coefficient + (((value&mask)*coefficient)>>bits);
    if(coefficient>-(1<<bits)&&coefficient<(1<<bits))return product;
    return sat24(product);
}
// Delay reads have eight zero fractional bits. Exploit that invariant rather
// than performing a general 24 x 24 multiply on the Cortex-M0+.
inline int32_t mul_delay_q23(int32_t value,int32_t coefficient) {
    const int32_t sample=value>>8;
    return sat24(sample*(coefficient>>15)+((sample*(coefficient&32767))>>15));
}
inline int32_t mul_delay_q9(int32_t value,int32_t coefficient) {
    return sat24(((value>>8)*coefficient)>>1);
}
inline int32_t mul_q23(int32_t a,int32_t b) {
    const int32_t ah=a>>12,bh=b>>12,al=a&4095,bl=b&4095;
    const int32_t cross=ah*bl+al*bh;
    const int32_t product=2*ah*bh+(cross>>11)+((((cross&2047)<<12)+al*bl)>>23);
    // Q23×Q23 cannot underflow; only (-1)×(-1) reaches positive full scale.
    return product==Q23_ONE?Q23_MAX:product;
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

constexpr DecodedInstruction decode(uint32_t raw) {
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
        if(k==0)return;
        const int32_t new_cos = sat24(cos + mul_q23(sin, k));
        const int32_t new_sin = sat24(sin - mul_q23(new_cos, k));
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
        uint32_t scaled = tri << (shift + 2u);
        if (scaled > uint32_t(Q23_MAX)) scaled = Q23_MAX;
        return static_cast<int32_t>(scaled);
    }
};


struct Machine {
    std::array<int32_t,64> regs{};
    std::array<int16_t,32768> delay16{};
    int32_t acc=0, pacc=0, lr=0;
    uint32_t delay_ptr=0;
    SineLfo sine[2]{};
    RampLfo ramp[2]{};
    bool first_run=true;
    void reset_state() {
        regs.fill(0); acc=pacc=lr=0; delay_ptr=0; first_run=true;
        for(auto &s:sine) s.jam();
        for(auto &r:ramp) r.jam();
    }
    void reset() { reset_state(); delay16.fill(0); }
    void begin(int32_t l,int32_t r,const uint16_t *pots) {
        regs[0x14]=sat24(l); regs[0x15]=sat24(r);
        for(int i=0;i<3;++i) regs[0x10+i]=std::min<uint16_t>(511,pots[i])*16384;
    }
    int32_t read_delay(int32_t address) {
        lr=int32_t(delay16[(delay_ptr+uint32_t(address))&32767])*256;
        return lr;
    }
    void write_delay(int32_t address,int32_t value) {
        delay16[(delay_ptr+uint32_t(address))&32767]=int16_t(sat24(value)>>8);
    }
    static int32_t log_fixed(int32_t x) {
        uint32_t m=uint32_t(x<0?-x:x);
        if(!m) return -8388608;
        unsigned e=0; uint32_t t=m;
        while(t>>1) {t>>=1; ++e;}
        const uint32_t norm=m<<(23-e);
        const uint32_t f=norm-8388608;
        unsigned i=f>>13; uint32_t rem=f&8191;
        int32_t y=log_table[i]+int32_t((int32_t(log_table[i+1]-log_table[i])*rem+4096)>>13);
        return sat24((int32_t(e)-23)*524288+y);
    }
    static int32_t exp_fixed(int32_t x) {
        if(x>=0) return 8388607;
        // x*16 is Q19. floor into an integer exponent and positive fraction.
        int32_t e=x>>19; uint32_t f=uint32_t(x)&524287;
        unsigned i=f>>9; uint32_t rem=f&511;
        int32_t y=exp_table[i]+int32_t((int32_t(exp_table[i+1]-exp_table[i])*rem+256)>>9);
        return sat24((y+(1<<(-e-1)))>>(-e));
    }
    void acc_to_pacc() { pacc = acc; }

    void end_sample() {
        first_run = false;
        delay_ptr = (delay_ptr - 1u) & DELAY_MASK;
        sine[0].tick(regs[FV1_REG_SIN0_RATE]);
        sine[1].tick(regs[FV1_REG_SIN1_RATE]);
        ramp[0].tick(regs[FV1_REG_RMP0_RATE], regs[FV1_REG_RMP0_RANGE]);
        ramp[1].tick(regs[FV1_REG_RMP1_RATE], regs[FV1_REG_RMP1_RANGE]);

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

    __attribute__((always_inline)) inline void execute_instruction(const DecodedInstruction& d, uint32_t& pc, bool& skipped) {
        skipped = false;
        switch (d.opcode) {
            case OP_RDA: {
                acc_to_pacc();
                const int32_t mem = read_delay(static_cast<int32_t>(d.u16));
                acc = sat24(acc + mul_delay_q9(mem, d.a));
                break;
            }
            case OP_RMPA: {
                acc_to_pacc();
                const int32_t relative = regs[FV1_REG_ADDR_PTR] >> 8;
                const int32_t mem = read_delay(relative);
                acc = sat24(acc + mul_delay_q9(mem, d.a));
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
                acc = sat24(mul_q(acc, d.a, 9) + lr);
                break;
            }
            case OP_RDAX: {
                acc_to_pacc();
                acc = sat24(acc + mul_q(regs[d.reg], d.a, 14));
                break;
            }
            case OP_RDFX: {
                acc_to_pacc();
                const int32_t r = regs[d.reg];
                acc = sat24(r + mul_q(sat24(acc - r), d.a, 14));
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
                acc = sat24(mul_q(acc, d.a, 14) + old_pacc);
                break;
            }
            case OP_WRLX: {
                const int32_t old_pacc = pacc;
                acc_to_pacc();
                regs[d.reg] = acc;
                acc = sat24(old_pacc + mul_q(sat24(old_pacc - acc), d.a, 14));
                break;
            }
            case OP_MAXX: {
                acc_to_pacc();
                const int32_t scaled = mul_q(regs[d.reg], d.a, 14);
                const int32_t aa = std::abs(acc);
                const int32_t bb = std::abs(scaled);
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
                const int32_t log_q = log_fixed(acc);
                acc = sat24(mul_q(log_q, d.a, 14) + (d.b * 8192));
                break;
            }
            case OP_EXP: {
                acc_to_pacc();
                const int32_t exp_q = exp_fixed(acc);
                acc = sat24(mul_q(exp_q, d.a, 14) + (d.b * 8192));
                break;
            }
            case OP_SOF: {
                acc_to_pacc();
                acc = sat24(mul_q(acc, d.a, 14) + (d.b * 8192));
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
                    regs[i == 0 ? FV1_REG_SIN0_RATE : FV1_REG_SIN1_RATE] = sat24(d.a * 16384);
                    regs[i == 0 ? FV1_REG_SIN0_RANGE : FV1_REG_SIN1_RANGE] = sat24(d.b * 256);
                    sine[i].jam();
                } else {
                    const uint8_t i = d.lfo & 1u;
                    regs[i == 0 ? FV1_REG_RMP0_RATE : FV1_REG_RMP1_RATE] = sat24(d.a * 256);
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
                    acc = sat24(acc + mul_delay_q23(mem, coeff));
                } else if (d.cho_type == CHO_TYPE_SOF) {
                    acc_to_pacc();
                    const int32_t offset_q23 = sign_extend(d.u16, 16) * 256;
                    acc = sat24(mul_q23(acc, coeff) + offset_q23);
                } else if (d.cho_type == CHO_TYPE_RDAL) {
                    acc_to_pacc();
                    if (d.lfo <= 1) {
                        acc = sine_wide(d.lfo, (d.flags & CHO_COS) != 0);
                    } else {
                        const uint8_t i = static_cast<uint8_t>(d.lfo - 2u);
                        const int32_t range_reg = regs[i == 0 ? FV1_REG_RMP0_RANGE : FV1_REG_RMP1_RANGE];
                        acc = sat24(ramp[i].value(range_reg, false) * 2);
                    }
                }
                break;
            }
            default:
                break;
        }
        ++pc;
    }

};
using Program = void (*)(Machine&);
} // namespace spin
