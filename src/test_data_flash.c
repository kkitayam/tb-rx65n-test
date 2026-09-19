/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Simple DF test: erase -> is_blank true -> write -> verify -> is_blank false
 */
#include "flash_type4.h"
#include <string.h>

#define DF_TEST_ADDR  (0x00100000u)
#define DF_TEST_SIZE  (64u)

#ifdef PERF
extern void uart_put_digit(const char* s, uint32_t value);
extern uint32_t get_cycle_count(void);
#define PUT_DIGIT(s,v)    uart_put_digit(s,v)
#define GET_CYCLE_COUNT() get_cycle_count()
#else
#define PUT_DIGIT(s,v)
#define GET_CYCLE_COUNT() 0
#endif

static uint8_t pattern[DF_TEST_SIZE];

int test_data_flash(void)
{
    size_t i;
    for (i = 0; i < DF_TEST_SIZE; i++)
        pattern[i] = (uint8_t)(0xA5 ^ i);

    flash_type4_init();

    uint32_t beg, end;
    (void)beg; (void)end;
    beg = GET_CYCLE_COUNT();
    if (flash_type4_erase(DF_TEST_ADDR, DF_TEST_SIZE) < 0)
        return -1;
    end = GET_CYCLE_COUNT();
    PUT_DIGIT("erase: ", end - beg);
    beg = GET_CYCLE_COUNT();
    if (!flash_type4_is_blank(DF_TEST_ADDR, DF_TEST_SIZE))
        return -2;
    end = GET_CYCLE_COUNT();
    PUT_DIGIT("is_blank false: ", end - beg);
    beg = GET_CYCLE_COUNT();
    if (flash_type4_write(DF_TEST_ADDR, pattern, DF_TEST_SIZE) != 0)
        return -3;
    end = GET_CYCLE_COUNT();
    PUT_DIGIT("write: ", end - beg);
    if (memcmp((void *)DF_TEST_ADDR, pattern, DF_TEST_SIZE) != 0)
        return -4;
    beg = GET_CYCLE_COUNT();
    if (flash_type4_is_blank(DF_TEST_ADDR, DF_TEST_SIZE))
        return -5;
    end = GET_CYCLE_COUNT();
    PUT_DIGIT("is_blank true: ", end - beg);
    return 0;
}
