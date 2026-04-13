/*
 * This file is part of the stm32-sine project.
 *
 * Copyright (C) 2015 Johannes Huebner <dev@johanneshuebner.com>
 * Copyright (C) 2021 David J. Fiddes <D.J@fiddes.net>
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
#ifndef SINEPWMGENERATIONFOC_H
#define SINEPWMGENERATIONFOC_H

#include "fu.h"
#include "my_fp.h"
#include "my_math.h"
#include "params.h"
#include "pwmgenerationbase.h"
#include "sine_core.h"
#include <stdint.h>

template <typename CurrentT, typename EncoderT, typename PwmDriverT>
class SinePwmGeneration : public PwmGenerationBase<
                              SinePwmGeneration<CurrentT, EncoderT, PwmDriverT>,
                              CurrentT,
                              PwmDriverT>
{
    typedef PwmGenerationBase<
        SinePwmGeneration<CurrentT, EncoderT, PwmDriverT>,
        CurrentT,
        PwmDriverT> BaseT;

    // We need to allow PwmGenerationBase to call PwmInit
    friend class PwmGenerationBase<
        SinePwmGeneration<CurrentT, EncoderT, PwmDriverT>,
        CurrentT,
        PwmDriverT>;

    // Bring inherited base class members into scope for unqualified access in
    // method bodies. Required by C++03 (TI compiler) where qualified access via
    // a dependent class name is not supported, and by C++11 strict two-phase
    // lookup which does not search dependent base classes for unqualified names.
    using BaseT::opmode;
    using BaseT::frq;
    using BaseT::angle;
    using BaseT::shiftForTimer;
    using BaseT::ampnom;
    using BaseT::fslip;
    using BaseT::slipIncr;
    using BaseT::pwmfrq;
    using BaseT::pwmdigits;
    using BaseT::polePairRatio;
    using BaseT::ilofs;
    using BaseT::Charge;
    using BaseT::GetCurrent;
    using BaseT::FrqToAngle;
    using BaseT::DigitToDegree;
public:
    using BaseT::SetCurrentOffset;

    static s32fp GetDebugAmpNomLimited() { return s_debugAmpNomLimited; }
    static uint32_t GetDebugAmp() { return s_debugAmp; }

public:
    static void Run()
    {
        if (opmode == MANUAL || opmode == RUN ||
            opmode == SINE)
        {
            int32_t dir = Param::GetInt(Param::dir);

            EncoderT::UpdateRotorAngle(dir);
            s32fp ampNomLimited = LimitCurrent();

            if (opmode == SINE)
                CalcNextAngleConstant(dir);
            else
                CalcNextAngleAsync(dir);

            uint32_t amp = MotorVoltage::GetAmpPerc(frq, ampNomLimited);
            s_debugAmpNomLimited = ampNomLimited;
            s_debugAmp = amp;

            SineCore::SetAmp(amp);
            Param::SetInt(Param::amp, amp);
            Param::SetFixed(Param::fstat, frq);
            Param::SetFixed(Param::angle, DigitToDegree(angle));
            SineCore::Calc(angle);

            /* Shut down PWM on zero voltage request */
            if (0 == amp || 0 == dir)
            {
                PwmDriverT::DisableMasterOutput();
            }
            else
            {
                PwmDriverT::EnableMasterOutput();
            }

            /* Match to PWM resolution */
            PwmDriverT::SetPhasePwm(
                SineCore::DutyCycles[0] >> shiftForTimer,
                SineCore::DutyCycles[1] >> shiftForTimer,
                SineCore::DutyCycles[2] >> shiftForTimer);
        }
        else if (opmode == BOOST || opmode == BUCK)
        {
            Charge();
        }
        else if (opmode == ACHEAT)
        {
            PwmDriverT::AcHeat(ampnom);
        }
    }

    static void SetTorquePercent(float torque)
    {
        const int   filterConst = 4;
        const float roundingError = FP_TOFLOAT((float)((1 << filterConst) - 1));
        ;
        float fslipmin = Param::GetFloat(Param::fslipmin);
        float ampmin = Param::GetFloat(Param::ampmin);
        float slipstart = Param::GetFloat(Param::slipstart);
        float ampnomLocal;
        float fslipspnt = 0;

        if (torque >= 0)
        {
            /* In async mode first X% throttle commands amplitude, X-100% raises
             * slip */
            ampnomLocal = ampmin + (100.0f - ampmin) * torque / slipstart;

            if (torque >= slipstart)
            {
                float fstat = Param::GetFloat(Param::fstat);
                float fweak = Param::GetFloat(Param::fweakcalc);
                float fslipmax = Param::GetFloat(Param::fslipmax);

                if (fstat > fweak)
                {
                    float fconst = Param::GetFloat(Param::fconst);
                    float fslipconstmax = Param::GetFloat(Param::fslipconstmax);
                    // Basically, for every Hz above fweak we get a fraction of
                    // the difference between fslipconstmax and fslipmax
                    // of additional slip
                    fslipmax += (fstat - fweak) / (fconst - fweak) *
                                (fslipconstmax - fslipmax);
                    fslipmax = MIN(
                        fslipmax, fslipconstmax); // never exceed fslipconstmax!
                }

                float fslipdiff = fslipmax - fslipmin;
                fslipspnt =
                    roundingError + fslipmin +
                    (fslipdiff * (torque - slipstart)) / (100.0f - slipstart);
            }
            else
            {
                fslipspnt = fslipmin + roundingError;
            }
        }
        else
        {
            float brkrampstr = Param::GetFloat(Param::brkrampstr);
            float rotorFrq = FP_TOFLOAT(EncoderT::GetRotorFrequency());

            ampnomLocal = -torque;

            fslipspnt = -fslipmin;
            if (rotorFrq < brkrampstr)
            {
                ampnomLocal = rotorFrq / brkrampstr * ampnomLocal;
            }
        }

        ampnomLocal = MIN(ampnomLocal, 100.0f);
        // anticipate sudden changes by filtering
        ampnom =
            IIRFILTER(ampnom, FP_FROMFLT(ampnomLocal), filterConst);
        fslip =
            IIRFILTER(fslip, FP_FROMFLT(fslipspnt), filterConst);
        Param::Set(Param::ampnom, ampnom);
        Param::Set(Param::fslipspnt, fslip);

        slipIncr = FrqToAngle(fslip);
    }

private:
    enum EdgeType // Sine
    {
        NoEdge,
        PosEdge,
        NegEdge
    };

private:
    static const uint16_t SHIFT_180DEG = 32768;
    static const uint16_t SHIFT_90DEG = 16384;

protected:
    static void PwmInit()
    {
        pwmfrq = PwmDriverT::TimerSetup(
            Param::GetInt(Param::deadtime),
            Param::GetInt(Param::pwmpol),
            pwmdigits);
        slipIncr = FrqToAngle(fslip);
        EncoderT::SetPwmFrequency(pwmfrq);

        PwmDriverT::DriverInit();

        if (opmode == ACHEAT)
            PwmDriverT::AcHeatTimerSetup();
    }

private:
    static s32fp ProcessCurrents()
    {
        static s32fp    currentMax[2];
        static int16_t  samples[2] = { 0 };
        static int32_t  sign = 1;
        static EdgeType lastEdge[2] = { PosEdge, PosEdge };

        s32fp il1 = GetCurrent(
            CurrentT::Phase1(), ilofs[0], Param::Get(Param::il1gain));
        s32fp il2 = GetCurrent(
            CurrentT::Phase2(), ilofs[1], Param::Get(Param::il2gain));
        s32fp    rms;
        s32fp    il1PrevRms = Param::Get(Param::il1rms);
        s32fp    il2PrevRms = Param::Get(Param::il2rms);
        EdgeType edge = CalcRms(
            il1, lastEdge[0], currentMax[0], rms, samples[0], il1PrevRms);

        if (edge != NoEdge)
        {
            Param::SetFixed(Param::il1rms, rms);

            if (opmode != BOOST || opmode != BUCK)
            {
                // rough approximation as we do not take power factor into
                // account
                s32fp idc = (SineCore::GetAmp() * rms) / SineCore::MAXAMP;
                idc = FP_DIV(
                    idc, FP_FROMFLT(1.2247)); // divide by sqrt(3)/sqrt(2)
                idc *= fslip < 0 ? -1 : 1;
                Param::SetFixed(Param::idc, idc);
            }
        }
        if (CalcRms(
                il2, lastEdge[1], currentMax[1], rms, samples[1], il2PrevRms))
        {
            Param::SetFixed(Param::il2rms, rms);
        }

        s32fp ilMax = sign * GetIlMax(il1, il2);

        Param::SetFixed(Param::il1, il1);
        Param::SetFixed(Param::il2, il2);
        Param::SetFixed(Param::ilmax, ilMax);

        return ilMax;
    }

    static void CalcNextAngleAsync(int32_t dir)
    {
        static uint16_t slipAngle = 0;
        uint16_t        rotorAngle = EncoderT::GetRotorAngle();

        frq =
            polePairRatio * EncoderT::GetRotorFrequency() + fslip;
        slipAngle += dir * slipIncr;

        if (frq < 0)
            frq = 0;

        angle = polePairRatio * rotorAngle + slipAngle;
    }

    static void CalcNextAngleConstant(int32_t dir)
    {
        frq = fslip;
        angle += dir * slipIncr;

        if (frq < 0)
            frq = 0;
    }

    static s32fp GetIlMax(s32fp il1, s32fp il2)
    {
        s32fp il3 = -il1 - il2;
        s32fp offset = SineCore::CalcSVPWMOffset(il1, il2, il3) / 2;
        offset = ABS(offset);
        il1 = ABS(il1);
        il2 = ABS(il2);
        il3 = ABS(il3);
        s32fp ilMax = MAX(il1, il2);
        ilMax = MAX(ilMax, il3);
        ilMax -= offset;

        return ilMax;
    }

    static s32fp LimitCurrent()
    {
        static s32fp curLimSpntFiltered = FP_FROMINT(100), slipFiltered = 0;
        s32fp        slipmin = Param::Get(Param::fslipmin);
        s32fp        imax = Param::Get(Param::iacmax);
        s32fp        ilMax = ProcessCurrents();

        // setting of 0 disables current limiting
        if (imax == 0)
            return ampnom;

        s32fp a = imax / 20; // Start acting at 80% of imax
        s32fp imargin = imax - ilMax;
        s32fp curLimSpnt = FP_DIV(100 * imargin, a);
        s32fp slipSpnt = FP_DIV(FP_MUL(fslip, imargin), a);
        slipSpnt = MAX(slipmin, slipSpnt);
        curLimSpnt = MAX(FP_FROMINT(40), curLimSpnt); // Never go below 40%
        int32_t filter = Param::GetInt(
            curLimSpnt < curLimSpntFiltered ? Param::ifltfall :
                                              Param::ifltrise);
        curLimSpntFiltered = IIRFILTER(curLimSpntFiltered, curLimSpnt, filter);
        slipFiltered = IIRFILTER(slipFiltered, slipSpnt, 1);

        s32fp ampNomLimited = MIN(ampnom, curLimSpntFiltered);
        slipSpnt = MIN(fslip, slipFiltered);
        slipIncr = FrqToAngle(slipSpnt);

        if (curLimSpnt < ampnom)
            ErrorMessage::Post(ERR_CURRENTLIMIT);

        return ampNomLimited;
    }

    static EdgeType CalcRms(
        s32fp     il,
        EdgeType& lastEdge,
        s32fp&    max,
        s32fp&    rms,
        int16_t&  samples,
        s32fp     prevRms)
    {
        const s32fp oneOverSqrt2 = FP_FROMFLT(0.707106781187);
        int32_t     minSamples = pwmfrq / (4 * FP_TOINT(frq));
        EdgeType    edgeType = NoEdge;

        minSamples = MAX(10, minSamples);

        if (samples > minSamples)
        {
            if (lastEdge == NegEdge && il > 0)
                edgeType = PosEdge;
            else if (lastEdge == PosEdge && il < 0)
                edgeType = NegEdge;
        }

        if (edgeType != NoEdge)
        {
            rms = (FP_MUL(oneOverSqrt2, max) + prevRms) /
                  2; // average with previous rms reading

            max = 0;
            samples = 0;
            lastEdge = edgeType;
        }

        il = ABS(il);
        max = MAX(il, max);
        samples++;

        return edgeType;
    }

    static s32fp s_debugAmpNomLimited;
    static uint32_t s_debugAmp;
};

template <typename CurrentT, typename EncoderT, typename PwmDriverT>
s32fp SinePwmGeneration<CurrentT, EncoderT, PwmDriverT>::s_debugAmpNomLimited = 0;

template <typename CurrentT, typename EncoderT, typename PwmDriverT>
uint32_t SinePwmGeneration<CurrentT, EncoderT, PwmDriverT>::s_debugAmp = 0;

#endif // SINEPWMGENERATION_H
