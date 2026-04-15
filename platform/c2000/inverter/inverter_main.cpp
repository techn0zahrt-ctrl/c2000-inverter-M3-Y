/*
 * This file is part of the stm32-sine project.
 *
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

#include "device.h"
#include "driverlib.h"
#include "errormessage.h"
#if CONTROL == CTRL_FOC
#include "focpwmgeneration.h"
#endif
#include "pmicdriver.h"
#include "c2000/current.h"
#include "c2000/encoder.h"
#include "c2000/eeprom.h"
#include "c2000/gatedriver.h"
#include "c2000/motoranalogcapture.h"
#include "c2000/performancecounter.h"
#include "c2000/pmicspidriver.h"
#include "c2000/pwmdriver.h"
#include "c2000/pwmgeneration.h"
#include "c2000/scheduler.h"
#include "c2000_can.h"
#include "canmap.h"
#include "cansdo.h"
#include "sdocommands.h"
#include "param_save.h"
#include "json_print.h"
#include "fu.h"
#include "sine_core.h"
#include "temp_meas.h"
#include "throttle.h"

extern char* ftoa(char* buf, float val, int decimals);
//#include <inttypes.h>

#define PRINTF(...) do { DINT; printf(__VA_ARGS__); EINT; } while(0)

// Pull in the whole C2000 namespace as this is platform specific code obviously
using namespace c2000;

static C2000Can* can __attribute__((unused));
static CanMap* canMap __attribute__((unused));
static CanSdo* canSdo __attribute__((unused));

void Param::Change(Param::PARAM_NUM paramNum)
{
    switch (paramNum)
    {
#if CONTROL == CTRL_SINE
        case Param::ampnom:
            PwmGeneration::SetAmpnom(Param::Get(Param::ampnom));
            break;
        case Param::fslipspnt:
            PwmGeneration::SetFslip(Param::Get(Param::fslipspnt));
            break;
#endif // CTRL_SINE

        case Param::canspeed:
            if (can)
                can->SetBaudrate(
                    (CanHardware::baudrates)Param::GetInt(Param::canspeed));
            break;

        case Param::nodeid:
            if (canSdo)
                canSdo->SetNodeId(Param::GetInt(Param::nodeid));
            break;

        // Throttle limit params that may be set frequently via CAN
        case Param::throtmax:
        case Param::throtmin:
        case Param::idcmin:
        case Param::idcmax:
            Throttle::throtmax = Param::GetFloat(Param::throtmax);
            Throttle::throtmin = Param::GetFloat(Param::throtmin);
            Throttle::idcmin   = Param::GetFloat(Param::idcmin);
            Throttle::idcmax   = Param::GetFloat(Param::idcmax);
            break;

        default:
            // Current limit and pole pair ratio
            PwmGeneration::SetCurrentLimitThreshold(Param::Get(Param::ocurlim));
            PwmGeneration::SetPolePairRatio(
                Param::GetInt(Param::polepairs) /
                Param::GetInt(Param::respolepairs));

#if CONTROL == CTRL_FOC
            // FOC controller gains
            PwmGeneration::SetControllerGains(
                Param::GetInt(Param::curkp),
                Param::GetInt(Param::curki),
                Param::GetInt(Param::fwkp));
#endif // CTRL_FOC

            // Motor voltage (SINE only; FOC ignores these)
            MotorVoltage::SetBoost(Param::GetInt(Param::boost));
            MotorVoltage::SetWeakeningFrq(Param::GetFloat(Param::fweakstrt));

            // Throttle pot calibration
            Throttle::potmin[0] = Param::GetInt(Param::potmin);
            Throttle::potmax[0] = Param::GetInt(Param::potmax);
            Throttle::potmin[1] = Param::GetInt(Param::pot2min);
            Throttle::potmax[1] = Param::GetInt(Param::pot2max);

            // Regen / braking
            Throttle::brknom      = Param::GetFloat(Param::brknom);
            Throttle::brknompedal = Param::GetFloat(Param::brknompedal);
            Throttle::brkmax      = Param::GetFloat(Param::brkmax);
            Throttle::brkcruise   = Param::GetFloat(Param::brkcruise);
            Throttle::regenRamp   = Param::GetFloat(Param::regenramp);

            // Throttle limits and ramp
            Throttle::throtmax    = Param::GetFloat(Param::throtmax);
            Throttle::throtmin    = Param::GetFloat(Param::throtmin);
            Throttle::throttleRamp = Param::GetFloat(Param::throtramp);

            // Speed / idle control
            Throttle::idleSpeed    = Param::GetInt(Param::idlespeed);
            Throttle::speedkp      = Param::GetFloat(Param::speedkp);
            Throttle::speedflt     = Param::GetInt(Param::speedflt);
            Throttle::idleThrotLim = Param::GetFloat(Param::idlethrotlim);

            // BMS torque limiting
            Throttle::bmslimhigh = Param::GetInt(Param::bmslimhigh);
            Throttle::bmslimlow  = Param::GetInt(Param::bmslimlow);

            // Voltage and current derating
            Throttle::udcmin = Param::GetFloat(Param::udcmin) * 0.99f;
            Throttle::udcmax = Param::GetFloat(Param::udcmax) * 1.01f;
            Throttle::idcmin = Param::GetFloat(Param::idcmin);
            Throttle::idcmax = Param::GetFloat(Param::idcmax);
            Throttle::idckp  = Param::GetFloat(Param::idckp);
            Throttle::fmax   = Param::GetFloat(Param::fmax);
            break;
    }
}

typedef TeslaM3PowerWatchdog<PmicSpiDriver> PowerWatchdog;

// task added to scheduler to strobe the powerwatchdog
static void taskStrobePowerWatchdog()
{
    PowerWatchdog::Strobe();
}

// task called every 100ms to update the resolver frequency estimate (10 Hz)
static void taskUpdateRotorFrequency()
{
    Encoder::UpdateRotorFrequency(10);
}

static void onPrintRequest(CanSdo* sdo, int request)
{
    if (request == 0)
        PrintParamsJson(sdo, canMap);
}

void main(void)
{
    //
    // Initialize device clock and peripherals
    //
    Device_init();
    //
    // Initialize GPIO and configure the GPIO pin as a push-pull output
    //
    Device_initGPIO();

    // Set up GPIO pinmux for EPWM
    GPIO_setPinConfig(GPIO_0_EPWM1A);
    GPIO_setPinConfig(GPIO_8_EPWM5A);
    GPIO_setPinConfig(GPIO_9_EPWM5B);
    GPIO_setPinConfig(GPIO_10_EPWM6A);
    GPIO_setPinConfig(GPIO_11_EPWM6B);
    GPIO_setPinConfig(GPIO_12_EPWM7A);
    GPIO_setPinConfig(GPIO_13_EPWM7B);

    //
    // Set up the heartbeat LED
    //
    uint32_t heartbeatLedPin;

    if (IsTeslaM3Inverter())
    {
        heartbeatLedPin = DEVICE_TESLAM3_GPIO_PIN_LED1;
    }
    else
    {
        heartbeatLedPin = DEVICE_LAUNCHXL_GPIO_PIN_LED1;
    }
    GPIO_setPadConfig(heartbeatLedPin, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(heartbeatLedPin, GPIO_DIR_MODE_OUT);
    GPIO_writePin(heartbeatLedPin, 1);

    // Set up second LED (red on Tesla)
    uint32_t heartbeatLedPin2;
    if (IsTeslaM3Inverter())
    {
        heartbeatLedPin2 = DEVICE_TESLAM3_GPIO_PIN_LED2;
    }
    else
    {
        heartbeatLedPin2 = DEVICE_LAUNCHXL_GPIO_PIN_LED2;
    }
    GPIO_setPadConfig(heartbeatLedPin2, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(heartbeatLedPin2, GPIO_DIR_MODE_OUT);
    GPIO_writePin(heartbeatLedPin2, 0);

    //
    // Initialize PIE and clear PIE registers. Disables CPU interrupts.
    //
    Interrupt_initModule();

    //
    // Initialize the PIE vector table with pointers to the shell Interrupt
    // Service Routines (ISR).
    //
    Interrupt_initVectorTable();
    //

    //*/
    // Turn on the gate drive PSU
    //
    GPIO_writePin(DEVICE_GPIO_PIN_GATE_PSU_ENABLE, 0);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_GATE_PSU_ENABLE, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_GATE_PSU_ENABLE, GPIO_DIR_MODE_OUT);
    printf("Gate Drive PSU ON\n");

    // Configure CD4051 temperature mux select lines (GPIO30/31/32)
    MotorAnalogCapture::InitTempMuxGpio();

    DEVICE_DELAY_US(50000);

    Scheduler::Init();
    printf("Pmic driver initialisation: %s\n",
        PowerWatchdog::Init() == PowerWatchdog::OK ? "OK" : "Fail");

    // add a task to strobe the power watchdog every 100ms
    Scheduler::AddTask(taskStrobePowerWatchdog, 100);

    // add a task to update resolver frequency estimate every 100ms (10 Hz)
    Scheduler::AddTask(taskUpdateRotorFrequency, 100);

    //
    // Set up the gate drivers for PWM operation
    //
    printf("Gate Drive initialisation: ");
    if (GateDriver::Init())
    {
        printf("OK\n");
        GateDriver::Enable();
    }
    else
    {
        printf("Fail\n");
    }

    // Set up the error message log and set operating parameters to default
    ErrorMessage::ResetAll();
    // TODO: Figure out where the timer tick comes from to increment this
    ErrorMessage::SetTime(1);
    Param::LoadDefaults();

    // Initialize EEPROM
    EEPROM::InitSPI();
    // Load CAN map from EEPROM if valid
    int loadResult = parm_load();

    // Configure the PWM generation
    PwmGeneration::SetCurrentOffset(2048, 2048);

    // We need the pole pair ratio set to correctly calculate the rotation
    // frequency
    PwmGeneration::SetPolePairRatio(1);

    // Ensure the system thinks we should be going forwards
    Param::SetInt(Param::dir, 1);


#if CONTROL == CTRL_FOC
    // initialise the controller gains from the default parameters
    PwmGeneration::SetControllerGains(
        Param::GetInt(Param::curkp),
        Param::GetInt(Param::curki),
        Param::GetInt(Param::fwkp));

    // Put in a bit of Q current to get the inverter to do something
    Param::Set(Param::manualiq, FP_FROMFLT(0.6));
#endif

//*/
    // Initialize CAN at 500kbps
    can = new C2000Can(CANA_BASE);
    can->SetBaudrate(CanHardware::Baud500);

    // Reset resolver encoder: sets startup delay (4000 PWM cycles = 400ms at
    // 10kHz) so ERR_LORESAMP is suppressed while the exciter signal ramps up
    Encoder::Reset();

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    // Let PWM ISR fire a few times so ADC readings are valid
    DEVICE_DELAY_US(1000);
    // Go for manual mode
    MotorVoltage::SetMaxAmp(SineCore::MAXAMP);
    MotorVoltage::SetBoost(Param::GetInt(Param::boost));
    MotorVoltage::SetWeakeningFrq(Param::GetFloat(Param::fweakstrt));
    PwmGeneration::SetOpmode(MANUAL);
    Param::SetEnum(Param::opmode, MANUAL);

    // Wait for PWM ISR to fire at least once so ADC readings are valid
    while (PwmGeneration::GetCpuLoad() == 0)
        DEVICE_DELAY_US(100);

    uint16_t phaseA = MotorAnalogCapture::PhaseACurrent();
    uint16_t phaseB = MotorAnalogCapture::PhaseBCurrent();
    PwmGeneration::SetCurrentOffset(phaseA, phaseB);
    PRINTF("Current offsets: PhaseA=%d PhaseB=%d\n", phaseA, phaseB);

    canMap = new CanMap(can);
    canSdo = new CanSdo(can, canMap);
    canSdo->SetPrintCallback(onPrintRequest);

    //
    // Turn on the global PWM buffer enable
    //
    GPIO_writePin(DEVICE_GPIO_PIN_PWM_ENABLE, 1);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_PWM_ENABLE, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_PWM_ENABLE, GPIO_DIR_MODE_OUT);

    //
    // Loop Forever
    //
    int blinkState = 0;
    int32_t lastLoad = PwmGeneration::GetCpuLoad();
    static int loopCount = 0;
    while (true)
    {
        canSdo->TriggerTimeout(10);

        if (canSdo->GetPrintRequest() >= 0)
        {
            loopCount = 0;
            PrintParamsJson(canSdo, canMap);
        }
        CanSdo::SdoFrame* sdoFrame = canSdo->GetPendingUserspaceSdo();
        if (sdoFrame != 0)
        {
            SdoCommands::ProcessStandardCommands(sdoFrame);
            canSdo->SendSdoReply(sdoFrame);
        }

        canMap->SendAll();

        // Feed params into PWM generation for manual control
        PwmGeneration::SetAmpnom(Param::Get(Param::ampnom));
        PwmGeneration::SetFslip(Param::Get(Param::fslipspnt));

        DEVICE_DELAY_US(5000);

        loopCount++;
        if (loopCount >= 100)
        {
            loopCount = 0;
            int32_t currentLoad = PwmGeneration::GetCpuLoad();

            // Gate driver health
            PRINTF("Gate Drive: %s\n", GateDriver::IsFaulty() ? "FAULT" : "OK");
            uint16_t gd_status1[6], gd_status2[6], gd_status3[6];
            GateDriver::GetStatus(gd_status1, gd_status2, gd_status3);
            PRINTF("GD0: S1=0x%x S2=0x%x S3=0x%x\n", gd_status1[0], gd_status2[0], gd_status3[0]);
            PRINTF("GD1: S1=0x%x S2=0x%x S3=0x%x\n", gd_status1[1], gd_status2[1], gd_status3[1]);
            PRINTF("GD2: S1=0x%x S2=0x%x S3=0x%x\n", gd_status1[2], gd_status2[2], gd_status3[2]);
            PRINTF("PWM cycles: %d\n", currentLoad - lastLoad);

            // DC link voltage (also updated in ISR; refresh boost/weakening here)
            MotorVoltage::SetBoost(Param::GetInt(Param::boost));
            MotorVoltage::SetWeakeningFrq(Param::GetFloat(Param::fweakstrt));
            char udcStr[16];
            PRINTF("UDC: %s V\n", ftoa(udcStr, Param::GetFloat(Param::udc), 1));

            // Phase currents
            PRINTF("Il1: %d A  Il2: %d A\n",
                (int16_t)Param::Get(Param::il1),
                (int16_t)Param::Get(Param::il2));

            // Resolver — raw, offset-corrected, and calculated angle
            {
                int16_t sinRaw = (int16_t)MotorAnalogCapture::ResolverSine();
                int16_t cosRaw = (int16_t)MotorAnalogCapture::ResolverCosine();
                int16_t offset = (int16_t)Param::GetInt(Param::sincosofs);
                int16_t sinC   = sinRaw - offset;
                int16_t cosC   = cosRaw - offset;
                char angleStr[16];
                PRINTF("Resolver: raw(sin=%d cos=%d) centered(sin=%d cos=%d) angle=%s deg\n",
                    (int)sinRaw, (int)cosRaw,
                    (int)sinC,   (int)cosC,
                    ftoa(angleStr, Param::GetFloat(Param::angle), 1));
            }

            // HVIL current
            char hvilStr[16];
            PRINTF("HVIL: %s mA\n",
                ftoa(hvilStr, Param::GetFloat(Param::hvilcur), 1));

            // Temperature mux — all 6 channels, update tmphs/tmpm params
            {
                float tmphsMax = -100.0f;
                float tmpm = 0.0f;
                char tempStr[16];
                for (uint8_t ch = 0; ch < 6; ch++)
                {
                    MotorAnalogCapture::SetTempMuxChannel(ch);
                    DEVICE_DELAY_US(200);
                    uint16_t raw = MotorAnalogCapture::TempMux();
                    float temp;
                    if (ch == 5)
                    {
                        temp = TempMeas::Lookup(raw, TempMeas::TEMP_TESLA_100K);
                        tmpm = temp;
                    }
                    else
                    {
                        temp = TempMeas::Lookup(raw, TempMeas::TEMP_TESLA_52K);
                        if (ch >= 2 && temp > tmphsMax)
                            tmphsMax = temp;
                    }
                    PRINTF("Temp ch%d: raw=%d %s C\n",
                        (int)ch, (int)raw, ftoa(tempStr, temp, 1));
                }
                if (tmphsMax > -100.0f)
                    Param::SetFloat(Param::tmphs, tmphsMax);
                Param::SetFloat(Param::tmpm, tmpm);
                char tmphsStr[16], tmpmStr[16];
                PRINTF("tmphs=%s C  tmpm=%s C\n",
                    ftoa(tmphsStr, tmphsMax, 1), ftoa(tmpmStr, tmpm, 1));
            }

            lastLoad = currentLoad;
        }
        if (loopCount % 25 == 0)
        {
            switch (blinkState)
            {
                case 0: GPIO_writePin(heartbeatLedPin, 0);  GPIO_writePin(heartbeatLedPin2, 1); break;
                case 1: GPIO_writePin(heartbeatLedPin, 1);  GPIO_writePin(heartbeatLedPin2, 1); break;
                case 2: GPIO_writePin(heartbeatLedPin, 0);  GPIO_writePin(heartbeatLedPin2, 1); break;
                case 3: GPIO_writePin(heartbeatLedPin, 1);  GPIO_writePin(heartbeatLedPin2, 1); break;
                case 4: GPIO_writePin(heartbeatLedPin, 1);  GPIO_writePin(heartbeatLedPin2, 0); break;
                case 5: GPIO_writePin(heartbeatLedPin, 1);  GPIO_writePin(heartbeatLedPin2, 1); break;
                case 6: GPIO_writePin(heartbeatLedPin, 1);  GPIO_writePin(heartbeatLedPin2, 0); break;
                case 7: GPIO_writePin(heartbeatLedPin, 1);  GPIO_writePin(heartbeatLedPin2, 1); break;
                default: blinkState = -1; break;
            }
            blinkState = (blinkState + 1) % 8;
        }
    }
}
