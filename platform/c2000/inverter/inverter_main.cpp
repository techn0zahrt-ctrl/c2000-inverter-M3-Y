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
#include "focpwmgeneration.h"
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
//#include <inttypes.h>

#define PRINTF(...) do { DINT; printf(__VA_ARGS__); EINT; } while(0)

// Pull in the whole C2000 namespace as this is platform specific code obviously
using namespace c2000;

static C2000Can* can __attribute__((unused));
static CanMap* canMap __attribute__((unused));
static CanSdo* canSdo __attribute__((unused));

volatile uint32_t testdata = 0;

void Param::Change(Param::PARAM_NUM paramNum)
{
    (void)paramNum;
}

typedef TeslaM3PowerWatchdog<PmicSpiDriver> PowerWatchdog;

// task added to scheduler to strobe the powerwatchdog
static void taskStrobePowerWatchdog()
{
    PowerWatchdog::Strobe();
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

    //*/
    // Turn on the gate drive PSU
    //
    GPIO_writePin(DEVICE_GPIO_PIN_GATE_PSU_ENABLE, 0);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_GATE_PSU_ENABLE, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_GATE_PSU_ENABLE, GPIO_DIR_MODE_OUT);
    PRINTF("Gate Drive PSU ON\n");

    //
    // Set up the gate drivers for PWM operation
    //
    PRINTF("Gate Drive initialisation: ");
    if (GateDriver::Init())
    {
        PRINTF("OK\n");
        GateDriver::Enable();
    }
    else
    {
        PRINTF("Fail\n");
    }

    EEPROM::InitSPI();
    // Load CAN map from EEPROM if valid
    //parm_load();
    int loadResult = parm_load();

    Scheduler::Init();
    PRINTF("Pmic driver initialisation: %s\n",
        PowerWatchdog::Init() == PowerWatchdog::OK ? "OK" : "Fail");

    // add a task to strobe the power watchdog every 100ms
    Scheduler::AddTask(taskStrobePowerWatchdog, 100);
    // Set up the error message log and set operating parameters to default
    ErrorMessage::ResetAll();
    // TODO: Figure out where the timer tick comes from to increment this
    ErrorMessage::SetTime(1);
    Param::LoadDefaults();

    // Configure the PWM generation
    PwmGeneration::SetCurrentOffset(2048, 2048);

    // We need the pole pair ratio set to correctly calculate the rotation
    // frequency
    PwmGeneration::SetPolePairRatio(1);

    // Ensure the system thinks we should be going forwards
    Param::SetInt(Param::dir, 1);

    // initialise the controller gains from the default parameters
    PwmGeneration::SetControllerGains(
        Param::GetInt(Param::curkp),
        Param::GetInt(Param::curki),
        Param::GetInt(Param::fwkp));

    // Override the default deadtime as the C2000 uses values in nS rather
    // than a coded STM32 value
    Param::SetInt(Param::deadtime, 875);

    // Put in a bit of Q current to get the inverter to do something
    Param::Set(Param::manualiq, FP_FROMFLT(0.6));

    // Go for manual mode
    PwmGeneration::SetOpmode(MANUAL);
//*/
    // Initialize CAN at 500kbps
    can = new C2000Can(CANA_BASE);
    can->SetBaudrate(CanHardware::Baud500);
    canMap = new CanMap(can);
    canSdo = new CanSdo(can, canMap);
    canSdo->SetPrintCallback(onPrintRequest);

    EEPROM::InitSPI();
    // Load CAN map from EEPROM if valid
    parm_load();

    //
    // Enable Global Interrupt (INTM) and realtime interrupt (DBGM)
    //
    EINT;
    ERTM;

    //
    // Turn on the global PWM buffer enable
    //
    GPIO_writePin(DEVICE_GPIO_PIN_PWM_ENABLE, 1);
    GPIO_setPadConfig(DEVICE_GPIO_PIN_PWM_ENABLE, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(DEVICE_GPIO_PIN_PWM_ENABLE, GPIO_DIR_MODE_OUT);

    EALLOW;
    uint32_t gpamux1 = HWREG(0x7C00U + 0x6U);
    uint32_t gpadir = HWREG(0x7C00U + 0xCU);
    uint32_t gpagmux1 = HWREG(0x7C00U + 0x20U); // GPAGMUX1
    EDIS;
    PRINTF("GPAMUX1=0x%x GPADIR=0x%x\n", (uint16_t)gpamux1, (uint16_t)gpadir);
    PRINTF("GPAGMUX1=0x%x\n", (uint16_t)gpagmux1);

    //
    // Loop Forever
    //
    int blinkState = 0;
    int32_t lastLoad = PwmGeneration::GetCpuLoad();
    static int loopCount = 0;
    //static int printCount = 0;
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

        DEVICE_DELAY_US(5000);

        loopCount++;
        if (loopCount >= 100)
        {
            loopCount = 0;
            // Test CAN frame - remove after CAN confirmed working
            //uint32_t testData[2] = { 0x12345678, 0xDEADBEEF };
            //can->Send(0x123, testData, 8);
            PRINTF("Test data: %d, 0x%x\n", (uint16_t)testdata, (uint16_t)testdata);

            PRINTF("PhaseA Current = %d, PhaseB Current = %d, Resolver Sine = %u, Resolver Cosine = %u\n",
                Param::Get(Param::il1),
                Param::Get(Param::il2),
                MotorAnalogCapture::ResolverSine(),
                MotorAnalogCapture::ResolverCosine());

            PRINTF("Gate Drive: %s\n", GateDriver::IsFaulty() ? "FAULT" : "OK");
            uint16_t gd_status1[6], gd_status2[6], gd_status3[6];
            GateDriver::GetStatus(gd_status1, gd_status2, gd_status3);
            PRINTF("GD0: S1=0x%x S2=0x%x S3=0x%x\n", gd_status1[0], gd_status2[0], gd_status3[0]);
            PRINTF("GD1: S1=0x%x S2=0x%x S3=0x%x\n", gd_status1[1], gd_status2[1], gd_status3[1]);
            PRINTF("GD2: S1=0x%x S2=0x%x S3=0x%x\n", gd_status1[2], gd_status2[2], gd_status3[2]);
            int32_t currentLoad = PwmGeneration::GetCpuLoad();
            PRINTF("PWM cycles: %d\n", currentLoad - lastLoad);
            PRINTF("polepairs raw = %d\r\n", (uint16_t)Param::Get(Param::polepairs));
            PRINTF("canspeed raw = %d\r\n", (uint16_t)Param::Get(Param::canspeed));
            PRINTF("nodeid raw = %d\r\n", (uint16_t)Param::Get(Param::nodeid));
            float myFloat = 123.456f;
            // Ensure "full" printf support is enabled in project properties
            PRINTF("The value is: %f\n", (float)myFloat);
            //PRINTF("data[1] hi=0x%x lo=0x%x\n", 
            //    (uint16_t)(canLastStatus >> 16),
            //    (uint16_t)canLastStatus);
            lastLoad = currentLoad;
            // Blink pattern: 2x green, 2x red, Repeat
            // States 0,1 = green on/off, States 2,3 = green on/off,
            // States 4,5 = red on/off, States 6,7 = red on/off
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
