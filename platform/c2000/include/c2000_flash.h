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
 * C2000 "flash" API wrappers matching the libopencm3 STM32 flash interface
 * used by canmap.cpp and param_save.cpp.
 *
 * On this platform, persistent parameter and CAN map storage is backed by
 * the Microchip 25LC256 SPI EEPROM rather than the C2000 internal flash.
 * All erase and program operations are routed through c2000::EEPROM.
 *
 * Addresses passed to these functions are EEPROM byte addresses as defined
 * in hwdefs.h (PARAM_FLASH_ADDR, CAN_FLASH_ADDR), not C2000 internal flash
 * word addresses.
 */
#ifndef C2000_FLASH_H_INCLUDED
#define C2000_FLASH_H_INCLUDED

#include <stdint.h>
#include "hwdefs.h"        /* FLASH_PAGE_SIZE, PARAM_FLASH_ADDR, CAN_FLASH_ADDR */
//#include "c2000/eeprom.h"  /* c2000::EEPROM::Write32Bits, Write8Bits */
/* Forward declarations — full definition in c2000/eeprom.h */
namespace c2000 {
class EEPROM {
public:
    static void Write32Bits(uint16_t address, uint32_t data);
    static void Write8Bits(uint16_t address, uint16_t data);
};
} // namespace c2000

/*
 * No lock/unlock mechanism is required — the 25LC256 uses per-transaction
 * WREN commands (handled inside c2000::EEPROM::Write32Bits) rather than a
 * persistent hardware lock register. These are intentional no-ops.
 */
static inline void flash_unlock(void)
{
}

static inline void flash_lock(void)
{
}

/*
 * Wait-state configuration is an internal flash concept; it has no meaning
 * for SPI EEPROM access. Intentional no-op.
 */
static inline void flash_set_ws(uint32_t ws)
{
    (void)ws;
}

/*
 * Erase a storage block by overwriting it with 0xFF bytes.
 *
 * Simulates a flash erase (which leaves all bits set to 1) by writing
 * 0xFFFFFFFF across the entire FLASH_PAGE_SIZE region starting at addr.
 * Writes are issued as 32-bit words, so FLASH_PAGE_SIZE must be a multiple
 * of 4 (guaranteed: 0x200 = 512 = 128 * 4).
 *
 * @param addr  Base EEPROM byte address of the block to erase.
 */
static inline void flash_erase_page(uint32_t addr)
{
    for (uint32_t offset = 0; offset < FLASH_PAGE_SIZE; offset += 4)
    {
        c2000::EEPROM::Write32Bits((uint16_t)(addr + offset), 0xFFFFFFFFUL);
    }
}

/*
 * Write a 32-bit word to the given EEPROM byte address.
 *
 * @param addr  EEPROM byte address to write to.
 * @param data  32-bit value to write.
 */
static inline void flash_program_word(uint32_t addr, uint32_t data)
{
    c2000::EEPROM::Write32Bits((uint16_t)addr, data);
}

/*
 * Return the storage size visible to canmap in units that keep the sizing
 * arithmetic consistent with FLASH_PAGE_SIZE.
 *
 * The 25LC256 is 32KB. With FLASH_PAGE_SIZE = 512 bytes:
 *   32768 / 512 = 64 pages
 *
 * canmap.cpp derives available storage as:
 *   desig_get_flash_size() * 1024 / FLASH_PAGE_SIZE
 * which with a return value of 32 (KB) and 512-byte pages also yields 64,
 * but returning the page count directly (64) keeps the intent explicit and
 * avoids the ×1024 inflation exceeding the actual EEPROM address space.
 *
 * @return  64 (number of 512-byte pages in the 25LC256).
 */
static inline uint16_t desig_get_flash_size(void)
{
    //return 64U;
    return 32U; /* 25LC256 = 32KB; formula: FLASH_BASE + 32*1024 = 0x8000 = EEPROM_TOP */
}

#endif /* C2000_FLASH_H_INCLUDED */
