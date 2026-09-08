/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Simple CF test: erase -> is_blank true -> write -> verify -> is_blank false
 * Test block address: 0xFFFE0000 (as specified)
 *
 * NOTE: The CF P/E code must be running from RAM (.text.flash_type4_pe_ram).
 * Ensure the section is copied to RAM at startup before calling this test.
 */
#include "flash_type4.h"
#include <string.h>

#define CF_TEST_ADDR  (0xFFFE0000u)
#define CF_BLOCK_SIZE (0x8000u)   /* 32 KiB */
#define CF_TEST_SIZE  (128u)      /* min program unit */

static uint8_t pattern[CF_TEST_SIZE];

int test_code_flash(void)
{
    size_t i;
    for (i = 0; i < CF_TEST_SIZE; i++)
        pattern[i] = (uint8_t)(0x5A ^ i);

    flash_type4_init();

    if (flash_type4_erase(CF_TEST_ADDR, CF_BLOCK_SIZE) < 0)
        return -1;
    if (!flash_type4_is_blank(CF_TEST_ADDR, CF_TEST_SIZE))
        return -2;
    if (flash_type4_write(CF_TEST_ADDR, pattern, CF_TEST_SIZE) != 0)
        return -3;
    if (memcmp((void *)CF_TEST_ADDR, pattern, CF_TEST_SIZE) != 0)
        return -4;
    if (flash_type4_is_blank(CF_TEST_ADDR, CF_TEST_SIZE))
        return -5;
    return 0;
}
