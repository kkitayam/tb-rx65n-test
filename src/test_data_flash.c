/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Simple DF test: erase -> is_blank true -> write -> verify -> is_blank false
 */
#include "flash_type4.h"
#include <string.h>

#define DF_TEST_ADDR  (0x00100000u)
#define DF_TEST_SIZE  (64u)

static uint8_t pattern[DF_TEST_SIZE];

int test_data_flash(void)
{
    size_t i;
    for (i = 0; i < DF_TEST_SIZE; i++)
        pattern[i] = (uint8_t)(0xA5 ^ i);

    flash_type4_init();

    if (flash_type4_erase(DF_TEST_ADDR, DF_TEST_SIZE) < 0)
        return -1;
    if (!flash_type4_is_blank(DF_TEST_ADDR, DF_TEST_SIZE))
        return -2;
    if (flash_type4_write(DF_TEST_ADDR, pattern, DF_TEST_SIZE) != 0)
        return -3;
    if (memcmp((void *)DF_TEST_ADDR, pattern, DF_TEST_SIZE) != 0)
        return -4;
    if (flash_type4_is_blank(DF_TEST_ADDR, DF_TEST_SIZE))
        return -5;
    return 0;
}
