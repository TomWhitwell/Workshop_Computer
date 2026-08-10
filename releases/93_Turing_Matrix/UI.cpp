#include "UI.h"
#include "Clock.h"
#define COMPUTERCARD_NOIMPL
#include "ComputerCard.h"
#include "MainApp.h"
#include <pico/time.h>

void UI::init(MainApp *a, Clock *c)
{
    app = a;
    clk = c;
}

void UI::ResetPickup(KnobPickup &pickup, uint16_t raw)
{
    pickup.entry = raw;
    pickup.pickedUp = false;
}

bool UI::ApplyPickup(KnobPickup &pickup, uint16_t raw, uint16_t target)
{
    if (pickup.pickedUp)
    {
        return true;
    }

    int32_t moved = int32_t(raw) - int32_t(pickup.entry);
    if (moved < 0)
        moved = -moved;

    int32_t distance = int32_t(raw) - int32_t(target);
    if (distance < 0)
        distance = -distance;

    if (moved >= int32_t(pickupMoveThreshold) && distance <= int32_t(pickupCatchThreshold))
    {
        pickup.pickedUp = true;
        return true;
    }

    return false;
}

uint16_t UI::StepToKnobValue(uint8_t step, uint8_t steps, uint32_t range)
{
    if (steps <= 1)
        return 0;

    if (step >= steps)
        step = steps - 1;

    uint32_t stepSize = range / steps;
    if (stepSize == 0)
        return 0;

    uint32_t centre = (uint32_t(step) * stepSize) + (stepSize / 2);
    if (step == steps - 1 || centre > range)
        centre = range;

    return static_cast<uint16_t>(centre);
}

void UI::Tick()
{

    // use simple round robin to call TriggerPulse1 and EndPulse1 in 50% of cycles,
    // and TriggerPulse2 and EndPulse2 in 50% of cycles, in order to reduce the time to
    // complete 48khz clocked events
    // store requested actions in pending variables

    static bool toggle = false;
    static bool trigger1Pending = false;
    static bool trigger2Pending = false;

    // First set up all the pending conditions

    if (clk->IsRisingEdge() && !app->PulseInConnected1())
    {
        trigger1Pending = true;
    }

    if (clk->IsRisingEdgeMult() && !app->PulseInConnected2())
    {
        trigger2Pending = true;
    }

    if (clk->ExtPulseReceived1())
    {
        trigger1Pending = true;
    }

    if (clk->ExtPulseReceived2())
    {
        trigger2Pending = true;
    }

    // Then act on the relevant functions according to the toggle

    if (toggle && trigger1Pending)
    {

        TriggerPulse1();

        trigger1Pending = false;
    }
    if (!toggle && trigger2Pending)
    {

        TriggerPulse2();

        trigger2Pending = false;
    }

    if (toggle)
    {
        EndPulse1(); // Countdown pulse timer and check if Pulse1 should be stopped
    }

    if (!toggle)
    {
        EndPulse2();
    }
    // Then flip the toggle
    toggle = !toggle;
}

void UI::SlowUI()
{
    const bool upper = app->VactrolLayerActive();
    if (!pickupInitialised)
    {
        ResetPickup(middleMain, app->KnobMain());
        ResetPickup(middleX, app->KnobX());
        ResetPickup(middleY, app->KnobY());
        ResetPickup(upperMain, app->KnobMain());
        ResetPickup(upperX, app->KnobX());
        ResetPickup(upperY, app->KnobY());
        lastLayerUpper = upper;
        pickupInitialised = true;
        pickupArmed = false;
    }
    else if (upper != lastLayerUpper)
    {
        ResetPickup(middleMain, app->KnobMain());
        ResetPickup(middleX, app->KnobX());
        ResetPickup(middleY, app->KnobY());
        ResetPickup(upperMain, app->KnobMain());
        ResetPickup(upperX, app->KnobX());
        ResetPickup(upperY, app->KnobY());
        lastLayerUpper = upper;
        pickupArmed = true;
    }

    if (upper)
    {
        uint16_t slew = app->KnobMain();
        uint16_t depth1 = app->KnobX();
        uint16_t depth2 = app->KnobY();

        if (pickupArmed)
        {
            slew = currentUpperMainValue;
            depth1 = currentUpperXValue;
            depth2 = currentUpperYValue;

            if (ApplyPickup(upperMain, app->KnobMain(), currentUpperMainValue))
                slew = app->KnobMain();
            if (ApplyPickup(upperX, app->KnobX(), currentUpperXValue))
                depth1 = app->KnobX();
            if (ApplyPickup(upperY, app->KnobY(), currentUpperYValue))
                depth2 = app->KnobY();
        }

        currentUpperMainValue = slew;
        currentUpperXValue = depth1;
        currentUpperYValue = depth2;
        app->SetVactrolControls(slew, depth1, depth2);
        return;
    }

    uint16_t minVal = 0;
    uint16_t maxVal = 4095;

    // Check for divide knob changes
    uint16_t knobTemp = app->KnobY();
    if (pickupArmed)
    {
        knobTemp = currentMiddleYValue;
        if (ApplyPickup(middleY, app->KnobY(), currentMiddleYValue))
            knobTemp = app->KnobY();
    }

    // Add knob value
    int16_t inputTemp = app->readInputIfConnected(ComputerCard::CV1); // returns zero if nothing connected 
    int16_t valueTemp = knobTemp + inputTemp;
    CLAMP(valueTemp, minVal, maxVal); 

    uint16_t step = QuantiseToStep(valueTemp, numDivideSteps, 4095);
    if (step >= numDivideSteps)
        step = numDivideSteps - 1;
    if (step != lastDivideStep)
    {
        app->divideKnobChanged(step);
        lastDivideStep = step;
    }
    currentMiddleYValue = StepToKnobValue(lastDivideStep, numDivideSteps, 4095);

    // Check for Length knob changes
    knobTemp = app->KnobX();
    if (pickupArmed)
    {
        knobTemp = currentMiddleXValue;
        if (ApplyPickup(middleX, app->KnobX(), currentMiddleXValue))
            knobTemp = app->KnobX();
    }
    step = QuantiseToStep(knobTemp, numLengthSteps, 4095);

    int newlen = lengths[step];

    if (newlen != lastLength)
    {
        app->lengthKnobChanged(newlen);

        lastLength = newlen;
    }
    currentMiddleXValue = StepToKnobValue(step, numLengthSteps, 4095);

    app->switchChanged();
    uint16_t mainValue = app->KnobMain();
    if (pickupArmed)
    {
        mainValue = currentMiddleMainValue;
        if (ApplyPickup(middleMain, app->KnobMain(), currentMiddleMainValue))
            mainValue = app->KnobMain();
    }
    currentMiddleMainValue = mainValue;
    app->SetTuringRandomness(mainValue);
}

// uint8_t UI::QuantiseToStep(uint32_t knobVal, uint8_t steps, uint32_t range)
// {
//     uint16_t step_size = range / steps;
//     return knobVal / step_size;
// }

uint8_t UI::QuantiseToStep(uint32_t knobVal, uint8_t steps, uint32_t range)
{
    if (steps == 0)
        return 0; // safety

    uint32_t step_size = range / steps;
    if (step_size == 0)
        return 0; // safety for very small ranges

    // Round to nearest step, not floor
    uint32_t step = (knobVal + (step_size / 2)) / step_size;

    // Clamp to max valid index
    if (step >= steps)
        step = steps - 1;

    return static_cast<uint8_t>(step);
}

void UI::TriggerPulse1()
{

    bool active = app->PulseOutput1(true);

    if (active)
    {

        app->PulseLed1(true);
        outputPulseTicksRemaining1 = outputPulseLength;
        ledPulseTicksRemaining1 = ledPulseLength;
        ledPulseActive1 = true;
        outputPulseActive1 = true;
    }

    app->updateMainTuring();
}

void UI::TriggerPulse2()
{
    bool active = app->PulseOutput2(true);

    app->PulseLed2(active);
    outputPulseTicksRemaining2 = outputDivideLength;
    ledPulseTicksRemaining2 = ledPulseLength;
    ledPulseActive2 = true; // always prepare to end pulses no matter if they're turned on or not
    outputPulseActive2 = true;

    app->updateDivTuring();
}

void UI::EndPulse1()
{
    if (outputPulseActive1 && --outputPulseTicksRemaining1 == 0)
    {
        outputPulseActive1 = false;
        app->PulseOutput1(false);
    }

    if (ledPulseActive1 && --ledPulseTicksRemaining1 == 0)
    {
        ledPulseActive1 = false;
        app->PulseLed1(false);
    }
}

void UI::EndPulse2()
{

    if (outputPulseActive2 && --outputPulseTicksRemaining2 == 0)
    {
        outputPulseActive2 = false;
        app->PulseOutput2(false);
    }

    if (ledPulseActive2 && --ledPulseTicksRemaining2 == 0)
    {
        ledPulseActive2 = false;
        app->PulseLed2(false);
    }
}

void UI::SetPulseLength(uint8_t lenPercent)
{
    uint32_t mainPercent = lenPercent + outputPulseMod1;
    uint32_t dividePercent = lenPercent + outputPulseMod2;

    if (mainPercent > 100)
        mainPercent = 100;

    if (mainPercent < 0)
        mainPercent = 0;

    if (dividePercent > 100)
        dividePercent = 100;

    if (dividePercent < 0)
        dividePercent = 0;

    uint32_t wholeStep = clk->GetTicksPerBeat();
    uint32_t newLen = (uint64_t(wholeStep) * mainPercent) / 200; // Not sure why, 50% = 100% length without this
    if (newLen < 96)                                             // Clamp minimum pulse at 96 = 2ms
        newLen = 96;

    outputPulseLength = newLen;

    uint32_t divideStep = clk->GetTicksPerSubclockBeat();
    uint32_t newDivLen = (uint64_t(divideStep) * dividePercent) / 200; // Not sure why, 50% = 100% length without this
    if (newDivLen < 96)                                                // Clamp minimum pulse at 96 = 2ms
        newDivLen = 96;

    outputDivideLength = newDivLen;
}

void UI::SetPulseMod(uint8_t level)
{
    pulseModLevel = level;
}

void UI::UpdatePulseMod(uint8_t turing1, uint8_t turing2)
{
    int bipolarModulation1 = (int)turing1 - 128;
    outputPulseMod1 = (bipolarModulation1 * pulseModLevel) / 128;

    int bipolarModulation2 = (int)turing2 - 128;
    outputPulseMod2 = (bipolarModulation2 * pulseModLevel) / 128;
}
