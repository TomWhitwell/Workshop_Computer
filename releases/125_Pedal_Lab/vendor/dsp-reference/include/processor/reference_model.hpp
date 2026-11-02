#pragma once

#include <processor/processor.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace fv1 {

/*
 * Phase-5C golden/reference FV-1 model.
 *
 * This deliberately does not link against FV1::core and does not reuse the
 * production engine's decoder or arithmetic helpers.  It is intentionally
 * straightforward and slower so differential tests have a second executable
 * interpretation of the Hardware Emulation Contract.
 */
class ReferenceModel {
public:
    explicit ReferenceModel(fv1_config config = {32768.0, FV1_DELAY_REFERENCE_16});
    ~ReferenceModel();

    ReferenceModel(const ReferenceModel&) = delete;
    ReferenceModel& operator=(const ReferenceModel&) = delete;
    ReferenceModel(ReferenceModel&&) noexcept;
    ReferenceModel& operator=(ReferenceModel&&) noexcept;

    void reset(bool clear_delay_ram = true);
    fv1_result load_words(const std::uint32_t* words, std::size_t count);
    fv1_result load_bytes(const std::uint8_t* bytes, std::size_t size);
    void set_pots(float pot0, float pot1, float pot2);

    fv1_result begin_sample(float in_l, float in_r);
    fv1_result step_instruction(fv1_trace* trace);
    fv1_result finish_sample(float* out_l, float* out_r);
    fv1_result process_sample(float in_l, float in_r, float* out_l, float* out_r);

    void get_snapshot(fv1_snapshot* snapshot) const;
    fv1_result read_delay_word(std::uint32_t address, std::int32_t* value) const;
    fv1_result get_state_digest(fv1_state_digest* digest) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fv1
