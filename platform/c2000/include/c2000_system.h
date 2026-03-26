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
 * C2000 system-level wrappers matching libopencm3 CM3/STM32 conventions.
 *
 * Provides:
 *   scb_reset_system()         — software reset via direct WDCR register write
 *   cm_disable_interrupts()    — global interrupt disable (DINT)
 *   cm_enable_interrupts()     — global interrupt enable  (EINT)
 *   DESIG_UNIQUE_ID0/1/2       — device identification registers
 *
 * No driverlib header is required. The C28x watchdog control register (WDCR)
 * is at address 0x7029. Writing an invalid key with the watchdog not disabled
 * triggers an immediate device reset. The valid key field [7:3] is 0x05
 * (0x05 << 3 = 0x28); any other value with the WD enabled causes a reset.
 */
#ifndef C2000_SYSTEM_H_INCLUDED
#define C2000_SYSTEM_H_INCLUDED

#include <stdint.h>

/*
 * Trigger a full device software reset.
 *
 * Equivalent to the Cortex-M SCB AIRCR SYSRESETREQ mechanism used by
 * libopencm3's scb_reset_system(). Writes an invalid key to the C28x
 * watchdog control register (WDCR, address 0x7029), which forces an
 * immediate watchdog reset of the entire device.
 *
 * This function does not return.
 */
static inline void scb_reset_system(void)
{
    /* Write invalid key to WDCR to force watchdog reset.
     * WDCR is a 16-bit register at C28x data-space address 0x7029.
     * Valid key = 0x05 in bits [7:3]; 0x0028 has key=0 which is invalid. */
    volatile unsigned int *wdcr = (volatile unsigned int *)0x7029U;
    *wdcr = 0x0028U;
    /* Should not reach here; add infinite loop for static analysis tools */
    while (1) {}
}

/*
 * Disable all maskable CPU interrupts globally.
 *
 * Maps to the C28x DINT instruction (sets INTM=1 in ST1, masking all
 * maskable interrupts). Equivalent to Cortex-M __disable_irq().
 */
//#define cm_disable_interrupts()  DINT
#define cm_disable_interrupts()  __asm(" DINT")

/*
 * Enable all maskable CPU interrupts globally.
 *
 * Maps to the C28x EINT instruction (clears INTM in ST1, unmasking
 * interrupts). Equivalent to Cortex-M __enable_irq().
 */
//#define cm_enable_interrupts()   EINT
#define cm_enable_interrupts()   __asm(" EINT")

/*
 * Device identification — libopencm3 DESIG_UNIQUE_ID compatibility.
 *
 * The F28377D has no dedicated 96-bit unique ID like the STM32. We use
 * the device part number registers as a stable identifier:
 *
 *   PARTIDL (0x5D228): lower 32 bits of part identification
 *   PARTIDH (0x5D22A): upper 32 bits of part identification
 *
 * These are read-only registers set at manufacture. ID2 is fixed at 0
 * since the F28377D provides only a 64-bit part number, not 96 bits.
 *
 * Note: C28x addresses are 16-bit word addresses. 0x5D228 and 0x5D22A
 * are word addresses; the cast to uint32_t* reads two consecutive words.
 */
#define DESIG_UNIQUE_ID0  (*(volatile uint32_t*)0x5D228UL)
#define DESIG_UNIQUE_ID1  (*(volatile uint32_t*)0x5D22AUL)
#define DESIG_UNIQUE_ID2  0UL

#endif /* C2000_SYSTEM_H_INCLUDED */
