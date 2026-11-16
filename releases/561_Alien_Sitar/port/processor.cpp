#include "processor.hpp"
#include "config.hpp"
namespace computer {
static_assert(mtm::RATE == timing::voiceRate, "Update the DSP tables with any rate change");
int32_t Processor::highpass(int32_t value, int32_t &state) {
    state += value - (state >> 9);
    return mtm::clip(value - (state >> 9));
}

mtm::Frame Processor::process(const Packet &p) {
    voice.setTarafNotes(p.settings.taraf);
    voice.setPitchQ16(p.settings.pitchQ16);
    voice.setParameters(p.settings.params);
    if (p.pluck) {
        voice.trigger();
    }
    if (p.damp) {
        voice.damp();
    }
    // Probe readings are refreshed at control rate. Fade each input over 5.3 ms
    // to avoid a cable or switch transition becoming a hard discontinuity.
    auto fade = [](int32_t gain, bool enabled) {
        return mtm::clip(gain + (enabled ? 256 : -256), 0, 32768);
    };
    stringGain = fade(stringGain, p.settings.external && p.settings.stringConnected);
    haloGain = fade(haloGain, p.settings.external && p.settings.haloConnected);
    int32_t string = highpass(p.settings.external ? p.stringInput : 0, stringDC);
    int32_t halo = highpass(p.settings.external ? p.haloInput : 0, haloDC);
    // The main string builds substantial energy at resonance. Its lower gain
    // provides useful range for sustained inputs; the halo has a separate gain.
    const int32_t stringSignal = mtm::mul(string, stringGain);
    const int32_t haloSignal = mtm::mul(halo, haloGain);
    auto frame = voice.next(p.settings.external ? stringSignal / 64 : 0,
                            p.settings.external ? haloSignal / 16 : 0);
    if (p.settings.external) {
        // The final stage has 4x makeup gain. Scale the dry path accordingly;
        // the mix is smoothed so future remote edits do not click.
        resonatorMix = mtm::approach(resonatorMix, mtm::clip(p.settings.resonatorMix, 0, 32768), 8);
        const int32_t dry = stringSignal / 4 + haloSignal / 4;
        frame.mix =
            mtm::clip(mtm::mul(frame.mix, resonatorMix) + mtm::mul(dry, 32768 - resonatorMix));
    }
    return frame;
}
} // namespace computer
