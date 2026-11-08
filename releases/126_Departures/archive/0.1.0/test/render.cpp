#include "Departures.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

static void U16(FILE *f, uint16_t x) { std::fputc(x & 255, f); std::fputc(x >> 8, f); }
static void U32(FILE *f, uint32_t x) { U16(f, x & 65535); U16(f, x >> 16); }
int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const int seconds = argc > 2 ? std::atoi(argv[2]) : 90;
    if (seconds < 1 || seconds > 600) return 1;
    FILE *f = std::fopen(argv[1], "wb"); if (!f) return 1;
    const uint32_t bytes = seconds * departures::kRate * 4;
    std::fwrite("RIFF", 1, 4, f); U32(f, bytes + 36); std::fwrite("WAVEfmt ", 1, 8, f);
    U32(f, 16); U16(f, 1); U16(f, 2); U32(f, departures::kRate); U32(f, departures::kRate * 4);
    U16(f, 4); U16(f, 16); std::fwrite("data", 1, 4, f); U32(f, bytes);
    departures::Tables::Init();
    auto e = std::make_unique<departures::Engine>(); e->Init(20261107);
    departures::Inputs in;
    if (argc > 3) in.space = std::atoi(argv[3]);
    if (argc > 4) in.main = std::atoi(argv[4]);
    for (int n = 0; n < seconds * departures::kRate; ++n) {
        const auto out = e->Process(in);
        U16(f, static_cast<uint16_t>(out.left * 16)); U16(f, static_cast<uint16_t>(out.right * 16));
    }
    std::fclose(f);
    std::printf("Rendered %ds: %u beats, %u melody notes, %u harmony changes\n", seconds, e->beats, e->melodyEvents, e->chordChanges);
}
