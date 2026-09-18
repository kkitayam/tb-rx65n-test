/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CF tests: 32 KiB region and 8 KiB region.
 * Each: erase -> is_blank true -> write -> verify -> is_blank false
 *
 * NOTE: CF P/E code must run from RAM (.text.flash_type4_pe_ram).
 * Copy that section to RAM at startup before calling these tests.
 *
 * 32K test: 0xFFFE0000 (linear/dual: 32K block area)
 * 8K  test: 0xFFFF0000 (block 7, 8K; avoids block 0 reset vectors)
 */
#include "flash_type4.h"
#include <string.h>

#define CF_TEST_32K_ADDR  (0xFFFE0000u)
#define CF_TEST_32K_ERASE (0x8000u)   /* 32 KiB */
#define CF_TEST_8K_ADDR   (0xFFFF0000u)
#define CF_TEST_8K_ERASE  (0x2000u)   /* 8 KiB */
#define CF_TEST_SIZE      (128u)      /* min program unit */

static uint8_t pattern[CF_TEST_SIZE];

static int cf_region_test(uintptr_t addr, size_t erase_size)
{
    for (size_t i = 0; i < CF_TEST_SIZE; i++)
        pattern[i] = (uint8_t)(0x5A ^ (unsigned)i);

    int erased = flash_type4_erase(addr, erase_size);
    if (erased < 0)
        return -1;
    if ((size_t)erased < erase_size)
        return -2;
    if (!flash_type4_is_blank(addr, CF_TEST_SIZE))
        return -3;
    if (flash_type4_write(addr, pattern, CF_TEST_SIZE) != 0)
        return -4;
    if (memcmp((const void *)addr, pattern, CF_TEST_SIZE) != 0)
        return -5;
    if (flash_type4_is_blank(addr, CF_TEST_SIZE))
        return -6;
    return 0;
}

/* 32 KiB block region */
int test_code_flash_32k(void)
{
    flash_type4_init();
    return cf_region_test(CF_TEST_32K_ADDR, CF_TEST_32K_ERASE);
}

/* 8 KiB block region (high-end blocks 0-7) */
int test_code_flash_8k(void)
{
    flash_type4_init();
    return cf_region_test(CF_TEST_8K_ADDR, CF_TEST_8K_ERASE);
}

/* Run both; return 0 if all pass, else first error (8k errors offset by -10) */
int test_code_flash(void)
{
    int rc = test_code_flash_32k();
    if (rc != 0)
        return rc;
    rc = test_code_flash_8k();
    if (rc != 0)
        return rc - 10;
    return 0;
}
