/*
 * This file is part of the stm32-sine project.
 *
 * Copyright (C) 2018 Johannes Huebner <dev@johanneshuebner.com>
 * Copyright (C) 2025 Damien Maguire <info@evbmw.com>
 * Copyright (C) 2025 David J. Fiddes <D.J@fiddes>
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
 *
 * Controls the 12V electric oil pump in the Tesla Model 3 Drive unit and reads
 * back information.
 */

#include "teslam3oilpump.h"
#include "errormessage.h"
#include "my_math.h"
#include "params.h"
#ifdef STM32F1
#include "digio.h"
#include <libopencm3/stm32/usart.h>
#endif

// LIN protocol PID definitions

static const uint8_t SpeedRequestPID = 0x0A;
static const uint8_t SpeedRequestLen = 2;

static const uint8_t FlowPressureTempStatusPID = 0x2A;
static const uint8_t FlowPressureTempStatusLen = 8;

static const uint8_t VoltageSpeedStatusPID = 0x30;
static const uint8_t VoltageSpeedStatusLen = 8;

//! \brief Aim for a 100ms loop duration
static const uint8_t MaxLoopTicks = 10;

//! \brief Maximum time we are prepared to wait for a valid response from the
//! pump in 10ms ticks
static const uint16_t StatusTimeout = 500;

//! \brief Lowest permitted oil temperature
static const int MinimumOilTemp = -30;

/**
 * \brief Initialise the oil pump controller
 */
TeslaM3OilPump::TeslaM3OilPump()
: lin(0), tickCount(0), ticksSinceLastResponse(0), oilTempController()
{
}

/**
 * \brief Configure the LIN interface
 *
 * On STM32F1: initialises UART4 at 19200 baud and enables the LIN transceiver
 * via DigIo.  On C2000: SCIA is already configured by the C2000Lin constructor
 * so Init() is a no-op; LIN transceiver enable is handled separately.
 */
void TeslaM3OilPump::SetLinInterface(LinBus* l)
{
   lin = l;

#ifdef STM32F1
   // Initialise the LIN UART with the STM32 UART4 peripheral
   lin->Init(UART4, 19200);

   // Turn on the LIN transceiver
   DigIo::lin_wake.Clear();
   DigIo::lin_nslp.Set();
#endif
}

/**
 * \brief Run the LIN state machine to send commands to the pump and retrieve
 * its status
 */
void TeslaM3OilPump::Ms10Task()
{
   ProcessStatusResponse();

   // Statically schedule requests. A 10ms tick is sufficient for a max length
   // LIN request to be processed
   switch (tickCount)
   {
   case 0:
      SendSpeedRequest();
      break;

   case 1:
      lin->Request(FlowPressureTempStatusPID, 0, 0);
      break;

   case 2:
      lin->Request(VoltageSpeedStatusPID, 0, 0);
      break;

   default:
      break;
   }

   tickCount++;
   if (tickCount >= MaxLoopTicks)
      tickCount = 0;

   CheckForFaults();
}

/**
 * \brief Send the currently configured static pump speed to the pump
 */
void TeslaM3OilPump::SendSpeedRequest()
{
   uint8_t lindata[SpeedRequestLen];
   lindata[0] = 0xFF;
   lindata[1] = oilTempController.PumpSpeed();
   lin->Request(SpeedRequestPID, lindata, sizeof(lindata));
}

/**
 * \brief Process status responses from the pump updating spot values as
 * required
 */
void TeslaM3OilPump::ProcessStatusResponse()
{
   if (lin->HasReceived(FlowPressureTempStatusPID, FlowPressureTempStatusLen))
   {
      uint8_t* data = lin->GetReceivedBytes();

      // Motor oil temperature
      // We limit to a minimum value to avoid underflowing the common
      // OI CAN map temp offset
      int tmpoil = MAX(MinimumOilTemp, (int16_t)data[3] - 40);
      Param::SetInt(Param::tmpoil, tmpoil);

      // Motor oil pressure in psi
      Param::SetFloat(Param::oilpres, (data[2] * 2) * 0.14503f);

      ticksSinceLastResponse = 0;
   }
   else if (lin->HasReceived(VoltageSpeedStatusPID, VoltageSpeedStatusLen))
   {
      uint8_t* data = lin->GetReceivedBytes();

      Param::SetFloat(Param::upmp, data[0] * 0.1f); // Oil pump 12V supply Voltage.
      Param::SetInt(Param::pmprev, (data[5] << 8) | (data[4])); // Oil pump RPM
      ticksSinceLastResponse = 0;
   }
}

/**
 * \brief Check to see if we are receiving timely status responses
 */
void TeslaM3OilPump::CheckForFaults()
{
   if (ticksSinceLastResponse < StatusTimeout)
   {
      ticksSinceLastResponse++;
   }
   else if (ticksSinceLastResponse == StatusTimeout)
   {
      // Advance beyond the timeout to avoid posting the error repeatedly
      ticksSinceLastResponse++;

      ErrorMessage::Post(ERR_OILPUMPFAULT);

      // Set default values to indicate a fault condition
      Param::SetInt(Param::tmpoil, MinimumOilTemp);
      Param::SetInt(Param::oilpres, 0);
      Param::SetInt(Param::upmp, 0);
      Param::SetInt(Param::pmprev, 0);
   }
}

/**
 * \brief Update the oil temperature controller state at a more leisurely
 * rate
 */
void TeslaM3OilPump::Ms100Task()
{
   oilTempController.Ms100Task();
}
