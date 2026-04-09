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
 * Parameter save/load for the C2000 platform.
 *
 * Persistent storage is the Microchip 25LC256 SPI EEPROM, accessed via
 * c2000::EEPROM::Read32Bits / Write32Bits.  Because the EEPROM is not
 * memory-mapped the STM32 pointer-cast approach cannot be used; both
 * functions use explicit EEPROM byte-address arithmetic instead.
 *
 * EEPROM layout within the PARAM_FLASH_ADDR block (PARAM_BLKSIZE = 1024 bytes):
 *
 *   Offset 0 .. (NUM_PARAMS*8 - 1)   parameter entries (8 EEPROM bytes each)
 *     word 0: bits[15:0] = parameter id (key)
 *             bits[31:24] = flags
 *     word 1: fixed-point value (s32fp)
 *   Offset NUM_PARAMS*8               CRC-32 of the entry region
 *   Offset NUM_PARAMS*8 + 4           padding (0xFFFFFFFF)
 *
 * This layout is byte-compatible with the STM32 PARAM_ENTRY struct stored
 * in little-endian flash (dummy byte at bits[23:16] is always zero).
 */

#include "c2000_crc.h"     /* crc_reset, crc_calculate_block              */
#include "c2000/eeprom.h"  /* c2000::EEPROM::Read32Bits                   */
#include "c2000_flash.h"   /* flash_unlock/lock/erase_page/program_word  */
#include "params.h"
#include "param_save.h"
#include "hwdefs.h"        /* PARAM_FLASH_ADDR, PARAM_BLKSIZE             */

/* Number of 32-bit EEPROM words that fit in one parameter block */
#define PARAM_WORDS_EEPROM  (PARAM_BLKSIZE / 4U)   /* 1024 / 4 = 256      */

/* Each parameter entry occupies 2 × 32-bit EEPROM words (= 8 EEPROM bytes).
 * The last 2 words of the block are the CRC word and a padding word, so: */
#define EEPROM_ENTRY_WORDS  2U
#define NUM_PARAMS          ((PARAM_WORDS_EEPROM - 2U) / EEPROM_ENTRY_WORDS)  /* 127 */

/**
 * Save parameters to EEPROM.
 *
 * Mirrors the STM32 parm_save logic: reads current block to decide
 * whether an erase is needed, packs all parameters into a flat word
 * buffer, computes CRC, then writes to EEPROM.
 *
 * @return CRC of the written parameter block.
 */
uint32_t parm_save(void)
{
    uint32_t buf[PARAM_WORDS_EEPROM];
    uint32_t i;

    /* Check whether the block is already blank (all 0xFF).
     * If so we can skip the erase and save EEPROM write cycles. */
    uint32_t check = 0xFFFFFFFFUL;
    for (i = 0; i < PARAM_WORDS_EEPROM; i++)
        check &= c2000::EEPROM::Read32Bits(PARAM_FLASH_ADDR + i * 4U);

    /* Initialise buffer to blank (0xFF = erased state) */
    for (i = 0; i < PARAM_WORDS_EEPROM; i++)
        buf[i] = 0xFFFFFFFFUL;

    /* Pack parameter entries into the buffer */
    uint32_t idx;
    for (idx = 0; Param::IsParam((Param::PARAM_NUM)idx) && idx < NUM_PARAMS; idx++)
    {
        const Param::Attributes *pAtr = Param::GetAttrib((Param::PARAM_NUM)idx);
        uint32_t flags = (uint32_t)Param::GetFlag((Param::PARAM_NUM)idx);
        /* word 0: key in bits[15:0], flags in bits[31:24] (dummy bits[23:16] = 0) */
        buf[idx * EEPROM_ENTRY_WORDS]      = (uint32_t)pAtr->id | (flags << 24);
        buf[idx * EEPROM_ENTRY_WORDS + 1U] = (uint32_t)Param::Get((Param::PARAM_NUM)idx);
    }

    /* Compute CRC over the entry region and store it */
    crc_reset();
    buf[2U * NUM_PARAMS] = crc_calculate_block(buf, 2U * NUM_PARAMS);

    /* Write to EEPROM */
    for (i = 0; i < PARAM_WORDS_EEPROM; i++)
        c2000::EEPROM::Write32Bits(PARAM_FLASH_ADDR + i * 4U, buf[i]);

    return buf[2U * NUM_PARAMS];
}

/**
 * Load parameters from EEPROM.
 *
 * Reads the entry region, verifies the CRC, then restores each
 * parameter value and its flags.
 *
 * @retval  0  Parameters loaded successfully.
 * @retval -1  CRC mismatch — block is empty or corrupt; parameters not loaded.
 */
int parm_load(void)
{
    /* Read entry data into RAM */
    uint32_t buf[2U * NUM_PARAMS];
    uint32_t i;
    for (i = 0; i < 2U * NUM_PARAMS; i++)
        buf[i] = c2000::EEPROM::Read32Bits(PARAM_FLASH_ADDR + i * 4U);

    /* Verify CRC */
    crc_reset();
    uint32_t crc       = crc_calculate_block(buf, 2U * NUM_PARAMS);
    uint32_t storedCrc = c2000::EEPROM::Read32Bits(
                             PARAM_FLASH_ADDR + 2U * NUM_PARAMS * 4U);

    if (crc != storedCrc)
        return -1;

    /* Restore parameter values */
    for (i = 0; i < NUM_PARAMS; i++)
    {
        uint32_t word0 = buf[i * EEPROM_ENTRY_WORDS];
        uint16_t key   = (uint16_t)(word0 & 0xFFFFU);
        uint32_t flags = word0 >> 24;

        if (key > 0)
        {
            Param::PARAM_NUM paramIdx = Param::NumFromId(key);
            if (paramIdx != Param::PARAM_INVALID)
            {
                Param::SetFixed(paramIdx, buf[i * EEPROM_ENTRY_WORDS + 1U]);
                Param::SetFlagsRaw(paramIdx, (uint_least8_t)flags);
            }
        }
    }
    return 0;
}
