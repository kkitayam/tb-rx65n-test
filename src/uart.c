/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Koji KITAYAMA */

#include "uart.h"

#include "iodefine.h"

#define SYSTEM_PRCR_PRC1      (1<<1)
#define SYSTEM_PRCR_PRKEY     (0xA5u<<8)
#define SCI_PCLK              60000000

void uart_init(void)
{
    /* Unlock MPC registers */
    MPC.PWPR.BIT.B0WI  = 0;
    MPC.PWPR.BIT.PFSWE = 1;

    /* UART TXD5 => PA4, RXD5 => PA3 */
    PORTA.PMR.BIT.B4 = 1U;
    PORTA.PCR.BIT.B4 = 1U;
    MPC.PA4PFS.BYTE  = 0b01010;
    PORTA.PMR.BIT.B3 = 1U;
    MPC.PA3PFS.BYTE  = 0b01010;

    /* Lock MPC registers */
    MPC.PWPR.BIT.PFSWE = 0;
    MPC.PWPR.BIT.B0WI  = 1;

    /* SCI5 */
    SYSTEM.PRCR.WORD   = SYSTEM_PRCR_PRKEY | SYSTEM_PRCR_PRC1;
    MSTP(SCI5)         = 0;
    SYSTEM.PRCR.WORD   = SYSTEM_PRCR_PRKEY;
    SCI5.SEMR.BIT.ABCS = 1;
    SCI5.SEMR.BIT.BGDM = 1;
    SCI5.SEMR.BIT.BRME = 1;
    SCI5.BRR           = (SCI_PCLK / (8 * 115200)) - 1;
    SCI5.MDDR          = 251;

    SCI5.SCR.BYTE  = 0x30;  // TE/RE
}

void uart_putc(char c)
{
    while (SCI5.SSR.BIT.TDRE == 0) ;
    SCI5.TDR = c;
}

void uart_puts(const char *str)
{
    if (str == NULL) return;
    char c;
    while ((c = *str)) {
        if ('\n' == c) {
            uart_putc('\r');       // CR+LF
        }
        uart_putc(c);
        str++;
    }
    /* wait for completion */
    while (SCI5.SSR.BIT.TEND == 0) ;
}

void _putchar(char character)
{
    if ('\n' == character) {
        uart_putc('\r');       // CR+LF
    }
    uart_putc(character);
}
