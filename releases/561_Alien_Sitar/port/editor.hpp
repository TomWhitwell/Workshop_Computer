/// @file editor.hpp
/// @brief USB live-editor protocol: parameter schema, line parser and the
///        cross-thread command/reply bridge.
///
/// The firmware is the schema source for the live USB editor (see
/// USB_EDITOR.md): editor.cpp supplies the parameter descriptor table
/// (stable text IDs, labels, units, timing hints, limits, defaults and
/// reset policy), a line-oriented command parser and the one-line JSON
/// reply formatter. Ownership split: the foreground (USB side) parses
/// bytes and formats replies; the audio-owned control tick on core0 owns
/// the Controller and consumes/produces the queued Command/Reply objects,
/// so all state writes happen at a single control boundary. Each queue
/// direction has exactly one producer and one consumer (SPSC,
/// including IRQ/foreground use); the epoch field invalidates work queued
/// by a stale connection after reconnect.
#pragma once
#include "controller.hpp"
#include "queue.hpp"
#include <cstddef>
namespace computer::editor {
/// @brief Protocol version reported in HELLO and stamped in replies.
constexpr unsigned protocolVersion = 1;

/// @brief Protocol parameter ordinals.
/// Ordinals index editor::descriptors[] and define the persisted layout:
/// parameters 0..ResonatorMix are the eleven values stored in
/// saved::State, Taraf1..Taraf12 follow. Stable text IDs cross the wire,
/// but the ordinal order itself is shared with saved records and must not
/// be changed.
enum Parameter {
    Jawari,           ///< Panel Jawari (saved base, raw counts 0..4095).
    Swell,            ///< Panel Swell (saved base, raw counts 0..4095).
    String,           ///< Panel String level (saved base, raw counts 0..4095).
    Sympathy,         ///< Sympathetic output amount, Q15 (0..32767).
    Octave,           ///< Whole-octave pitch offset (-3..+3).
    Pick,             ///< Bipolar release softness/sharpness, Q15.
    PickPosition,     ///< Excitation comb position, % of string (5..40).
    HaloLevel,        ///< Independent halo output trim, % (0..100).
    MelodyDecay,      ///< Melody decay duration multiplier, % (25..400).
    SympatheticDecay, ///< Sympathetic decay duration multiplier, % (25..400).
    ResonatorMix,     ///< External resonator wet fraction, % (0..100).
    HeldMain,         ///< Held-assignment storage ID for the Main knob.
    HeldX,            ///< Held-assignment storage ID for X (left knob).
    HeldY,            ///< Held-assignment storage ID for Y (right knob).
    PitchInOffset,    ///< CV1 user offset correction, mV (-500..500).
    PitchInScale,     ///< CV1 user scale correction, ppm.
    PitchOutOffset,   ///< Pitch-out user offset correction, mV (-500..500).
    PitchOutScale,    ///< Pitch-out user scale correction, ppm.
    Taraf1,           ///< Taraf chord note 1, MIDI number (36..84).
    Taraf2,           ///< Taraf chord note 2, MIDI number (36..84).
    Taraf3,           ///< Taraf chord note 3, MIDI number (36..84).
    Taraf4,           ///< Taraf chord note 4, MIDI number (36..84).
    Taraf5,           ///< Taraf chord note 5, MIDI number (36..84).
    Taraf6,           ///< Taraf chord note 6, MIDI number (36..84).
    Taraf7,           ///< Taraf chord note 7, MIDI number (36..84).
    Taraf8,           ///< Taraf chord note 8, MIDI number (36..84).
    Taraf9,           ///< Taraf chord note 9, MIDI number (36..84).
    Taraf10,          ///< Taraf chord note 10, MIDI number (36..84).
    Taraf11,          ///< Taraf chord note 11, MIDI number (36..84).
    Taraf12,          ///< Taraf chord note 12, MIDI number (36..84).
    Count             ///< One past the last parameter (ORDINAL count).
};

/// @brief Schema entry for one editable parameter, served on HELLO.
struct Descriptor {
    /// Stable wire ID, display label, unit ("panel", "Q15", "%", "mV",
    /// "ppm", ...) and update-timing hint ("smoothed", "next pluck",
    /// "held Down", "fade retune", "calibration").
    const char *id, *label, *unit, *timing;
    /// Inclusive editor-unit bounds and the Reset/initial value.
    int32_t minimum, maximum, initial;
    /// True when Reset restores the live panel knob reading instead of
    /// initial (the three primary knobs).
    bool panelDefault;
};
/// @brief Parameter table indexed by Parameter; definition lives in
///        editor.cpp so the firmware remains the schema source.
extern const Descriptor descriptors[Count];

/// @brief Editor command types.
/// Hello (discovery + schema), Get (read all), Set (write one), Reset
/// (restore one parameter or all to panel/initial defaults), Save
/// (persist via the program-card journal), Restore (apply the state
/// shipped in the command), Defaults (apply the built-in default state)
/// and SetTaraf (write all twelve taraf notes).
enum class Operation { Hello, Get, Set, Reset, Save, Restore, Defaults, SetTaraf };

/// @brief Editor status codes; errorName() gives the JSON string form.
/// None (ok), Syntax (unparsable line), TooLong (line overflow),
/// UnknownParameter, Range (value outside bounds), Busy (refused while
/// the Down switch is held), QueueFull (bridge backpressure) and Storage
/// (journal/flash failure).
enum class Error { None, Syntax, TooLong, UnknownParameter, Range, Busy, QueueFull, Storage };

/// @brief Command object flowing foreground (producer) -> control tick
///        (consumer) through Bridge::commands.
struct Command {
    /// Monotonic request ID (>= 1) echoed in the matching reply, plus the
    /// connection epoch captured at enqueue time (stale epochs are dropped).
    uint32_t sequence = 0, epoch = 0;
    /// Operation requested by the editor.
    Operation operation = Operation::Get;
    /// SET target parameter ordinal and editor-unit value (both
    /// range-checked at parse time; value for Set only).
    int32_t parameter = -1, value = 0;
    /// Full settings snapshot for Restore, serialized in the foreground.
    saved::State state{};
    /// Twelve taraf MIDI notes for SetTaraf.
    uint8_t notes[12]{};
};

/// @brief Reply object flowing control tick (producer) -> foreground
///        (consumer) through Bridge::replies.
struct Reply {
    /// Echo of the command's sequence/epoch/operation, plus the outcome.
    uint32_t sequence = 0, epoch = 0;
    /// Operation this reply answers.
    Operation operation = Operation::Get;
    /// Error::None when the request fully succeeded.
    Error error = Error::None;
    /// Snapshot of every parameter value in editor units at reply time.
    int32_t values[Count]{};
    /// Captured saved::State snapshot; foreground input for Save.
    saved::State state{};
    /// Bitmask over KnobIndex: bit i set while knob i awaits pickup.
    uint32_t pickup = 0;
    /// Switch state at service time: Down held / Up (external) active.
    bool held = false, external = false;
    /// Jawari/swell as they take effect in the Settings snapshot (panel
    /// value plus CV contribution, clipped; CV skipped in external mode),
    /// the factory CV1 reading before user correction, all for the
    /// calibration/reflection UI.
    int32_t effectiveJawari = 0, effectiveSwell = 0, pitchFactoryMv = 0;
};

// Foreground parses bytes; audio control tick owns Controller and replies.
// Each direction has one producer and consumer, including IRQ/foreground use.
/// @brief Single producer/consumer bridge between the foreground USB task
///        and the audio-owned control tick.
/// @details The control tick drains one command per service() call and
/// pushes one reply. Foreground bumps epoch when a connection (re)opens
/// so queued commands from the stale connection are dropped unheard.
struct Bridge {
    std::atomic<uint32_t> epoch{0}; // Foreground invalidates queued work on reconnect.
    /// Command queue foreground -> control tick.
    Queue<Command, 8> commands;
    /// Reply queue control tick -> foreground.
    Queue<Reply, 8> replies;
    void service(Controller &controller, const Controls &controls, const Settings &settings);
};

/// @brief Copy the controller's full editor-visible state into a
///        saved::State snapshot (runs on the control tick).
/// @param state Snapshot to fill (parameters, held IDs, taraf notes and
///        both pitch calibration corrections).
/// @param controller State engine to read.
void capture(saved::State &state, const Controller &controller);

/// @brief Overlay a saved::State snapshot onto the controller.
/// @param state Snapshot to apply (must pass saved::valid()).
/// @param controller State engine to update.
/// @return True when applied; false for an invalid record or while the
///         Down switch is held (parameter writes must not race held edits).
bool apply(const saved::State &state, Controller &controller);

/// @brief Boot-time bootstrap of controller state from a saved record.
/// @param state Saved record to apply.
/// @param controller Freshly constructed controller.
/// @param controls First real hardware sample.
/// @return The settings for the first real control tick after restore.
///
/// Runs one synthetic update with position Middle so knob baselines are
/// initialised before apply(); a physically held Down switch cannot block
/// boot restore, because apply() only refuses the real held sequence.
Settings restoreAtBoot(const saved::State &state, Controller &controller, const Controls &controls);

/// @brief Foreground line assembler for editor command lines.
/// @details Accumulates up to 95 printable ASCII characters (32..126) per
/// line; '\r' is dropped, '\n' finalises. One instance per connection;
/// reset() clears the partial line between connections.
class Parser {
public:
    // Returns true only at a completed line (including malformed/oversized lines).
    /// @brief Consume one received byte.
    /// @param byte Next byte from the serial stream.
    /// @param command Filled only when this call returns true.
    /// @param error Set on failure, cleared on success (same returns).
    /// @return True only at a completed line; malformed, oversized and
    ///         unknown commands finalise with the matching error set.
    bool feed(char byte, Command &command, Error &error);
    /// @brief Discard any partial line state.
    void reset() {
        length = 0;
        overflow = false;
    }

private:
    // Line buffer for printable ASCII only; the 95-byte cap bounds work
    // per line, and overflow is flagged instead of truncating.
    char line[96]{};
    unsigned length = 0;
    bool overflow = false;
};

/// @brief Map an Error to its stable lowercase JSON name.
/// @param error Status code to render.
/// @return Static string such as "none" or "release_down_switch";
///         "unknown" as the fallthrough for unmapped codes.
const char *errorName(Error error);

// JSON output is only formatted in foreground. Returns 0 if buffer too small.
/// @brief Render one reply as a single-line JSON object.
/// @param reply Reply to serialize (values, plus the schema on HELLO).
/// @param output Caller-supplied buffer.
/// @param capacity Buffer size in bytes.
/// @return Bytes written, or 0 when the buffer was too small; the caller
///         must discard the buffer content in that case.
size_t formatReply(const Reply &reply, char *output, size_t capacity);
} // namespace computer::editor
