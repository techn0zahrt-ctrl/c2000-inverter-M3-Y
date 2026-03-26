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
#ifndef HWDEFS_H_INCLUDED
#define HWDEFS_H_INCLUDED

/*
 * C2000 platform header (provides device.h, driverlib.h, etc.)
 */
//#include "device.h"

/*
 * Parameter and CAN map storage address layout — Microchip 25LC256 SPI EEPROM
 * ============================================================================
 *
 * NOTE: These addresses are EEPROM byte addresses (0x0000–0x7FFF), NOT
 * C2000 internal flash word addresses. The "flash" abstraction layer in
 * c2000_flash.h routes all erase/program calls to c2000::EEPROM instead
 * of the internal C2000 flash, so the address space here is the 25LC256's
 * 32KB byte address space, not the C28x memory map.
 *
 * Blocks are numbered 1-based from the top of the EEPROM address space,
 * matching the STM32 convention of placing persistent storage in the last
 * pages of flash:
 *
 *   EEPROM top (exclusive): 0x8000  (32KB = 32768 bytes)
 *   Block address = EEPROM_TOP - blknum * FLASH_PAGE_SIZE
 *
 *   PARAM block (blknum=1): 0x8000 - 0x0200 = 0x7E00
 *   CAN   block (blknum=2): 0x8000 - 0x0400 = 0x7C00
 *
 * Both addresses fit comfortably within the 25LC256's 0x0000–0x7FFF range.
 * Each block is 512 bytes, well within the chip's 32KB capacity.
 */

/** Storage block size: 1024 bytes (fits within 25LC256's 32KB address space) */
#define FLASH_PAGE_SIZE   0x400U

/** Block number (from top of EEPROM) used for parameter storage */
#define PARAM_BLKNUM      1U

/** Block number (from top of EEPROM) used for CAN map storage */
#define CAN_BLKNUM        2U

/** Size of the parameter storage block */
#define PARAM_BLKSIZE     FLASH_PAGE_SIZE

/** Size of the CAN map storage block */
#define CAN_BLKSIZE       FLASH_PAGE_SIZE

/** Top of 25LC256 EEPROM address space (exclusive upper bound, 32KB) */
#define EEPROM_TOP        0x8000UL

/** Base EEPROM byte address of the parameter storage block */
#define PARAM_FLASH_ADDR  (EEPROM_TOP - (PARAM_BLKNUM * FLASH_PAGE_SIZE))

/** Base EEPROM byte address of the CAN map storage block */
#define CAN_FLASH_ADDR    (EEPROM_TOP - (CAN_BLKNUM * FLASH_PAGE_SIZE))

/** FLASH_BASE = 0 since EEPROM addresses are absolute byte addresses */
#define FLASH_BASE        0x0000UL

/** CAN map block number from top of storage — matches CAN_BLKNUM */
#define CAN1_BLKNUM       CAN_BLKNUM
#endif /* HWDEFS_H_INCLUDED */
