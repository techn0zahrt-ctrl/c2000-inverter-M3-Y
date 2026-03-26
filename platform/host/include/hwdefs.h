#ifndef HWDEFS_H_INCLUDED
#define HWDEFS_H_INCLUDED

#include <stdint.h>
#include <string.h>

/*
 * Host build stubs for flash and CRC functions used by canmap.cpp.
 * On the host these are no-ops — flash save/load is never actually
 * called in unit tests.
 */

#define FLASH_PAGE_SIZE   0x400U
#define FLASH_BASE        0x0000UL
#define CAN1_BLKNUM       2U

static inline void     flash_unlock(void) {}
static inline void     flash_lock(void) {}
static inline void     flash_set_ws(uint32_t) {}
static inline void     flash_erase_page(uint32_t) {}
static inline void     flash_program_word(uint32_t, uint32_t) {}
static inline uint16_t desig_get_flash_size(void) { return 32U; }

static uint32_t _host_crc;
static inline void     crc_reset(void) { _host_crc = 0xFFFFFFFFUL; }
static inline uint32_t crc_calculate(uint32_t) { return _host_crc; }
static inline uint32_t crc_calculate_block(uint32_t*, uint32_t) { return _host_crc; }

/* Stubs for sdocommands.cpp */
#define DESIG_UNIQUE_ID0  0x12345678UL
#define DESIG_UNIQUE_ID1  0x9ABCDEF0UL
#define DESIG_UNIQUE_ID2  0x00000000UL

static inline void scb_reset_system(void) {}
static inline void cm_disable_interrupts(void) {}
static inline void cm_enable_interrupts(void) {}
#endif /* HWDEFS_H_INCLUDED */
