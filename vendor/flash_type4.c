/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2014 Renesas Electronics Corporation.
 * Derived from Renesas FIT r_flash_rx (Type 4 / FCU path).
 *
 * Target: RX65N (R5F565NEDDFP) Flash Type 4
 * ICLK 120 MHz / FCLK 60 MHz
 * CF 2 MiB (32 KiB blocks, 128 B program)
 * DF 32 KiB (64 B blocks, 4 B program)
 */
#include "flash_type4.h"
#include "iodefine.h"

/* ---------- constants (RX65N 2 MiB CF / 32 KiB DF) ---------- */
#define CF_START          (0xFFE00000u)
#define CF_END            (0xFFFFFFFFu)
#define CF_BLOCK_SIZE     (0x8000u)      /* 32 KiB */
#define CF_WRITE_UNIT     (128u)

#define DF_START          (0x00100000u)
#define DF_END            (0x00107FFFu)
#define DF_BLOCK_SIZE     (64u)
#define DF_WRITE_UNIT     (4u)

#define FACI_CMD_PROGRAM      (0xE8u)
#define FACI_CMD_ERASE        (0x20u)
#define FACI_CMD_BLANK        (0x71u)
#define FACI_CMD_CONFIG       (0x40u)
#define FACI_CMD_FORCED_STOP  (0xB3u)
#define FACI_CMD_END          (0xD0u)

#define FACI_CONFIG_WORDS     (8u)       /* N = 8, 16 bytes */

/* FACI command-issuing area @ 0x007E0000 (iodefine-style, no cast at use site) */
#define FACI_CMD_AREA \
    (*(volatile union { uint8_t BYTE; uint16_t WORD; uint32_t LONG; } *)0x007E0000u)

/* CF P/E code section (change name here if needed) */
#define FLASH_TYPE4_PE_RAM_ATTR \
    __attribute__((section(".text.flash_type4_pe_ram"), noinline))

/* rough busy-loop: ~4 cycles/loop @ ICLK */
#define US_TO_LOOPS(us)   ((uint32_t)((us) * (FLASH_TYPE4_ICLK_MHZ / 4)))

/* ========================================================================== */
/* Internal helpers                                                           */
/* ========================================================================== */

static int wait_frdy(uint32_t loops)
{
    while (FLASH.FSTATR.BIT.FRDY == 0) {
        if (loops-- == 0)
            return FLASH_TYPE4_ERR_TIMEOUT;
    }
    if (FLASH.FASTAT.BIT.CMDLK)
        return FLASH_TYPE4_ERR_FAILURE;
    return 0;
}

static void pe_exit(void)
{
    FLASH.FENTRYR.WORD = 0xAA00;
    while (FLASH.FENTRYR.WORD != 0x0000)
        ;
}

static int pe_enter_df(void)
{
    FLASH.FENTRYR.WORD = 0xAA80;
    while (FLASH.FENTRYR.WORD != 0x0080)
        ;
    return 0;
}

FLASH_TYPE4_PE_RAM_ATTR
static int pe_enter_cf(void)
{
    FLASH.FENTRYR.WORD = 0xAA01;
    while (FLASH.FENTRYR.WORD != 0x0001)
        ;
    return 0;
}

FLASH_TYPE4_PE_RAM_ATTR
static int wait_frdy_ram(uint32_t loops)
{
    while (FLASH.FSTATR.BIT.FRDY == 0) {
        if (loops-- == 0)
            return FLASH_TYPE4_ERR_TIMEOUT;
    }
    if (FLASH.FASTAT.BIT.CMDLK)
        return FLASH_TYPE4_ERR_FAILURE;
    return 0;
}

FLASH_TYPE4_PE_RAM_ATTR
static void pe_exit_ram(void)
{
    FLASH.FENTRYR.WORD = 0xAA00;
    while (FLASH.FENTRYR.WORD != 0x0000)
        ;
}

/* Return 0 if all bytes are 0xFF, 1 if not. Uses RX SWHILE.B. */
/*
 * SWHILE.B (RX software manual):
 *   R1 = start address (updated)
 *   R2 = compare value (0xFF)
 *   R3 = count (updated; 0 means ignored / all matched)
 */
static int mem_is_blank(const void *addr, size_t nbytes)
{
    register uint32_t ptr __asm__("r1") = (uint32_t)(uintptr_t)addr;
    register uint32_t cmp __asm__("r2") = 0xFFu;
    register uint32_t cnt __asm__("r3") = (uint32_t)nbytes;
    __asm volatile (
        "swhile.b"
        : "+r"(ptr), "+r"(cnt)
        : "r"(cmp), "m"(*(const uint8_t (*)[nbytes])addr)
        : "cc"
    );
    return (cnt == 0u) ? 0 : 1;
}

/* ---------- DF write (XIP) ---------- */
static int df_write(uintptr_t addr, const uint8_t *src, size_t size)
{
    if (pe_enter_df() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    for (size_t done = 0; rc == 0 && done < size; ) {
        size_t n = DF_WRITE_UNIT;
        if (done + n > size)
            n = size - done;

        FLASH.FSADDR.LONG = (uint32_t)(addr + done);
        FACI_CMD_AREA.BYTE = FACI_CMD_PROGRAM;
        FACI_CMD_AREA.BYTE = (uint8_t)(n / 2);

        const uint16_t *p = (const uint16_t *)(src + done);
        for (size_t i = 0; i < n / 2; i++)
            FACI_CMD_AREA.WORD = p[i];

        FACI_CMD_AREA.BYTE = FACI_CMD_END;
        rc = wait_frdy(US_TO_LOOPS(2000));
        done += n;
    }
    pe_exit();
    return rc;
}

/* ---------- CF write (RAM section) ---------- */
FLASH_TYPE4_PE_RAM_ATTR
static int cf_write(uintptr_t addr, const uint8_t *src, size_t size)
{
    if (pe_enter_cf() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    for (size_t done = 0; rc == 0 && done < size; ) {
        size_t n = CF_WRITE_UNIT;
        if (done + n > size)
            n = size - done;

        FLASH.FSADDR.LONG = (uint32_t)(addr + done);
        FACI_CMD_AREA.BYTE = FACI_CMD_PROGRAM;
        FACI_CMD_AREA.BYTE = (uint8_t)(n / 2);

        const uint16_t *p = (const uint16_t *)(src + done);
        for (size_t i = 0; i < n / 2; i++)
            FACI_CMD_AREA.WORD = p[i];

        FACI_CMD_AREA.BYTE = FACI_CMD_END;
        rc = wait_frdy_ram(US_TO_LOOPS(5000));
        done += n;
    }
    pe_exit_ram();
    return rc;
}

/* ---------- erase: start/end already block-aligned by caller ---------- */
static int df_erase(uintptr_t start, uintptr_t end)
{
    if (pe_enter_df() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    for (; rc == 0 && start < end; start += DF_BLOCK_SIZE) {
        FLASH.FSADDR.LONG = (uint32_t)start;
        FACI_CMD_AREA.BYTE = FACI_CMD_ERASE;
        FACI_CMD_AREA.BYTE = FACI_CMD_END;
        rc = wait_frdy(US_TO_LOOPS(5000));
    }
    pe_exit();
    return rc;
}

FLASH_TYPE4_PE_RAM_ATTR
static int cf_erase(uintptr_t start, uintptr_t end)
{
    if (pe_enter_cf() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    for (; rc == 0 && start < end; start += CF_BLOCK_SIZE) {
        FLASH.FSADDR.LONG = (uint32_t)start;
        FACI_CMD_AREA.BYTE = FACI_CMD_ERASE;
        FACI_CMD_AREA.BYTE = FACI_CMD_END;
        rc = wait_frdy_ram(US_TO_LOOPS(30000));
    }
    pe_exit_ram();
    return rc;
}

/* DF blank: always HW (erased value undefined). start/end 4-byte aligned, in range. */
static bool df_is_blank_hw(uintptr_t start, uintptr_t end)
{
    if (pe_enter_df() != 0)
        return false;

    FLASH.FBCCNT.BIT.BCDIR = 0;
    FLASH.FSADDR.LONG = (uint32_t)start;
    FLASH.FEADDR.LONG = (uint32_t)(end - 1);

    FACI_CMD_AREA.BYTE = FACI_CMD_BLANK;
    FACI_CMD_AREA.BYTE = FACI_CMD_END;

    int rc = wait_frdy(US_TO_LOOPS(10000));
    pe_exit();

    if (rc != 0)
        return false;
    return (FLASH.FBCSTAT.BIT.BCST == 0);
}

/* Configuration set (CF P/E mode, 16-byte unit) */
FLASH_TYPE4_PE_RAM_ATTR
static int cf_config_set(uintptr_t fsaddr, const uint16_t *words)
{
    if (pe_enter_cf() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    FLASH.FSADDR.LONG = (uint32_t)fsaddr;
    FACI_CMD_AREA.BYTE = FACI_CMD_CONFIG;
    FACI_CMD_AREA.BYTE = (uint8_t)FACI_CONFIG_WORDS;

    for (size_t i = 0; i < FACI_CONFIG_WORDS; i++)
        FACI_CMD_AREA.WORD = words[i];

    FACI_CMD_AREA.BYTE = FACI_CMD_END;

    int rc = wait_frdy_ram(US_TO_LOOPS(10000));
    pe_exit_ram();
    return rc;
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

void flash_type4_init(void)
{
    FLASH.FWEPROR.BYTE = 0x01;
    FLASH.FPCKAR.WORD = (uint16_t)((FLASH_TYPE4_FCLK_MHZ << 8) | 0x1E);
    FLASH.EEPFCLK = (uint8_t)FLASH_TYPE4_FCLK_MHZ;

    if (FLASH.FENTRYR.WORD != 0x0000) {
        FLASH.FENTRYR.WORD = 0xAA00;
        while (FLASH.FENTRYR.WORD != 0x0000)
            ;
    }
}

int flash_type4_write(uintptr_t address, const void *data, size_t size)
{
    if (data == 0 || size == 0)
        return FLASH_TYPE4_ERR_PARAM;

    if (address >= DF_START && (address + size - 1) <= DF_END) {
        if ((address & (DF_WRITE_UNIT - 1)) != 0 || (size & (DF_WRITE_UNIT - 1)) != 0)
            return FLASH_TYPE4_ERR_ALIGN;
        return df_write(address, (const uint8_t *)data, size);
    }

    if (address >= CF_START && (address + size - 1) <= CF_END) {
        if ((address & (CF_WRITE_UNIT - 1)) != 0 || (size & (CF_WRITE_UNIT - 1)) != 0)
            return FLASH_TYPE4_ERR_ALIGN;
        return cf_write(address, (const uint8_t *)data, size);
    }

    return FLASH_TYPE4_ERR_PARAM;
}

int flash_type4_erase(uintptr_t address, size_t size)
{
    if (size == 0)
        return 0;

    if (address >= DF_START && address <= DF_END) {
        uintptr_t start = address & ~(DF_BLOCK_SIZE - 1);
        uintptr_t end   = (address + size + DF_BLOCK_SIZE - 1) & ~(DF_BLOCK_SIZE - 1);
        if (end - 1 > DF_END)
            return FLASH_TYPE4_ERR_PARAM;
        int rc = df_erase(start, end);
        if (rc != 0)
            return rc;
        return (int)(end - start);
    }

    if (address >= CF_START && address <= CF_END) {
        uintptr_t start = address & ~(CF_BLOCK_SIZE - 1);
        uintptr_t end   = (address + size + CF_BLOCK_SIZE - 1) & ~(CF_BLOCK_SIZE - 1);
        if (end - 1 > CF_END)
            return FLASH_TYPE4_ERR_PARAM;
        int rc = cf_erase(start, end);
        if (rc != 0)
            return rc;
        return (int)(end - start);
    }

    return FLASH_TYPE4_ERR_PARAM;
}

bool flash_type4_is_blank(uintptr_t address, size_t size)
{
    if (size == 0)
        return true;

    if (address >= DF_START && (address + size - 1) <= DF_END) {
        uintptr_t start = address & ~0x3u;
        uintptr_t end   = (address + size + 3u) & ~0x3u;
        if (end <= start || end - 1 > DF_END)
            return false;
        return df_is_blank_hw(start, end);
    }

    if (address >= CF_START && (address + size - 1) <= CF_END)
        return mem_is_blank((const void *)address, size) == 0;

    return false;
}

int flash_type4_config_set(uintptr_t address, const void *data)
{
    if (data == 0)
        return FLASH_TYPE4_ERR_PARAM;
    return cf_config_set(address, (const uint16_t *)data);
}
