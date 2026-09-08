/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Koji KITAYAMA */

#include <string.h>
#include "system.h"
#include "rx_core.h"
#include "iodefine.h"

void _init(void) {
    /* A dummy function for C runtime initialization */
}

void system_init(void)
{

    FLASH.ROMCIV.WORD = 1;
    while (FLASH.ROMCIV.WORD) ;
    FLASH.ROMCE.WORD = 1;
    while (!FLASH.ROMCE.WORD) ;

    SYSTEM.PRCR.WORD = 0xA503u;
    if (!SYSTEM.RSTSR1.BYTE) {
        RTC.RCR4.BYTE = 0;
        RTC.RCR3.BYTE = 12;
        while (12 != RTC.RCR3.BYTE) ;
    }
    SYSTEM.SOSCCR.BYTE = 1;

    if (SYSTEM.HOCOCR.BYTE) {
        SYSTEM.HOCOCR.BYTE = 0;
        while (!SYSTEM.OSCOVFSR.BIT.HCOVF) ;
    }
    SYSTEM.PLLCR.WORD  = 0x1D10u; /* HOCO x 15 */
    SYSTEM.PLLCR2.BYTE = 0;
    while (!SYSTEM.OSCOVFSR.BIT.PLOVF) ;

    SYSTEM.SCKCR.LONG  = 0x21C11222u;
    SYSTEM.SCKCR2.WORD = 0x0041u;
    SYSTEM.ROMWT.BYTE  = 0x02u;
    while (0x02u != SYSTEM.ROMWT.BYTE) ;
    SYSTEM.SCKCR3.WORD = 0x400u;
    SYSTEM.PRCR.WORD   = 0xA500u;
}

void system_reset(void)
{
RESET:
    SYSTEM.PRCR.WORD = 0xA502;
    SYSTEM.SWRR      = 0xA501;
    SYSTEM.PRCR.WORD = 0xA500;
    goto RESET;
}

void prepare_handoff(void)
{
}

void jump_to_firmware(uint32_t entry_point)
{
    typedef void (*entry_func_t)(void);
    entry_func_t entry = (entry_func_t)entry_point;
    entry();
END:
    __WAIT();
    goto END;
}

void halt(void)
{
END:
    __WAIT();
    goto END;
}

/* 120MHz counter = PCLKA(120MHz) */
void init_cycle_counter(void)
{
    SYSTEM.PRCR.WORD = 0xA502U;
    MSTP(MTU8) = 0U;
    SYSTEM.PRCR.WORD = 0xA500U;

    MTU.TSTRA.BIT.CST8 = 0U;  /* Stop MTU8. */
    MTU8.TMDR1.BYTE = 0U;     /* TCNT counts continuously. */
    MTU8.TCR.BYTE = 0U;       /* PCLKA / 1. */
    MTU8.TCNT = 0U;
    MTU.TSTRA.BIT.CST8 = 1U;  /* Start MTU8. */
}

int memcmp(const void* s1, const void* s2, size_t n)
{
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

void *memmove(void *dest, const void *src, size_t n)
{
    if (dest < src) { /* forward */
        memcpy(dest, src, n);
    } else {          /* backward */
        uint8_t *d = (uint8_t *)dest + n - 1;
        const uint8_t *s = (const uint8_t *)src + n - 1;
        rx_smovb(d, s, n);
    }
    return dest;
}

uint32_t get_cycle_count(void)
{
    return MTU8.TCNT;
}
