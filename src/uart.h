/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Koji KITAYAMA */

#ifndef UART_H
#define UART_H

#include <stddef.h>

extern void uart_init(void);
extern void uart_putc(char c);
extern void uart_puts(const char* str);

#endif /* UART_H */
