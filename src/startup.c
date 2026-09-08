#include "rx_core.h"
#include <stdint.h>

/* Symbols defined by the linker script */
extern const uint8_t _isp_top[];
extern const uint8_t _usp_top[];
extern const uint8_t _text_load[];
extern const uint8_t _data_load[];
extern uint8_t _text[];
extern uint8_t _etext[];
extern uint8_t _data[];
extern uint8_t _edata[];
extern uint8_t _bss[];
extern uint8_t _ebss[];

extern void system_init(void) __attribute__((section(".text.reset")));
extern int main(void);
extern void *memcpy(void*,const void*,size_t);
extern void *memset(void*,int,size_t);

void Reset_Handler(void) __attribute__((section(".text.reset"),noinline));

void Reset_Handler(void)
{
    __MVTC(ISP, (uintptr_t)_isp_top);
    __MVTC(USP, (uintptr_t)_usp_top);
    system_init();
    memcpy(_text, _text_load, _etext - _text);
    memcpy(_text, _text_load, _etext - _text);
    memcpy(_data, _data_load, _edata - _data);
    memset(_bss, 0, _ebss - _bss);
    main();
END:
    __WAIT();
    goto END;
}
