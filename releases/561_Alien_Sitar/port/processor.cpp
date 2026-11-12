#include "processor.hpp"
#include "config.hpp"
namespace computer {
static_assert(mtm::RATE == timing::voiceRate, "Update the DSP tables with any rate change");
int32_t Processor::highpass(int32_t value, int32_t &state) {
    state += value - (state >> 9);
    return mtm::clip(value - (state >> 9));
}

mtm::Frame Processor::process(const Packet &p) {
    voice.setPitchQ16(p.settings.pitchQ16);
    voice.setParameters(p.settings.params);
    if (p.pluck) {
        voice.trigger();
    }
    if (p.damp) {
        voice.damp();
    }
    int32_t string = highpass(p.settings.external ? p.stringInput : 0, stringDC);
    int32_t halo = highpass(p.settings.external ? p.haloInput : 0, haloDC);
    // Modular audio can be much stronger/longer than the finite wire pluck.
    // Fixed input attenuation leaves headroom for sustained resonant excitation.
    return voice.next(p.settings.external ? string / 8 : 0, p.settings.external ? halo / 8 : 0);
}
} // namespace computer
