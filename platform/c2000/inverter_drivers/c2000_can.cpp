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
#define TX_MSG_OBJ_ID  32U //1 for now change back to 32 when fixed
//#define RX_MSG_OBJ_ID  2U //1 for now change back to 32 when fixed

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
      GPIO_setDirectionMode(5U, GPIO_DIR_MODE_IN);
      GPIO_setDirectionMode(4U, GPIO_DIR_MODE_OUT);
      GPIO_setQualificationMode(5U, GPIO_QUAL_ASYNC);
      instanceA = this;
   }
   else // CANB_BASE
   {
      GPIO_setPinConfig(DEVICE_GPIO_CFG_CANRXB);
      GPIO_setPinConfig(DEVICE_GPIO_CFG_CANTXB);
      instanceB = this;
   }

   CAN_initModule(base);
}

void C2000Can::SetBaudrate(enum baudrates baudrate)
{
   CAN_setBitRate(base, DEVICE_SYSCLK_FREQ, baudRateTable[baudrate], 16U);
   ConfigureFilters();
   RegisterInterrupts();
}

void C2000Can::RegisterInterrupts()
{
   // Register and enable the CAN interrupt in the PIE
   if (base == CANA_BASE)
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
   CAN_startModule(base);
}

void C2000Can::Send(uint32_t canId, uint32_t data[2], uint8_t len)
{
   // On C28x, HWREGB in CAN_writeDataReg performs 16-bit word writes because
   // uint8_t = uint16_t (CHAR_BIT=16). Each write covers two CAN data bytes.
   // Register layout (word offsets from IF1DATA base):
   // Pack two bytes per element so each 16-bit write fills both byte slots.
   uint16_t buf[8] = {0};
   //buf[1] = (data[0] & 0xFF) | ((data[0] >> 8) & 0xFF) << 8;
   //buf[3] = ((data[0] >> 16) & 0xFF) | ((data[0] >> 24) & 0xFF) << 8;
   //buf[5] = (data[1] & 0xFF) | ((data[1] >> 8) & 0xFF) << 8;
   //buf[7] = ((data[1] >> 16) & 0xFF) | ((data[1] >> 24) & 0xFF) << 8;
   buf[1] = (data[0] & 0xFF) | (((data[0] >> 8) & 0xFF) << 8);  // unchanged
   buf[3] = (((data[0] >> 16) & 0xFF) << 8) | (((data[0] >> 24) & 0xFF));  // swapped
   buf[5] = (data[1] & 0xFF) | (((data[1] >> 8) & 0xFF) << 8);  // unchanged
   buf[7] = (((data[1] >> 16) & 0xFF) << 8) | (((data[1] >> 24) & 0xFF));  // swapped

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
      //uint32_t mask = userMasks[i];
      uint32_t mask = extended ? 0x1FFFFFFFU : 0x7FFU;
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
   /*// Set up TX mailbox simple
   CAN_setupMessageObject(base, TX_MSG_OBJ_ID,
                           0U,
                           CAN_MSG_FRAME_STD,
                           CAN_MSG_OBJ_TYPE_TX,
                           0U,
                           CAN_MSG_OBJ_NO_FLAGS,
                           8U);
   CAN_setupMessageObject(base, RX_MSG_OBJ_ID,
                           0x601U,
                           CAN_MSG_FRAME_STD,
                           CAN_MSG_OBJ_TYPE_RX,
                           0x7FFU,
                           CAN_MSG_OBJ_USE_ID_FILTER | CAN_MSG_OBJ_RX_INT_ENABLE,
                           8U);
*/
}

void C2000Can::HandleMessage()
{
   uint32_t cause = CAN_getInterruptCause(base);
   extern volatile uint32_t canRxCount;
   extern volatile uint32_t canIsrCount;
   extern volatile uint32_t canStatusCount;
   extern volatile uint32_t canLastCause;
   extern volatile uint32_t canLastStatus;

   canIsrCount++;
   canLastCause = cause;

   if (cause == CAN_INT_INT0ID_STATUS)
   {
      canStatusCount++;
      canLastStatus = CAN_getStatus(base);
      CAN_clearInterruptStatus(base, CAN_INT_INT0ID_STATUS);
      CAN_clearGlobalInterruptStatus(base, CAN_GLOBAL_INT_CANINT0);
      return;
   }

   if (cause < 1U || cause > 31U)
   {
      CAN_clearGlobalInterruptStatus(base, CAN_GLOBAL_INT_CANINT0);
      return;
   }

   uint16_t buf[8] = {0};

   if (CAN_readMessage(base, cause, buf))
   {
      canRxCount++;

      uint32_t arb = HWREG(base + CAN_O_IF2ARB);
      uint32_t msgId;
      bool extended = (arb & CAN_IF2ARB_XTD) != 0U;

      if (extended)
      {
         msgId = arb & CAN_IF2ARB_ID_M;
         msgId |= CAN_FORCE_EXTENDED;
      }
      else
      {
         msgId = (arb >> CAN_IF2ARB_STD_ID_S) & 0x7FFU;
      }
      extern volatile uint32_t canLastMsgId;
      canLastMsgId = msgId;

      uint32_t data[2];
      //data[0] = ((uint32_t)buf[3] << 16) | (uint32_t)buf[1];
      //data[1] = ((uint32_t)buf[7] << 16) | (uint32_t)buf[5];
      data[0] = (((uint32_t)((buf[3] << 8) | (buf[3] >> 8))) << 16) | (uint32_t)buf[1];
      data[1] = (((uint32_t)((buf[7] << 8) | (buf[7] >> 8))) << 16) | (uint32_t)buf[5];
      //data[0] = ((uint32_t)((buf[3] >> 8) | (buf[3] << 8)) << 16) | 
      //         (uint32_t)((buf[1] >> 8) | (buf[1] << 8));
      //data[1] = ((uint32_t)((buf[7] >> 8) | (buf[7] << 8)) << 16) | 
      //         (uint32_t)((buf[5] >> 8) | (buf[5] << 8));
      CanHardware::HandleRx(msgId, data, 8U);
   }

   CAN_clearInterruptStatus(base, cause);
   CAN_clearGlobalInterruptStatus(base, CAN_GLOBAL_INT_CANINT0);
}

extern "C" __interrupt void canAISR(void)
{
   if (C2000Can::instanceA != 0)
      C2000Can::instanceA->HandleMessage();
   Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}

extern "C" __interrupt void canBISR(void)
{
   if (C2000Can::instanceB != 0)
      C2000Can::instanceB->HandleMessage();
   Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}
