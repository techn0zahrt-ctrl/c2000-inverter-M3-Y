/*
 * This file is part of the c2000-inverter project.
 *
 * Copyright (C) 2024 David J. Fiddes <D.J@fiddes.net>
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
#ifndef CANLOGGER_H
#define CANLOGGER_H

#include "canhardware.h"

/**
 * Demand-driven CAN debug logger.
 *
 * Control channel (ID 0x7FE):
 *   Byte 0 = command: 0x01 = start, 0x00 = stop
 *   Byte 1 = interval in 100 ms units (1 = 100 ms, 20 = 2 s)
 *
 * Log channel (ID 0x7FF):
 *   ASCII text in 8-byte null-padded frames. A null byte signals
 *   end-of-message to the receiver.
 *
 * Logging stops automatically 30 s after the last start command
 * (keepalive). The Python client must re-send the start command every
 * ~5–10 s to keep logging active.
 *
 * Use tools/can_logger.py with a PCAN USB adapter.
 * Compile with -DUSE_CIO_DEBUG to fall back to CIO printf over JTAG.
 */
class CanLogger
{
public:
    /** Attach CAN bus and register the control-frame receiver. */
    static void Init(CanHardware* can);

    /**
     * Advance the interval and keepalive timers by ms milliseconds.
     * Returns true when it is time to emit a debug snapshot.
     * Call once per main-loop iteration with the loop period in ms.
     */
    static bool Tick(uint32_t ms);

    /** Send a plain string (only when logging is enabled). */
    static void Print(const char* msg);

    /** Format and send a printf-style message (max 128 chars). */
    static void Printf(const char* fmt, ...);

    static bool IsEnabled() { return s_enabled; }

    static void HandleControlFrame(uint32_t canId, uint32_t data[2],
                                   uint8_t dlc);

private:

    static CanHardware* s_can;
    static bool         s_enabled;
    static uint32_t     s_intervalMs;
    static uint32_t     s_intervalAccumMs;
    static uint32_t     s_keepaliveMs;

    static const uint32_t CAN_LOG_ID          = 0x7FFU;
    static const uint32_t CAN_CTRL_ID         = 0x7FEU;
    static const uint32_t KEEPALIVE_TIMEOUT_MS = 30000U;
    static const uint32_t DEFAULT_INTERVAL_MS  = 2000U;
};

#endif // CANLOGGER_H
