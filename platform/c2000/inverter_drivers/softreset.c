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

/*
 * C2000 software reset via the RTS entry point.
 *
 * Must be a .c file (not .cpp) so that the extern declaration of _c_int00
 * uses plain C linkage with no name mangling. The TI RTS symbol is exactly
 * "_c_int00" and must not be mangled by the C++ compiler.
 */

#include "driverlib.h"

/* RTS C runtime entry point — defined in the TI ROM-based runtime library */
extern void _c_int00(void);

/**
 * Restart firmware execution from the beginning without a hardware reset.
 *
 * Disables all maskable interrupts then jumps to the C runtime startup entry
 * point, which re-initialises .bss/.data and calls main(). This is the
 * correct soft-reset mechanism for RAM-loaded C2000 firmware where the
 * hardware watchdog reset path is unavailable.
 */
void SoftReset(void)
{
    DINT;
    (*_c_int00)();
}
