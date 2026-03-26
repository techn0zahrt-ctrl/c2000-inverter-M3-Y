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
 *   SCI does not have a dedicated break-send bit. The break is generated
 *   by temporarily lowering the SCIA baud rate to 9600 (half of 19200)
 *   and transmitting 0x00.  At 9600 baud one character spans ~20 nominal
 *   bit periods, satisfying the LIN 2.x requirement of >= 13 bit periods.
 *   The baud rate is restored to 19200 before the sync + PID bytes are sent.
 *
 *   NOTE: The user-specified rate of ~1200 baud would produce an 8.3 ms break,
 *   which would not leave time for the slave response to arrive within the
 *   10 ms tick window.  Use 9600 baud (or tune BREAK_BAUD below as needed).
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
   /** Initialise SCIA hardware (GPIO48 TX / GPIO49 RX, 19200 8N1). */
   C2000Lin();

   /**
    * No-op: SCIA is configured in the constructor.
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

private:
   /** Lower baud to 9600, send 0x00 (break), restore 19200. */
   void SendBreak();

   /** Reset SCIA RX FIFO and clear the local receive buffer. */
   void FlushRx();

   /** Drain the SCIA RX FIFO into rawBuffer[]. */
   void CollectRxBytes();

   /* Raw bytes received from SCIA since last FlushRx().
    * Capacity: break-echo(1) + sync(1) + PID(1) + 8 data + checksum(1) + margin */
   uint8_t rawBuffer[14];
   int     rxCount;

   /* Extracted payload from the last successfully validated response. */
   uint8_t responseData[8];
};

#endif // C2000_LIN_H
