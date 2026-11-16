#pragma once
namespace computer {
// Called once after voltage/clock setup. Owns all hardware I/O and launches core1.
void runInstrument();
} // namespace computer
