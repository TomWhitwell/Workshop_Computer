#pragma once
#include "controller.hpp"
#include "queue.hpp"
#include <cstddef>
namespace computer::editor {
constexpr unsigned protocolVersion = 1;
enum Parameter {
    Jawari,
    Swell,
    String,
    Sympathy,
    Octave,
    Pick,
    PickPosition,
    HaloLevel,
    MelodyDecay,
    SympatheticDecay,
    ResonatorMix,
    HeldMain,
    HeldX,
    HeldY,
    PitchInOffset,
    PitchInScale,
    PitchOutOffset,
    PitchOutScale,
    Taraf1,
    Taraf2,
    Taraf3,
    Taraf4,
    Taraf5,
    Taraf6,
    Taraf7,
    Taraf8,
    Taraf9,
    Taraf10,
    Taraf11,
    Taraf12,
    Count
};
struct Descriptor {
    const char *id, *label, *unit, *timing;
    int32_t minimum, maximum, initial;
    bool panelDefault;
};
extern const Descriptor descriptors[Count];
enum class Operation { Hello, Get, Set, Reset, Save, Restore, Defaults, SetTaraf };
enum class Error { None, Syntax, TooLong, UnknownParameter, Range, Busy, QueueFull, Storage };
struct Command {
    uint32_t sequence = 0, epoch = 0;
    Operation operation = Operation::Get;
    int32_t parameter = -1, value = 0;
    saved::State state{};
    uint8_t notes[12]{};
};
struct Reply {
    uint32_t sequence = 0, epoch = 0;
    Operation operation = Operation::Get;
    Error error = Error::None;
    int32_t values[Count]{};
    saved::State state{};
    uint32_t pickup = 0;
    bool held = false, external = false;
    int32_t effectiveJawari = 0, effectiveSwell = 0, pitchFactoryMv = 0;
};
// Foreground parses bytes; audio control tick owns Controller and replies.
// Each direction has one producer and consumer, including IRQ/foreground use.
struct Bridge {
    std::atomic<uint32_t> epoch{0}; // Foreground invalidates queued work on reconnect.
    Queue<Command, 8> commands;
    Queue<Reply, 8> replies;
    void service(Controller &controller, const Controls &controls, const Settings &settings);
};
void capture(saved::State &state, const Controller &controller);
bool apply(const saved::State &state, Controller &controller);
Settings restoreAtBoot(const saved::State &state, Controller &controller, const Controls &controls);
class Parser {
public:
    // Returns true only at a completed line (including malformed/oversized lines).
    bool feed(char byte, Command &command, Error &error);
    void reset() {
        length = 0;
        overflow = false;
    }

private:
    char line[96]{};
    unsigned length = 0;
    bool overflow = false;
};
const char *errorName(Error error);
// JSON output is only formatted in foreground. Returns 0 if buffer too small.
size_t formatReply(const Reply &reply, char *output, size_t capacity);
} // namespace computer::editor
