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
/*
 * C2000 LIN hardware driver using SCIA (GPIO48 TX, GPIO49 RX).
 *
 * Inherits from LinBus and overrides the hardware methods so that
 * TeslaM3OilPump can hold a LinBus* and receive a C2000Lin instance.
 *
 * LIN break generation:
 *   SCI has no hardware break-send bit.  Baud-rate tricks are unreliable with
 *   FIFO enabled because TXFFST (what SCI_isTransmitterBusy checks) clears the
 *   instant the byte enters the shift register, not when it finishes transmitting.
 *   Fix: disable FIFO, drop to 9600 baud, transmit 0x00 (produces a 10-bit
 *   dominant field = 1.04 ms > 13 bits at 19200), poll TXEMPTY (SCICTL2 bit 6)
 *   which correctly reflects both the TX buffer and shift register when FIFO is
 *   off, then restore 19200 baud and re-enable the FIFO.
 *
 * Receive handling:
 *   After a read request the LIN transceiver reflects the sync (0x55) and
 *   PID bytes back on the RX line.  C2000Lin::HasReceived() reads whatever
 *   bytes have accumulated in the SCIA RX FIFO, scans for the 0x55 sync
 *   byte as an anchor, and validates the PID echo and checksum.
 *   Received payload bytes are copied to an internal responseData[] buffer
 *   which GetReceivedBytes() returns.
 */
#ifndef C2000_LIN_H
#define C2000_LIN_H

#include "linbus.h"
#include <stdint.h>

class C2000Lin : public LinBus
{
public:
   /** Constructor — does not touch hardware (see HwInit). */
   C2000Lin();

   /**
    * Configure SCIA hardware (GPIO48 TX / GPIO49 RX, 19200 8N1).
    * Must be called after Device_init() in main() since Device_init()
    * resets all peripherals.
    */
   void HwInit();

   /**
    * No-op: SCIA is configured via HwInit().
    * Called by TeslaM3OilPump::SetLinInterface; usart and baudrate are ignored.
    */
   void Init(uint32_t usart, int baudrate) override;

   /**
    * Send a LIN frame: break + sync (0x55) + PID [+ data + checksum].
    * For read requests (len == 0) only break + sync + PID are sent;
    * the slave is expected to respond with data + checksum.
    */
   void Request(uint8_t id, uint8_t* data, uint8_t len) override;

   /**
    * Collect bytes from the SCIA RX FIFO and check whether a valid response
    * for the given id and length has been received.
    * Locates the sync byte (0x55) in the raw receive buffer as an anchor,
    * then verifies the PID echo and checksum.
    */
   bool HasReceived(uint8_t id, uint8_t requiredLen) override;

   /** Return pointer to the last successfully received payload bytes. */
   uint8_t* GetReceivedBytes() override { return responseData; }

   /** Return rxCount captured after the last HasReceived() call (-1 = never called). */
   int GetLastRxCount() const { return lastRxCount; }

   /** Return pointer to raw receive buffer (up to 14 bytes). */
   const uint8_t* GetRawBuffer() const { return rawBuffer; }

private:
   /** Send LIN break: disable FIFO, 9600 baud 0x00, poll TXEMPTY, restore 19200 + FIFO. */
   void SendBreak();

   /** Reset SCIA RX FIFO and clear the local receive buffer. */
   void FlushRx();

   /** Drain the SCIA RX FIFO into rawBuffer[]. */
   void CollectRxBytes();

   /* Raw bytes received from SCIA since last FlushRx().
    * Capacity: break-echo(1) + sync(1) + PID(1) + 8 data + checksum(1) + margin */
   uint8_t rawBuffer[14];
   int     rxCount;
   int     lastRxCount;

   /* Extracted payload from the last successfully validated response. */
   uint8_t responseData[8];
};

#endif // C2000_LIN_H
