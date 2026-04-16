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
 * Equivalent to the Cortex-M SCB AIRCR SYSRESETREQ / libopencm3
 * scb_reset_system(). Mirrors the TI driverlib SysCtl_resetDevice() sequence:
 *
 *   1. EALLOW  — unlock protected register space
 *   2. Write 0x0028 (WDCHK=101, WDDIS=0) to WDCR — re-enables the watchdog
 *      with the valid check-bit pattern so the write is accepted
 *   3. Write 0x0000 (WDCHK=000) to WDCR — invalid check bits cause an
 *      immediate watchdog reset of the entire device
 *   4. EDIS / while(1) — never reached; satisfies static analysis tools
 *
 * WDCR is at word address 0x7029 (WD_BASE=0x7000 + SYSCTL_O_WDCR=0x29).
 * WDCHK occupies bits [5:3]; valid value = 101 (0x0028); any other value
 * with the watchdog enabled triggers an immediate reset.
 *
 * Note: Device_init() calls SysCtl_disableWatchdog(), so the watchdog is
 * disabled by the time we get here. The first write re-enables it before
 * the second write triggers the reset.
 *
 * This function does not return.
 */
static inline void scb_reset_system(void)
{
    volatile uint16_t* wdcr = (volatile uint16_t*)0x7029U;
    __asm(" EALLOW");
    *wdcr = 0x0028U; /* valid key, WDDIS=0 — re-enable watchdog */
    *wdcr = 0x0000U; /* invalid key — triggers immediate reset    */
    __asm(" EDIS");
    while (1) {} /* never reached */
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

/*
 * Software reset — jumps to the C runtime entry point (_c_int00) after
 * disabling interrupts. Correct soft-reset for RAM-loaded C2000 firmware.
 * Implemented in softreset.c (plain C to avoid C++ name mangling of _c_int00).
 */
#ifdef __cplusplus
extern "C" {
#endif
void SoftReset(void);
#ifdef __cplusplus
}
#endif

#endif /* C2000_SYSTEM_H_INCLUDED */
