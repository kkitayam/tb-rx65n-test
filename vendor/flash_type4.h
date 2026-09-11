/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2014 Renesas Electronics Corporation.
 * Derived from Renesas FIT r_flash_rx.
 */
#ifndef FLASH_TYPE4_H
#define FLASH_TYPE4_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes (negative) */
#define FLASH_TYPE4_ERR_PARAM   (-1)
#define FLASH_TYPE4_ERR_TIMEOUT (-3)
#define FLASH_TYPE4_ERR_FAILURE (-4)
#define FLASH_TYPE4_ERR_ALIGN   (-5)

#ifndef FLASH_TYPE4_ICLK_MHZ
#define FLASH_TYPE4_ICLK_MHZ (120)
#endif
#ifndef FLASH_TYPE4_FCLK_MHZ
#define FLASH_TYPE4_FCLK_MHZ (60)
#endif

/* Enable DF access etc. Call once from main. */
void flash_type4_init(void);

/* CF/DF auto. 0 success, negative error. */
int flash_type4_write(uintptr_t address, const void *data, size_t size);

/* size floored to erase unit. Returns bytes erased or negative error. */
int flash_type4_erase(uintptr_t address, size_t size);

/* [address, address+size) blank -> true. Error/out-of-range -> false.
 * DF: always HW blank check. CF: software (SWHILE.B). */
bool flash_type4_is_blank(uintptr_t address, size_t size);

/* Swap the current flash bank if dual-mode enabled. Returns 0 on success, negative error code on failure. */
int flash_type4_swap_bank(void);

#ifdef __cplusplus
}
#endif
#endif /* FLASH_TYPE4_H */
