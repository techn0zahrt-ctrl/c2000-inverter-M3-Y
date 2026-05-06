/*
 * This file is part of the c2000-inverter project.
 *
 * Copyright (C) 2021 David J. Fiddes <D.J@fiddes.net>
 * Copyright (C) 2024 David J. Fiddes <D.J@fiddes.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "c2000/encoder.h"
#include "c2000/motoranalogcapture.h"
#include "errormessage.h"
#include "my_fp.h"
#include "params.h"
#include <math.h>
#include <stdint.h>

namespace c2000 {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const float PI     = 3.14159265359f;
static const float TWO_PI = 2.0f * PI;

// Minimum peak ADC count (single channel, after offset correction) to consider
// the resolver signal valid. With 12-bit ADC centred at 2048, 500 counts is
// ~12% of full scale (~0.4 V peak on 3.3 V ref). Much more lenient than the
// original peak-to-peak threshold which failed for stationary motors parked in
// quadrant 1 or 3.
static const int16_t MIN_RESOLVER_AMPLITUDE = 500;

// Minimum angular displacement over one UpdateRotorFrequency interval before
// we consider the motor to be moving (10 degrees in radians).
static const float STABLE_ANGLE = (10.0f * TWO_PI) / 360.0f;

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

static float    s_angle                 = 0.0f;
static float    s_lastAngle             = 0.0f;
static float    s_turnsSinceLastSample  = 0.0f;  // float avoids int truncation
static float    s_lastFrequency         = 0.0f;
static int      s_detectedDirection     = 0;
static int32_t  s_startupDelay          = 0;
static uint32_t s_fullTurns             = 0;
static int      s_poleCounter           = 0;
static uint32_t s_pwmFrq __attribute__((unused)) = 1;
static int16_t  s_resolverPeakAmplitude = 0;
static float    s_lastAbsTurns          = 0.0f;
static float    s_maxSignedDiff         = 0.0f;
static float    s_lastMaxSignedDiff     = 0.0f;
static uint32_t s_sampleCount          = 0;
static uint32_t s_lastSampleCount      = 0;

// ---------------------------------------------------------------------------
// Adapter: offset-correct the raw ADC readings for atan2
// ---------------------------------------------------------------------------

/**
 * Return resolver sine ADC reading centred on zero by subtracting sincosofs.
 * MotorAnalogCapture returns uint16_t 0-4095; sincosofs default = 2048.
 */
static inline int16_t SinCentered()
{
    return (int16_t)MotorAnalogCapture::ResolverSine() -
           (int16_t)Param::GetInt(Param::sincosofs);
}

/**
 * Return resolver cosine ADC reading centred on zero.
 */
static inline int16_t CosCentered()
{
    return (int16_t)MotorAnalogCapture::ResolverCosine() -
           (int16_t)Param::GetInt(Param::sincosofs);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * Decode the current rotor angle from the resolver ADC readings.
 *
 * Uses per-channel absolute peak tracking so the amplitude check passes for
 * a stationary motor regardless of which quadrant the rotor is parked in.
 * Returns the angle in radians in the range (-pi, pi], or 0.0f if the signal
 * amplitude has not yet reached MIN_RESOLVER_AMPLITUDE counts.
 */
static float DecodeAngle()
{
    int16_t sinVal = SinCentered();
    int16_t cosVal = CosCentered();

    // Track the largest absolute value seen across either channel
    int16_t absSin = sinVal < 0 ? (int16_t)-sinVal : sinVal;
    int16_t absCos = cosVal < 0 ? (int16_t)-cosVal : cosVal;
    int16_t peak   = absSin > absCos ? absSin : absCos;
    if (peak > s_resolverPeakAmplitude)
        s_resolverPeakAmplitude = peak;

    if (s_resolverPeakAmplitude > MIN_RESOLVER_AMPLITUDE)
    {
        return atan2f((float)sinVal, (float)cosVal);
    }
    else
    {
        if (s_startupDelay == 0)
            ErrorMessage::Post(ERR_LORESAMP);
        return s_lastAngle;
    }
}

/**
 * Accumulate angular displacement since the last UpdateRotorFrequency call.
 * Handles the 0/2pi wrap-around correctly.
 */
static void UpdateTurns()
{
    float signedDiff = s_angle - s_lastAngle;
    float absDiff    = signedDiff < 0.0f ? -signedDiff : signedDiff;
    float sign       = signedDiff < 0.0f ? -1.0f : 1.0f;

    if (absDiff > PI) // wrap detection
    {
        sign = -sign;
        signedDiff += sign * TWO_PI;
    }

    float absDiff2 = signedDiff < 0.0f ? -signedDiff : signedDiff;
    if (absDiff2 > s_maxSignedDiff) s_maxSignedDiff = absDiff2;

    s_sampleCount++;
    s_turnsSinceLastSample += signedDiff;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * Reset the encoder to a known initial state.
 * Sets the startup delay (4000 PWM cycles ≈ 400 ms at 10 kHz) to suppress
 * ERR_LORESAMP while the exciter signal ramps up.
 */
void Encoder::Reset()
{
    s_angle                 = 0.0f;
    s_lastAngle             = 0.0f;
    s_turnsSinceLastSample  = 0.0f;
    s_lastFrequency         = 0.0f;
    s_detectedDirection     = 0;
    s_startupDelay          = 4000;
    s_fullTurns             = 0;
    s_poleCounter           = 0;
    s_pwmFrq                = 1;
    s_resolverPeakAmplitude = 0;
}

/** A resolver always knows its absolute position. */
bool Encoder::SeenNorthSignal()
{
    return true;
}

/**
 * Update rotor angle from resolver ADC readings.
 * Called every PWM cycle from SinePwmGeneration::Run() / FocPwmGeneration::Run().
 */
void Encoder::UpdateRotorAngle(int /*dir*/)
{
    s_angle = DecodeAngle();

    UpdateTurns();

    // Count complete electrical turns (pole pairs) for odometry
    if ((s_lastAngle <= PI) && (s_angle > PI))
    {
        if (s_poleCounter == 0)
        {
            s_fullTurns++;
            s_poleCounter = Param::GetInt(Param::respolepairs);
        }
        else
        {
            s_poleCounter--;
        }
    }

    s_startupDelay = s_startupDelay > 0 ? s_startupDelay - 1 : 0;
    s_lastAngle    = s_angle;
}

/**
 * Update the rotor frequency estimate.
 * Must be called at a known, regular rate (callingFrequency Hz).
 * Called at 10 Hz (every 100 ms) via scheduler task.
 */
void Encoder::UpdateRotorFrequency(int callingFrequency)
{
    float absTurns = s_turnsSinceLastSample < 0.0f
                         ? -s_turnsSinceLastSample
                         : s_turnsSinceLastSample;

    s_lastAbsTurns = absTurns;
    s_lastMaxSignedDiff = s_maxSignedDiff;
    s_maxSignedDiff = 0.0f;
    s_lastSampleCount = s_sampleCount;
    s_sampleCount = 0;
    float candidate = ((float)callingFrequency * absTurns) / TWO_PI;
    if (s_startupDelay == 0 && absTurns > STABLE_ANGLE)
    {
        if (candidate < 500.0f)
        {
            s_lastFrequency     = candidate;
            s_detectedDirection = s_turnsSinceLastSample > 0.0f ? 1 : -1;
        }
        // else: implausible spike — keep previous s_lastFrequency
    }
    else
    {
        s_lastFrequency     = 0.0f;
        s_detectedDirection = 0;
    }
    s_turnsSinceLastSample = 0.0f;
}

/** Return the number of UpdateTurns() calls in the last UpdateRotorFrequency interval. */
uint32_t Encoder::GetLastSampleCount()
{
    return s_lastSampleCount;
}

/** Return the largest single-step signedDiff magnitude seen in the last UpdateRotorFrequency interval. */
float Encoder::GetLastMaxSignedDiff()
{
    return s_lastMaxSignedDiff;
}

/** Return the absolute accumulated turns from the last UpdateRotorFrequency interval. */
float Encoder::GetLastAbsTurns()
{
    return s_lastAbsTurns;
}

/** Inform the encoder of the PWM carrier frequency (Hz). */
void Encoder::SetPwmFrequency(uint32_t frq)
{
    s_pwmFrq = frq;
}

/**
 * Return the rotor angle in the range [0, 65535] (65536 = 360°).
 * atan2f returns (-pi, pi]; map the negative half to (pi, 2pi) before
 * converting to avoid signed-to-unsigned truncation artefacts.
 */
uint16_t Encoder::GetRotorAngle()
{
    float normalised = s_angle < 0.0f ? s_angle + TWO_PI : s_angle;
    return (uint16_t)((normalised * 65536.0f) / TWO_PI);
}

/** Return rotor frequency in fixed-point Hz. */
u32fp Encoder::GetRotorFrequency()
{
    float clamped = s_lastFrequency > 200.0f ? 200.0f : s_lastFrequency;
    return FP_FROMFLT(clamped);
}

/** Return motor speed in RPM (electrical frequency * 60 / pole pairs). */
int Encoder::GetSpeed()
{
    int polepairs = Param::GetInt(Param::polepairs);
    if (polepairs == 0) polepairs = 1;
    return (int)(FP_TOFLOAT(GetRotorFrequency()) * 60.0f / polepairs);
}

/** Return -1 (backwards), 0 (stationary), or 1 (forwards). */
int Encoder::GetRotorDirection()
{
    return s_detectedDirection;
}

} // namespace c2000
