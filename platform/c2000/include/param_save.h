/*
 * This file is part of the libopeninv project.
 *
 * Copyright (C) 2011 Johannes Huebner <dev@johanneshuebner.com>
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
 * C2000 platform version of the parameter save/load interface.
 * Adapted from the STM32F1 version; STM32-specific includes replaced
 * with C2000 equivalents.
 */
#ifndef PARAM_SAVE_H_INCLUDED
#define PARAM_SAVE_H_INCLUDED

#include <stdint.h>
#include "hwdefs.h"       /* PARAM_BLKNUM, CAN_BLKNUM, FLASH_PAGE_SIZE, etc. */
#include "c2000_flash.h"  /* flash_unlock/lock/erase/program wrappers */
#include "c2000_crc.h"    /* crc_reset, crc_calculate, crc_calculate_block */

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Save all parameters to flash.
 *
 * Erases the parameter flash block and writes current parameter values
 * followed by a CRC-32 checksum. Returns the number of parameters saved,
 * or 0 on error.
 */
uint32_t parm_save(void);

/*
 * Load parameters from flash.
 *
 * Reads parameter values from the parameter flash block, verifies the
 * CRC-32 checksum, and restores parameter values. Returns 0 on success,
 * a negative value on CRC mismatch or empty flash.
 */
int parm_load(void);

#ifdef __cplusplus
}
#endif

#endif /* PARAM_SAVE_H_INCLUDED */
