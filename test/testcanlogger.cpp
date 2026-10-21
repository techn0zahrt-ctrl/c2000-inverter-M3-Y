// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "canlogger.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>

class RecordingCan : public CanHardware
{
public:
    RecordingCan() : frameCount(0) {}
    void SetBaudrate(baudrates) override {}
    void Send(uint32_t id, uint32_t data[2], uint8_t length) override
    {
        assert(id == 0x7FF && length == 8);
        ++frameCount;
        for (unsigned i = 0; i < length; ++i)
        {
            const char byte = (data[i / 4] >> (8 * (i % 4))) & 0xFF;
            if (byte != 0)
                pending += byte;
            else if (!pending.empty())
            {
                messages.push_back(pending);
                pending.clear();
            }
        }
    }

    unsigned frameCount;
    std::string pending;
    std::vector<std::string> messages;

private:
    void ConfigureFilters() override {}
};

int main()
{
    RecordingCan bus;
    CanLogger::Print("before init");
    CanLogger::Init(&bus);
    CanLogger::Print("disabled");
    assert(bus.frameCount == 0);

    uint32_t control[2] = { 0x1401, 0 };
    bus.HandleRx(0x7FE, control, 2);
    assert(CanLogger::IsEnabled());
    CanLogger::Print(0);
    assert(bus.frameCount == 0);

    const unsigned lengths[] = { 0, 1, 7, 8, 9, 15, 16, 17, 127, 128 };
    for (unsigned n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n)
    {
        std::string text;
        for (unsigned i = 0; i < lengths[n]; ++i)
            text += char('A' + i % 26);
        bus.frameCount = 0;
        bus.messages.clear();
        CanLogger::Print(text.c_str());
        assert(bus.pending.empty());
        assert(bus.frameCount == (text.empty() ? 0 : text.size() / 8 + 1));
        assert(bus.messages.size() == (text.empty() ? 0U : 1U));
        if (!text.empty()) assert(bus.messages[0] == text);
    }

    bus.messages.clear();
    CanLogger::Printf("12345678");
    CanLogger::Print("next");
    assert(bus.pending.empty());
    assert(bus.messages.size() == 2);
    assert(bus.messages[0] == "12345678" && bus.messages[1] == "next");

    const unsigned beforeStop = bus.frameCount;
    control[0] = 0;
    bus.HandleRx(0x7FE, control, 2);
    CanLogger::Print("stopped");
    CanLogger::Printf("stopped");
    assert(bus.frameCount == beforeStop);
    puts("CAN logger framing checks passed");
}
