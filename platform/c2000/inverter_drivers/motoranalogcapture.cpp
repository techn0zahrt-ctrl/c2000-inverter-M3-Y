/*
 * This file is part of the stm32-sine project.
 *
 * Copyright (C) 2022 David J. Fiddes <D.J@fiddes.net>
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
#include "device.h"
#include "driverlib.h"
#include "c2000/motoranalogcapture.h"

namespace c2000 {

/**
 * \brief Initialise the synchronous analog capture
 */
void MotorAnalogCapture::Init()
{
    // Initialise all ADC channels identically
    InitAdcChannel(ADCA_BASE);
    InitAdcChannel(ADCB_BASE);
    InitAdcChannel(ADCD_BASE);

    // Wait 1ms until all channels have powered up
    DEVICE_DELAY_US(1000);
}

/**
 * \brief Configure a single ADC channel
 */
void MotorAnalogCapture::InitAdcChannel(uint32_t base)
{
    //
    // This should give us a 50MHz ADC clock
    //
    ADC_setPrescaler(base, ADC_CLK_DIV_4_0);
    ADC_setMode(base, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);

    //
    // Set pulse positions to late
    //
    ADC_setInterruptPulseMode(base, ADC_PULSE_END_OF_CONV);

    //
    // Power up the ADC. We'll wait once all ADC are configured
    //
    ADC_enableConverter(base);
}

/**
 * \brief Configure the synchronised capture of the motor analog signals
 */
void MotorAnalogCapture::ConfigureSoc(ADC_Trigger trigger)
{
    // We use 15 cycles for the conversion window which equates to 75ns at
    // 200MHz SYSCLK
    const uint32_t sampleWindow = 15;

    // Configure the SOC0 for each of the channels

    // ADC-A SOC0 Input 4 - PHASE A CURRENT SENSOR
    ADC_setupSOC(
        ADCA_BASE, ADC_SOC_NUMBER0, trigger, ADC_CH_ADCIN4, sampleWindow);

    // ADC-A SOC1 Input 0 - RESOLVER SINE
    ADC_setupSOC(
        ADCA_BASE, ADC_SOC_NUMBER1, trigger, ADC_CH_ADCIN0, sampleWindow);

    // ADC-B SOC0 Input 1 - RESOLVER COSINE
    ADC_setupSOC(
        ADCB_BASE, ADC_SOC_NUMBER0, trigger, ADC_CH_ADCIN1, sampleWindow);

    // ADC-B SOC1 Input 2 - DC LINK VOLTAGE
    ADC_setupSOC(
        ADCB_BASE, ADC_SOC_NUMBER1, trigger, ADC_CH_ADCIN2, sampleWindow);

    // ADC-D SOC0 Input 2 - PHASE B CURRENT SENSOR
    ADC_setupSOC(
        ADCD_BASE, ADC_SOC_NUMBER0, trigger, ADC_CH_ADCIN2, sampleWindow);

    // ADC-A SOC2 Input 5 - HVIL CURRENT SENSE
    ADC_setupSOC(
        ADCA_BASE, ADC_SOC_NUMBER2, trigger, ADC_CH_ADCIN5, sampleWindow);

    //
    // Configure the ADC conversion complete interrupt for motor signals
    //
    ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
    ADC_enableInterrupt(ADCA_BASE, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
}

/**
 * \brief Return phase current reading
 */
uint16_t MotorAnalogCapture::PhaseACurrent()
{
    return ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);
}

/**
 * \brief Return phase B current reading
 */
uint16_t MotorAnalogCapture::PhaseBCurrent()
{
    return ADC_readResult(ADCDRESULT_BASE, ADC_SOC_NUMBER0);
}

/**
 * \brief Return resolver sine signal reading
 */
uint16_t MotorAnalogCapture::ResolverSine()
{
    return ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER1);
}

/**
 * \brief Return resolver cosine signal reading
 */
uint16_t MotorAnalogCapture::ResolverCosine()
{
    return ADC_readResult(ADCBRESULT_BASE, ADC_SOC_NUMBER0);
}

/**
 * \brief Return DC link voltage reading
 */
uint16_t MotorAnalogCapture::UdcVoltage()
{
    return ADC_readResult(ADCBRESULT_BASE, ADC_SOC_NUMBER1);
}

/**
 * \brief Return HVIL current sense reading (ADCINA5)
 * 1 count = 0.1875 mA (3.3V ref / 4095 counts / 4.3 mV/mA)
 */
uint16_t MotorAnalogCapture::HvilCurrent()
{
    return ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER2);
}

} // namespace c2000
