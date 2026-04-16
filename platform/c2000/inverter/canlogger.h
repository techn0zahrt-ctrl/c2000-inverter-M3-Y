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
 * CAN-based debug logger.
 *
 * Sends ASCII text over CAN as 8-byte frames on ID 0x7FF (highest standard
 * ID, reserved for debug use). Text longer than 8 bytes is split across
 * multiple frames. Each frame is null-padded to 8 bytes. A null byte
 * signals end-of-message to the receiver.
 *
 * Use tools/can_logger.py with a PCAN USB adapter to receive and print
 * the messages on a PC.
 *
 * Compile with -DUSE_CIO_DEBUG to fall back to CIO printf over JTAG.
 */
class CanLogger
{
public:
    /** Attach the CAN bus instance. Must be called before Print/Printf. */
    static void Init(CanHardware* can);

    /** Send a plain string. Splits across multiple 8-byte frames as needed. */
    static void Print(const char* msg);

    /** Format and send a printf-style message over CAN. Max 128 chars. */
    static void Printf(const char* fmt, ...);

private:
    static CanHardware* s_can;
};

#endif // CANLOGGER_H
