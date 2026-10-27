#include <processor/reference_model.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace fv1 {
namespace {

constexpr std::int32_t kOne = 1 << 23;
constexpr std::int32_t kMax = kOne - 1;
constexpr std::int32_t kMin = -kOne;
constexpr std::uint32_t kDelayMask = FV1_DELAY_WORDS - 1u;
constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

constexpr std::uint8_t kRda = 0x00;
constexpr std::uint8_t kRmpa = 0x01;
constexpr std::uint8_t kWra = 0x02;
constexpr std::uint8_t kWrap = 0x03;
constexpr std::uint8_t kRdax = 0x04;
constexpr std::uint8_t kRdfx = 0x05;
constexpr std::uint8_t kWrax = 0x06;
constexpr std::uint8_t kWrhx = 0x07;
constexpr std::uint8_t kWrlx = 0x08;
constexpr std::uint8_t kMaxx = 0x09;
constexpr std::uint8_t kMulx = 0x0a;
constexpr std::uint8_t kLog = 0x0b;
constexpr std::uint8_t kExp = 0x0c;
constexpr std::uint8_t kSof = 0x0d;
constexpr std::uint8_t kAnd = 0x0e;
constexpr std::uint8_t kOr = 0x0f;
constexpr std::uint8_t kXor = 0x10;
constexpr std::uint8_t kSkp = 0x11;
constexpr std::uint8_t kWlds = 0x12;
constexpr std::uint8_t kJam = 0x13;
constexpr std::uint8_t kCho = 0x14;

constexpr std::uint8_t kSkpNeg = 0x01;
constexpr std::uint8_t kSkpGez = 0x02;
constexpr std::uint8_t kSkpZro = 0x04;
constexpr std::uint8_t kSkpZrc = 0x08;
constexpr std::uint8_t kSkpRun = 0x10;

constexpr std::uint8_t kChoCos = 0x01;
constexpr std::uint8_t kChoCompc = 0x04;
constexpr std::uint8_t kChoCompa = 0x08;
constexpr std::uint8_t kChoRptr2 = 0x10;
constexpr std::uint8_t kChoNa = 0x20;

constexpr std::uint8_t kChoRda = 0;
constexpr std::uint8_t kChoSof = 2;
constexpr std::uint8_t kChoRdal = 3;

std::int32_t clamp24(std::int64_t value) {
    if (value > kMax) return kMax;
    if (value < kMin) return kMin;
    return static_cast<std::int32_t>(value);
}

std::int32_t sx(std::uint32_t value, unsigned bits) {
    const std::uint32_t sign = 1u << (bits - 1u);
    const std::uint32_t mask = (1u << bits) - 1u;
    value &= mask;
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

std::int32_t fixed_mul(std::int32_t value, std::int32_t coefficient, unsigned fractional_bits) {
    const std::int64_t product = static_cast<std::int64_t>(value) * static_cast<std::int64_t>(coefficient);
    return clamp24(product >> fractional_bits);
}

std::int32_t fixed_mul23(std::int32_t left, std::int32_t right) {
    const std::int64_t product = static_cast<std::int64_t>(left) * static_cast<std::int64_t>(right);
    return clamp24(product >> 23);
}

std::int32_t from_float(float value) {
    const double bounded = std::clamp(static_cast<double>(value), -1.0, std::nextafter(1.0, 0.0));
    return clamp24(static_cast<std::int64_t>(std::llround(bounded * static_cast<double>(kOne))));
}

float to_float(std::int32_t value) {
    return static_cast<float>(static_cast<double>(value) / static_cast<double>(kOne));
}

std::int32_t pot_value(float value) {
    const float bounded = std::clamp(value, 0.0f, 1.0f);
    const int code = std::clamp(static_cast<int>(std::lround(bounded * 511.0f)), 0, 511);
    return static_cast<std::int32_t>(code * 16384);
}

void hash_byte(std::uint64_t& hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= kFnvPrime;
}

template <typename T>
void hash_le(std::uint64_t& hash, T value) {
    using U = std::make_unsigned_t<T>;
    U u = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        hash_byte(hash, static_cast<std::uint8_t>(u & static_cast<U>(0xffu)));
        u >>= 8u;
    }
}

struct RefInstruction {
    std::uint32_t raw{};
    std::uint8_t opcode{};
    std::uint8_t reg{};
    std::uint8_t flags{};
    std::uint8_t lfo{};
    std::uint8_t cho_type{};
    std::uint8_t condition{};
    std::uint8_t skip{};
    std::uint16_t address{};
    std::int32_t first{};
    std::int32_t second{};
};

RefInstruction parse_word(std::uint32_t word) {
    RefInstruction i{};
    i.raw = word;
    i.opcode = static_cast<std::uint8_t>(word & 31u);

    if (i.opcode == kRda || i.opcode == kWra || i.opcode == kWrap) {
        i.address = static_cast<std::uint16_t>((word >> 5u) & 0x7fffu);
        i.first = sx((word >> 21u) & 0x7ffu, 11);
    } else if (i.opcode == kRmpa) {
        /* The production model currently follows the 11-bit coefficient
           encoding used by the project's SpinASM-compatible assembler.  The
           Hardware Emulation Contract marks exact RMPA encoding/precision for
           future cross-check against original SpinAsm/silicon evidence. */
        i.first = sx((word >> 21u) & 0x7ffu, 11);
    } else if (i.opcode >= kRdax && i.opcode <= kMaxx) {
        i.reg = static_cast<std::uint8_t>((word >> 5u) & 0x3fu);
        i.first = sx((word >> 16u) & 0xffffu, 16);
    } else if (i.opcode == kMulx) {
        i.reg = static_cast<std::uint8_t>((word >> 5u) & 0x3fu);
    } else if (i.opcode == kLog || i.opcode == kExp || i.opcode == kSof) {
        i.first = sx((word >> 16u) & 0xffffu, 16);
        i.second = sx((word >> 5u) & 0x7ffu, 11);
    } else if (i.opcode == kAnd || i.opcode == kOr || i.opcode == kXor) {
        i.first = static_cast<std::int32_t>((word >> 8u) & 0xffffffu);
    } else if (i.opcode == kSkp) {
        i.condition = static_cast<std::uint8_t>((word >> 27u) & 0x1fu);
        i.skip = static_cast<std::uint8_t>((word >> 21u) & 0x3fu);
    } else if (i.opcode == kWlds) {
        if ((word & 0x40000000u) != 0u) {
            i.flags = 1;
            i.lfo = static_cast<std::uint8_t>((word >> 29u) & 1u);
            i.first = sx((word >> 13u) & 0xffffu, 16);
            i.second = static_cast<std::int32_t>((word >> 5u) & 3u);
        } else {
            i.flags = 0;
            i.lfo = static_cast<std::uint8_t>((word >> 29u) & 1u);
            i.first = static_cast<std::int32_t>((word >> 20u) & 0x1ffu);
            i.second = static_cast<std::int32_t>((word >> 5u) & 0x7fffu);
        }
    } else if (i.opcode == kJam) {
        i.lfo = static_cast<std::uint8_t>((word >> 6u) & 1u);
    } else if (i.opcode == kCho) {
        i.cho_type = static_cast<std::uint8_t>((word >> 30u) & 3u);
        i.flags = static_cast<std::uint8_t>((word >> 24u) & 0x3fu);
        i.lfo = static_cast<std::uint8_t>((word >> 21u) & 3u);
        i.address = static_cast<std::uint16_t>((word >> 5u) & 0xffffu);
    }
    return i;
}

struct SineState {
    std::int32_t sine = 0;
    std::int32_t cosine = -kMax;

    void reset() {
        sine = 0;
        cosine = -kMax;
    }

    void advance(std::int32_t rate_register) {
        const std::int32_t coefficient = rate_register >> 8;
        const std::int32_t next_cosine = clamp24(static_cast<std::int64_t>(cosine) + fixed_mul23(sine, coefficient));
        const std::int32_t next_sine = clamp24(static_cast<std::int64_t>(sine) - fixed_mul23(next_cosine, coefficient));
        cosine = next_cosine;
        sine = next_sine;
    }
};

struct RampState {
    std::int32_t position = 0;

    void reset() { position = 0; }

    static std::uint32_t mask_for(std::int32_t range_register) {
        const unsigned shift = static_cast<unsigned>((static_cast<std::uint32_t>(range_register) >> 21u) & 3u);
        return 0x3fffffu >> shift;
    }

    void advance(std::int32_t rate_register, std::int32_t range_register) {
        const std::int32_t step = rate_register >> 12;
        const std::uint32_t mask = mask_for(range_register);
        position = static_cast<std::int32_t>((static_cast<std::uint32_t>(position - step)) & mask);
    }

    std::int32_t pointer(std::int32_t range_register, bool second) const {
        const std::uint32_t mask = mask_for(range_register);
        std::uint32_t value = static_cast<std::uint32_t>(position) & mask;
        if (second) value = (value + ((mask + 1u) >> 1u)) & mask;
        return static_cast<std::int32_t>(value);
    }

    std::int32_t crossfade(std::int32_t range_register) const {
        const std::uint32_t mask = mask_for(range_register);
        const std::uint32_t value = static_cast<std::uint32_t>(position) & mask;
        const std::uint32_t half = (mask + 1u) >> 1u;
        const std::uint32_t triangle = value < half ? value : (mask - value);
        const unsigned shift = static_cast<unsigned>((static_cast<std::uint32_t>(range_register) >> 21u) & 3u);
        std::uint64_t normalized = static_cast<std::uint64_t>(triangle) << (shift + 2u);
        if (normalized > static_cast<std::uint64_t>(kMax)) normalized = static_cast<std::uint64_t>(kMax);
        return static_cast<std::int32_t>(normalized);
    }
};

} // namespace

struct ReferenceModel::Impl {
    fv1_config config{32768.0, FV1_DELAY_REFERENCE_16};
    std::array<std::uint32_t, FV1_PROGRAM_WORDS> words{};
    std::array<RefInstruction, FV1_PROGRAM_WORDS> instructions{};
    std::array<std::int32_t, FV1_REGISTER_COUNT> regs{};
    std::array<std::int32_t, FV1_DELAY_WORDS> delay{};
    std::int32_t acc = 0;
    std::int32_t pacc = 0;
    std::int32_t lr = 0;
    std::uint32_t delay_pointer = 0;
    SineState sine[2]{};
    RampState ramp[2]{};
    float pots[3]{0.0f, 0.0f, 0.0f};
    bool first_run = true;
    bool loaded = false;
    bool sample_active = false;
    bool sample_finished = false;
    std::uint32_t pc = 0;
    std::uint64_t sample_counter = 0;
    std::uint32_t instruction_counter = 0;

    std::int32_t delay_read(std::int32_t relative) {
        const std::uint32_t index = (delay_pointer + static_cast<std::uint32_t>(relative)) & kDelayMask;
        lr = clamp24(delay[index]);
        return lr;
    }

    void delay_write(std::int32_t relative, std::int32_t value) {
        const std::uint32_t index = (delay_pointer + static_cast<std::uint32_t>(relative)) & kDelayMask;
        std::int32_t stored = clamp24(value);
        if (config.delay_model == FV1_DELAY_REFERENCE_16) stored = (stored >> 8) * 256;
        delay[index] = stored;
    }

    void copy_acc_to_pacc() { pacc = acc; }

    void start_sample(float left, float right) {
        regs[FV1_REG_ADCL] = from_float(left);
        regs[FV1_REG_ADCR] = from_float(right);
        regs[FV1_REG_POT0] = pot_value(pots[0]);
        regs[FV1_REG_POT1] = pot_value(pots[1]);
        regs[FV1_REG_POT2] = pot_value(pots[2]);
        pc = 0;
        instruction_counter = 0;
        sample_active = true;
        sample_finished = false;
    }

    void complete_sample() {
        first_run = false;
        delay_pointer = (delay_pointer - 1u) & kDelayMask;
        sine[0].advance(regs[FV1_REG_SIN0_RATE]);
        sine[1].advance(regs[FV1_REG_SIN1_RATE]);
        ramp[0].advance(regs[FV1_REG_RMP0_RATE], regs[FV1_REG_RMP0_RANGE]);
        ramp[1].advance(regs[FV1_REG_RMP1_RATE], regs[FV1_REG_RMP1_RANGE]);
        ++sample_counter;
        sample_active = false;
        sample_finished = true;
        instruction_counter = 0;
    }

    std::int32_t sine_output(std::uint8_t index, bool cosine) const {
        const std::int32_t base = cosine ? sine[index].cosine : sine[index].sine;
        const std::int32_t range = regs[index == 0 ? FV1_REG_SIN0_RANGE : FV1_REG_SIN1_RANGE];
        return fixed_mul23(base, range);
    }

    struct ChoState {
        std::int32_t wide = 0;
        std::int32_t fraction = 0;
        std::int32_t xfade = 0;
    };

    ChoState cho_state(std::uint8_t lfo, std::uint8_t flags) const {
        ChoState out{};
        if (lfo <= 1) {
            out.wide = sine_output(lfo, (flags & kChoCos) != 0u);
            if ((flags & kChoCompa) != 0u) out.wide = -out.wide;
            const std::uint32_t low = static_cast<std::uint32_t>(out.wide) & 0x3ffu;
            out.fraction = static_cast<std::int32_t>(low * 8192u);
        } else {
            const std::uint8_t index = static_cast<std::uint8_t>(lfo - 2u);
            const std::int32_t range = regs[index == 0 ? FV1_REG_RMP0_RANGE : FV1_REG_RMP1_RANGE];
            out.wide = ramp[index].pointer(range, (flags & kChoRptr2) != 0u);
            const std::uint32_t low = static_cast<std::uint32_t>(out.wide) & 0x3ffu;
            out.fraction = static_cast<std::int32_t>(low * 8192u);
            out.xfade = ramp[index].crossfade(range);
            if ((flags & kChoCompa) != 0u) {
                const std::uint32_t mask = RampState::mask_for(range);
                out.wide = static_cast<std::int32_t>(mask - (static_cast<std::uint32_t>(out.wide) & mask));
            }
        }
        return out;
    }

    void execute(const RefInstruction& i, std::uint32_t& current_pc, bool& skipped) {
        skipped = false;
        switch (i.opcode) {
            case kRda: {
                copy_acc_to_pacc();
                const std::int32_t memory = delay_read(static_cast<std::int32_t>(i.address));
                acc = clamp24(static_cast<std::int64_t>(acc) + fixed_mul(memory, i.first, 9));
                break;
            }
            case kRmpa: {
                copy_acc_to_pacc();
                const std::int32_t relative = regs[FV1_REG_ADDR_PTR] >> 8;
                const std::int32_t memory = delay_read(relative);
                acc = clamp24(static_cast<std::int64_t>(acc) + fixed_mul(memory, i.first, 9));
                break;
            }
            case kWra: {
                copy_acc_to_pacc();
                delay_write(static_cast<std::int32_t>(i.address), acc);
                acc = fixed_mul(acc, i.first, 9);
                break;
            }
            case kWrap: {
                copy_acc_to_pacc();
                delay_write(static_cast<std::int32_t>(i.address), acc);
                acc = clamp24(static_cast<std::int64_t>(fixed_mul(acc, i.first, 9)) + lr);
                break;
            }
            case kRdax: {
                copy_acc_to_pacc();
                acc = clamp24(static_cast<std::int64_t>(acc) + fixed_mul(regs[i.reg], i.first, 14));
                break;
            }
            case kRdfx: {
                copy_acc_to_pacc();
                const std::int32_t reg = regs[i.reg];
                const std::int32_t difference = clamp24(static_cast<std::int64_t>(acc) - reg);
                acc = clamp24(static_cast<std::int64_t>(reg) + fixed_mul(difference, i.first, 14));
                break;
            }
            case kWrax: {
                copy_acc_to_pacc();
                regs[i.reg] = acc;
                acc = fixed_mul(acc, i.first, 14);
                break;
            }
            case kWrhx: {
                const std::int32_t prior_pacc = pacc;
                copy_acc_to_pacc();
                regs[i.reg] = acc;
                acc = clamp24(static_cast<std::int64_t>(fixed_mul(acc, i.first, 14)) + prior_pacc);
                break;
            }
            case kWrlx: {
                const std::int32_t prior_pacc = pacc;
                copy_acc_to_pacc();
                regs[i.reg] = acc;
                const std::int32_t difference = clamp24(static_cast<std::int64_t>(prior_pacc) - acc);
                acc = clamp24(static_cast<std::int64_t>(prior_pacc) + fixed_mul(difference, i.first, 14));
                break;
            }
            case kMaxx: {
                copy_acc_to_pacc();
                const std::int32_t scaled = fixed_mul(regs[i.reg], i.first, 14);
                const std::int64_t a = std::llabs(static_cast<long long>(acc));
                const std::int64_t b = std::llabs(static_cast<long long>(scaled));
                acc = clamp24(std::max(a, b));
                break;
            }
            case kMulx:
                copy_acc_to_pacc();
                acc = fixed_mul23(acc, regs[i.reg]);
                break;
            case kLog: {
                copy_acc_to_pacc();
                double logarithm = -1.0;
                if (acc != 0) {
                    const double magnitude = std::abs(static_cast<double>(acc) / static_cast<double>(kOne));
                    logarithm = std::log2(std::max(magnitude, std::numeric_limits<double>::min())) / 16.0;
                }
                const std::int32_t encoded = clamp24(static_cast<std::int64_t>(std::llround(logarithm * kOne)));
                acc = clamp24(static_cast<std::int64_t>(fixed_mul(encoded, i.first, 14)) +
                              static_cast<std::int64_t>(i.second) * 8192);
                break;
            }
            case kExp: {
                copy_acc_to_pacc();
                const double x = static_cast<double>(acc) / static_cast<double>(kOne);
                const double exponential = x >= 0.0 ? 1.0 : std::exp2(x * 16.0);
                const double bounded = std::min(exponential, std::nextafter(1.0, 0.0));
                const std::int32_t encoded = clamp24(static_cast<std::int64_t>(std::llround(bounded * kOne)));
                acc = clamp24(static_cast<std::int64_t>(fixed_mul(encoded, i.first, 14)) +
                              static_cast<std::int64_t>(i.second) * 8192);
                break;
            }
            case kSof:
                copy_acc_to_pacc();
                acc = clamp24(static_cast<std::int64_t>(fixed_mul(acc, i.first, 14)) +
                              static_cast<std::int64_t>(i.second) * 8192);
                break;
            case kAnd: {
                copy_acc_to_pacc();
                const std::uint32_t a = static_cast<std::uint32_t>(acc) & 0xffffffu;
                acc = clamp24(sx(a & static_cast<std::uint32_t>(i.first), 24));
                break;
            }
            case kOr: {
                copy_acc_to_pacc();
                const std::uint32_t a = static_cast<std::uint32_t>(acc) & 0xffffffu;
                acc = clamp24(sx(a | static_cast<std::uint32_t>(i.first), 24));
                break;
            }
            case kXor: {
                copy_acc_to_pacc();
                const std::uint32_t a = static_cast<std::uint32_t>(acc) & 0xffffffu;
                acc = clamp24(sx(a ^ static_cast<std::uint32_t>(i.first), 24));
                break;
            }
            case kSkp: {
                bool take = i.condition == 0;
                if ((i.condition & kSkpRun) != 0u) take = take || !first_run;
                if ((i.condition & kSkpZro) != 0u) take = take || acc == 0;
                if ((i.condition & kSkpGez) != 0u) take = take || acc >= 0;
                if ((i.condition & kSkpNeg) != 0u) take = take || acc < 0;
                if ((i.condition & kSkpZrc) != 0u) take = take || ((acc < 0) != (pacc < 0));
                if (take) {
                    current_pc = std::min<std::uint32_t>(FV1_PROGRAM_WORDS,
                                                        current_pc + static_cast<std::uint32_t>(i.skip) + 1u);
                    skipped = true;
                    return;
                }
                break;
            }
            case kWlds: {
                const std::uint8_t index = i.lfo & 1u;
                if (i.flags == 0) {
                    regs[index == 0 ? FV1_REG_SIN0_RATE : FV1_REG_SIN1_RATE] =
                        clamp24(static_cast<std::int64_t>(i.first) * 16384);
                    regs[index == 0 ? FV1_REG_SIN0_RANGE : FV1_REG_SIN1_RANGE] =
                        clamp24(static_cast<std::int64_t>(i.second) * 256);
                    sine[index].reset();
                } else {
                    regs[index == 0 ? FV1_REG_RMP0_RATE : FV1_REG_RMP1_RATE] =
                        clamp24(static_cast<std::int64_t>(i.first) * 256);
                    regs[index == 0 ? FV1_REG_RMP0_RANGE : FV1_REG_RMP1_RANGE] =
                        static_cast<std::int32_t>((i.second & 3) * (1 << 21));
                    ramp[index].reset();
                }
                break;
            }
            case kJam:
                ramp[i.lfo & 1u].reset();
                break;
            case kCho: {
                const ChoState state = cho_state(i.lfo, i.flags);
                std::int32_t coefficient = ((i.flags & kChoNa) != 0u && i.lfo >= 2)
                    ? state.xfade : state.fraction;
                if ((i.flags & kChoCompc) != 0u) coefficient = kMax - coefficient;

                if (i.cho_type == kChoRda) {
                    copy_acc_to_pacc();
                    std::int32_t address = static_cast<std::int32_t>(i.address);
                    if ((i.flags & kChoNa) == 0u) address += state.wide >> 10;
                    const std::int32_t memory = delay_read(address);
                    acc = clamp24(static_cast<std::int64_t>(acc) + fixed_mul23(memory, coefficient));
                } else if (i.cho_type == kChoSof) {
                    copy_acc_to_pacc();
                    const std::int32_t offset = sx(i.address, 16) * 256;
                    acc = clamp24(static_cast<std::int64_t>(fixed_mul23(acc, coefficient)) + offset);
                } else if (i.cho_type == kChoRdal) {
                    copy_acc_to_pacc();
                    if (i.lfo <= 1) {
                        acc = sine_output(i.lfo, (i.flags & kChoCos) != 0u);
                    } else {
                        const std::uint8_t index = static_cast<std::uint8_t>(i.lfo - 2u);
                        const std::int32_t range = regs[index == 0 ? FV1_REG_RMP0_RANGE : FV1_REG_RMP1_RANGE];
                        acc = clamp24(static_cast<std::int64_t>(ramp[index].pointer(range, false)) * 2);
                    }
                }
                break;
            }
            default:
                break;
        }
        ++current_pc;
    }

    fv1_result step(fv1_trace* trace) {
        if (!sample_active || sample_finished) return FV1_ERROR_BAD_STATE;
        if (trace) std::memset(trace, 0, sizeof(*trace));

        if (pc >= FV1_PROGRAM_WORDS) {
            const std::uint64_t sample_index = sample_counter;
            const std::uint32_t instruction_index = instruction_counter;
            complete_sample();
            if (trace) {
                trace->sample_finished = 1;
                trace->sample_index = sample_index;
                trace->instruction_index = instruction_index;
            }
            return FV1_OK;
        }

        const std::uint64_t sample_index = sample_counter;
        const std::uint32_t instruction_index = instruction_counter;
        const std::uint32_t before = pc;
        const RefInstruction& instruction = instructions[before];
        if (trace) {
            trace->pc_before = before;
            trace->raw_instruction = instruction.raw;
            trace->opcode = instruction.opcode;
            trace->acc_before = acc;
            trace->sample_index = sample_index;
            trace->instruction_index = instruction_index;
        }

        bool skipped = false;
        execute(instruction, pc, skipped);
        ++instruction_counter;

        if (trace) {
            trace->pc_after = pc;
            trace->acc_after = acc;
            trace->pacc_after = pacc;
            trace->lr_after = lr;
            trace->skipped = skipped ? 1 : 0;
        }
        if (pc >= FV1_PROGRAM_WORDS) {
            complete_sample();
            if (trace) trace->sample_finished = 1;
        }
        return FV1_OK;
    }
};

ReferenceModel::ReferenceModel(fv1_config config) : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    if (!(impl_->config.virtual_sample_rate > 0.0)) impl_->config.virtual_sample_rate = 32768.0;
    reset(true);
}

ReferenceModel::~ReferenceModel() = default;
ReferenceModel::ReferenceModel(ReferenceModel&&) noexcept = default;
ReferenceModel& ReferenceModel::operator=(ReferenceModel&&) noexcept = default;

void ReferenceModel::reset(bool clear_delay_ram) {
    Impl& m = *impl_;
    m.regs.fill(0);
    m.acc = 0;
    m.pacc = 0;
    m.lr = 0;
    m.delay_pointer = 0;
    m.sine[0].reset();
    m.sine[1].reset();
    m.ramp[0].reset();
    m.ramp[1].reset();
    m.first_run = true;
    m.sample_active = false;
    m.sample_finished = false;
    m.pc = 0;
    m.sample_counter = 0;
    m.instruction_counter = 0;
    if (clear_delay_ram) m.delay.fill(0);
}

fv1_result ReferenceModel::load_words(const std::uint32_t* words, std::size_t count) {
    if (!words || count != FV1_PROGRAM_WORDS) return FV1_ERROR_INVALID_ARGUMENT;
    for (std::size_t n = 0; n < FV1_PROGRAM_WORDS; ++n) {
        const RefInstruction decoded = parse_word(words[n]);
        if (decoded.opcode > kCho) return FV1_ERROR_INVALID_PROGRAM;
        impl_->words[n] = words[n];
        impl_->instructions[n] = decoded;
    }
    impl_->loaded = true;
    reset(true);
    return FV1_OK;
}

fv1_result ReferenceModel::load_bytes(const std::uint8_t* bytes, std::size_t size) {
    if (!bytes || size != FV1_PROGRAM_BYTES) return FV1_ERROR_INVALID_ARGUMENT;
    std::array<std::uint32_t, FV1_PROGRAM_WORDS> words{};
    for (std::size_t n = 0; n < FV1_PROGRAM_WORDS; ++n) {
        const std::size_t offset = n * 4u;
        words[n] = (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
                   (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u) |
                   (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u) |
                   static_cast<std::uint32_t>(bytes[offset + 3u]);
    }
    return load_words(words.data(), words.size());
}

void ReferenceModel::set_pots(float pot0, float pot1, float pot2) {
    impl_->pots[0] = std::clamp(pot0, 0.0f, 1.0f);
    impl_->pots[1] = std::clamp(pot1, 0.0f, 1.0f);
    impl_->pots[2] = std::clamp(pot2, 0.0f, 1.0f);
}

fv1_result ReferenceModel::begin_sample(float in_l, float in_r) {
    if (!impl_->loaded) return FV1_ERROR_BAD_STATE;
    if (impl_->sample_active) return FV1_ERROR_BAD_STATE;
    impl_->start_sample(in_l, in_r);
    return FV1_OK;
}

fv1_result ReferenceModel::step_instruction(fv1_trace* trace) {
    if (!trace) return FV1_ERROR_INVALID_ARGUMENT;
    return impl_->step(trace);
}

fv1_result ReferenceModel::finish_sample(float* out_l, float* out_r) {
    if (!out_l || !out_r) return FV1_ERROR_INVALID_ARGUMENT;
    if (impl_->sample_active || !impl_->sample_finished) return FV1_ERROR_BAD_STATE;
    *out_l = to_float(impl_->regs[FV1_REG_DACL]);
    *out_r = to_float(impl_->regs[FV1_REG_DACR]);
    impl_->sample_finished = false;
    return FV1_OK;
}

fv1_result ReferenceModel::process_sample(float in_l, float in_r, float* out_l, float* out_r) {
    if (!out_l || !out_r) return FV1_ERROR_INVALID_ARGUMENT;
    fv1_result result = begin_sample(in_l, in_r);
    if (result != FV1_OK) return result;
    while (impl_->sample_active) {
        result = impl_->step(nullptr);
        if (result != FV1_OK) return result;
    }
    *out_l = to_float(impl_->regs[FV1_REG_DACL]);
    *out_r = to_float(impl_->regs[FV1_REG_DACR]);
    impl_->sample_finished = false;
    return FV1_OK;
}

void ReferenceModel::get_snapshot(fv1_snapshot* snapshot) const {
    if (!snapshot) return;
    const Impl& m = *impl_;
    std::memset(snapshot, 0, sizeof(*snapshot));
    snapshot->acc = m.acc;
    snapshot->pacc = m.pacc;
    snapshot->lr = m.lr;
    std::copy(m.regs.begin(), m.regs.end(), snapshot->regs);
    snapshot->delay_pointer = m.delay_pointer;
    snapshot->sin_lfo[0] = m.sine_output(0, false);
    snapshot->sin_lfo[1] = m.sine_output(1, false);
    snapshot->cos_lfo[0] = m.sine_output(0, true);
    snapshot->cos_lfo[1] = m.sine_output(1, true);
    snapshot->ramp_lfo[0] = m.ramp[0].pointer(m.regs[FV1_REG_RMP0_RANGE], false);
    snapshot->ramp_lfo[1] = m.ramp[1].pointer(m.regs[FV1_REG_RMP1_RANGE], false);
    snapshot->program_counter = m.pc;
    snapshot->first_run = m.first_run ? 1u : 0u;
    snapshot->debug_sample_active = m.sample_active ? 1u : 0u;
    snapshot->sample_counter = m.sample_counter;
    snapshot->instruction_counter = m.instruction_counter;
}

fv1_result ReferenceModel::read_delay_word(std::uint32_t address, std::int32_t* value) const {
    if (!value || address >= FV1_DELAY_WORDS) return FV1_ERROR_INVALID_ARGUMENT;
    *value = clamp24(impl_->delay[address]);
    return FV1_OK;
}

fv1_result ReferenceModel::get_state_digest(fv1_state_digest* digest) const {
    if (!digest) return FV1_ERROR_INVALID_ARGUMENT;
    const Impl& m = *impl_;
    std::uint64_t architecture = kFnvOffset;
    hash_le(architecture, m.acc);
    hash_le(architecture, m.pacc);
    hash_le(architecture, m.lr);
    for (const std::int32_t reg : m.regs) hash_le(architecture, reg);
    hash_le(architecture, m.delay_pointer);
    for (const SineState& lfo : m.sine) {
        hash_le(architecture, lfo.sine);
        hash_le(architecture, lfo.cosine);
    }
    for (const RampState& lfo : m.ramp) hash_le(architecture, lfo.position);
    hash_byte(architecture, m.first_run ? 1u : 0u);
    hash_byte(architecture, m.loaded ? 1u : 0u);
    hash_byte(architecture, m.sample_active ? 1u : 0u);
    hash_byte(architecture, m.sample_finished ? 1u : 0u);
    hash_le(architecture, m.pc);
    hash_le(architecture, m.sample_counter);
    hash_le(architecture, m.instruction_counter);

    std::uint64_t delay_hash = kFnvOffset;
    for (const std::int32_t value : m.delay) hash_le(delay_hash, clamp24(value));

    digest->architectural_hash = architecture;
    digest->delay_hash = delay_hash;
    digest->sample_counter = m.sample_counter;
    return FV1_OK;
}

} // namespace fv1
