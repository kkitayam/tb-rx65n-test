/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2014 Renesas Electronics Corporation.
 * Derived from Renesas FIT r_flash_rx (Type 4 / FCU path).
 *
 * Target: RX65N (R5F565NEDDFP) Flash Type 4
 * ICLK 120 MHz / FCLK 60 MHz
 * CF 2 MiB (8/32 KiB blocks by region, 128 B program)
 * DF 32 KiB (64 B blocks, 4 B program)
 */
#include "flash_type4.h"
#include "iodefine.h"
#include <string.h>

/* ---------- constants (RX65N 2 MiB CF / 32 KiB DF) ---------- */
#define CF_START          (0xFFE00000u)
#define CF_END            (0xFFFFFFFFu)
#define CF_BLOCK_8K       (0x2000u)      /* 8 KiB */
#define CF_BLOCK_32K      (0x8000u)      /* 32 KiB */
#define CF_WRITE_UNIT     (128u)

/*
 * 8 Kbyte erase regions (Hardware Manual sec. 59.2):
 *   Linear (BANKMD=111b): blocks 0-7  at 0xFFFF0000-0xFFFFFFFF
 *   Dual   (BANKMD=000b): blocks 0-7  at 0xFFFF0000-0xFFFFFFFF
 *                         blocks 38-45 at 0xFFEE0000-0xFFEFFFFF
 * All other CF blocks are 32 Kbyte.
 */
#define CF_8K_HI_START    (0xFFFF0000u)
#define CF_8K_LO_START    (0xFFEE0000u)
#define CF_8K_REGION      (0x10000u)     /* 64 KiB = 8 x 8 KiB */

#define DF_START          (0x00100000u)
#define DF_END            (0x00107FFFu)
#define DF_BLOCK_SIZE     (64u)
#define DF_WRITE_UNIT     (4u)

#define CONFIGFLASH_ADDR_OFFSET (0xFD800000u)
#define CONFIG_BANKSEL_ADDR (0xFE7F5D20u)

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

/*
 * SWHILE.B (RX software manual):
 *   R1 = start address (updated)
 *   R2 = compare value (0xFF)
 *   R3 = count (updated; 0 means ignored / all matched)
 */
static bool mem_is_blank(const void *addr, size_t nbytes)
{
    bool result;
    register uint32_t ptr __asm__("r1") = (uint32_t)(uintptr_t)addr;
    register uint32_t cmp __asm__("r2") = 0xFFu;
    register uint32_t cnt __asm__("r3") = (uint32_t)nbytes;
    __asm volatile (
        "setpsw Z\n\t"
        "swhile.b\n\t"
        "sceq.l %0"
        : "=r"(result),"+r"(ptr), "+r"(cnt)
        : "r"(cmp), "m"(*(const uint8_t (*)[nbytes])addr)
        : "cc"
    );
    return result;
}

/* ---------- DF write (XIP) ---------- */
/* size is 4-byte multiple; pad short units with 0xFFFF. */
static int df_write(uintptr_t addr, const uint8_t *src, size_t size)
{
    if (pe_enter_df() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    for (size_t done = 0; rc == 0 && done < size; done += DF_WRITE_UNIT) {
        FLASH.FSADDR.LONG = (uint32_t)(addr + done);
        FACI_CMD_AREA.BYTE = FACI_CMD_PROGRAM;
        FACI_CMD_AREA.BYTE = (uint8_t)(DF_WRITE_UNIT / 2);

        const uint16_t *p = (const uint16_t *)(src + done);
        size_t words = (size - done > DF_WRITE_UNIT)
                       ? (DF_WRITE_UNIT / 2)
                       : ((size - done) / 2);
        size_t i;
        for (i = 0; i < words; i++)
            FACI_CMD_AREA.WORD = p[i];
        for (; i < DF_WRITE_UNIT / 2; i++)
            FACI_CMD_AREA.WORD = 0xFFFFu;

        FACI_CMD_AREA.BYTE = FACI_CMD_END;
        rc = wait_frdy(US_TO_LOOPS(2000));
    }
    pe_exit();
    return rc;
}

/* ---------- CF write (RAM section) ---------- */
/* Always programs full 128 B units; short tail padded with 0xFFFF. */
FLASH_TYPE4_PE_RAM_ATTR
static int cf_write(uintptr_t addr, const uint8_t *src, size_t size)
{
    if (pe_enter_cf() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    for (size_t done = 0; rc == 0 && done < size; done += CF_WRITE_UNIT) {
        FLASH.FSADDR.LONG = (uint32_t)(addr + done);
        FACI_CMD_AREA.BYTE = FACI_CMD_PROGRAM;
        FACI_CMD_AREA.BYTE = (uint8_t)(CF_WRITE_UNIT / 2);  /* N = 64 words */

        const uint16_t *p = (const uint16_t *)(src + done);
        size_t words = (size - done > CF_WRITE_UNIT)
                       ? (CF_WRITE_UNIT / 2)
                       : ((size - done) / 2);
        size_t i;
        for (i = 0; i < words; i++)
            FACI_CMD_AREA.WORD = p[i];
        for (; i < CF_WRITE_UNIT / 2; i++)
            FACI_CMD_AREA.WORD = 0xFFFFu;

        FACI_CMD_AREA.BYTE = FACI_CMD_END;
        rc = wait_frdy_ram(US_TO_LOOPS(5000));
    }
    pe_exit_ram();
    return rc;
}

/* ---------- erase: start/end already block-aligned by caller ---------- */
/* DF blank check while already in DF P/E. true = blank. */
static bool df_blank_check_pe(uintptr_t start, uintptr_t end)
{
    FLASH.FBCCNT.BIT.BCDIR = 0;
    FLASH.FSADDR.LONG = (uint32_t)start;
    FLASH.FEADDR.LONG = (uint32_t)(end - 1);
    FACI_CMD_AREA.BYTE = FACI_CMD_BLANK;
    FACI_CMD_AREA.BYTE = FACI_CMD_END;
    if (wait_frdy(US_TO_LOOPS(10000)) != 0)
        return false;
    return (FLASH.FBCSTAT.BIT.BCST == 0);
}

/*
 * One DF P/E session: blank-check then erase only if needed.
 * FACI allows consecutive commands without leaving P/E mode.
 */
static int df_erase(uintptr_t start, uintptr_t end)
{
    if (pe_enter_df() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    if (!df_blank_check_pe(start, end)) {
        for (; rc == 0 && start < end; start += DF_BLOCK_SIZE) {
            FLASH.FSADDR.LONG = (uint32_t)start;
            FACI_CMD_AREA.BYTE = FACI_CMD_ERASE;
            FACI_CMD_AREA.BYTE = FACI_CMD_END;
            rc = wait_frdy(US_TO_LOOPS(5000));
        }
    }
    pe_exit();
    return rc;
}

/* dual: true if BANKMD[2:0]==000b. Pure helper (no OFSM). */
static size_t cf_block_size_at(uintptr_t addr, bool dual)
{
    if (addr >= CF_8K_HI_START)
        return CF_BLOCK_8K;
    if (dual
        && addr >= CF_8K_LO_START
        && addr < (CF_8K_LO_START + CF_8K_REGION))
        return CF_BLOCK_8K;
    return CF_BLOCK_32K;
}

static uintptr_t cf_block_start(uintptr_t addr, bool dual)
{
    size_t bs = cf_block_size_at(addr, dual);
    return addr & ~(uintptr_t)(bs - 1u);
}

/*
 * Bytes from block-start of `first` through end of block containing `last`.
 * Avoids exclusive-end pointers past 0xFFFFFFFF (32-bit overflow).
 */
static size_t cf_aligned_span(uintptr_t first, uintptr_t last, bool dual)
{
    uintptr_t s = cf_block_start(first, dual);
    uintptr_t e = cf_block_start(last, dual);
    return (size_t)(e - s) + cf_block_size_at(last, dual);
}

/*
 * Erase CF blocks covering `total` bytes from `start` (block-aligned).
 * dual must be resolved from OFSM *before* pe_enter_cf.
 * Branching is outside the per-block for; each homogeneous region uses a fixed step.
 * Uses remaining-byte count (not exclusive end) so the top 8K region cannot wrap to 0.
 */
FLASH_TYPE4_PE_RAM_ATTR
static int cf_erase(uintptr_t start, size_t total, bool dual)
{
    /* Read mode only: must run before pe_enter_cf. */
    if (mem_is_blank((const void *)start, total))
        return 0;

    if (pe_enter_cf() != 0)
        return FLASH_TYPE4_ERR_FAILURE;

    int rc = 0;
    uintptr_t addr = start;
    size_t done = 0;

    while (rc == 0 && done < total) {
        size_t bs;
        size_t region_left;

        if (addr >= CF_8K_HI_START) {
            bs = CF_BLOCK_8K;
            region_left = total - done;
        } else if (dual
                   && addr >= CF_8K_LO_START
                   && addr < (CF_8K_LO_START + CF_8K_REGION)) {
            bs = CF_BLOCK_8K;
            region_left = (CF_8K_LO_START + CF_8K_REGION) - addr;
            if (region_left > total - done)
                region_left = total - done;
        } else {
            bs = CF_BLOCK_32K;
            uintptr_t boundary = (dual && addr < CF_8K_LO_START)
                                 ? CF_8K_LO_START
                                 : CF_8K_HI_START;
            region_left = boundary - addr;
            if (region_left > total - done)
                region_left = total - done;
        }

        for (; rc == 0 && region_left > 0; region_left -= bs, done += bs, addr += bs) {
            FLASH.FSADDR.LONG = (uint32_t)addr;
            FACI_CMD_AREA.BYTE = FACI_CMD_ERASE;
            FACI_CMD_AREA.BYTE = FACI_CMD_END;
            rc = wait_frdy_ram(US_TO_LOOPS(30000));
        }
    }
    pe_exit_ram();
    return rc;
}

/* DF blank: always HW (erased value undefined). start/end 4-byte aligned, in range. */
static bool df_is_blank_hw(uintptr_t start, uintptr_t end)
{
    if (pe_enter_df() != 0)
        return false;
    bool blank = df_blank_check_pe(start, end);
    pe_exit();
    return blank;
}

/* Configuration set (CF P/E mode, 16-byte unit) */
FLASH_TYPE4_PE_RAM_ATTR
static int cf_config_set(uintptr_t addr, const uint16_t words[8])
{
    int err;

    err = pe_enter_cf();
    if (err) return FLASH_TYPE4_ERR_FAILURE;

    const uint32_t fsaddr = ((uint32_t)addr - CONFIGFLASH_ADDR_OFFSET) & ~(sizeof(uint16_t) * FACI_CONFIG_WORDS - 1);

    FLASH.FSADDR.LONG = fsaddr;
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
    /* size: 4-byte units; short HW program units are padded with 0xFFFF */
    if ((size & 3u) != 0)
        return FLASH_TYPE4_ERR_ALIGN;

    if (address >= DF_START && (address + size - 1) <= DF_END) {
        if ((address & (DF_WRITE_UNIT - 1)) != 0)
            return FLASH_TYPE4_ERR_ALIGN;
        return df_write(address, (const uint8_t *)data, size);
    }

    if (address >= CF_START && (address + size - 1) <= CF_END) {
        /* address still on CF program boundary (128 B) */
        if ((address & (CF_WRITE_UNIT - 1)) != 0)
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
        /* OFSM must be read before CF P/E mode */
        bool dual = (OFSM.MDE.BIT.BANKMD == 0);
        uintptr_t last = address + size - 1u;
        if (last < address || last > CF_END)
            return FLASH_TYPE4_ERR_PARAM;
        uintptr_t start = cf_block_start(address, dual);
        size_t total = cf_aligned_span(address, last, dual);
        int rc = cf_erase(start, total, dual);
        if (rc != 0)
            return rc;
        return (int)total;
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
        return mem_is_blank((const void *)address, size);

    return false;
}

int flash_type4_swap_bank(void)
{
    union ConfigData {
        uint32_t LONGS[4];
        uint16_t WORDS[8];
    };
    uint32_t mde = OFSM.MDE.LONG;
    if (mde & 0x70) {
        /* if linear mode is set, switch into dual mode to enable bank swap. */
        const union ConfigData opts = { .LONGS = { mde & ~0x70, OFSM.OFS0.LONG, OFSM.OFS1.LONG, 0xFFFFFFFF} };
        int err;
        err = cf_config_set((uintptr_t)&OFSM.MDE, opts.WORDS);
        if (err) return FLASH_TYPE4_ERR_FAILURE;
    }
    const uint32_t bankswp = OFSM.BANKSEL.BIT.BANKSWP;
    const union ConfigData banksel = { .LONGS = { bankswp ^ 7, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF } };
    return cf_config_set((uintptr_t)&OFSM.BANKSEL, banksel.WORDS);
}
