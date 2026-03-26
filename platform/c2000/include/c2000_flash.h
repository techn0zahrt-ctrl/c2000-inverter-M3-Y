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
#ifndef C2000_FLASH_H_INCLUDED
#define C2000_FLASH_H_INCLUDED

#include <stdint.h>
#include "hwdefs.h"

/* Only forward-declare EEPROM if the real eeprom.h hasn't been included yet.
 * When eeprom.h is included first it defines EEPROM_H and the full class
 * definition is already present. When only c2000_flash.h is included (e.g.
 * from libopeninv) the forward declaration satisfies the compiler and the
 * linker resolves the actual implementation at link time. */
#ifndef EEPROM_H
namespace c2000 {
class EEPROM {
public:
    static void Write32Bits(uint16_t address, uint32_t data);
    static void Write8Bits(uint16_t address, uint16_t data);
    static uint32_t Read32Bits(uint16_t address);
};
} // namespace c2000
#endif

static inline void flash_unlock(void) {}
static inline void flash_lock(void) {}
static inline void flash_set_ws(uint32_t ws) { (void)ws; }

static inline void flash_erase_page(uint32_t addr)
{
    for (uint32_t offset = 0; offset < FLASH_PAGE_SIZE; offset += 4)
    {
        c2000::EEPROM::Write32Bits((uint16_t)(addr + offset), 0xFFFFFFFFUL);
    }
}

static inline void flash_program_word(uint32_t addr, uint32_t data)
{
    c2000::EEPROM::Write32Bits((uint16_t)addr, data);
}

static inline uint16_t desig_get_flash_size(void)
{
    return 32U;
}

#endif /* C2000_FLASH_H_INCLUDED */
