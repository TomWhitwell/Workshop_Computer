/// @file editor.cpp
/// @brief Implementation of the USB live-editor protocol: schema table,
///        line parsing, controller application and JSON replies.
///
/// The firmware owns the parameter schema; the table below is the single
/// source for labels, units, bounds, defaults, reset policy and timing
/// hints, and is served verbatim in HELLO replies. Parsing and formatting
/// run in the foreground; all Controller state access happens inside
/// Bridge::service() on the audio-owned control tick. Helpers are
/// allocation-free; std::snprintf is the only text formatting entry point.
#include "editor.hpp"
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace computer::editor {
/// @brief Parameter schema, ordered by Parameter ordinal.
/// Bounds and initials below are mirrored by saved::valid() (parameters
/// 0..ResonatorMix) and by the parser's check on SET/RESET lines. The
/// first three rows carry panelDefault, so Reset takes the live knob
/// reading and arms pickup; everything else resets to initial.
const Descriptor descriptors[Count] = {
    {"jawari", "Jawari", "panel", "smoothed", 0, 4095, 0, true},
    {"swell", "Swell", "panel", "smoothed", 0, 4095, 0, true},
    {"string", "String", "panel", "smoothed", 0, 4095, 0, true},
    {"sympathy", "Sympathy", "Q15", "smoothed", 0, 32767, 32767, false},
    {"octave", "Octave", "octaves", "smoothed pitch", -3, 3, -1, false},
    {"pick", "Pick release", "bipolar Q15", "next pluck", -32767, 32767, 0, false},
    {"pick_position_percent", "Pick position", "% of string", "next pluck", 5, 40, 17, false},
    {"halo_level_percent", "Halo output level", "%", "smoothed", 0, 100, 50, false},
    {"melody_decay_percent", "Melody duration", "%", "smoothed", 25, 400, 100, false},
    {"sympathetic_decay_percent", "Sympathetic duration", "%", "smoothed", 25, 400, 100, false},
    {"resonator_mix_percent", "External resonator mix", "%", "smoothed", 0, 100, 50, false},
    // Held-assignment rows carry the storage IDs (see HeldAssignment), which
    // are also persisted verbatim in saved::State.
    {"held_main", "Main held assignment", "assignment", "held Down", 0, 8, 1, false},
    {"held_x", "X held assignment", "assignment", "held Down", 0, 8, 2, false},
    {"held_y", "Y held assignment", "assignment", "held Down", 0, 8, 3, false},
    {"pitch_in_offset_mv", "Pitch input offset", "mV", "calibration", -500, 500, 0, false},
    {"pitch_in_scale_ppm", "Pitch input scale", "ppm", "calibration", 800000, 1200000, 1000000,
     false},
    {"pitch_out_offset_mv", "Pitch output offset", "mV", "calibration", -500, 500, 0, false},
    {"pitch_out_scale_ppm", "Pitch output scale", "ppm", "calibration", 800000, 1200000, 1000000,
     false},
    // Taraf rows hold the twelve chord MIDI notes; the 36..84 range matches
    // saved::valid() and the SET_TARAF parser check.
    {"taraf_1", "Taraf 1", "MIDI note", "fade retune", 36, 84, 61, false},
    {"taraf_2", "Taraf 2", "MIDI note", "fade retune", 36, 84, 59, false},
    {"taraf_3", "Taraf 3", "MIDI note", "fade retune", 36, 84, 62, false},
    {"taraf_4", "Taraf 4", "MIDI note", "fade retune", 36, 84, 62, false},
    {"taraf_5", "Taraf 5", "MIDI note", "fade retune", 36, 84, 64, false},
    {"taraf_6", "Taraf 6", "MIDI note", "fade retune", 36, 84, 66, false},
    {"taraf_7", "Taraf 7", "MIDI note", "fade retune", 36, 84, 68, false},
    {"taraf_8", "Taraf 8", "MIDI note", "fade retune", 36, 84, 69, false},
    {"taraf_9", "Taraf 9", "MIDI note", "fade retune", 36, 84, 71, false},
    {"taraf_10", "Taraf 10", "MIDI note", "fade retune", 36, 84, 73, false},
    {"taraf_11", "Taraf 11", "MIDI note", "fade retune", 36, 84, 74, false},
    {"taraf_12", "Taraf 12", "MIDI note", "fade retune", 36, 84, 76, false},
};

/// @brief Map an Error to its stable lowercase JSON name.
/// @param error Status code to render.
/// @return Static string ("none", "release_down_switch" for a refusal
///         while Down is held, "busy" for bridge backpressure, ...);
///         "unknown" for unmapped codes.
const char *errorName(Error e) {
    switch (e) {
    case Error::None:
        return "none";
    case Error::Syntax:
        return "syntax";
    case Error::TooLong:
        return "line_too_long";
    case Error::UnknownParameter:
        return "unknown_parameter";
    case Error::Range:
        return "out_of_range";
    case Error::Busy:
        return "release_down_switch";
    case Error::Storage:
        return "storage_error";
    case Error::QueueFull:
        return "busy";
    }
    return "unknown";
}
namespace {
/// @brief Parse a strict decimal integer (optional '-', digits only).
/// @param s NUL-terminated token; must not be empty after any sign.
/// @param value Parsed result on success.
/// @return False on non-digit characters or uint32 overflow while
/// accumulating, so the magnitude cannot wrap silently.
bool integer(const char *s, int32_t &value) {
    bool negative = *s == '-';
    if (negative) {
        ++s;
    }
    if (!*s) {
        return false;
    }
    uint32_t n = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        unsigned digit = unsigned(*s - '0');
        if (n > (uint32_t(INT32_MAX) - digit) / 10u) {
            return false;
        }
        n = n * 10 + digit;
    }
    value = negative ? -int32_t(n) : int32_t(n);
    return true;
}

/// @brief Look up a parameter by stable text ID.
/// @param id NUL-terminated wire ID from the command line.
/// @return Parameter ordinal, or -1 when unknown.
int parameter(const char *id) {
    for (unsigned i = 0; i < Count; ++i) {
        if (!std::strcmp(id, descriptors[i].id)) {
            return int(i);
        }
    }
    return -1;
}
} // namespace

/// @brief Consume one received byte; the declaration carries the
///        completed-line contract.
/// @param byte Stream byte.
/// @param c Command filled out when the call returns true.
/// @param error Error::None for a valid command, otherwise the failure
///        code; always set to a definite value on the returning path.
/// @return True only at a completed line.
bool Parser::feed(char byte, Command &c, Error &error) {
    if (byte != '\n') {
        if (byte == '\r') {
            return false;
        }
        // Only printable ASCII is buffered; a byte that no longer fits the
        // buffer marks the whole line TooLong rather than truncating.
        if (length + 1 < sizeof(line) && byte >= 32 && byte <= 126) {
            line[length++] = byte;
        } else {
            overflow = true;
        }
        return false;
    }
    c = {};
    error = Error::Syntax;
    if (overflow) {
        reset();
        error = Error::TooLong;
        return true;
    }
    line[length] = 0;
    length = 0;
    // Split on spaces in place; slot 0 is the verb. Fourteen words is the
    // largest valid command (SET_TARAF), so a 15th word is invalid.
    char *words[14]{};
    unsigned count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ') {
            ++p;
        }
        if (!*p) {
            break;
        }
        if (count == 14) {
            return true;
        }
        words[count++] = p;
        while (*p && *p != ' ') {
            ++p;
        }
        if (*p) {
            *p++ = 0;
        }
    }
    int32_t sequence;
    // Every verb needs a sequence >= 1 right after it.
    if (count < 2 || !integer(words[1], sequence) || sequence < 1) {
        return true;
    }
    c.sequence = uint32_t(sequence);
    if (!std::strcmp(words[0], "HELLO") && count == 2) {
        c.operation = Operation::Hello;
    } else if (!std::strcmp(words[0], "GET") && count == 2) {
        c.operation = Operation::Get;
    } else if (!std::strcmp(words[0], "SAVE") && count == 2) {
        c.operation = Operation::Save;
    } else if (!std::strcmp(words[0], "RESTORE") && count == 2) {
        c.operation = Operation::Restore;
    } else if (!std::strcmp(words[0], "DEFAULTS") && count == 2) {
        c.operation = Operation::Defaults;
    } else if (!std::strcmp(words[0], "RESET") && count == 3) {
        c.operation = Operation::Reset;
        c.parameter = parameter(words[2]);
        if (c.parameter < 0 && std::strcmp(words[2], "all")) {
            error = Error::UnknownParameter;
            return true;
        }
    } else if (!std::strcmp(words[0], "SET_TARAF") && count == 14) {
        c.operation = Operation::SetTaraf;
        // Exactly twelve notes 36..84; any bad note rejects the line.
        for (unsigned i = 0; i < 12; ++i) {
            int32_t note;
            if (!integer(words[i + 2], note)) {
                return true;
            }
            if (note < 36 || note > 84) {
                error = Error::Range;
                return true;
            }
            c.notes[i] = uint8_t(note);
        }
    } else if (!std::strcmp(words[0], "SET") && count == 4) {
        c.operation = Operation::Set;
        c.parameter = parameter(words[2]);
        if (c.parameter < 0) {
            error = Error::UnknownParameter;
            return true;
        }
        if (!integer(words[3], c.value)) {
            return true;
        }
        const auto &d = descriptors[c.parameter];
        if (c.value < d.minimum || c.value > d.maximum) {
            error = Error::Range;
            return true;
        }
    } else {
        return true;
    }
    error = Error::None;
    return true;
}

/// @brief Snapshot the controller's full editor-visible state into a
///        saved::State record (control-boundary helper; reads only).
void capture(saved::State &state, const Controller &controller) {
    for (unsigned i = 0; i < saved::parameterCount; ++i) {
        state.parameters[i] = controller.editorValue(i);
    }
    for (unsigned i = 0; i < 3; ++i) {
        state.held[i] = uint8_t(controller.heldAssignment(i));
    }
    for (unsigned i = 0; i < saved::noteCount; ++i) {
        state.notes[i] = uint8_t(controller.editorValue(Taraf1 + i));
    }
    state.pitchIn = controller.pitchInputCalibration;
    state.pitchOut = controller.pitchOutputCalibration;
}

/// @brief Overlay a saved::State record onto the controller.
/// @param state Snapshot to apply; must pass saved::valid().
/// @param controller State engine to update.
/// @return False for an invalid record or while the Down switch was held
///         on the previous tick; true after full application.
bool apply(const saved::State &state, Controller &controller) {
    if (!saved::valid(state) || controller.previousPosition() == Down) {
        return false;
    }
    // Parameter 0..ResonatorMix restores first, then the held assignments,
    // then the taraf notes and both CV corrections.
    for (unsigned i = 0; i < saved::parameterCount; ++i) {
        controller.setEditorValue(i, state.parameters[i]);
    }
    for (unsigned i = 0; i < 3; ++i) {
        controller.setHeldAssignment(i, state.held[i]);
    }
    for (unsigned i = 0; i < saved::noteCount; ++i) {
        controller.setEditorValue(Taraf1 + i, state.notes[i]);
    }
    controller.pitchInputCalibration = state.pitchIn;
    controller.pitchOutputCalibration = state.pitchOut;
    return true;
}

/// @brief Boot-time bootstrap: baseline knobs, apply the saved record, then
///        take one real update pass.
/// @param state Saved record to apply.
/// @param controller Freshly constructed controller.
/// @param controls First real hardware sample.
/// @return Settings for the first real control tick after restore.
Settings restoreAtBoot(const saved::State &state, Controller &controller,
                       const Controls &controls) {
    // Initialise physical positions before restoring; use a synthetic Middle
    // solely for bootstrap so a physically held Down cannot block restore.
    Controls initial = controls;
    initial.position = Middle;
    controller.update(initial);
    apply(state, controller);
    return controller.update(controls);
}

/// @brief Control-tick side of the bridge: drain one command, mutate only
///        this controller, and answer with a full snapshot reply.
/// @param controller State engine owned by the control tick.
/// @param controls Latest hardware sample (panel truth for reset policy).
/// @param settings Latest worker snapshot for the effective-value fields.
///
/// Mutating commands are refused (Busy) while the switch is Down so they
/// never race a held-edit session; commands are dropped without a reply
/// when no reply slot is free or the epoch has moved on.
void Bridge::service(Controller &controller, const Controls &controls, const Settings &settings) {
    // Backpressure: with no reply slot free, drop the command outright.
    if (replies.size() == replies.capacity) {
        return;
    }
    Command command;
    if (!commands.pop(command)) {
        return;
    }
    // A stale epoch means the command arrived from a dead connection.
    if (command.epoch != epoch.load(std::memory_order_acquire)) {
        return;
    }
    Reply reply;
    reply.epoch = command.epoch;
    reply.sequence = command.sequence;
    reply.operation = command.operation;
    reply.held = controls.position == Down;
    reply.external = settings.external;
    if (command.operation == Operation::Save && reply.held) {
        reply.error = Error::Busy;
    }
    if (command.operation == Operation::Restore || command.operation == Operation::Defaults) {
        if (reply.held || !apply(command.state, controller)) {
            reply.error = Error::Busy;
        }
    }
    if (command.operation == Operation::Set || command.operation == Operation::Reset ||
        command.operation == Operation::SetTaraf) {
        if (reply.held) {
            reply.error = Error::Busy;
        } else if (command.operation == Operation::SetTaraf) {
            for (unsigned i = 0; i < 12; ++i) {
                controller.setEditorValue(Taraf1 + i, command.notes[i]);
            }
        } else if (command.operation == Operation::Set) {
            if (!controller.setEditorValue(command.parameter, command.value)) {
                reply.error = Error::Range;
            }
        } else {
            // Reset: one parameter or all; primaries take their live panel
            // knob reading, everything else the descriptor's initial value.
            for (int i = 0; i < Count; ++i) {
                if (command.parameter >= 0 && command.parameter != i) {
                    continue;
                }
                const auto &d = descriptors[i];
                const int knobIndex = i == Jawari ? MainKnob : (i == Swell ? LeftKnob : RightKnob);
                controller.setEditorValue(i,
                                          d.panelDefault ? controls.knobs[knobIndex] : d.initial);
            }
        }
    }
    // Always answer with the complete state, even after a refused command.
    capture(reply.state, controller);
    for (int i = 0; i < Count; ++i) {
        reply.values[i] = controller.editorValue(i);
    }
    for (unsigned i = 0; i < KnobCount; ++i) {
        if (controller.awaitingPickup(i)) {
            reply.pickup |= 1u << i;
        }
    }
    reply.pitchFactoryMv = controls.pitchFactoryMv;
    reply.effectiveJawari = settings.params.jawari;
    reply.effectiveSwell = settings.params.swell;
    replies.push(reply);
}

/// @brief Render one reply as a single-line JSON object; see the
///        declaration for the buffer-too-small contract.
/// @param reply Reply to serialize.
/// @param output Caller-supplied buffer.
/// @param capacity Buffer size in bytes.
/// @return Bytes used on success; 0 when any write did not fit.
size_t formatReply(const Reply &r, char *output, size_t capacity) {
    size_t used = 0;
    bool valid = true;
    // Any append that overruns the capacity poisons the rest of the reply.
    auto append = [&](const char *format, auto... args) {
        if (!valid) {
            return;
        }
        int count = std::snprintf(output + used, capacity - used, format, args...);
        if (count < 0 || size_t(count) >= capacity - used) {
            valid = false;
        } else {
            used += size_t(count);
        }
    };
    append("{\"seq\":%lu,\"protocol\":1,\"device\":\"mtm-sitar\",\"firmware\":\"usb-editor-1\","
           "\"error\":\"%s\"",
           static_cast<unsigned long>(r.sequence), errorName(r.error));
    if (r.error == Error::None) {
        append(",\"values\":{");
        for (unsigned i = 0; i < Count; ++i) {
            append("%s\"%s\":%ld", i ? "," : "", descriptors[i].id, static_cast<long>(r.values[i]));
        }
        append("},\"pitchInputFactoryMv\":%ld,\"pickup\":%lu,\"held\":%s,\"external\":%s,"
               "\"effective\":{\"jawari\":%ld,"
               "\"swell\":%ld}",
               static_cast<long>(r.pitchFactoryMv), static_cast<unsigned long>(r.pickup),
               r.held ? "true" : "false", r.external ? "true" : "false",
               static_cast<long>(r.effectiveJawari), static_cast<long>(r.effectiveSwell));
        if (r.operation == Operation::Hello) {
            append(",\"parameters\":[");
            for (unsigned i = 0; i < Count; ++i) {
                const auto &d = descriptors[i];
                append("%s{\"id\":\"%s\",\"label\":\"%s\",\"unit\":\"%s\",\"timing\":\"%s\","
                       "\"min\":%ld,\"max\":%ld,\"default\":%ld,\"panelDefault\":%s}",
                       i ? "," : "", d.id, d.label, d.unit, d.timing, static_cast<long>(d.minimum),
                       static_cast<long>(d.maximum), static_cast<long>(d.initial),
                       d.panelDefault ? "true" : "false");
            }
            append("]");
        }
    }
    append("}\n");
    return valid ? used : 0;
}
} // namespace computer::editor
