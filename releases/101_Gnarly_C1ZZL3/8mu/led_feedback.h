#pragma once

#include <cstdint>

// 8mu LED protocol, also documented by Chris Johnson's ComputerCard/EightMU.h:
// note 0-7 sets LED brightness; SysEx 0x30/0x31 controls activity flashing.
// Query 0x1F reads configuration. Never send 0x21: it resets the active bank.
class EightMuLedFeedback
{
public:
    void identified() { isEightMu = true; }

    template <typename Send>
    void poll(uint32_t now, bool wanted, const uint8_t levels[8], Send send)
    {
        // The controller needs SysEx in a separate USB read from LED notes.
        if (haveSent && now - lastTx < 50000u) return;

        if ((!isEightMu && attempts < 8u && (!haveQueried || now - lastQuery >= 250000u)) ||
            (isEightMu && now - lastQuery >= 500000u))
        {
            if (command(0x1Fu, send))
            {
                haveQueried = true;
                lastQuery = now;
                if (attempts < 8u) ++attempts;
                sent(now);
            }
            return;
        }
        if (!isEightMu) return; // Do not send LED notes to other MIDI controllers.

        if (wanted && (!owned || now - lastBlinkOff >= 2000000u))
        {
            if (command(0x30u, send))
            {
                owned = true;
                primed = false;
                lastBlinkOff = now;
                sent(now);
            }
            return;
        }
        if (!wanted && owned)
        {
            if (releaseIndex < 8u)
            {
                while (releaseIndex < 8u)
                {
                    uint8_t off[3] = {0x90u, releaseIndex, 0u};
                    if (!send(off, 3u)) return;
                    ++releaseIndex;
                    sent(now);
                }
                primed = false;
                return;
            }
            if (command(0x31u, send))
            {
                owned = false;
                releaseIndex = 0u;
                sent(now);
            }
            return;
        }
        if (!wanted) return;
        releaseIndex = 0u;

        bool refresh = !primed || now - lastRefresh >= 250000u;
        for (uint8_t i = 0; i < 8u; ++i)
        {
            if (!refresh && levels[i] == lastLevels[i]) continue;
            uint8_t note[3] = {0x90u, i, levels[i]};
            if (!send(note, 3u)) return;
            lastLevels[i] = levels[i];
            sent(now);
        }
        if (refresh) lastRefresh = now;
        primed = true;
    }

private:
    template <typename Send>
    static bool command(uint8_t cmd, Send send)
    {
        const uint8_t message[6] = {0xF0u, 0x7Du, 0u, 0u, cmd, 0xF7u};
        return send(message, 6u);
    }

    void sent(uint32_t now) { haveSent = true; lastTx = now; }
    bool isEightMu = false, owned = false, primed = false;
    bool haveSent = false, haveQueried = false;
    uint8_t attempts = 0u, releaseIndex = 0u;
    uint8_t lastLevels[8] = {};
    uint32_t lastTx = 0u, lastQuery = 0u, lastBlinkOff = 0u, lastRefresh = 0u;
};
