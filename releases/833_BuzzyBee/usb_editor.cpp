#include "usb_editor.h"

#include <cstring>
#include "tusb.h"

namespace
{
constexpr uint16_t kMagic = 0x0b47;
constexpr uint8_t kQueueSize = 16;

using EditorPreset = buzzypreset;

constexpr EditorPreset kDefaultPresets[7] = {
    {122, 163, 980, 411, 2048, 3077, 1111, 0, -42, 1792},
    {184, 163, -18, 997, 1392, 3071, -331, 0, -204, 1792},
    {732, 163, -18, 1351, 2047, 1535, 3285, 0, 919, 1792},
    {1094, 163, 925, 693, 2048, 1536, 4151, 0, 3785, 1792},
    {-5, 623, 1351, -116, 1675, 81, 3797, 0, 4096, 1792},
    {0, 849, 1359, 614, 1433, -651, 3797, 0, -196, 1792},
    {-2, 163, 0, -17, 2059, 3072, 11, 0, 3, 1792},
};

UsbEditorCommand queue[kQueueSize] = {};
uint8_t queue_read = 0;
uint8_t queue_write = 0;

struct EditorState
{
    EditorPreset presets[7];
    int16_t x;
    int16_t y;
};

EditorState editor_state = {};
bool editor_state_initialized = false;

void ensure_editor_state()
{
    if (!editor_state_initialized)
    {
        std::memcpy(editor_state.presets, kDefaultPresets, sizeof(kDefaultPresets));
        editor_state_initialized = true;
    }
}

bool queue_push(const UsbEditorCommand &command)
{
    const uint8_t write = __atomic_load_n(&queue_write, __ATOMIC_RELAXED);
    const uint8_t next = static_cast<uint8_t>((write + 1) & (kQueueSize - 1));
    if (next == __atomic_load_n(&queue_read, __ATOMIC_ACQUIRE))
    {
        return false;
    }

    queue[write] = command;
    __atomic_store_n(&queue_write, next, __ATOMIC_RELEASE);
    return true;
}

void write_presets()
{
    int16_t packet[14] = {};
    packet[0] = static_cast<int16_t>(kMagic);
    packet[1] = editor_state.x;
    packet[2] = editor_state.y;
    for (int i = 0; i < 7; ++i)
    {
        packet[3] = i;
        std::memcpy(&packet[4], &editor_state.presets[i], sizeof(EditorPreset));
        tud_vendor_write(packet, 4 * sizeof(int16_t) + sizeof(EditorPreset));
        tud_vendor_write_flush();
    }
}
} // namespace

bool usb_editor_pop(UsbEditorCommand &command)
{
    const uint8_t read = __atomic_load_n(&queue_read, __ATOMIC_RELAXED);
    if (read == __atomic_load_n(&queue_write, __ATOMIC_ACQUIRE))
    {
        return false;
    }

    command = queue[read];
    __atomic_store_n(&queue_read, static_cast<uint8_t>((read + 1) & (kQueueSize - 1)), __ATOMIC_RELEASE);
    return true;
}

void usb_editor_set_presets(const buzzypreset presets[7])
{
    std::memcpy(editor_state.presets, presets, sizeof(editor_state.presets));
    editor_state_initialized = true;
}

void usb_editor_get_presets(buzzypreset presets[7])
{
    ensure_editor_state();
    std::memcpy(presets, editor_state.presets, sizeof(editor_state.presets));
}

void usb_editor_service()
{
    ensure_editor_state();
    if (!tud_vendor_available())
    {
        return;
    }

    int16_t packet[32] = {};
    const uint32_t bytes = tud_vendor_read(packet, sizeof(packet));
    const uint32_t words = bytes / sizeof(int16_t);
    if (words == 0 || packet[0] != static_cast<int16_t>(kMagic))
    {
        return;
    }

    if (words == 1)
    {
        write_presets();
        return;
    }
    if (words < 3)
    {
        return;
    }

    editor_state.x = packet[1];
    editor_state.y = packet[2];
    UsbEditorCommand command = {};
    command.type = UsbEditorCommandType::SetXY;
    command.values[0] = editor_state.x;
    command.values[1] = editor_state.y;
    queue_push(command);

    if (words == 4 + sizeof(EditorPreset) / sizeof(int16_t) && packet[3] >= 0 && packet[3] < 7)
    {
        const uint8_t preset = static_cast<uint8_t>(packet[3]);
        std::memcpy(&editor_state.presets[preset], &packet[4], sizeof(EditorPreset));
        command = {};
        command.type = UsbEditorCommandType::SetPreset;
        command.index[0] = preset;
        std::memcpy(command.values, &editor_state.presets[preset], sizeof(EditorPreset));
        queue_push(command);
    }
    else if (words == 10 && (packet[3] & 0x7f) < 10 && packet[4] >= 0 && packet[4] < 7 && packet[5] >= 0 && packet[5] < 7 &&
             packet[6] >= 0 && packet[6] < 7)
    {
        const uint8_t parameter = static_cast<uint8_t>(packet[3] & 0x7f);
        const uint8_t u = static_cast<uint8_t>(packet[4]);
        const uint8_t v = static_cast<uint8_t>(packet[5]);
        const uint8_t w = static_cast<uint8_t>(packet[6]);
        reinterpret_cast<int16_t *>(&editor_state.presets[u])[parameter] = packet[7];
        reinterpret_cast<int16_t *>(&editor_state.presets[v])[parameter] = packet[8];
        reinterpret_cast<int16_t *>(&editor_state.presets[w])[parameter] = packet[9];
        command = {};
        command.type = UsbEditorCommandType::SetParameter;
        command.index[0] = parameter;
        command.index[1] = u;
        command.index[2] = v;
        command.index[3] = w;
        command.values[0] = packet[7];
        command.values[1] = packet[8];
        command.values[2] = packet[9];
        queue_push(command);
    }
}
