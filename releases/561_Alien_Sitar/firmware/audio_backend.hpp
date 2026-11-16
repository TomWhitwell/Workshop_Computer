/// @file audio_backend.hpp
/// @brief Entry point of the RP2040 (Pico SDK) firmware backend owning all hardware I/O.
///
/// The backend drives the Workshop Computer "ComputerCard" hardware on core0
/// at 48 kHz: ADC mux, normalisation-probe, DAC and PWM work happens inside
/// the DMA-driven audio interrupt. Filtered input is decimated to 24 kHz and
/// exchanged with the core1 fixed-point sitar worker through bounded SPSC
/// queues; returned frames are reconstructed to 48 kHz, with underrun fades
/// instead of waiting. Program-card flash saves (storage.hpp) and the
/// USB editor foreground poll also run on
/// this core. main.cpp raises the regulator/clock before calling into here.
/// Timing figures reference the port's declared budgets and SWD counters;
/// they describe measurements, not proven hardware performance guarantees.
#pragma once
namespace computer {
// Called once after voltage/clock setup. Owns all hardware I/O and launches core1.
/// @brief Runs the complete instrument backend on core0. Never returns.
///
/// Builds the statically stored Card after voltage/clock configuration,
/// prefills the output queue with 64 silent frames (2.667 ms of elastic
/// lead), launches the core1 24 kHz worker, optionally starts the USB editor,
/// and enters ComputerCard's control loop. Local-static construction finishes
/// on core0 before core1 starts, which keeps the -fno-threadsafe-statics
/// build valid without runtime thread-safety locking.
/// @return Does not return; ComputerCard::Run() loops indefinitely.
void runInstrument();
} // namespace computer
