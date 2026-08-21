/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/storage/ide.h"
#include "drivers/input/keyboard.h" 
#include "commands/commands.h"

extern void draw_boot_log(void);

#define IDE_PRIMARY_BASE   0x1F0
#define IDE_SECONDARY_BASE 0x170

#define IDE_REG_DATA       0x00
#define IDE_REG_ERROR      0x01
#define IDE_REG_SEC_COUNT  0x02
#define IDE_REG_LBA_LOW    0x03
#define IDE_REG_LBA_MID    0x04
#define IDE_REG_LBA_HIGH   0x05
#define IDE_REG_DRIVE_SEL  0x06
#define IDE_REG_COMMAND    0x07
#define IDE_REG_STATUS     0x07

#define IDE_CMD_READ_PIO   0x20
#define IDE_CMD_WRITE_PIO  0x30
#define IDE_CMD_IDENTIFY   0xEC

extern void serial_puts(const char* s);

static int ide_wait_busy(uint16_t base, uint32_t timeout) {
    uint32_t check = 0;
    while (timeout--) {
        if (!(port_byte_in(base + IDE_REG_STATUS) & 0x80)) return 0;
        if (++check % 256 == 0) draw_boot_log();
        for(volatile int i=0; i<10; i++);
    }
    return -1;
}

static int ide_wait_ready(uint16_t base, uint32_t timeout) {
    uint32_t check = 0;
    while (timeout--) {
        uint8_t status = port_byte_in(base + IDE_REG_STATUS);
        if (status & 0x08) return 0;
        if (status & 0x01) return -1; // Error
        if (++check % 256 == 0) draw_boot_log();
        for(volatile int i=0; i<10; i++);
    }
    return -1;
}

int ide_read_sectors(block_device_t* dev, uint64_t lba, uint32_t count, void* buf) {
    uint16_t base = (uintptr_t)dev->priv;
    uint16_t* ptr = (uint16_t*)buf;

    if (ide_wait_busy(base, 1000) < 0) return -1;
    
    port_byte_out(base + IDE_REG_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
    port_byte_out(base + IDE_REG_SEC_COUNT, count);
    port_byte_out(base + IDE_REG_LBA_LOW, (uint8_t)lba);
    port_byte_out(base + IDE_REG_LBA_MID, (uint8_t)(lba >> 8));
    port_byte_out(base + IDE_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    port_byte_out(base + IDE_REG_COMMAND, IDE_CMD_READ_PIO);

    for (uint32_t i = 0; i < count; i++) {
        if (ide_wait_busy(base, 1000) < 0) return -1;
        if (ide_wait_ready(base, 1000) < 0) return -1;
        for (int j = 0; j < 256; j++) {
            *ptr++ = port_word_in(base + IDE_REG_DATA);
        }
    }
    return 0;
}

void ide_init() {
    serial_puts("[IDE] Initializing...\n");
    // Non-blocking presence check
    uint8_t status = port_byte_in(IDE_PRIMARY_BASE + IDE_REG_STATUS);
    if (status == 0xFF || status == 0x00) {
        serial_puts("[IDE] No primary master found. Continuing...\n");
        return;
    }

    // Attempt limited wait, but don't stall
    port_byte_out(IDE_PRIMARY_BASE + IDE_REG_DRIVE_SEL, 0xA0);
    
    // Minimal delay
    for(volatile int i=0; i<100; i++);
    
    status = port_byte_in(IDE_PRIMARY_BASE + IDE_REG_STATUS);
    if (status & 0x80) {
        if (ide_wait_busy(IDE_PRIMARY_BASE, 100) < 0) {
            serial_puts("[IDE] Primary master not responsive.\n");
            return;
        }
    }

    static block_device_t ide0;
    strcpy(ide0.name, "ide0");
    ide0.block_size = 512;
    ide0.read = ide_read_sectors;
    ide0.priv = (void*)IDE_PRIMARY_BASE;
    storage_register_device(&ide0);
    serial_puts("[IDE] Registered ide0\n");
}
