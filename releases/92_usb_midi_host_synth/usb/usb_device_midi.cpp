#include "card.h"

#include "config_store.h"
#include "midi_parser.h"
#include "param_maps.h"
#include "runtime_state.h"
#include "sysex_editor.h"

#include "pico/time.h"
#include "tusb.h"

#include <cstring>

bool UsbMidiHostCard::midiStreamWriteMessage(uint8_t const *data, uint32_t size)
{
    if (g_isUsbHost || !tud_mounted() || size == 0)
        return false;
    if (midiTxBusy_)
        return false;

    midiTxBusy_ = true;
    uint32_t sent = 0;
    absolute_time_t startDeadline = make_timeout_time_ms(2);
    absolute_time_t commitDeadline = nil_time;
    bool ok = false;

    while (sent < size)
    {
        uint32_t n = tud_midi_stream_write(0, data + sent, size - sent);
        if (n)
        {
            if (sent == 0)
                commitDeadline = make_timeout_time_ms(30);
            sent += n;
            continue;
        }

        if (sent == 0)
        {
            if (absolute_time_diff_us(get_absolute_time(), startDeadline) <= 0)
                break; // never started — safe to drop
        }
        else if (absolute_time_diff_us(get_absolute_time(), commitDeadline) <= 0)
        {
            uint8_t end = 0xF7;
            for (int i = 0; i < 32; ++i)
            {
                if (tud_midi_stream_write(0, &end, 1))
                    break;
                tud_task();
            }
            break;
        }

        tud_task(); // pump USB only — do not parse RX / send nested SysEx
    }
    ok = (sent == size);
    midiTxBusy_ = false;
    return ok;
}

bool UsbMidiHostCard::enqueueSysEx(uint8_t const *msg, uint32_t total)
{
    if (txqCount_ >= kTxqDepth || total > kTxqMsgMax)
        return false;
    uint8_t slot = txqTail_;
    memcpy(txqBuf_[slot], msg, total);
    txqLen_[slot] = (uint16_t)total;
    txqTail_ = (uint8_t)((txqTail_ + 1) % kTxqDepth);
    ++txqCount_;
    return true;
}

void UsbMidiHostCard::flushSysExQueue()
{
    while (txqCount_ && !midiTxBusy_)
    {
        uint8_t slot = txqHead_;
        uint16_t len = txqLen_[slot];
        txqHead_ = (uint8_t)((txqHead_ + 1) % kTxqDepth);
        --txqCount_;
        if (!midiStreamWriteMessage(txqBuf_[slot], len))
            break; // TX busy — try again next loop
    }
}

bool UsbMidiHostCard::sendSysEx(uint8_t *data, uint32_t size)
{
    if (g_isUsbHost || !tud_mounted())
        return false;
    if (size > sizeof(ExtConfig) + 4)
        return false;

    uint8_t msg[kTxqMsgMax];
    uint32_t total = 0;
    msg[total++] = 0xF0;
    msg[total++] = kMidiMfrId;
    msg[total++] = kDeviceId;
    memcpy(msg + total, data, size);
    total += size;
    msg[total++] = 0xF7;

    // If already transmitting (or called from RX while TX active), defer.
    if (midiTxBusy_ || txqCount_)
        return enqueueSysEx(msg, total);

    if (midiStreamWriteMessage(msg, total))
        return true;
    // Soft-fail: queue for the main USB loop instead of dropping replies.
    return enqueueSysEx(msg, total);
}

void UsbMidiHostCard::onPanelStreamEnable()
{
    panelTxValid_ = false;
    // Defer snapshot to usbCore — never send from inside RX/SysEx parse.
    panelSnapshotReq_ = true;
}

void UsbMidiHostCard::sendPanelSnapshot(bool force)
{
    (void)force;
    panelSnapshotReq_ = true;
}

void UsbMidiHostCard::sendPanelState(bool force)
{
    uint16_t main = (uint16_t)g_panelMain;
    uint16_t x = (uint16_t)g_panelX;
    uint16_t y = (uint16_t)g_panelY;
    uint8_t sw = g_panelSwitch;
    if (main > 4095)
        main = 4095;
    if (x > 4095)
        x = 4095;
    if (y > 4095)
        y = 4095;
    if (sw > 2)
        sw = 2;

    uint16_t qMain = (uint16_t)((main / 32u) * 32u);
    uint16_t qX = (uint16_t)((x / 32u) * 32u);
    uint16_t qY = (uint16_t)((y / 32u) * 32u);

    uint8_t reply[] = {
        kCmdPanelState,
        (uint8_t)((qMain >> 7) & 0x7F),
        (uint8_t)(qMain & 0x7F),
        (uint8_t)((qX >> 7) & 0x7F),
        (uint8_t)(qX & 0x7F),
        (uint8_t)((qY >> 7) & 0x7F),
        (uint8_t)(qY & 0x7F),
        sw,
        (uint8_t)(g_appMode & 0x7F),
        (uint8_t)(g_setupSlot & 0x7F),
        (uint8_t)(g_ext.audioVoice & 0x7F),
        (uint8_t)(g_ext.attack & 0x7F),
        (uint8_t)(g_ext.decay & 0x7F),
        (uint8_t)(g_ext.sustain & 0x7F),
        (uint8_t)(g_ext.releaseAmp & 0x7F),
        (uint8_t)(g_ext.cutoff & 0x7F),
        (uint8_t)(g_ext.pwmWidth & 0x7F),
        (uint8_t)(g_setupSlotPending & 0x7F),
        (uint8_t)(g_cvOutsCalibrated ? 1 : 0),
    };

    bool discrete = !panelTxValid_;
    bool continuous = false;
    if (panelTxValid_)
    {
        // Mode / slot / engine / ADSR — send ASAP when any change.
        if (memcmp(reply + 7, panelTxPayload_ + 7, sizeof(reply) - 7) != 0)
            discrete = true;

        auto knobMoved = [](uint16_t a, uint16_t b) {
            int d = (int)a - (int)b;
            return d >= kPanelKnobHyst || d <= -kPanelKnobHyst;
        };
        if (knobMoved(qMain, panelTxMain_) || knobMoved(qX, panelTxX_) ||
            knobMoved(qY, panelTxY_))
            continuous = true;
    }

    if (!force && !discrete)
    {
        int64_t age = absolute_time_diff_us(panelTxAt_, get_absolute_time());
        if (continuous)
        {
            if (age < (int64_t)kPanelMinIntervalMs * 1000)
                return;
        }
        else if (age < (int64_t)kPanelKeepaliveMs * 1000)
        {
            return;
        }
    }

    // Drop whole message if TX is busy — never leave a half SysEx on the wire.
    if (!sendSysEx(reply, sizeof(reply)))
        return;

    memcpy(panelTxPayload_, reply, sizeof(reply));
    panelTxMain_ = qMain;
    panelTxX_ = qX;
    panelTxY_ = qY;
    panelTxValid_ = true;
    panelTxAt_ = get_absolute_time();
}

void UsbMidiHostCard::parseDeviceMidiBytes(uint8_t *rxBuf, unsigned bytesReceived)
{
    for (unsigned i = 0; i < bytesReceived; i++)
    {
        uint8_t b = rxBuf[i];
        if (b == 0xF0)
        {
            sysexActive_ = true;
            sysexLen_ = 0;
            sysexBuf_[sysexLen_++] = b;
            continue;
        }
        if (!sysexActive_)
            parseMidiByte(b);
        else
        {
            if (sysexLen_ < kSysexBufSize)
                sysexBuf_[sysexLen_++] = b;
            if (b == 0xF7)
            {
                if (sysexLen_ >= 5 && sysexBuf_[1] == kMidiMfrId &&
                    sysexBuf_[2] == kDeviceId)
                    sysexProcessIncoming(*this, sysexBuf_ + 3, sysexLen_ - 4);
                sysexActive_ = false;
                sysexLen_ = 0;
            }
        }
    }
}

void UsbMidiHostCard::drainDeviceMidiRx(unsigned maxBytes)
{
    uint8_t packet[64];
    unsigned got = 0;
    while (got < maxBytes && tud_midi_available())
    {
        uint32_t n = tud_midi_stream_read(packet, sizeof(packet));
        if (!n)
            break;
        parseDeviceMidiBytes(packet, n);
        got += n;
    }
}
