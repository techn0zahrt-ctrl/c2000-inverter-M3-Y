// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HVILINTERLOCK_H
#define HVILINTERLOCK_H

#include "my_fp.h"

class HvilInterlock
{
public:
    HvilInterlock() : requiredSamples(0), goodSamples(0), tripped(false) {}

    void Configure(uint16_t sampleFrequency)
    {
        // Require 100 ms of valid samples, rounded up at every PWM frequency.
        requiredSamples = ((uint32_t)sampleFrequency + 9U) / 10U;
        goodSamples = 0;
        // Reinitialising PWM must never clear a latched fault.
    }

    void Update(uint16_t raw, s32fp minimum, s32fp maximum)
    {
        const s32fp current = (s32fp)raw * FP_FROMFLT(0.1875);
        if (requiredSamples == 0 || minimum <= 0 || minimum >= maximum ||
            maximum >= (s32fp)4095 * FP_FROMFLT(0.1875) ||
            raw >= 4095 || current < minimum || current > maximum)
        {
            tripped = true;
            goodSamples = 0;
        }
        else if (!tripped && goodSamples < requiredSamples)
        {
            goodSamples = goodSamples + 1U;
        }
    }

    bool AllowsOutput() const
    {
        return !tripped && requiredSamples != 0 && goodSamples >= requiredSamples;
    }

    bool Tripped() const { return tripped; }

private:
    volatile uint16_t requiredSamples;
    volatile uint16_t goodSamples;
    volatile bool tripped;
};

#endif
