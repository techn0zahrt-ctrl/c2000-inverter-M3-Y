/*
 * This file is part of the libopeninv project.
 *
 * Copyright (C) 2024 openinverter.org
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
 * C2000 CRC API wrappers matching the libopencm3 STM32 CRC interface.
 *
 * The F28377D does not expose a standalone hardware CRC-32 peripheral
 * equivalent to the STM32 CRC unit. This implementation provides a
 * software CRC-32 using the same polynomial and algorithm as the STM32
 * hardware (IEEE 802.3, poly = 0x04C11DB7, initial value = 0xFFFFFFFF,
 * input/output NOT reflected — matching STM32 CRC peripheral behaviour).
 *
 * Drop-in replacements for:
 *   crc_reset()
 *   crc_calculate(uint32_t data)
 *   crc_calculate_block(uint32_t *data, uint32_t size)
 */
#ifndef C2000_CRC_H_INCLUDED
#define C2000_CRC_H_INCLUDED

#include <stdint.h>

/** CRC-32 polynomial matching the STM32 hardware CRC unit (IEEE 802.3) */
#define CRC32_POLY  0x04C11DB7UL

/** Running CRC accumulator — mirrors the STM32 CRC data register */
//static uint32_t crc32_accum;
extern uint32_t crc32_accum;

/*
 * Compute one step of CRC-32 (non-reflected, matching STM32 hardware).
 *
 * The STM32 CRC peripheral processes data MSB-first without bit reflection,
 * which differs from standard Ethernet CRC. This helper matches that
 * behaviour exactly so stored checksums are compatible.
 */
static inline uint32_t crc32_step(uint32_t crc, uint32_t data)
{
    uint16_t i;
    crc ^= data;
    for (i = 0U; i < 32U; i++)
    {
        if (crc & 0x80000000UL)
        {
            crc = (crc << 1U) ^ CRC32_POLY;
        }
        else
        {
            crc <<= 1U;
        }
    }
    return crc;
}

/*
 * Reset the CRC unit to its initial state (all ones, matching STM32 reset
 * value of the CRC_DR register after a CRC_CR.RESET write).
 */
static inline void crc_reset(void)
{
    crc32_accum = 0xFFFFFFFFUL;
}

/*
 * Feed a single 32-bit word into the CRC and return the updated value.
 *
 * Matches the STM32 libopencm3 crc_calculate() signature and behaviour:
 * writes one word to the CRC data register and reads back the result.
 *
 * @param data  32-bit word to process.
 * @return      Current CRC value after processing data.
 */
static inline uint32_t crc_calculate(uint32_t data)
{
    crc32_accum = crc32_step(crc32_accum, data);
    return crc32_accum;
}

/*
 * Feed an array of 32-bit words into the CRC and return the final value.
 *
 * Matches the STM32 libopencm3 crc_calculate_block() signature.
 *
 * @param data  Pointer to array of 32-bit words.
 * @param size  Number of 32-bit words in the array.
 * @return      CRC value after processing all words.
 */
static inline uint32_t crc_calculate_block(uint32_t *data, uint32_t size)
{
    uint32_t i;
    for (i = 0U; i < size; i++)
    {
        crc32_accum = crc32_step(crc32_accum, data[i]);
    }
    return crc32_accum;
}

#endif /* C2000_CRC_H_INCLUDED */
