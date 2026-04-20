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

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------

CanHardware* CanLogger::s_can             = 0;
bool         CanLogger::s_enabled         = false;
uint32_t     CanLogger::s_intervalMs      = CanLogger::DEFAULT_INTERVAL_MS;
uint32_t     CanLogger::s_intervalAccumMs = 0U;
uint32_t     CanLogger::s_keepaliveMs     = 0U;

// ---------------------------------------------------------------------------
// CAN RX callback — dispatches 0x7FE control frames to HandleControlFrame
// ---------------------------------------------------------------------------

class CanLoggerCallback : public CanCallback
{
public:
    void HandleRx(uint32_t canId, uint32_t data[2], uint8_t dlc) override
    {
        CanLogger::HandleControlFrame(canId, data, dlc);
    }
    void HandleClear() override {}
};

static CanLoggerCallback s_callback;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CanLogger::Init(CanHardware* can)
{
    s_can = can;
    can->RegisterUserMessage(CAN_CTRL_ID);
    can->AddCallback(&s_callback);
}

/**
 * Advance timers by ms milliseconds.
 *
 * Keepalive: if no start command has been received for 30 s, disable logging.
 * Interval:  return true (time to dump) when the requested interval elapses.
 * The first call after a start command always returns true so the client sees
 * output immediately without waiting a full interval.
 */
bool CanLogger::Tick(uint32_t ms)
{
    if (!s_enabled)
        return false;

    s_keepaliveMs += ms;
    if (s_keepaliveMs >= KEEPALIVE_TIMEOUT_MS)
    {
        s_enabled = false;
        return false;
    }

    s_intervalAccumMs += ms;
    if (s_intervalAccumMs >= s_intervalMs)
    {
        s_intervalAccumMs = 0U;
        return true;
    }
    return false;
}

/**
 * Handle a control frame on ID 0x7FE.
 *   data[0] bits [7:0]  = command byte (0x01 start, 0x00 stop)
 *   data[0] bits [15:8] = interval in 100 ms units (0 → use default 2 s)
 */
void CanLogger::HandleControlFrame(uint32_t canId, uint32_t data[2],
                                   uint8_t /*dlc*/)
{
    if (canId != CAN_CTRL_ID)
        return;

    uint8_t cmd      = (uint8_t)(data[0] & 0xFFU);
    uint8_t interval = (uint8_t)((data[0] >> 8) & 0xFFU);

    if (cmd == 0x01U)
    {
        s_keepaliveMs = 0U;
        uint32_t newInterval = (interval > 0U)
                                   ? (uint32_t)interval * 100U
                                   : DEFAULT_INTERVAL_MS;
        if (!s_enabled)
        {
            // Fresh start: trigger immediate first dump and apply interval
            s_enabled         = true;
            s_intervalMs      = newInterval;
            s_intervalAccumMs = s_intervalMs;
        }
        else if (newInterval != s_intervalMs)
        {
            // Interval changed mid-session: apply without disrupting timing
            s_intervalMs = newInterval;
        }
    }
    else if (cmd == 0x00U)
    {
        s_enabled = false;
    }
}

// ---------------------------------------------------------------------------
// IPutChar adapter for Printf
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

/**
 * Send a null-terminated string as 8-byte CAN frames on ID 0x7FF.
 * Silently dropped when logging is disabled.
 *
 * On C2000 (EABI) uint8_t is 8-bit but the CanHardware uint8_t[] overload
 * casts to uint32_t* which only reads data[0..1] (covers 4 chars, not 8).
 * Pack directly into uint32_t[2] to avoid that truncation.
 */
void CanLogger::Print(const char* msg)
{
    if (!s_can || !msg || !s_enabled)
        return;

    while (*msg)
    {
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
 * Format a printf-style message (max 128 chars) then send via Print().
 * Silently dropped when logging is disabled.
 */
void CanLogger::Printf(const char* fmt, ...)
{
    if (!s_enabled)
        return;

    static const int LOG_BUF_SIZE = 128;
    char buf[LOG_BUF_SIZE];
    BufferPutChar sink(buf, LOG_BUF_SIZE);

    va_list args;
    va_start(args, fmt);
    vfprintf(&sink, fmt, args);
    va_end(args);

    Print(buf);
}
