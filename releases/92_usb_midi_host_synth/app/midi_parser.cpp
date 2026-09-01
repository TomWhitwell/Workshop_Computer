#include "midi_parser.h"

#include "param_maps.h"

void parseMidiByte(uint8_t b)
{
    static uint8_t runningStatus = 0;
    static uint8_t buf[3];
    static int bufPos = 0;

    if (b & 0x80)
    {
        if (b >= 0xF8)
            return;
        if (b >= 0xF0)
        {
            runningStatus = 0;
            bufPos = 0;
            return;
        }
        runningStatus = b;
        buf[0] = b;
        bufPos = 1;
    }
    else
    {
        if (runningStatus == 0)
            return;
        if (bufPos == 0)
        {
            buf[0] = runningStatus;
            bufPos = 1;
        }
        buf[bufPos++] = b;

        uint8_t type = runningStatus & 0xF0;
        int expected = 0;
        if (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 ||
            type == 0xE0)
            expected = 3;
        else if (type == 0xC0 || type == 0xD0)
            expected = 2;

        if (bufPos >= expected && expected > 0)
        {
            handleChannelMessage(buf);
            bufPos = 1;
        }
    }
}

void parseMidiStream(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
        parseMidiByte(data[i]);
}
