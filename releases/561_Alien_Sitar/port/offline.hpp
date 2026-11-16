#pragma once
#include "auxiliary.hpp"
#include "events.hpp"
#include "output.hpp"
#include "processor.hpp"
namespace computer {
// Synchronous offline equivalent of the hardware 48/24k filter paths. Does not
// model core1 execution time or the firmware's additional 64-frame elastic lead.
class Offline {
public:
    mtm::Frame next(const Controls &controls, bool pluck = false, bool damp = false,
                    int32_t stringInput = 0, int32_t haloInput = 0);

    const AuxiliaryOutputs &auxiliaryOutputs() const { return auxiliary; }

private:
    Processor processor;
    mtm::Fir in1, in2, out1, out2;
    mtm::Frame frame{};
    Controller controller;
    Events events;
    Settings settings;
    AuxiliaryOutputs auxiliary;
    Output output;
    uint32_t tick = 0;
};
} // namespace computer
