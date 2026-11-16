#include "editor.hpp"
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace computer::editor {
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
    {"held_main", "Main held assignment", "assignment", "held Down", 0, 8, 1, false},
    {"held_x", "X held assignment", "assignment", "held Down", 0, 8, 2, false},
    {"held_y", "Y held assignment", "assignment", "held Down", 0, 8, 3, false},
    {"pitch_in_offset_mv", "Pitch input offset", "mV", "calibration", -500, 500, 0, false},
    {"pitch_in_scale_ppm", "Pitch input scale", "ppm", "calibration", 800000, 1200000, 1000000,
     false},
    {"pitch_out_offset_mv", "Pitch output offset", "mV", "calibration", -500, 500, 0, false},
    {"pitch_out_scale_ppm", "Pitch output scale", "ppm", "calibration", 800000, 1200000, 1000000,
     false},
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
int parameter(const char *id) {
    for (unsigned i = 0; i < Count; ++i) {
        if (!std::strcmp(id, descriptors[i].id)) {
            return int(i);
        }
    }
    return -1;
}
} // namespace
bool Parser::feed(char byte, Command &c, Error &error) {
    if (byte != '\n') {
        if (byte == '\r') {
            return false;
        }
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
bool apply(const saved::State &state, Controller &controller) {
    if (!saved::valid(state) || controller.previousPosition() == Down) {
        return false;
    }
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
void Bridge::service(Controller &controller, const Controls &controls, const Settings &settings) {
    if (replies.size() == replies.capacity) {
        return;
    }
    Command command;
    if (!commands.pop(command)) {
        return;
    }
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
size_t formatReply(const Reply &r, char *output, size_t capacity) {
    size_t used = 0;
    bool valid = true;
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
