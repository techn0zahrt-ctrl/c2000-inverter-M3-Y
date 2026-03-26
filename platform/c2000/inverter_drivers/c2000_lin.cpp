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
#include "c2000_lin.h"
#include "device.h"
#include "driverlib.h"

/* Baud rate used to generate the LIN break field.
 * At 9600 baud one character (10 bit periods) spans ~1.04 ms, which equals
 * ~20 nominal bit periods at 19200 baud -- well above the LIN 2.x minimum
 * of 13 nominal bit periods (677 us).  Total frame time (break + sync/PID +
 * 9-byte response) is ~6.8 ms, leaving comfortable margin in a 10 ms tick. */
#define BREAK_BAUD    9600U
#define LIN_BAUD      19200U

/* 8-bit, no parity, 1 stop bit */
#define LIN_SCI_CONFIG (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE)

C2000Lin::C2000Lin() : rxCount(0)
{
   /* Configure GPIO48 as SCIA TX, GPIO49 as SCIA RX */
   GPIO_setPinConfig(GPIO_48_SCITXDA);
   GPIO_setPadConfig(48U, GPIO_PIN_TYPE_STD);
   GPIO_setDirectionMode(48U, GPIO_DIR_MODE_OUT);

   GPIO_setPinConfig(GPIO_49_SCIRXDA);
   GPIO_setPadConfig(49U, GPIO_PIN_TYPE_STD);
   GPIO_setDirectionMode(49U, GPIO_DIR_MODE_IN);

   /* Configure SCIA for 19200 8N1 */
   SCI_setConfig(SCIA_BASE, DEVICE_LSPCLK_FREQ, LIN_BAUD, LIN_SCI_CONFIG);
   SCI_enableFIFO(SCIA_BASE);
   SCI_resetRxFIFO(SCIA_BASE);
   SCI_resetTxFIFO(SCIA_BASE);
}

void C2000Lin::Init(uint32_t /*usart*/, int /*baudrate*/)
{
   /* No-op: hardware is configured in the constructor.
    * TeslaM3OilPump::SetLinInterface calls this with STM32 UART4 arguments
    * which are simply ignored on C2000. */
}

void C2000Lin::SendBreak()
{
   /* Lower baud rate and send 0x00 to produce the LIN break field.
    * The stop bit of 0x00 serves as the break delimiter. */
   SCI_setBaud(SCIA_BASE, DEVICE_LSPCLK_FREQ, BREAK_BAUD);

   uint8_t zero = 0U;
   SCI_writeCharArray(SCIA_BASE, &zero, 1U);

   /* Wait for the break character to finish transmitting before
    * restoring the normal baud rate and sending sync + PID. */
   while (SCI_isTransmitterBusy(SCIA_BASE)) {}

   SCI_setBaud(SCIA_BASE, DEVICE_LSPCLK_FREQ, LIN_BAUD);
}

void C2000Lin::FlushRx()
{
   SCI_resetRxFIFO(SCIA_BASE);
   rxCount = 0;
}

void C2000Lin::CollectRxBytes()
{
   /* Read however many bytes the SCIA RX FIFO currently holds. */
   uint32_t available = (uint32_t)SCI_getRxFIFOStatus(SCIA_BASE);

   while (available > 0U && rxCount < (int)sizeof(rawBuffer))
   {
      uint8_t temp;
      SCI_readCharArray(SCIA_BASE, &temp, 1U);
      rawBuffer[rxCount++] = temp;
      available--;
   }
}

void C2000Lin::Request(uint8_t id, uint8_t* data, uint8_t len)
{
   if (len > 8U) return;

   FlushRx();
   SendBreak();

   uint8_t pid = LinBus::Parity(id);

   /* Build the LIN frame in a local buffer (sync + PID [+ data + checksum]) */
   uint8_t frame[11];
   uint8_t frameLen;

   frame[0] = 0x55U;   /* sync byte */
   frame[1] = pid;

   if (len == 0U)
   {
      /* Read request: master sends sync + PID only; slave provides the data */
      frameLen = 2U;
   }
   else
   {
      /* Write request: master sends sync + PID + data + checksum */
      for (uint8_t i = 0U; i < len; i++)
         frame[2U + i] = data[i];
      frame[2U + len] = LinBus::Checksum(pid, data, (int)len);
      frameLen = len + 3U;
   }

   /* SCI_writeCharArray is blocking — returns after all bytes are queued
    * in the TX FIFO (transmission may still be in progress). */
   SCI_writeCharArray(SCIA_BASE, frame, (uint16_t)frameLen);
}

bool C2000Lin::HasReceived(uint8_t id, uint8_t requiredLen)
{
   if (requiredLen > 8U) return false;

   CollectRxBytes();

   /* Scan for the sync byte (0x55) which anchors the frame in the raw buffer.
    * The LIN transceiver reflects TX bytes back on the RX line, so the
    * received stream is: [optional break echo] [0x55 sync echo] [PID echo]
    * [requiredLen response bytes] [checksum]. */
   int syncPos = -1;
   for (int i = 0; i < rxCount; i++)
   {
      if (rawBuffer[i] == 0x55U)
      {
         syncPos = i;
         break;
      }
   }

   if (syncPos < 0) return false;

   int pidPos  = syncPos + 1;
   int dataPos = syncPos + 2;

   /* Need PID echo + requiredLen data bytes + checksum */
   if (rxCount < dataPos + (int)requiredLen + 1) return false;

   uint8_t pid = LinBus::Parity(id);
   if (rawBuffer[pidPos] != pid) return false;

   uint8_t checksum = LinBus::Checksum(pid, &rawBuffer[dataPos], (int)requiredLen);
   if (checksum != rawBuffer[dataPos + (int)requiredLen]) return false;

   /* Copy validated payload to responseData so GetReceivedBytes() can return it */
   for (int i = 0; i < (int)requiredLen; i++)
      responseData[i] = rawBuffer[dataPos + i];

   return true;
}
