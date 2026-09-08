#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#include "system.h"
#include "uart.h"
#include "flash_type4.h"

extern int test_data_flash(void);
extern int test_code_flash(void);

static void uart_put_uint(const char* s, uint32_t value, bool negative)
{
    char buf[32]; /* int32_t max is 11 digits + sign + null terminator */
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';

    do {
        *--p = '0' + (value % 10);
        value /= 10;
    } while (value != 0);

    if (negative) {
        *--p = '-';
    }

    uart_puts(s);
    uart_puts(p);
    uart_putc('\n');
}

void uart_put_digit(const char* s, uint32_t value)
{
    uart_put_uint(s, value, false);
}

void uart_put_int(const char* s, int value)
{
    bool negative = value < 0;
    unsigned u = negative ? -(unsigned)value : (unsigned)value;
    uart_put_uint(s, u, negative);
}

int main(void)
{
    int err;
    uint32_t last, cur;

    init_cycle_counter();
    uart_init();
    uart_puts("Hello UART\n");

    flash_type4_init();

    last = get_cycle_count();
    uart_put_digit("Last count: ", last);

    last = get_cycle_count();
    err = test_data_flash();
    cur = get_cycle_count();
    if (err) { uart_put_int("fail test_data_flash: ", err); return -1; }
    uart_put_digit("pass test_data_flash: ", cur - last);

    last = get_cycle_count();
    err = test_code_flash();
    cur = get_cycle_count();
    if (err) { uart_put_int("fail test_code_flash: ", err); return -1; }
    uart_put_digit("pass test_code_flash: ", cur - last);

    return 0;
}

void __libc_init_array()
{
    main();
}
