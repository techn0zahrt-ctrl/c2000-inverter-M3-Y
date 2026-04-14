/*
 * C2000 platform wrapper for temp_meas.cpp.
 *
 * On C28x, uint8_t is provided by hw_types.h (via driverlib). Include it
 * first so the type is available when the shared implementation is compiled.
 */
#include "driverlib.h"

#define __TEMP_LU_TABLES
#include "temp_meas.h"

// Pull in the shared implementation directly.
#include "../../../platform/stm32f1/inverter/temp_meas.cpp"
