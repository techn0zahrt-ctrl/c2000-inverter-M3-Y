// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "hvilinterlock.h"
#include <assert.h>
#include <stdio.h>

static const s32fp MinCurrent = FP_FROMINT(10);
static const s32fp MaxCurrent = FP_FROMINT(30);

static void Qualify(HvilInterlock& interlock, uint16_t frequency)
{
    interlock.Configure(frequency);
    const uint32_t samples = ((uint32_t)frequency + 9U) / 10U;
    for (uint32_t i = 1; i < samples; ++i)
    {
        interlock.Update(106, MinCurrent, MaxCurrent);
        assert(!interlock.AllowsOutput());
    }
    interlock.Update(106, MinCurrent, MaxCurrent);
    assert(interlock.AllowsOutput());
}

int main()
{
    HvilInterlock pending;
    assert(!pending.AllowsOutput());

    const uint16_t frequencies[] = { 6103, 12207, 24414 };
    for (unsigned i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); ++i)
    {
        HvilInterlock interlock;
        Qualify(interlock, frequencies[i]);
        interlock.Update(0, MinCurrent, MaxCurrent);
        assert(interlock.Tripped());
        assert(!interlock.AllowsOutput());
        interlock.Configure(frequencies[i]);
        for (uint32_t sample = 0; sample < frequencies[i]; ++sample)
            interlock.Update(106, MinCurrent, MaxCurrent);
        assert(interlock.Tripped());
        assert(!interlock.AllowsOutput());
    }

    // An invalid startup sample or a single glitch cannot lead to auto-resume.
    const uint16_t invalidSamples[] = { 0, 53, 161, 4095, 65535 };
    for (unsigned i = 0; i < sizeof(invalidSamples) / sizeof(invalidSamples[0]); ++i)
    {
        HvilInterlock interlock;
        interlock.Configure(12207);
        interlock.Update(invalidSamples[i], MinCurrent, MaxCurrent);
        assert(interlock.Tripped());
        interlock.Update(106, MinCurrent, MaxCurrent);
        assert(!interlock.AllowsOutput());
    }

    HvilInterlock edges;
    Qualify(edges, 12207);
    edges.Update(54, MinCurrent, MaxCurrent); // 10.125 mA
    assert(edges.AllowsOutput());
    edges.Update(160, MinCurrent, MaxCurrent); // 30 mA, inclusive upper bound
    assert(edges.AllowsOutput());

    const s32fp invalidLimits[][2] = {
        { 0, MaxCurrent }, { MaxCurrent, MinCurrent },
        { MinCurrent, MinCurrent }, { MinCurrent, FP_FROMINT(768) }
    };
    for (unsigned i = 0; i < sizeof(invalidLimits) / sizeof(invalidLimits[0]); ++i)
    {
        HvilInterlock interlock;
        Qualify(interlock, 12207);
        interlock.Update(106, invalidLimits[i][0], invalidLimits[i][1]);
        assert(interlock.Tripped());
        assert(!interlock.AllowsOutput());
    }

    HvilInterlock stoppedClock;
    stoppedClock.Configure(0);
    stoppedClock.Update(106, MinCurrent, MaxCurrent);
    assert(stoppedClock.Tripped());
    assert(!stoppedClock.AllowsOutput());

    HvilInterlock changedFrequency;
    Qualify(changedFrequency, 12207);
    changedFrequency.Configure(24414);
    assert(!changedFrequency.AllowsOutput());

    puts("HVIL interlock checks passed");
}
