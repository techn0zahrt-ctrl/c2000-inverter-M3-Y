/*
 * This file is part of the c2000-inverter project.
 *
 * Copyright (C) 2025 openinverter.org
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
#include "c2000_can.h"
#include "device.h"
#include "driverlib.h"
#include "inc/hw_ints.h"

// TX uses mailbox 32; RX mailboxes are 1..31
#define TX_MSG_OBJ_ID  32U
// Wildcard "receive-everything" mailbox occupies slot 31
#define RX_CATCH_ALL_OBJ_ID  31U

C2000Can* C2000Can::instanceA = 0;
C2000Can* C2000Can::instanceB = 0;

static const uint32_t baudRateTable[] =
{
    125000U,   // Baud125
    250000U,   // Baud250
    500000U,   // Baud500
    800000U,   // Baud800
    1000000U,  // Baud1000
};

C2000Can::C2000Can(uint32_t canBase)
   : base(canBase)
{
   // Configure GPIO pins for the selected CAN peripheral
   if (canBase == CANA_BASE)
   {
      GPIO_setPinConfig(DEVICE_GPIO_CFG_CANRXA);
      GPIO_setPinConfig(DEVICE_GPIO_CFG_CANTXA);
      instanceA = this;
   }
   else // CANB_BASE
   {
      GPIO_setPinConfig(DEVICE_GPIO_CFG_CANRXB);
      GPIO_setPinConfig(DEVICE_GPIO_CFG_CANTXB);
      instanceB = this;
   }

   CAN_initModule(base);

   // Register and enable the CAN interrupt in the PIE
   if (canBase == CANA_BASE)
   {
      Interrupt_register(INT_CANA0, &canAISR);
      Interrupt_enable(INT_CANA0);
   }
   else
   {
      Interrupt_register(INT_CANB0, &canBISR);
      Interrupt_enable(INT_CANB0);
   }

   CAN_enableInterrupt(base, CAN_INT_IE0 | CAN_INT_ERROR | CAN_INT_STATUS);
   CAN_enableGlobalInterrupt(base, CAN_GLOBAL_INT_CANINT0);
}

void C2000Can::SetBaudrate(enum baudrates baudrate)
{
   CAN_setBitRate(base, DEVICE_SYSCLK_FREQ, baudRateTable[baudrate], 16U);
   CAN_startModule(base);

   // Set up a catch-all RX mailbox with no ID filtering
   CAN_setupMessageObject(base, RX_CATCH_ALL_OBJ_ID, 0U,
                          CAN_MSG_FRAME_STD,
                          CAN_MSG_OBJ_TYPE_RX,
                          0U,
                          CAN_MSG_OBJ_RX_INT_ENABLE | CAN_MSG_OBJ_NO_FLAGS,
                          8U);

   // Configure the specific user-registered ID filters
   ConfigureFilters();
}

void C2000Can::Send(uint32_t canId, uint32_t data[2], uint8_t len)
{
   // Pack the two uint32_t words into an 8-byte uint16_t array.
   // CAN driverlib expects uint16_t* with one byte per element (lower byte used).
   uint16_t buf[8];
   const uint8_t* src = (const uint8_t*)data;
   for (int i = 0; i < 8; i++)
      buf[i] = src[i];

   bool extended = (canId & CAN_FORCE_EXTENDED) != 0U;
   uint32_t rawId = canId & ~CAN_FORCE_EXTENDED;

   CAN_setupMessageObject(base, TX_MSG_OBJ_ID, rawId,
                          extended ? CAN_MSG_FRAME_EXT : CAN_MSG_FRAME_STD,
                          CAN_MSG_OBJ_TYPE_TX,
                          0U,
                          CAN_MSG_OBJ_NO_FLAGS,
                          len);

   CAN_sendMessage(base, TX_MSG_OBJ_ID, len, buf);
}

void C2000Can::ConfigureFilters()
{
   // Set up one mailbox per registered user message, using exact-ID filtering.
   // Mailboxes are numbered 1..(RX_CATCH_ALL_OBJ_ID-1).
   // Any IDs that don't match fall through to the catch-all mailbox 31.
   for (int i = 0; i < nextUserMessageIndex; i++)
   {
      uint32_t id   = userIds[i] & ~CAN_FORCE_EXTENDED;
      bool extended = (userIds[i] & CAN_FORCE_EXTENDED) != 0U;
      uint32_t mask = userMasks[i];
      uint32_t flags = CAN_MSG_OBJ_RX_INT_ENABLE | CAN_MSG_OBJ_USE_ID_FILTER;
      if (extended)
         flags |= CAN_MSG_OBJ_USE_EXT_FILTER;

      // Use mask 0 (accept-all) when no mask was specified
      CAN_setupMessageObject(base, (uint32_t)(i + 1), id,
                             extended ? CAN_MSG_FRAME_EXT : CAN_MSG_FRAME_STD,
                             CAN_MSG_OBJ_TYPE_RX,
                             mask,
                             flags,
                             8U);
   }
}

void C2000Can::HandleMessage()
{
   uint32_t cause = CAN_getInterruptCause(base);

   if (cause == CAN_INT_INT0ID_STATUS)
   {
      // Status interrupt — clear it and return
      CAN_clearInterruptStatus(base, CAN_INT_INT0ID_STATUS);
      CAN_clearGlobalInterruptStatus(base, CAN_GLOBAL_INT_CANINT0);
      return;
   }

   // cause contains the message object number that triggered the interrupt
   if (cause < 1U || cause > 31U)
   {
      CAN_clearGlobalInterruptStatus(base, CAN_GLOBAL_INT_CANINT0);
      return;
   }

   uint16_t buf[8] = {0};
   CAN_MsgFrameType frameType;
   uint32_t msgId = 0;

   if (CAN_readMessageWithID(base, cause, &frameType, &msgId, buf))
   {
      // Repack uint16_t driverlib buffer into uint32_t[2] for HandleRx
      uint32_t data[2];
      uint8_t* dst = (uint8_t*)data;
      for (int i = 0; i < 8; i++)
         dst[i] = (uint8_t)buf[i];

      if (frameType == CAN_MSG_FRAME_EXT)
         msgId |= CAN_FORCE_EXTENDED;

      CanHardware::HandleRx(msgId, data, 8U);
   }

   CAN_clearInterruptStatus(base, cause);
   CAN_clearGlobalInterruptStatus(base, CAN_GLOBAL_INT_CANINT0);
}

extern "C" __interrupt void canAISR(void)
{
   if (C2000Can::instanceA != 0)
      C2000Can::instanceA->HandleMessage();
}

extern "C" __interrupt void canBISR(void)
{
   if (C2000Can::instanceB != 0)
      C2000Can::instanceB->HandleMessage();
}
