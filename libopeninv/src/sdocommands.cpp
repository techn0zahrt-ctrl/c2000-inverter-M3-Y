/*
 * This file is part of the stm32-... project.
 *
 * Copyright (C) 2025 Johannes Huebner <dev@johanneshuebner.com>
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
#ifdef C2000
#include "c2000_flash.h"
#include "c2000_system.h"
#elif defined(STM32F1)
#include <libopencm3/stm32/desig.h>
#include <libopencm3/cm3/scb.h>
#else
#include "hwdefs.h"
#endif
#include "sdocommands.h"
#include "param_save.h"

//Some functions use the "register" keyword which C++ doesn't like
//We can safely ignore that as we don't even use those functions
#ifdef STM32F1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wregister"
#include <libopencm3/cm3/cortex.h>
#pragma GCC diagnostic pop
#endif

#define SDO_INDEX_SERIAL      0x5000
#define SDO_INDEX_STRINGS     0x5001
#define SDO_CMD_SAVE          0
#define SDO_CMD_LOAD          1
#define SDO_CMD_RESET         2
#define SDO_CMD_DEFAULTS      3

bool SdoCommands::saveEnabled = true;
CanMap* SdoCommands::canMap;

void SdoCommands::ProcessStandardCommands(CanSdo::SdoFrame* sdoFrame)
{
   if (sdoFrame->index == SDO_INDEX_SERIAL && sdoFrame->cmd == SDO_READ)
   {
      sdoFrame->cmd = SDO_READ_REPLY;
      switch (sdoFrame->subIndex)
      {
      case 0:
         sdoFrame->data = DESIG_UNIQUE_ID0;
         break;
      case 1:
         sdoFrame->data = DESIG_UNIQUE_ID1;
         break;
      case 2:
         sdoFrame->data = DESIG_UNIQUE_ID2;
         break;
      case 3:
         sdoFrame->data = Param::GetIdSum();
         break;
      default:
         sdoFrame->cmd = SDO_ABORT;
         sdoFrame->data = SDO_ERR_INVIDX;
      }
   }
   else if (sdoFrame->index == SDO_INDEX_COMMANDS && sdoFrame->cmd == SDO_WRITE)
   {
      sdoFrame->cmd = SDO_WRITE_REPLY;

      switch (sdoFrame->subIndex)
      {
      case SDO_CMD_SAVE:
         if (saveEnabled)
         {
            cm_disable_interrupts();
            if (0 != canMap) canMap->Save();
            parm_save();
            cm_enable_interrupts();
         }
         else
         {
            sdoFrame->cmd = SDO_ABORT;
            sdoFrame->data = SDO_ERR_GENERAL;
         }
         break;
      case SDO_CMD_LOAD:
         //We disable interrupts to prevent concurrent access of the CRC unit
         cm_disable_interrupts();
         parm_load();
         cm_enable_interrupts();
         Param::Change(Param::PARAM_LAST);
         break;
      case SDO_CMD_RESET:
#ifdef C2000
         // On C2000 the firmware runs from RAM, so a hardware watchdog reset
         // would just halt. SoftReset() disables interrupts and jumps to the
         // C runtime entry point (_c_int00), re-running full initialisation.
         SoftReset();
#else
         scb_reset_system();
#endif
         break;
      case SDO_CMD_DEFAULTS:
         Param::LoadDefaults();
         Param::Change(Param::PARAM_LAST);
         break;
      default:
         sdoFrame->cmd = SDO_ABORT;
         sdoFrame->data = SDO_ERR_INVIDX;
      }
   }
   else
   {
      sdoFrame->cmd = SDO_ABORT;
      sdoFrame->data = SDO_ERR_INVIDX;
   }
}
