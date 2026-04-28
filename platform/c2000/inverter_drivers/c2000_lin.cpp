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

/* LIN baud rate */
#define LIN_BAUD      19200U
/* Half of LIN baud — 0x00 at this rate produces a ~1.04 ms dominant field (>13 bits at 19200) */
#define BREAK_BAUD    9600U

/* 8-bit, no parity, 1 stop bit */
#define LIN_SCI_CONFIG (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE)

C2000Lin::C2000Lin() : rxCount(0), lastRxCount(-1)
{
   /* Hardware init is deferred to HwInit() so that it runs after
    * Device_init() in main() — Device_init() resets all peripherals
    * including SCIA, which would wipe any configuration done here. */
}

void C2000Lin::HwInit()
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
   SCI_enableModule(SCIA_BASE);
}

void C2000Lin::Init(uint32_t /*usart*/, int /*baudrate*/)
{
   /* No-op: hardware is configured in the constructor.
    * TeslaM3OilPump::SetLinInterface calls this with STM32 UART4 arguments
    * which are simply ignored on C2000. */
}

void C2000Lin::SendBreak()
{
   /* Generate LIN break using the baud-rate trick, but with FIFO disabled
    * so TXEMPTY correctly reflects the shift register state.
    * With FIFO enabled TXEMPTY is unreliable; TXFFST (what
    * SCI_isTransmitterBusy checks) clears the instant the byte moves to the
    * shift register, causing the baud rate to be restored too early.
    * Disabling FIFO before the break and re-enabling after gives a clean
    * single-stream transmission analogous to STM32's USART SBK+LINEN path. */
   SCI_disableFIFO(SCIA_BASE);

   SCI_setBaud(SCIA_BASE, DEVICE_LSPCLK_FREQ, BREAK_BAUD);

   uint8_t zero = 0U;
   SCI_writeCharArray(SCIA_BASE, &zero, 1U);

   /* Without FIFO, TXEMPTY (SCICTL2 bit 6) correctly indicates both the TX
    * buffer and shift register are empty — i.e. the full break character has
    * been transmitted. */
   while ((HWREGH(SCIA_BASE + SCI_O_CTL2) & SCI_CTL2_TXEMPTY) == 0U) {}

   SCI_setBaud(SCIA_BASE, DEVICE_LSPCLK_FREQ, LIN_BAUD);

   SCI_enableFIFO(SCIA_BASE);
   SCI_resetTxFIFO(SCIA_BASE);
   SCI_resetRxFIFO(SCIA_BASE);
   /* Cycle SWRESET to clear SCIRXBUF/RXRDY/BRKDT left by the break echo that
    * arrived in non-FIFO mode — otherwise the stale state silently blocks the
    * FIFO receiver from accepting new bytes. SCIFFTX/SCIFFRX and baud registers
    * are unaffected by SWRESET. */
   SCI_performSoftwareReset(SCIA_BASE);
   rxCount = 0;
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
   lastRxCount = rxCount;

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
