/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* api81_demo.c — deterministic API81 smoke test: spawns hello.elf at boot
   (after the GUI is up) so userland can be validated without manual typing. */
#include <stdint.h>
#include <stddef.h>

extern void serial_puts(const char* s);
extern int user_module_find(const char* name, const void** data, uint64_t* size);
extern int create_user_process_elf(const char* name);

void serial_u64(uint64_t v) {
    char buf[24];
    int i = 0;
    if (v == 0) { serial_puts("0"); return; }
    while (v > 0) { buf[i++] = '0' + (char)(v % 10); v /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) { char t = buf[j]; buf[j] = buf[i - 1 - j]; buf[i - 1 - j] = t; }
    serial_puts(buf);
}

void api81_demo(void) {
    const void* data = 0;
    uint64_t size = 0;

    serial_puts("[DEMO] api81_demo() start\n");
    extern uint32_t timer_get_ms(void);
    serial_puts("[DEMO] boot ms=");
    serial_u64((uint64_t)timer_get_ms());
    serial_puts("\n");
    if (user_module_find("hello", &data, &size) != 0) {
        serial_puts("[DEMO] hello module NOT FOUND\n");
        return;
    }
    serial_puts("[DEMO] hello module found (");
    serial_u64(size);
    serial_puts(" bytes)\n");

    int pid = create_user_process_elf("pthreadtest");
    if (pid < 0) {
        serial_puts("[DEMO] create_user_process_elf FAILED (rc=");
        serial_u64((uint64_t)(-pid));
        serial_puts(")\n");
        return;
    }
    serial_puts("[DEMO] process created, pid=");
    serial_u64((uint64_t)pid);
    serial_puts("\n");
}