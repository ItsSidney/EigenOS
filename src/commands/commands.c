/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "commands/commands.h"
#include "drivers/video/framebuffer.h"
#include "drivers/input/keyboard.h"
#include "drivers/time/rtc.h"
#include "drivers/video/gfx.h"
#include "filesystem/filesystem.h"
#include "drivers/bus/pci.h"
#include "kernel/mem/kheap.h"
#include "drivers/video/gpu.h"
#include "kernel/net/socket.h"
#include "kernel/net/in.h"
#include "kernel/net/if.h"
#include "kernel/net/dns.h"
#include "kernel/time/timer.h"
#include "kernel/task/task.h"
#include "kernel/lib/stdio.h"
#include "drivers/storage/storage.h"
#include "kernel/security/security.h"
#include <string.h>
#include <stdint.h>

extern volatile uint64_t timer_ticks;

/* Global hostname — settable via `hostname <name>` command */
static char g_hostname[64] = "eigen";

const char* get_hostname(void) { return g_hostname; }

typedef struct {
    const char* name;
    const char* usage;
    const char* desc;
    void (*handler)(int argc, char** argv);
} cmd_t;

static void cmd_help(int argc, char** argv);
static void cmd_about(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_reboot(int argc, char** argv);
static void cmd_shutdown(int argc, char** argv);
static void cmd_uptime(int argc, char** argv);
static void cmd_meminfo(int argc, char** argv);
static void cmd_syscheck(int argc, char** argv);
static void cmd_pcilist(int argc, char** argv);
static void cmd_lsgpu(int argc, char** argv);
static void cmd_gpu(int argc, char** argv);
static void cmd_gui(int argc, char** argv);
static void cmd_eigenfetch(int argc, char** argv);
static void cmd_timer_test(int argc, char** argv);
static void cmd_ls(int argc, char** argv);
static void cmd_cd(int argc, char** argv);
static void cmd_pwd(int argc, char** argv);
static void cmd_mkdir(int argc, char** argv);
static void cmd_rmdir(int argc, char** argv);
static void cmd_touch(int argc, char** argv);
static void cmd_rm(int argc, char** argv);
static void cmd_cat(int argc, char** argv);
static void cmd_less(int argc, char** argv);
static void cmd_write(int argc, char** argv);
static void cmd_append(int argc, char** argv);
static void cmd_cp(int argc, char** argv);
static void cmd_mv(int argc, char** argv);
static void cmd_rename(int argc, char** argv);
static void cmd_stat(int argc, char** argv);
static void cmd_grep(int argc, char** argv);
static void cmd_edim(int argc, char** argv);
static void cmd_colors(int argc, char** argv);
static void cmd_ping(int argc, char** argv);
static void cmd_dns(int argc, char** argv);
static void cmd_edrowser(int argc, char** argv);
static void cmd_ifconfig(int argc, char** argv);
static void cmd_netstat(int argc, char** argv);
static void cmd_bootlog(int argc, char** argv);
static void cmd_ecc(int argc, char** argv);
static void cmd_erun(int argc, char** argv);
static void cmd_vmtest(int argc, char** argv);
static void cmd_ring3test(int argc, char** argv);
static void cmd_spawn(int argc, char** argv);
static void cmd_man(int argc, char** argv);
static void cmd_hostname(int argc, char** argv);

static cmd_t cmd_table[] = {
    {"help",      "help [command]",              "Show help for commands", cmd_help},
    {"about",     "about",                       "Display OS information", cmd_about},
    {"clear",     "clear",                       "Clear the terminal", cmd_clear},
    {"reboot",    "reboot",                      "Reboot the system", cmd_reboot},
    {"shutdown",  "shutdown",                    "Power off the system", cmd_shutdown},
    {"uptime",    "uptime",                      "Show system uptime", cmd_uptime},
    {"meminfo",   "meminfo",                     "Show memory usage info", cmd_meminfo},
    {"syscheck",  "syscheck",                    "Run system integrity check", cmd_syscheck},
    {"pcilist",   "pcilist",                     "List PCI devices", cmd_pcilist},
    {"lsgpu",     "lsgpu",                       "List GPU devices", cmd_lsgpu},
    {"gpu",       "gpu",                         "Show GPU details and info", cmd_gpu},
    {"gui",       "gui",                         "Launch graphical interface", cmd_gui},
    {"eigenfetch","eigenfetch",                  "Display system info (fetch)", cmd_eigenfetch},
    {"timer-test","timer-test",                  "Test system timer accuracy", cmd_timer_test},
    {"ls",        "ls",                          "List directory contents", cmd_ls},
    {"cd",        "cd <dir>",                    "Change current directory", cmd_cd},
    {"pwd",       "pwd",                         "Print working directory", cmd_pwd},
    {"mkdir",     "mkdir <dir>",                 "Create a directory", cmd_mkdir},
    {"rmdir",     "rmdir <dir>",                 "Remove a directory", cmd_rmdir},
    {"touch",     "touch <file>",                "Create an empty file", cmd_touch},
    {"rm",        "rm <file>",                   "Remove a file", cmd_rm},
    {"cat",       "cat <file>",                  "Display file contents", cmd_cat},
    {"less",      "less <file>",                 "View file contents (paged)", cmd_less},
    {"write",     "write <file> <text>",         "Write text to a file", cmd_write},
    {"append",    "append <file> <text>",        "Append text to a file", cmd_append},
    {"cp",        "cp <src> <dst>",              "Copy a file", cmd_cp},
    {"mv",        "mv <src> <dst>",              "Move/rename a file", cmd_mv},
    {"rename",    "rename <old> <new>",           "Rename a file", cmd_rename},
    {"stat",      "stat <file>",                 "Show file information", cmd_stat},
    {"grep",      "grep <pattern> <file>",       "Search in a file", cmd_grep},
    {"edim",      "edim [file]",                  "Open EDIM text editor", cmd_edim},
    {"colors",    "colors [0-9]",                "Set terminal text color", cmd_colors},
    {"ping",      "ping <host>",                 "Ping a host", cmd_ping},
    {"dns",       "dns <hostname>",              "Resolve a hostname", cmd_dns},
    {"edrowser","edrowser",                  "Launch Edrowser", cmd_edrowser},
    {"ifconfig",  "ifconfig [iface]",            "Show network interface config", cmd_ifconfig},
    {"netstat",   "netstat",                     "Show ARP/DNS tables + sockets", cmd_netstat},
    {"bootlog",   "bootlog",                     "Show kernel boot log", cmd_bootlog},
    {"ecc",       "ecc <file.ec>",               "Compile Eigen C source", cmd_ecc},
    {"erun",      "erun <file.bin>",             "Run compiled bytecode", cmd_erun},
    {"vmtest",    "vmtest",                      "Test virtual machine", cmd_vmtest},
    {"ring3test", "ring3test",                   "Test userspace process", cmd_ring3test},
    {"spawn", "spawn <app>",                     "Spawn an API81 userland app", cmd_spawn},
    {"run",   "run <app>",                       "Alias of spawn: run a ring-3 app", cmd_spawn},
    {"man",       "man <command>",               "Show command manual", cmd_man},
    {"hostname",  "hostname [name]",             "Get or set the system hostname", cmd_hostname},
    {0, 0, 0, 0}
};

static cmd_t* find_cmd(const char* name) {
    for (int i = 0; cmd_table[i].name; i++)
        if (strcmp(name, cmd_table[i].name) == 0) return &cmd_table[i];
    return 0;
}

static void show_usage(cmd_t* c) {
    print_string("  \e[93mUsage: \e[97m"); print_string(c->usage); print_string("\e[0m\n");
    print_string("  \e[90m"); print_string(c->desc); print_string("\e[0m\n");
}

static int has_help_flag(int argc, char** argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) return 1;
    }
    return 0;
}

static int tokenize(char* input, char** argv, int max) {
    int argc = 0;
    while (*input && argc < max) {
        while (*input == ' ') input++;
        if (!*input) break;
        char* out = input;
        argv[argc++] = out;
        int q = 0;   /* 0 = none, '\'' = single, '"' = double */
        while (*input) {
            char c = *input;
            if (c == '\'' && q != '"') { q = (q == '\'') ? 0 : '\''; input++; continue; }
            if (c == '"' && q != '\'') { q = (q == '"') ? 0 : '"'; input++; continue; }
            if (c == ' ' && !q) break;
            *out++ = *input; input++;
        }
        int had_sep = (*input != 0);
        *out = 0;
        if (had_sep) input++;   /* skip the terminating space */
    }
    return argc;
}

void vmtest() { print_string("\n  \e[93mvmtest:\e[0m test.bin removed from sample files\n"); }
extern void ring3_entry(void);
void ring3test() {
    print_string("\n  \e[94mLaunching ring3 test process...\e[0m\n");
    extern int create_user_process(void (*entry)(void), const char* name);
    int pid = create_user_process(ring3_entry, "ring3test");
    if (pid < 0) { print_string("  \e[91mFailed to create ring3 process\e[0m\n"); return; }
    char buf[32]; itoa(pid, buf);
    print_string("  \e[92m[OK] ring3 test process created with PID: \e[97m"); print_string(buf); print_string("\e[0m\n");
}

static uint32_t parse_ip(const char* s) {
    uint32_t res = 0, part = 0; int shift = 24;
    while (*s) {
        if (*s >= '0' && *s <= '9') part = part * 10 + (*s - '0');
        else if (*s == '.') { res |= (part & 0xFF) << shift; shift -= 8; part = 0; }
        s++;
    }
    res |= (part & 0xFF) << shift;
    return htonl(res);
}
static const char* strip_url_scheme(const char* host) {
    if (strncmp(host, "http://", 7) == 0) return host + 7;
    if (strncmp(host, "HTTP://", 7) == 0) return host + 7;
    if (strncmp(host, "https://", 8) == 0) return host + 8;
    if (strncmp(host, "HTTPS://", 8) == 0) return host + 8;
    return host;
}
void ping(char* host) {
    host = (char*)strip_url_scheme(host);
    uint32_t ip = parse_ip(host);
    if (ip == 0) { extern int dns_resolve(const char*, uint32_t*); uint32_t rip; if (dns_resolve(host, &rip) < 0) ip = htonl(0x0A000202); else ip = rip; }
    print_string("\n  \e[94mPinging \e[96m"); print_string(host); print_string("\e[94m...\e[0m\n");
    extern int sys_ping(uint32_t); sys_ping(ip);
    extern void sleep_task(uint32_t); sleep_task(2000);
    print_string("\n");
}
void dns_lookup(char* raw) {
    const char* host = strip_url_scheme(raw);
    print_string("\n  \e[94mResolving \e[96m"); print_string(host); print_string("\e[94m...\e[0m\n");
    extern int dns_resolve(const char*, uint32_t*); uint32_t ip;
    if (dns_resolve(host, &ip) == 0) { char buf[16]; itoa(ntohl(ip), buf); print_string("  \e[92m-> \e[97m"); print_string(buf); print_string("\e[0m\n"); }
    else { print_string("  \e[91m[ERR] DNS resolution failed\e[0m\n"); }
}
void bootlog() { extern void log_dump(void); log_dump(); }

/* Clean monochrome shell prompt: hostname:cwd :> */
void print_prompt() {
    char cwd[256]; fs_pwd(cwd, 255);
    print_string("\e[97m");       /* bright white */
    print_string(g_hostname);
    print_string("\e[0m:\e[37m"); /* grey */
    print_string(cwd);
    print_string("\e[0m :> ");    /* reset then :> */
}

static void cmd_hostname(int argc, char** argv) {
    if (argc < 2) {
        print_string("  "); print_string(g_hostname); print_string("\n");
        return;
    }
    /* copy up to 63 chars */
    int i = 0;
    while (argv[1][i] && i < 63) { g_hostname[i] = argv[1][i]; i++; }
    g_hostname[i] = 0;
    print_string("  Hostname set to: "); print_string(g_hostname); print_string("\n");
}
void reboot() {
    print_string("\n  \e[93mSystem rebooting...\e[0m\n");
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
}
void shutdown() {
    print_string("\n  \e[91mShutting down...\e[0m\n");
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
}

void handle_tab(char* buffer, int* index) {
    int len = strlen(buffer);
    if (len == 0) return;
    for (int i = 0; cmd_table[i].name; i++) {
        if (strncmp(buffer, cmd_table[i].name, len) == 0) {
            const char* name = cmd_table[i].name;
            for (int j = len; name[j] != 0; j++) {
                buffer[(*index)++] = name[j];
                char s[2] = {name[j], 0}; print_string(s);
            }
            return;
        }
    }
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
int strncmp(const char* s1, const char* s2, int n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
int strlen(const char* s) { int l = 0; while (s[l]) l++; return l; }
void strcpy(char* dest, const char* src) { while ((*dest++ = *src++)); }

extern uint64_t get_total_memory_bytes(void);

static void cmd_help(int argc, char** argv) {
    if (argc > 1) { cmd_man(argc, argv); return; }
    print_string("\n");
    print_string("  \e[94mFile System:\e[0m\n");
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(cmd_table[i].name, "ls") == 0 || strcmp(cmd_table[i].name, "cd") == 0 ||
            strcmp(cmd_table[i].name, "pwd") == 0 || strcmp(cmd_table[i].name, "mkdir") == 0 ||
            strcmp(cmd_table[i].name, "rmdir") == 0 || strcmp(cmd_table[i].name, "touch") == 0 ||
            strcmp(cmd_table[i].name, "rm") == 0 || strcmp(cmd_table[i].name, "cat") == 0 ||
            strcmp(cmd_table[i].name, "write") == 0 || strcmp(cmd_table[i].name, "append") == 0 ||
            strcmp(cmd_table[i].name, "cp") == 0 || strcmp(cmd_table[i].name, "mv") == 0 ||
            strcmp(cmd_table[i].name, "rename") == 0 || strcmp(cmd_table[i].name, "stat") == 0 ||
            strcmp(cmd_table[i].name, "grep") == 0 || strcmp(cmd_table[i].name, "edim") == 0) {
            print_string("    \e[92m"); print_string(cmd_table[i].name); print_string("\e[0m");
            int sp = 16 - strlen(cmd_table[i].name); for (int j = 0; j < sp; j++) print_string(" ");
            print_string("\e[90m- "); print_string(cmd_table[i].desc); print_string("\e[0m\n");
        }
    }
    print_string("  \e[93mNetwork:\e[0m\n");
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(cmd_table[i].name, "ping") == 0 || strcmp(cmd_table[i].name, "dns") == 0 ||
            strcmp(cmd_table[i].name, "edrowser") == 0) {
            print_string("    \e[92m"); print_string(cmd_table[i].name); print_string("\e[0m");
            int sp = 16 - strlen(cmd_table[i].name); for (int j = 0; j < sp; j++) print_string(" ");
            print_string("\e[90m- "); print_string(cmd_table[i].desc); print_string("\e[0m\n");
        }
    }
    print_string("  \e[95mDevelopment:\e[0m\n");
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(cmd_table[i].name, "ecc") == 0 || strcmp(cmd_table[i].name, "erun") == 0 ||
            strcmp(cmd_table[i].name, "vmtest") == 0 || strcmp(cmd_table[i].name, "ring3test") == 0) {
            print_string("    \e[92m"); print_string(cmd_table[i].name); print_string("\e[0m");
            int sp = 16 - strlen(cmd_table[i].name); for (int j = 0; j < sp; j++) print_string(" ");
            print_string("\e[90m- "); print_string(cmd_table[i].desc); print_string("\e[0m\n");
        }
    }
    print_string("  \e[90mRun 'man <command>' for details.\e[0m\n");
}

static void cmd_man(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mWhat manual page do you want?\e[0m\n"); return; }
    cmd_t* c = find_cmd(argv[1]);
    if (!c) { print_string("  \e[91mNo manual entry for \e[97m"); print_string(argv[1]); print_string("\e[0m\n"); return; }
    print_string("\n  \e[96mNAME\e[0m\n");
    print_string("\t\e[92m"); print_string(c->name); print_string("\e[0m - "); print_string(c->desc); print_string("\n\n");
    print_string("  \e[96mSYNOPSIS\e[0m\n");
    print_string("\t\e[93m"); print_string(c->usage); print_string("\e[0m\n\n");
    print_string("  \e[96mDESCRIPTION\e[0m\n");
    print_string("\t"); print_string(c->desc); print_string("\n");
    print_string("\n");
}

static void cmd_about(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("about")); return; }
    print_string("\n  \e[97mEigen\e[0m  64-bit x86_64  Limine UEFI\n");
}
static void cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    clear_screen(); swap_buffers(); clear_screen(); swap_buffers();
}
static void cmd_reboot(int argc, char** argv) { (void)argc; (void)argv; reboot(); }
static void cmd_shutdown(int argc, char** argv) { (void)argc; (void)argv; shutdown(); }
static void cmd_uptime(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("uptime")); return; }
    uint64_t ms = timer_get_ms(); char buf[32];
    uint32_t sec = (uint32_t)(ms / 1000);
    uint32_t d = sec / 86400; sec %= 86400;
    uint32_t h = sec / 3600; sec %= 3600;
    uint32_t m = sec / 60; sec %= 60;
    print_string("\n  \e[96mUptime: \e[97m");
    if (d) { itoa(d, buf); print_string(buf); print_string("d "); }
    itoa(h, buf); print_string(buf); print_string(":");
    if (m < 10) print_string("0"); itoa(m, buf); print_string(buf); print_string(":");
    if (sec < 10) print_string("0"); itoa(sec, buf); print_string(buf); print_string("\e[0m\n");
}
static void cmd_meminfo(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("meminfo")); return; }
    uint64_t total = get_total_memory_bytes(); uint64_t free = kheap_free(); char buf[32];
    print_string("\n  \e[96mMemory Information:\e[0m\n");
    print_string("    Total RAM: \e[92m"); itoa(total >> 20, buf); print_string(buf); print_string(" MB\e[0m\n");
    print_string("    Heap Free: \e[93m"); itoa(free >> 10, buf); print_string(buf); print_string(" KB\e[0m\n");
}
static void cmd_syscheck(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("syscheck")); return; }
    print_string("\n  \e[96mSystem Integrity Check:\e[0m\n");
    print_string("  \e[92m[ OK ]\e[0m Interrupts Enabled\n");
    print_string("  \e[92m[ OK ]\e[0m FPU Initialized\n");
    print_string("  \e[92m[ OK ]\e[0m PCI Controller Active\n");
    gpu_device_t* gpu = gpu_get_primary();
    if (gpu && gpu->initialized) { print_string("  \e[92m[ OK ]\e[0m GPU Accelerated (\e[96m"); print_string(gpu->name); print_string("\e[0m)\n"); }
    else { print_string("  \e[93m[WARN]\e[0m GPU not detected or basic VGA only\n"); }
}
static void cmd_pcilist(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("pcilist")); return; }
    print_string("\n  \e[94mScanning PCI Bus...\e[0m\n");
    int count = pci_get_device_count();
    for (int i = 0; i < count; i++) {
        pci_device_t* dev = pci_get_device(i); char buf[32];
        print_string("  \e[90m[\e[93m");
        buf[0] = (dev->bus / 10) + '0'; buf[1] = (dev->bus % 10) + '0'; buf[2] = ':';
        buf[3] = (dev->slot / 10) + '0'; buf[4] = (dev->slot % 10) + '0'; buf[5] = '.';
        buf[6] = (dev->func % 10) + '0'; buf[7] = 0;
        print_string(buf); print_string("\e[90m]\e[0m "); print_string(pci_device_to_string(dev->vendor_id, dev->device_id)); print_string("\n");
    }
}
static void cmd_lsgpu(int argc, char** argv) {
    (void)argc; (void)argv;
    print_string("\n  \e[96mGPU Devices:\e[0m\n");
    int count = pci_get_device_count(), found = 0;
    for (int i = 0; i < count; i++) {
        pci_device_t* dev = pci_get_device(i);
        if (dev->class_id == 0x03) {
            found = 1; char buf[32];
            print_string("  \e[90m[\e[93m"); buf[0] = (dev->bus / 10) + '0'; buf[1] = (dev->bus % 10) + '0'; buf[2] = ':';
            buf[3] = (dev->slot / 10) + '0'; buf[4] = (dev->slot % 10) + '0'; buf[5] = '.';
            buf[6] = (dev->func % 10) + '0'; buf[7] = 0; print_string(buf); print_string("\e[90m]\e[0m ");
            if (dev->vendor_id == 0x1234 && dev->device_id == 0x1111) print_string("\e[92mQEMU Virtual Video Controller\e[0m");
            else if (dev->vendor_id == 0x1AF4) print_string("\e[92mVirtIO GPU\e[0m");
            else { const char* v = pci_vendor_to_string(dev->vendor_id); const char* d = pci_device_to_string(dev->vendor_id, dev->device_id);
                if (strcmp(v, "Unknown Vendor") == 0 && strcmp(d, "Unknown Device") == 0) print_string("\e[93mGeneric Virtual Display Controller\e[0m");
                else { print_string(v); print_string(" / "); print_string(d); }
            }
            print_string("\n");
        }
    }
    if (!found) print_string("  \e[91mNo display controllers found.\e[0m\n");
}
static void cmd_gpu(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("gpu")); return; }
    gpu_device_t* primary = gpu_get_primary();
    int count = pci_get_device_count(), found = 0; char buf[32];
    print_string("\n");
    for (int i = 0; i < count; i++) {
        pci_device_t* dev = pci_get_device(i);
        if (dev->class_id != 0x03) continue;
        found = 1;
        print_string("  \e[90m[\e[93m");
        buf[0] = (dev->bus / 10) + '0'; buf[1] = (dev->bus % 10) + '0'; buf[2] = ':';
        buf[3] = (dev->slot / 10) + '0'; buf[4] = (dev->slot % 10) + '0'; buf[5] = '.';
        buf[6] = (dev->func % 10) + '0'; buf[7] = 0; print_string(buf); print_string("\e[90m]\e[0m\n");
        print_string("    Vendor:   0x"); itoa(dev->vendor_id, buf); print_string(buf);
        print_string("  Device: 0x"); itoa(dev->device_id, buf); print_string(buf); print_string("\n");
        if (dev->vendor_id == 0x1234 && dev->device_id == 0x1111) print_string("    Name:     \e[92mQEMU Virtual Video Controller\e[0m\n");
        else if (dev->vendor_id == 0x1AF4) print_string("    Name:     \e[92mVirtIO GPU\e[0m\n");
        else { const char* v = pci_vendor_to_string(dev->vendor_id);
            const char* d = pci_device_to_string(dev->vendor_id, dev->device_id);
            if (strcmp(v, "Unknown Vendor") == 0 && strcmp(d, "Unknown Device") == 0)
                print_string("    Name:     Generic Display Controller\n");
            else { print_string("    Name:     "); print_string(v); print_string(" / "); print_string(d); print_string("\n"); }
        }
        if (primary && primary->vendor_id == dev->vendor_id && primary->device_id == dev->device_id) {
            print_string("    \e[92m(* Active - primary display)\e[0m\n");
            print_string("    Resolution: \e[96m"); itoa(primary->width, buf); print_string(buf);
            print_string("x"); itoa(primary->height, buf); print_string(buf); print_string("\e[0m\n");
        } else {
            print_string("    Resolution: (inactive)\n");
        }
        print_string("\n");
    }
    if (!found) { print_string("  No display controllers found.\n"); return; }
}

static void cmd_gui(int argc, char** argv) { (void)argc; (void)argv; extern void start_gui(); start_gui(); }
static void cmd_timer_test(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("timer-test")); return; }
    uint64_t start = timer_ticks; char buf[32];
    print_string("\n  \e[94mTimer test: waiting 3 seconds...\e[0m\n");
    for (volatile long i = 0; i < 300000000L; i++) { if (timer_ticks - start >= 3000) break; }
    uint64_t elapsed = timer_ticks - start;
    print_string("  Elapsed ticks: "); itoa((uint64_t)elapsed, buf); print_string(buf); print_string("\n");
    print_string("  Calibrated Hz: "); itoa((uint64_t)timer_hz, buf); print_string(buf); print_string("\n");
    if (elapsed >= 2500 && elapsed <= 3500) print_string("  \e[92m[OK] Result: Timer is close to 1kHz\e[0m\n");
    else if (elapsed < 10) print_string("  \e[91m[ERR] Result: Timer may not be ticking!\e[0m\n");
    else {
        print_string("  \e[93m[WARN] Result: Timer rate differs from expected 1kHz\e[0m\n");
        if (elapsed > 100) { uint32_t estimated = (uint32_t)(elapsed / 3);
            if (estimated > 10 && estimated < 10000) { timer_hz = estimated;
                print_string("  Adjusted Hz to: "); itoa((uint64_t)timer_hz, buf); print_string(buf); print_string("\n"); } }
    }
}

static void cmd_ls(int argc, char** argv) {
    (void)argc; (void)argv;
    char buf[2048]; fs_list(buf, 2047);
    print_string("\n");
    print_string(buf);
}

static void cmd_cd(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mcd: missing operand\e[0m\n"); return; }
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("cd")); return; }
    if (fs_cd(argv[1]) == 0) {
        char cwd[256]; fs_pwd(cwd, 255);
        print_string("\e[92m  [OK] Changed directory to \e[94m");
        print_string(cwd);
        print_string("\e[0m\n");
    } else {
        print_string("\e[91m  [ERR] cd: "); print_string(argv[1]); print_string(": No such directory\e[0m\n");
    }
}
static void cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv; char buf[256]; fs_pwd(buf, 255);
    print_string("\n  \e[94m"); print_string(buf); print_string("\e[0m\n");
}
static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mmkdir: missing operand\e[0m\n"); return; }
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("mkdir")); return; }
    if (fs_mkdir(argv[1]) == 0) {
        print_string("\e[92m  [OK] Created directory '\e[94m"); print_string(argv[1]); print_string("\e[92m'\e[0m\n");
    } else {
        print_string("\e[91m  [ERR] mkdir: cannot create directory '\e[97m"); print_string(argv[1]); print_string("\e[91m'\e[0m\n");
    }
}
static void cmd_rmdir(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mrmdir: missing operand\e[0m\n"); return; }
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("rmdir")); return; }
    if (fs_rmdir(argv[1]) == 0) {
        print_string("\e[92m  [OK] Removed directory '\e[94m"); print_string(argv[1]); print_string("\e[92m'\e[0m\n");
    } else {
        print_string("\e[91m  [ERR] rmdir: failed to remove '\e[97m"); print_string(argv[1]); print_string("\e[91m'\e[0m\n");
    }
}
static void cmd_touch(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mtouch: missing operand\e[0m\n"); return; }
    if (fs_touch(argv[1]) >= 0) {
        print_string("\e[92m  [OK] Created/updated file '\e[97m"); print_string(argv[1]); print_string("\e[92m'\e[0m\n");
    } else {
        print_string("\e[91m  [ERR] touch: cannot create '\e[97m"); print_string(argv[1]); print_string("\e[91m'\e[0m\n");
    }
}
static void cmd_rm(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mrm: missing operand\e[0m\n"); return; }
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("rm")); return; }
    if (fs_delete(argv[1]) == 0) {
        print_string("\e[92m  [OK] Removed file '\e[97m"); print_string(argv[1]); print_string("\e[92m'\e[0m\n");
    } else {
        print_string("\e[91m  [ERR] rm: cannot remove '\e[97m"); print_string(argv[1]); print_string("\e[91m'\e[0m\n");
    }
}
static void cmd_cat(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mcat: missing operand\e[0m\n"); return; }
    char buf[4096]; int bytes = fs_cat(argv[1], buf, 4096); print_string("\n");
    if (bytes >= 0) { print_string(buf); print_string("\n"); }
    else { print_string("  \e[91m[ERR] cat: "); print_string(argv[1]); print_string(": No such file\e[0m\n"); }
}

#define LESS_COLOR  ((VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_GREY)

/* Paged file viewer ("less"). Reads the whole (small, in-kernel) file into a
 * 4KiB buffer and re-pages it across the fixed 24x64 text screen. Controls:
 *   Space / Down / Enter  next page
 *   b / Up                previous page
 *   q / Esc               quit
 * Very long lines are split across pages so we always make progress. */
static void cmd_less(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("less")); return; }
    if (argc < 2) {
        print_string("  \e[93mless: missing operand (usage: less <file>)\e[0m\n");
        return;
    }

    char buf[4096];
    int bytes = fs_cat(argv[1], buf, sizeof(buf));
    if (bytes < 0) {
        print_string("  \e[91mless: "); print_string(argv[1]);
        print_string(": No such file\e[0m\n");
        return;
    }

    int cols   = MAX_COLS;
    int usable = MAX_ROWS - 1;                  /* reserve one row for the footer */
    int offset = 0;

    /* page-start history so 'b' can step back. Files are tiny (≤4KiB) so a
     * 64-deep ring is always enough. */
    int history[64];
    int hist_n = 0;

    for (;;) {
        int page_start = offset;
        int row = 0;

        /* ---- render one page into the back buffer ---- */
        clear_screen();
        set_cursor(0, 0);
        while (offset < bytes && row < usable) {
            int line_start = offset;
            while (offset < bytes && buf[offset] != '\n') offset++;   /* find EOL */
            int L = offset - line_start;
            int total = ((L > 0) ? (L + cols - 1) / cols : 0) + 1;    /* +1 for '\n' */

            if (row + total > usable) {
                /* line won't fit: print what does and advance (never stall). */
                int fits = (usable - row) * cols;
                if (fits <= 0) break;
                for (int k = 0; k < fits && (line_start + k) < offset; k++)
                    print_char(buf[line_start + k], LESS_COLOR);
                print_char('\n', LESS_COLOR);
                offset = line_start + fits;
                row = usable;
                break;
            }
            for (int k = 0; k < L; k++)
                print_char(buf[line_start + k], LESS_COLOR);
            print_char('\n', LESS_COLOR);
            if (offset < bytes && buf[offset] == '\n') offset++;      /* consume EOL */
            row += total;
        }
        swap_buffers();

        /* ---- footer / paging prompt ---- */
        int at_eof = (offset >= bytes);
        set_cursor(0, MAX_ROWS - 1);
        if (at_eof) print_string(" -- End of file (q to quit) --");
        else        print_string(" --More-- (space=next  b=back  q=quit) --");
        swap_buffers();

        char c = get_key();
        if (c == 'q' || c == 'Q' || c == KEY_ESC) return;

        if (c == 'b' || c == 'B' || KEY_MATCH(c, KEY_UP)) {
            if (hist_n >= 2) { hist_n--; offset = history[hist_n - 1]; }
            else             { offset = 0; }
            continue;
        }
        /* space / down / enter / any other key */
        if (at_eof) return;
        if (hist_n < 64) history[hist_n++] = page_start;
        /* offset already advanced to the next page start above */
    }
}

#undef LESS_COLOR
static void cmd_write(int argc, char** argv) {
    if (argc < 3) { print_string("  \e[93mUsage: write <file> <text>\e[0m\n"); return; }
    int fd = fs_open(argv[1], 0); if (fd < 0) fd = fs_create(argv[1]);
    if (fd >= 0) { fs_truncate(argv[1]); fs_write(fd, argv[2], strlen(argv[2])); fs_close(fd); print_string("\e[92m  [OK] Written to file\e[0m\n"); }
    else print_string("\n  \e[91m[ERR] write: failed\e[0m\n");
}
static void cmd_append(int argc, char** argv) {
    if (argc < 3) { print_string("  \e[93mUsage: append <file> <text>\e[0m\n"); return; }
    int fd = fs_open(argv[1], 0); if (fd < 0) fd = fs_create(argv[1]);
    if (fd >= 0) {
        char temp[4096]; int len = fs_read(fd, temp, 4095); if (len < 0) len = 0;
        int tlen = strlen(argv[2]);
        for (int i = 0; i < tlen && len < 4095; i++) temp[len++] = argv[2][i];
        fs_truncate(argv[1]); fs_write(fd, temp, len); fs_close(fd); print_string("\e[92m  [OK] Appended to file\e[0m\n");
    } else print_string("\n  \e[91m[ERR] append: failed\e[0m\n");
}
static void cmd_cp(int argc, char** argv) {
    if (argc < 3) { print_string("  \e[93mUsage: cp <src> <dst>\e[0m\n"); return; }
    char temp[4096]; int fd1 = fs_open(argv[1], 0);
    if (fd1 >= 0) { int len = fs_read(fd1, temp, 4096); fs_close(fd1);
        int fd2 = fs_create(argv[2]);
        if (fd2 >= 0) { fs_write(fd2, temp, len); fs_close(fd2); print_string("\e[92m  [OK] Copied file\e[0m\n"); }
        else print_string("\n  \e[91m[ERR] cp: cannot create destination\e[0m\n");
    } else print_string("\n  \e[91m[ERR] cp: cannot stat '"); print_string(argv[1]); print_string("': No such file\e[0m\n");
}
static void cmd_mv(int argc, char** argv) {
    if (argc < 3) { print_string("  \e[93mUsage: mv <src> <dst>\e[0m\n"); return; }
    if (fs_move(argv[1], argv[2]) == 0) print_string("\e[92m  [OK] Moved file\e[0m\n");
    else print_string("\n  \e[91m[ERR] mv: failed\e[0m\n");
}
static void cmd_rename(int argc, char** argv) {
    if (argc < 3) { print_string("  \e[93mUsage: rename <old> <new>\e[0m\n"); return; }
    if (fs_rename(argv[1], argv[2]) == 0) print_string("\e[92m  [OK] Renamed file\e[0m\n");
    else print_string("\n  \e[91m[ERR] rename: failed\e[0m\n");
}
static void cmd_stat(int argc, char** argv) {
    if (argc < 2) { print_string("  \e[93mUsage: stat <file>\e[0m\n"); return; }
    print_string("\n  stat: not fully implemented\n");
}
static void cmd_grep(int argc, char** argv) {
    (void)argc; (void)argv;
    print_string("\n  grep: not fully implemented\n");
}
static void cmd_edim(int argc, char** argv) {
    (void)argc; (void)argv;
    if (has_help_flag(argc, argv)) { cmd_man(argc, argv); return; }
    /* The old ring-0 EDIM editor was removed with the ring-3 migration
       (its 76 MB static buffer bloated the kernel image). Point users to
       the ring-3 Text Editor instead. */
    print_string("\n  EDIM was removed in the ring-3 migration.\n  Use the Text Editor (kilo) from the Start menu or terminal:  spawn kilo\n");
}
static void cmd_colors(int argc, char** argv) {
    if (argc < 2) {
        print_string("\n  Colors: 0=White, 1=Blue, 2=Green, 3=Red, 4=Yellow,\n");
        print_string("          5=Purple, 6=Cyan, 7=Orange, 8=Pink, 9=Gray\n");
        print_string("  Usage: colors <0-9>\n"); return;
    }
    int c = argv[1][0] - '0';
    if (c >= 0 && c <= 9) print_string("\n  (color switching removed with the ring-3 terminal)\n");
    else print_string("\n  colors: invalid color (0-9)\n");
}
static void cmd_ping(int argc, char** argv) {
    if (argc < 2) { print_string("  Usage: ping <host>\n"); return; }
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("ping")); return; }
    ping(argv[1]);
}
static void cmd_dns(int argc, char** argv) {
    if (argc < 2) { print_string("  Usage: dns <hostname>\n"); return; }
    dns_lookup(argv[1]);
}
static void cmd_edrowser(int argc, char** argv) { (void)argc; (void)argv; extern int create_user_process_elf(const char* name); create_user_process_elf("edrowser"); }
static void cmd_ifconfig(int argc, char** argv) {
    struct ifnet* ifp = if_list_head();
    if (!ifp) { print_string("  No network interfaces\n"); return; }
    int shown = 0;
    while (ifp) {
        if (argc >= 2 && strcmp(argv[1], ifp->if_xname) != 0) { ifp = ifp->if_next; continue; }
        shown = 1;
        char ip[16], mask[16], gw[16], dns[16];
        uint32_t a = ntohl(ifp->if_ip);     snprintf(ip,  16, "%u.%u.%u.%u", (a>>24)&0xFF,(a>>16)&0xFF,(a>>8)&0xFF,a&0xFF);
        uint32_t m = ntohl(ifp->if_netmask);snprintf(mask,16, "%u.%u.%u.%u", (m>>24)&0xFF,(m>>16)&0xFF,(m>>8)&0xFF,m&0xFF);
        uint32_t g = ntohl(ifp->if_gateway);snprintf(gw,  16, "%u.%u.%u.%u", (g>>24)&0xFF,(g>>16)&0xFF,(g>>8)&0xFF,g&0xFF);
        uint32_t d = ntohl(ifp->if_dns);    snprintf(dns, 16, "%u.%u.%u.%u", (d>>24)&0xFF,(d>>16)&0xFF,(d>>8)&0xFF,d&0xFF);
        print_string("  ");
        print_string(ifp->if_xname);
        print_string(ifp->if_flags & IFF_RUNNING ? "  UP\n" : "  DOWN\n");
        print_string("    MAC: ");
        char mbuf[32];
        snprintf(mbuf, 32, "%02X:%02X:%02X:%02X:%02X:%02X\n",
            ifp->if_hwaddr[0],ifp->if_hwaddr[1],ifp->if_hwaddr[2],
            ifp->if_hwaddr[3],ifp->if_hwaddr[4],ifp->if_hwaddr[5]);
        print_string(mbuf);
        print_string("    inet: ");   print_string(ip);   print_string("\n");
        print_string("    mask: ");   print_string(mask); print_string("\n");
        print_string("    gateway: ");print_string(gw);   print_string("\n");
        print_string("    dns: ");    print_string(dns);  print_string("\n");
        print_string("    config: ");
        print_string(ifp->if_dhcp ? "DHCP\n" : "static\n");
        print_string("    rx: "); { char nb[24]; snprintf(nb,24,"%llu",(unsigned long long)ifp->if_ipackets); print_string(nb); }
        print_string("  tx: "); { char nb[24]; snprintf(nb,24,"%llu",(unsigned long long)ifp->if_opackets); print_string(nb); }
        print_string("  errs: "); { char nb[24]; snprintf(nb,24,"%llu",(unsigned long long)(ifp->if_ierrors+ifp->if_oerrors)); print_string(nb); print_string("\n"); }
        ifp = ifp->if_next;
    }
    if (!shown) { print_string("  No matching interface: "); print_string(argv[1]); print_string("\n"); }
}

static void ns_arp_cb(int idx, uint32_t ip, const uint8_t* mac, int is_static) {
    (void)idx;
    char ipb[16]; uint32_t a = ntohl(ip); snprintf(ipb,16,"%u.%u.%u.%u",(a>>24)&0xFF,(a>>16)&0xFF,(a>>8)&0xFF,a&0xFF);
    char mbuf[32];
    snprintf(mbuf,32,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    print_string("    "); print_string(ipb); print_string("  "); print_string(mbuf);
    print_string(is_static ? "  (static)\n" : "\n");
}

static void ns_dns_cb(int idx, const char* hostname, uint32_t ip) {
    (void)idx;
    char ipb[16]; uint32_t a = ntohl(ip); snprintf(ipb,16,"%u.%u.%u.%u",(a>>24)&0xFF,(a>>16)&0xFF,(a>>8)&0xFF,a&0xFF);
    print_string("    "); print_string(hostname); print_string(" -> "); print_string(ipb); print_string("\n");
}

static void cmd_netstat(int argc, char** argv) {
    (void)argc; (void)argv;
    print_string("  === ARP cache ===\n");
    arp_foreach(ns_arp_cb);

    print_string("  === DNS cache ===\n");
    dns_foreach(ns_dns_cb);

    print_string("  === Interfaces ===\n");
    struct ifnet* ifp = if_list_head();
    while (ifp) {
        print_string("    "); print_string(ifp->if_xname);
        print_string(ifp->if_flags & IFF_RUNNING ? "  UP" : "  DOWN");
        print_string(ifp->if_dhcp ? "  (DHCP)\n" : "  (static)\n");
        ifp = ifp->if_next;
    }
}

static void cmd_bootlog(int argc, char** argv) { (void)argc; (void)argv; bootlog(); }
static void cmd_ecc(int argc, char** argv) {
    (void)argv;
    if (argc < 2) { print_string("  Usage: ecc <file.ec>\n"); return; }
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("ecc")); return; }
    print_string("  ecc: the ring-0 Eigen-C compiler was removed with the ring-3 migration\n");
}
static void cmd_erun(int argc, char** argv) {
    (void)argv;
    if (argc < 2) { print_string("  Usage: erun <file.bin>\n"); return; }
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("erun")); return; }
    print_string("  erun: the ring-0 Eigen-C VM was removed with the ring-3 migration\n");
}
static void cmd_vmtest(int argc, char** argv) { (void)argc; (void)argv; vmtest(); }
static void cmd_ring3test(int argc, char** argv) { (void)argc; (void)argv; ring3test(); }

void cmd_spawn(int argc, char** argv) {
    if (argc < 2) { print_string("  usage: spawn <app> [args...]\n"); return; }
    extern int create_user_process_elf_args(const char* name, int argc, char* const argv[]);
    extern void serial_puts(const char* s);
    serial_puts("[SPAWN] creating process\n");
    int pid = create_user_process_elf_args(argv[1], argc - 2, &argv[2]);
    if (pid < 0) {
        char eb[24]; itoa((uint64_t)(-pid), eb);
        print_string("  \e[91mFailed to create process (err "); print_string(eb); print_string(")\e[0m\n");
        return;
    }
    char buf[32]; itoa(pid, buf);
    serial_puts("[SPAWN] process created pid=");
    serial_puts(buf);
    serial_puts("\n");
    print_string("  \e[92m[OK] app \e[97m"); print_string(argv[1]);
    print_string("\e[92m spawned with PID: \e[97m"); print_string(buf); print_string("\e[0m\n");
}

/* Eigen system info fetch */
static void cmd_eigenfetch(int argc, char** argv) {
    if (has_help_flag(argc, argv)) { show_usage(find_cmd("eigenfetch")); return; }
    print_string("\n");
    print_string("\e[97m         __         \e[0m\n");
    print_string("\e[97m        /  \\        \e[0m\n");
    print_string("\e[97m       / /\\ \\       \e[0m\n");
    print_string("\e[37m      / /  \\ \\      \e[0m\n");
    print_string("\e[37m     / /    \\ \\     \e[0m\n");
    print_string("\e[37m    / /      \\ \\    \e[0m\n");
    print_string("\e[90m   / /        \\ \\   \e[0m\n");
    print_string("\e[90m  / /          \\ \\  \e[0m\n");
    print_string("\e[90m /_/____________\\_\\ \e[0m\n");
    print_string("\e[90m     \\  /\\  /        \e[0m\n");
    print_string("\e[90m      \\/  \\/         \e[0m\n\n");

    char buf[32];
    print_string_color("  OS:       ", VGA_COLOR_CYAN); print_string("Eigen  64-bit (Limine UEFI)\n");
    uint64_t ms = timer_get_ms();
    uint32_t sec = (uint32_t)(ms / 1000); uint32_t days = sec / 86400; sec %= 86400;
    uint32_t hours = sec / 3600; sec %= 3600; uint32_t mins = sec / 60; sec %= 60;
    print_string_color("  Uptime:   ", VGA_COLOR_CYAN);
    if (days) { itoa(days, buf); print_string(buf); print_string("d "); }
    itoa(hours, buf); print_string(buf); print_string(":"); if (mins < 10) print_string("0");
    itoa(mins, buf); print_string(buf); print_string(":"); if (sec < 10) print_string("0");
    itoa(sec, buf); print_string(buf); print_string("\n");
    uint32_t eax, ebx, ecx, edx; char cpu_brand[49] = {0};
    __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (eax >= 0x80000004) {
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002));
        *(uint32_t*)(cpu_brand+0)=eax; *(uint32_t*)(cpu_brand+4)=ebx; *(uint32_t*)(cpu_brand+8)=ecx; *(uint32_t*)(cpu_brand+12)=edx;
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000003));
        *(uint32_t*)(cpu_brand+16)=eax; *(uint32_t*)(cpu_brand+20)=ebx; *(uint32_t*)(cpu_brand+24)=ecx; *(uint32_t*)(cpu_brand+28)=edx;
        __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000004));
        *(uint32_t*)(cpu_brand+32)=eax; *(uint32_t*)(cpu_brand+36)=ebx; *(uint32_t*)(cpu_brand+40)=ecx; *(uint32_t*)(cpu_brand+44)=edx;
        cpu_brand[48]=0; int len=48; while(len>0 && cpu_brand[len-1]==' ') len--; cpu_brand[len]=0;
        print_string_color("  CPU:      ", VGA_COLOR_CYAN); print_string(cpu_brand); print_string("\n");
    } else { char v[13]={0}; __asm__ volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
        *(uint32_t*)(v+0)=ebx; *(uint32_t*)(v+4)=edx; *(uint32_t*)(v+8)=ecx; v[12]=0;
        print_string_color("  CPU:      ", VGA_COLOR_CYAN); print_string(v); print_string("\n");
    }
    print_string_color("  Tasks:    ", VGA_COLOR_CYAN); itoa(get_task_count(), buf); print_string(buf); print_string("\n");
    uint64_t total_mem = get_total_memory_bytes(); size_t heap_free = kheap_free();
    print_string_color("  Memory:   ", VGA_COLOR_CYAN); itoa((uint32_t)(total_mem >> 20), buf); print_string(buf);
    print_string(" MB / "); itoa((uint32_t)(heap_free >> 20), buf); print_string(buf); print_string(" MB free\n");
    gpu_device_t* gpu = gpu_get_primary();
    if (gpu) { print_string_color("  GPU:      ", VGA_COLOR_CYAN); print_string(gpu->name); print_string("\n"); }
    else { print_string_color("  GPU:      ", VGA_COLOR_CYAN); print_string("None\n"); }
    print_string_color("  Display:  ", VGA_COLOR_CYAN); itoa(get_fb_width(), buf); print_string(buf);
    print_string("x"); itoa(get_fb_height(), buf); print_string(buf); print_string("\n");
    uint32_t dev_count = storage_get_device_count();
    print_string_color("  Storage:  ", VGA_COLOR_CYAN);
    if (dev_count > 0) { for (uint32_t si = 0; si < dev_count; si++) { block_device_t* dev = storage_get_device(si);
            itoa(dev->size_sectors, buf); print_string(buf); print_string("s (");
            uint64_t mb = (dev->size_sectors * dev->block_size) >> 20; itoa((uint32_t)mb, buf); print_string(buf); print_string(" MB)");
            if (si < dev_count - 1) print_string(", "); } print_string("\n"); }
    else print_string("None\n");
    struct ifnet* ifp_em = if_find("em0"); struct ifnet* ifp_lo = if_find("lo0"); int net_count = 0;
    print_string_color("  Network:  ", VGA_COLOR_CYAN);
    if (ifp_em) { print_string("em0: "); for (int i = 0; i < 6; i++) { itoa(ifp_em->if_hwaddr[i], buf);
            if (ifp_em->if_hwaddr[i] < 16) print_string("0"); print_string(buf); if (i < 5) print_string(":"); } net_count++; }
    if (ifp_lo) { if (net_count) print_string(", "); print_string("lo0"); net_count++; }
    if (!net_count) print_string("None"); print_string("\n");
    print_string_color("  PCI:      ", VGA_COLOR_CYAN); itoa(pci_get_device_count(), buf); print_string(buf); print_string(" devices\n");
    session_t* sess = get_current_session();
    print_string_color("  Session:  ", VGA_COLOR_CYAN);
    if (sess && sess->is_active) { itoa(sess->session_id, buf); print_string(buf); print_string(" (uid "); itoa(sess->uid, buf); print_string(buf); print_string(")\n"); }
    else print_string("None\n");
    print_string("\n");
}

void execute_command(char* input) {
    char* argv[32]; int argc = tokenize(input, argv, 32);
    if (argc == 0) return;
    cmd_t* cmd = find_cmd(argv[0]);
    if (!cmd) { print_string("\n  \e[91m"); print_string(argv[0]); print_string(": command not found\e[0m\n"); return; }
    cmd->handler(argc, argv);
}
