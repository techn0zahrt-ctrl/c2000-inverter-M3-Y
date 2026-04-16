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

#include "canlogger.h"
#include <stdarg.h>
#include <stdint.h>
#include "printf.h"

// CAN ID used exclusively for debug text frames
static const uint32_t CAN_LOG_ID = 0x7FFU;

// Maximum formatted message length (including null terminator)
static const int LOG_BUF_SIZE = 128;

CanHardware* CanLogger::s_can = 0;

// ---------------------------------------------------------------------------
// IPutChar adapter: collects formatted characters into a fixed-size buffer.
// Used by Printf to format via libopeninv's fprintf without needing vsprintf.
// ---------------------------------------------------------------------------
class BufferPutChar : public IPutChar
{
public:
    explicit BufferPutChar(char* buf, int maxLen)
        : m_buf(buf), m_maxLen(maxLen), m_pos(0)
    {
        m_buf[0] = '\0';
    }

    void PutChar(char c)
    {
        if (m_pos < m_maxLen - 1)
        {
            m_buf[m_pos++] = c;
            m_buf[m_pos]   = '\0';
        }
    }

private:
    char* m_buf;
    int   m_maxLen;
    int   m_pos;
};

// ---------------------------------------------------------------------------

void CanLogger::Init(CanHardware* can)
{
    s_can = can;
}

/**
 * Send a null-terminated string as a sequence of 8-byte CAN frames on
 * ID 0x7FF. Each frame is zero-padded to 8 bytes. A null byte in the
 * payload signals end-of-message to the receiver.
 *
 * On C2000 (non-EABI) uint8_t is 16-bit, so a uint8_t[8] array is twice
 * as large as C2000Can::Send expects when cast to uint32_t*. Avoid that
 * overload entirely by packing chars directly into uint32_t[2] here.
 */
void CanLogger::Print(const char* msg)
{
    if (!s_can || !msg)
        return;

    while (*msg)
    {
        // Pack up to 8 chars into two 32-bit words (4 chars each), null-padded.
        uint32_t data[2] = {0U, 0U};

        int i = 0;
        while (i < 4 && msg[i] != '\0')
        {
            data[0] |= ((uint32_t)(unsigned char)msg[i]) << (i * 8);
            i++;
        }
        while (i < 8 && msg[i] != '\0')
        {
            data[1] |= ((uint32_t)(unsigned char)msg[i]) << ((i - 4) * 8);
            i++;
        }
        msg += i;

        s_can->Send(CAN_LOG_ID, data, 8);
    }
}

/**
 * Format a printf-style message into a 128-byte buffer via libopeninv's
 * vfprintf (with a BufferPutChar adapter), then send via Print().
 */
void CanLogger::Printf(const char* fmt, ...)
{
    char buf[LOG_BUF_SIZE];
    BufferPutChar sink(buf, LOG_BUF_SIZE);

    va_list args;
    va_start(args, fmt);
    vfprintf(&sink, fmt, args);
    va_end(args);

    Print(buf);
}
