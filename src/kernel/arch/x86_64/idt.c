/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "kernel/arch/x86_64/idt.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/framebuffer.h"
#include "kernel/task/task.h"
#include "gui/wm.h"
#include <string.h>

idt_entry_t idt[IDT_ENTRIES];
idt_ptr_t idt_ptr;

extern void irq0_handler();
extern void irq1_handler();
extern void irq12_handler();
extern void syscall_handler_stub();

// ISRs from interrupts.asm
extern void isr0(); extern void isr1(); extern void isr2(); extern void isr3();
extern void isr4(); extern void isr5(); extern void isr6(); extern void isr7();
extern void isr8(); extern void isr9(); extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();

//extern void serial_puts(const char* s);
extern void itoa(uint64_t n, char* s);

void set_idt_entry(int num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = base & 0xFFFF;
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].type_attr = flags;
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].zero = 0;
}

static void dump_hex(uint64_t val) {
    char hex[20];
    const char* hex_chars = "0123456789ABCDEF";
    for (int i = 0; i < 16; i++) hex[15-i] = hex_chars[(val >> (i * 4)) & 0x0F];
    hex[16] = 0;
    print_string(hex);
}

/* Render the fault details directly on the framebuffer (VGA text grid),
   bypassing the GUI — visible even when the GUI is frozen and there is no
   serial console (VirtualBox default). */
static void fault_fmt_hex(char* out, uint64_t v) {
    const char* hex_chars = "0123456789ABCDEF";
    for (int i = 0; i < 16; i++) out[15-i] = hex_chars[(v >> (i * 4)) & 0x0F];
    out[16] = 0;
}

static void fault_draw_line(int row, const char* s, int color) {
    extern void print_char_at(char character, int color, int x, int y);
    int col = 0;
    while (*s && col < 79) print_char_at(*s++, color, col++, row);
}

static void fault_dump_screen(uint64_t isr_num, uint64_t err_code, uint64_t* regs) {
    extern uint32_t* get_fb_ptr(void);
    extern void print_char_at(char character, int color, int x, int y);
    uint32_t fw = get_fb_width(), fh = get_fb_height();
    uint32_t* fb = get_fb_ptr();
    if (!fb || !fw || !fh) return;
    for (uint32_t i = 0; i < fw * fh; i++) fb[i] = 0x16060A;

    char line[96];
    char buf[80];
    int row = 0;
    for (int r = 0; r < 25; r++)
        for (int c = 0; c < 80; c++)
            print_char_at(' ', 0x0F, c, r);

    fault_draw_line(row++, "  !!! CPU CRITICAL FAULT !!!", 0x4F);
    fault_draw_line(row++, "", 0x0F);

    void app(char* dst, int* n, const char* src, int max) {
        while (*src && *n < max - 1) dst[(*n)++] = *src++;
        dst[*n] = 0;
    }
    int nb;

    nb = 0;
    app(buf, &nb, "ISR: ", 79);
    itoa(isr_num, line);
    app(buf, &nb, line, 79);
    fault_draw_line(row++, buf, 0x1F);

    task_t* cur = get_current_task();
    if (cur) {
        nb = 0;
        app(buf, &nb, "Task: ", 79);
        app(buf, &nb, cur->name, 79);
        fault_draw_line(row++, buf, 0x1F);
    }

    fault_fmt_hex(line, regs[17]);   /* RIP */
    nb = 0;
    app(buf, &nb, "RIP: 0x", 79);
    app(buf, &nb, line, 79);
    fault_draw_line(row++, buf, 0x3F);

    fault_fmt_hex(line, regs[20]);   /* RSP */
    nb = 0;
    app(buf, &nb, "RSP: 0x", 79);
    app(buf, &nb, line, 79);
    fault_draw_line(row++, buf, 0x3F);

    fault_fmt_hex(line, regs[16]);   /* error code */
    nb = 0;
    app(buf, &nb, "ERR: 0x", 79);
    app(buf, &nb, line, 79);
    fault_draw_line(row++, buf, 0x1F);

    fault_fmt_hex(line, regs[10]);   /* RBP */
    nb = 0;
    app(buf, &nb, "RBP: 0x", 79);
    app(buf, &nb, line, 79);
    fault_draw_line(row++, buf, 0x1F);

    fault_fmt_hex(line, regs[19]);   /* RFLAGS */
    nb = 0;
    app(buf, &nb, "RFL: 0x", 79);
    app(buf, &nb, line, 79);
    fault_draw_line(row++, buf, 0x1F);

    fault_fmt_hex(line, regs[18]);   /* CS */
    nb = 0;
    app(buf, &nb, "CS: 0x", 79);
    app(buf, &nb, line, 79);
    fault_draw_line(row++, buf, 0x1F);

    if (isr_num == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        fault_fmt_hex(line, cr2);
        nb = 0;
        app(buf, &nb, "CR2: 0x", 79);
        app(buf, &nb, line, 79);
        fault_draw_line(row++, buf, 0x3F);
    }

    fault_draw_line(row++, "", 0x0F);

    /* DIAGNOSTIC: dump the iretq frame irq0_handler was about to pop.
       Fault RSP (regs[20]) is after the 15 GPR pops, so frame_ptr = RSP-120. */
    {
        uint64_t frame = regs[20] > 120 ? regs[20] - 120 : 0;
        fault_fmt_hex(line, frame);
        nb = 0;
        app(buf, &nb, "FRAME: 0x", 79);
        app(buf, &nb, line, 79);
        if (cur) {
            fault_fmt_hex(line, cur->rsp);
            app(buf, &nb, " saved_rsp:0x", 79);
            app(buf, &nb, line, 79);
            fault_fmt_hex(line, cur->kernel_stack_base);
            app(buf, &nb, " kbase:0x", 79);
            app(buf, &nb, line, 79);
        }
        fault_draw_line(row++, buf, 0x2F);

        if (frame >= 0xffffffff80000000ULL && frame < 0xffffffff90000000ULL) {
            for (int i = 0; i < 20; i += 3) {
                nb = 0;
                char t[80];
                for (int k = 0; k < 3 && i + k < 20; k++) {
                    uint64_t v = 0;
                    __builtin_memcpy(&v, (void*)(uintptr_t)(frame + (i + k) * 8), 8);
                    itoa(i + k, t);
                    app(buf, &nb, " f[", 79);
                    app(buf, &nb, t, 79);
                    app(buf, &nb, "]=", 79);
                    fault_fmt_hex(line, v);
                    app(buf, &nb, line, 79);
                }
                fault_draw_line(row++, buf, 0x0F);
            }
        } else {
            fault_draw_line(row++, "  frame memory outside kernel range", 0x4F);
        }
    }

    /* HEAP-OVERLAP: which kmalloc block owns this task's kernel stack? */
    if (cur) {
        extern int kheap_block_info(uint64_t addr, uint64_t* os, uint64_t* oz, int* of);
        uint64_t s = 0, z = 0; int f = -1;
        nb = 0;
        app(buf, &nb, "STKBLK: ", 79);
        if (kheap_block_info(cur->kernel_stack_base, &s, &z, &f)) {
            fault_fmt_hex(line, s);
            app(buf, &nb, "start=0x", 79);
            app(buf, &nb, line, 79);
            app(buf, &nb, " sz=", 79);
            char t[32]; itoa(z, t);
            app(buf, &nb, t, 79);
            app(buf, &nb, " free=", 79);
            itoa(f, t);
            app(buf, &nb, t, 79);
        } else {
            app(buf, &nb, "NOT IN HEAP", 79);
        }
        fault_draw_line(row++, buf, 0x2F);

        extern uint64_t gfx_back_buffer_addr(void);
        extern uint64_t gfx_back_buffer_size(void);
        extern uint64_t gui_wp_old_addr(void);
        fault_fmt_hex(line, gfx_back_buffer_addr());
        nb = 0;
        app(buf, &nb, "BACKBUF: 0x", 79);
        app(buf, &nb, line, 79);
        app(buf, &nb, " sz=", 79);
        char t[32]; itoa(gfx_back_buffer_size(), t);
        app(buf, &nb, t, 79);
        fault_draw_line(row++, buf, 0x1F);

        fault_fmt_hex(line, gui_wp_old_addr());
        nb = 0;
        app(buf, &nb, "WPOLD: 0x", 79);
        app(buf, &nb, line, 79);
        fault_draw_line(row++, buf, 0x1F);
    }

    fault_draw_line(row++, "", 0x0F);
    fault_draw_line(row++, "  System Halted. Reboot the VM.", 0x1F);

    (void)err_code;
    swap_buffers();
}

void core_exception_handler(uint64_t* registers) {
    uint64_t isr_num = registers[15];
    uint64_t err_code = registers[16];
    char buf[32];

    extern void serial_puts(const char* s);

    {
        task_t* cur_task = get_current_task();
        if (cur_task) {
            print_string("\n  Task: ");
            print_string(cur_task->name);
            print_string(" (id=");
            itoa(cur_task->id, buf);
            print_string(buf);
            print_string(")\n");
            serial_puts("\n  Task: ");
            serial_puts(cur_task->name);
            serial_puts(" (id=");
            serial_puts(buf);
            serial_puts(")\n");
        }
    }
    
    itoa(isr_num, buf);
    print_string("  !!! CPU CRITICAL FAULT: ");
    print_string(buf);
    print_string(" !!!\n");
    serial_puts("  !!! CPU CRITICAL FAULT: ");
    serial_puts(buf);
    serial_puts(" !!!\n");

    uint64_t cr2 = 0;
    if (isr_num == 14) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        print_string("  Page Fault at: 0x");
        char hex[20];
        const char* hex_chars = "0123456789ABCDEF";
        for (int i = 0; i < 16; i++) hex[15-i] = hex_chars[(cr2 >> (i * 4)) & 0x0F];
        hex[16] = 0;
        print_string(hex);
        print_string("\n  Error Code: ");
        itoa(err_code, buf);
        print_string(buf);
        print_string("\n");
        serial_puts("  Page Fault at: 0x");
        serial_puts(hex);
        serial_puts("  Error Code: ");
        serial_puts(buf);
        serial_puts("\n");
    }

    /* Full register dump (PUSH_ALL order: rax,rcx,rdx,rsi,rdi,r8..r11,
       rbx,rbp,r12..r15, then ISR#, err, RIP, CS, RFLAGS, RSP, SS). */
    itoa(registers[17], buf);
    serial_puts("  Fault RIP: ");
    serial_puts(buf);
    serial_puts("  CS: ");
    itoa(registers[18], buf);
    serial_puts(buf);
    serial_puts("  RFLAGS: ");
    itoa(registers[19], buf);
    serial_puts(buf);
    serial_puts("  RSP: ");
    itoa(registers[20], buf);
    serial_puts(buf);
    serial_puts("  ERR: ");
    itoa(registers[16], buf);
    serial_puts(buf);
    serial_puts("\n  rax=");
    itoa(registers[0], buf); serial_puts(buf);
    serial_puts(" rcx=");
    itoa(registers[1], buf); serial_puts(buf);
    serial_puts(" rdx=");
    itoa(registers[2], buf); serial_puts(buf);
    serial_puts(" rbx=");
    itoa(registers[9], buf); serial_puts(buf);
    serial_puts("\n  rsi=");
    itoa(registers[3], buf); serial_puts(buf);
    serial_puts(" rdi=");
    itoa(registers[4], buf); serial_puts(buf);
    serial_puts(" rbp=");
    itoa(registers[10], buf); serial_puts(buf);
    serial_puts("\n  r8=");
    itoa(registers[5], buf); serial_puts(buf);
    serial_puts(" r9=");
    itoa(registers[6], buf); serial_puts(buf);
    serial_puts(" r10=");
    itoa(registers[7], buf); serial_puts(buf);
    serial_puts(" r11=");
    itoa(registers[8], buf); serial_puts(buf);
    serial_puts(" r12=");
    itoa(registers[11], buf); serial_puts(buf);
    serial_puts(" r13=");
    itoa(registers[12], buf); serial_puts(buf);
    serial_puts(" r14=");
    itoa(registers[13], buf); serial_puts(buf);
    serial_puts(" r15=");
    itoa(registers[14], buf); serial_puts(buf);
    serial_puts("\n");

    /* DIAGNOSTIC: dump the iretq frame that was about to be popped for the
       current task (ISR 13 at irq0_handler's iretq pops this). The fault
       RSP is after the 15 GPR pops, so frame_ptr = RSP - 120. */
    {
        uint64_t frame = registers[20] > 120 ? registers[20] - 120 : 0;
        serial_puts("  [diag] frame_ptr=");
        const char* hx = "0123456789ABCDEF";
        char h[20];
        for (int i = 0; i < 16; i++) h[15-i] = hx[(frame >> (i * 4)) & 0x0F];
        h[16] = 0;
        serial_puts(h);
        serial_puts(" task_idx=");
        task_t* dt = get_current_task();
        if (dt) {
            serial_puts(dt->name);
            serial_puts(" id="); itoa(dt->id, buf); serial_puts(buf);
            serial_puts(" saved_rsp=");
            for (int i = 0; i < 16; i++) h[15-i] = hx[(dt->rsp >> (i * 4)) & 0x0F];
            serial_puts(h);
            serial_puts(" kbase=");
            for (int i = 0; i < 16; i++) h[15-i] = hx[(dt->kernel_stack_base >> (i * 4)) & 0x0F];
            serial_puts(h);
            serial_puts(" state="); itoa(dt->state, buf); serial_puts(buf);
            serial_puts(" pml4=");
            for (int i = 0; i < 16; i++) h[15-i] = hx[(dt->pml4_phys >> (i * 4)) & 0x0F];
            serial_puts(h);
            serial_puts("\n");
            for (int i = 0; i < 20; i++) {
                uint64_t v = 0;
                __builtin_memcpy(&v, (void*)(uintptr_t)(frame + i * 8), 8);
                serial_puts("  f[");
                itoa(i, buf); serial_puts(buf);
                serial_puts("]=");
                for (int j = 0; j < 16; j++) h[15-j] = hx[(v >> (j * 4)) & 0x0F];
                serial_puts(h);
                serial_puts("\n");
            }

            /* HEAP-OVERLAP DIAGNOSTIC: which kmalloc block owns the faulting
               task's kernel stack? If a big GUI buffer (back/background/
               wallpaper snapshot) or a window buffer physically overlaps it,
               the app that keeps painting that buffer has been clobbering the
               stack. Block state 1 = marked free (double allocation). */
            extern int kheap_block_info(uint64_t addr, uint64_t* os, uint64_t* oz, int* of);
            extern uint64_t gfx_back_buffer_addr(void);
            extern uint64_t gfx_bg_buffer_addr(void);
            extern uint64_t gfx_back_buffer_size(void);
            extern uint64_t gui_wp_old_addr(void);
            extern uint64_t gui_wp_old_size(void);
            extern int wm_dump_user_windows(uint64_t* ids, uint64_t* bs, uint64_t* cs, int* ps, int max);
            uint64_t s = 0, z = 0; int f = -1;
            serial_puts("  [heap] kbase block: ");
            if (kheap_block_info(dt->kernel_stack_base, &s, &z, &f)) {
                for (int i = 0; i < 16; i++) h[15-i] = hx[(s >> (i * 4)) & 0x0F];
                serial_puts("start="); serial_puts(h);
                serial_puts(" size="); itoa(z, buf); serial_puts(buf);
                serial_puts(" free="); itoa(f, buf); serial_puts(buf);
            } else {
                serial_puts("NOT IN HEAP");
            }
            serial_puts("\n");
            serial_puts("  [heap] back_buf=");
            uint64_t bb = gfx_back_buffer_addr(), bbs = gfx_back_buffer_size();
            for (int i = 0; i < 16; i++) h[15-i] = hx[(bb >> (i * 4)) & 0x0F];
            serial_puts(h);
            serial_puts(" size="); itoa(bbs, buf); serial_puts(buf);
            serial_puts("\n  [heap] bg_buf=");
            uint64_t bg = gfx_bg_buffer_addr();
            for (int i = 0; i < 16; i++) h[15-i] = hx[(bg >> (i * 4)) & 0x0F];
            serial_puts(h);
            serial_puts("\n  [heap] wp_old=");
            uint64_t wo = gui_wp_old_addr(), wos = gui_wp_old_size();
            for (int i = 0; i < 16; i++) h[15-i] = hx[(wo >> (i * 4)) & 0x0F];
            serial_puts(h);
            serial_puts(" size="); itoa(wos, buf); serial_puts(buf);
            serial_puts("\n");
            uint64_t wids[8], wbs[8], wcs[8]; int wps[8];
            int wn = wm_dump_user_windows(wids, wbs, wcs, wps, 8);
            for (int w = 0; w < wn; w++) {
                serial_puts("  [heap] win id=");
                itoa(wids[w], buf); serial_puts(buf);
                serial_puts(" pid="); itoa(wps[w], buf); serial_puts(buf);
                serial_puts(" buf=");
                for (int i = 0; i < 16; i++) h[15-i] = hx[(wbs[w] >> (i * 4)) & 0x0F];
                serial_puts(h);
                serial_puts(" cbuf=");
                for (int i = 0; i < 16; i++) h[15-i] = hx[(wcs[w] >> (i * 4)) & 0x0F];
                serial_puts(h);
                serial_puts("\n");
            }
            for (int w = 0; w < wn; w++) {
                serial_puts("  [heap] win-overlap id=");
                itoa(wids[w], buf); serial_puts(buf);
                if (kheap_block_info(wbs[w], &s, &z, &f)) {
                    for (int i = 0; i < 16; i++) h[15-i] = hx[(s >> (i * 4)) & 0x0F];
                    serial_puts(" start="); serial_puts(h);
                    serial_puts(" size="); itoa(z, buf); serial_puts(buf);
                    serial_puts(" free="); itoa(f, buf); serial_puts(buf);
                } else serial_puts(" NOT IN HEAP");
                serial_puts("\n");
            }

            /* TARGETED: scan the WHOLE trace ring for any alloc/free that
               touched a window's buffer or cbuf. This names the exact culprit
               kfree/kmalloc (and its caller RIP) in a single compact block. */
            extern int kheap_trace_find(uint64_t t, uint64_t* op, uint64_t* oz, int* of, uint64_t* oc, int m);
            uint64_t fp_[16], fz_[16], fc_[16]; int ff_[16];
            for (int w = 0; w < wn; w++) {
                for (int k = 0; k < 2; k++) {
                    uint64_t target = (k == 0) ? wbs[w] : wcs[w];
                    int fn = kheap_trace_find(target, fp_, fz_, ff_, fc_, 16);
                    if (fn <= 0) continue;
                    serial_puts("  [winbuf-events] id="); itoa(wids[w], buf); serial_puts(buf);
                    serial_puts(k == 0 ? " buf" : " cbuf");
                    for (int e = 0; e < fn; e++) {
                        serial_puts("  ");
                        serial_puts(ff_[e] ? "KFREE " : "KALLOC");
                        serial_puts(" size="); itoa(fz_[e], buf); serial_puts(buf);
                        serial_puts(" from=0x");
                        for (int i = 0; i < 16; i++) h[15-i] = hx[(fc_[e] >> (i * 4)) & 0x0F];
                        serial_puts(h);
                        serial_puts("\n");
                    }
                }
            }

            /* CHAIN DUMP: walk the free-list chain so we can see where it
               breaks and what garbage the next/prev pointers contain. */
            extern int kheap_chain_dump(uint64_t* cs, uint64_t* cz, int* cf, uint64_t* cn, int cm);
            uint64_t cstart[40], csize[40], cnext[40]; int cfree[40];
            int cn_ = kheap_chain_dump(cstart, csize, cfree, cnext, 40);
            serial_puts("  [chain] blocks="); itoa(cn_, buf); serial_puts(buf);
            serial_puts("\n");
            for (int c = 0; c < cn_; c++) {
                serial_puts("  [chain] b");
                itoa(c, buf); serial_puts(buf);
                serial_puts(" start=");
                for (int i = 0; i < 16; i++) h[15-i] = hx[(cstart[c] >> (i * 4)) & 0x0F];
                serial_puts(h);
                serial_puts(" size="); itoa(csize[c], buf); serial_puts(buf);
                serial_puts(" free="); itoa(cfree[c], buf); serial_puts(buf);
                serial_puts(" next=");
                for (int i = 0; i < 16; i++) h[15-i] = hx[(cnext[c] >> (i * 4)) & 0x0F];
                serial_puts(h);
                serial_puts("\n");
            }

            /* ALLOC TRACE: last kmalloc/kfree events with caller RIPs. */
            extern int kheap_trace_dump(uint64_t* tp, uint64_t* tz, int* tf, uint64_t* tc, int tm);
            uint64_t tp_[80], tz_[80], tc_[80]; int tf_[80];
            int tn_ = kheap_trace_dump(tp_, tz_, tf_, tc_, 80);
            for (int t = 0; t < tn_; t++) {
                serial_puts("  [trace] ");
                serial_puts(tf_[t] ? "KFREE " : "KALLOC");
                serial_puts(" ptr=");
                for (int i = 0; i < 16; i++) h[15-i] = hx[(tp_[t] >> (i * 4)) & 0x0F];
                serial_puts(h);
                serial_puts(" size="); itoa(tz_[t], buf); serial_puts(buf);
                serial_puts(" from=0x");
                for (int i = 0; i < 16; i++) h[15-i] = hx[(tc_[t] >> (i * 4)) & 0x0F];
                serial_puts(h);
                serial_puts("\n");
            }
        }
    }

    if (isr_num == 14) {
        extern uint64_t hhdm_offset;
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        serial_puts("  cr3=");
        char hex[20];
        const char* hex_chars = "0123456789ABCDEF";
        for (int i = 0; i < 16; i++) hex[15-i] = hex_chars[(cr3 >> (i * 4)) & 0x0F];
        hex[16] = 0;
        serial_puts(hex);
        serial_puts("\n");
        uint64_t* pml4 = (uint64_t*)(cr3 + hhdm_offset);
        uint64_t i4 = (cr2 >> 39) & 0x1FF;
        uint64_t i3 = (cr2 >> 30) & 0x1FF;
        uint64_t i2 = (cr2 >> 21) & 0x1FF;
        uint64_t i1 = (cr2 >> 12) & 0x1FF;
        uint64_t e4 = pml4[i4];
        uint64_t e3 = (e4 & 1) ? ((uint64_t*)((e4 & 0x000FFFFFFFFFF000ULL) + hhdm_offset))[i3] : 0;
        uint64_t e2 = (e3 & 1) ? ((uint64_t*)((e3 & 0x000FFFFFFFFFF000ULL) + hhdm_offset))[i2] : 0;
        uint64_t e1 = ((e2 & 1) && !(e2 & (1ULL << 7))) ? ((uint64_t*)((e2 & 0x000FFFFFFFFFF000ULL) + hhdm_offset))[i1] : 0;
        serial_puts("  l4=");
        for (int i = 0; i < 16; i++) hex[15-i] = hex_chars[(e4 >> (i * 4)) & 0x0F];
        serial_puts(hex);
        serial_puts(" l3=");
        for (int i = 0; i < 16; i++) hex[15-i] = hex_chars[(e3 >> (i * 4)) & 0x0F];
        serial_puts(hex);
        serial_puts(" l2=");
        for (int i = 0; i < 16; i++) hex[15-i] = hex_chars[(e2 >> (i * 4)) & 0x0F];
        serial_puts(hex);
        serial_puts("  l1=");
        for (int i = 0; i < 16; i++) hex[15-i] = hex_chars[(e1 >> (i * 4)) & 0x0F];
        serial_puts(hex);
        serial_puts("\n");
    }

    /* Ring-3 fault: kill the task instead of halting the whole OS. The
       crash details were already dumped to serial above. */
    uint64_t cs = registers[18];
    if ((cs & ~(uint64_t)3) == 0x18) {
        uint64_t cr2 = 0;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_puts("  [ring-3 fault] killing task, OS continues.\n");
        task_t* ft = get_current_task();
        if (ft) {
            wm_mark_crashed(ft->id, registers[17], cr2, err_code);
            exit_task(1);
        }
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    print_string("  System Halted to prevent data corruption.");
    serial_puts("  System Halted to prevent data corruption.\n");

    /* On-screen fault dump: render the fault directly on the framebuffer
       (bypassing the GUI), so it is visible even with the GUI frozen and
       no serial console (default VirtualBox). */
    fault_dump_screen(isr_num, err_code, registers);

    while(1) { __asm__ ("cli; hlt"); }
}

// I/O delay for 8259A PIC: required on real hardware between ICW bytes.
// Port 0x80 is the POST diagnostic port — writing to it is a safe ~1us delay.
static inline void pic_io_wait(void) {
    port_byte_out(0x80, 0);
}

static void remap_pic() {
    // Save current masks
    uint8_t mask1 = port_byte_in(0x21);
    uint8_t mask2 = port_byte_in(0xA1);

    // ICW1: Start initialization sequence, cascade mode, ICW4 needed
    port_byte_out(0x20, 0x11); pic_io_wait();
    port_byte_out(0xA0, 0x11); pic_io_wait();

    // ICW2: Remap IRQ base vectors (master 0x20, slave 0x28)
    port_byte_out(0x21, 0x20); pic_io_wait();
    port_byte_out(0xA1, 0x28); pic_io_wait();

    // ICW3: Tell master that slave is at IRQ2, tell slave its cascade identity
    port_byte_out(0x21, 0x04); pic_io_wait();  // Master: slave on IRQ2
    port_byte_out(0xA1, 0x02); pic_io_wait();  // Slave: cascade identity = 2

    // ICW4: 8086 mode
    port_byte_out(0x21, 0x01); pic_io_wait();
    port_byte_out(0xA1, 0x01); pic_io_wait();

    // OCW1: Set interrupt masks
    // Master: 0xF8 = 11111000 — enable IRQ0 (timer), IRQ1 (kbd), IRQ2 (cascade)
    // Slave:  0xEF = 11101111 — enable IRQ12 (mouse, bit 4 of slave)
    port_byte_out(0x21, 0xF8); pic_io_wait();
    port_byte_out(0xA1, 0xEF); pic_io_wait();

    (void)mask1; (void)mask2; // masks saved but not restored — we set new ones above
}

void init_idt() {
    idt_ptr.limit = IDT_SIZE - 1;
    idt_ptr.base = (uint64_t)&idt;
    for (int i = 0; i < IDT_ENTRIES; i++) set_idt_entry(i, 0, 0, 0);
    
    // Register Exceptions (0-31)
    void (*isrs[])() = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };
    for (int i = 0; i < 32; i++) {
        set_idt_entry(i, (uint64_t)isrs[i], 0x08, 0x8E);
    }
    
    // IRQ0 — timer
    set_idt_entry(0x20, (uint64_t)irq0_handler, 0x08, 0x8E);
    // IRQ1 — keyboard
    set_idt_entry(0x21, (uint64_t)irq1_handler, 0x08, 0x8E); 
    // IRQ12 — mouse
    set_idt_entry(0x2C, (uint64_t)irq12_handler, 0x08, 0x8E);
    
    // Syscall interrupt (int 0x80) — Ring 3 accessible
    set_idt_entry(0x80, (uint64_t)syscall_handler_stub, 0x08, 0xEE);

    remap_pic();
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}
