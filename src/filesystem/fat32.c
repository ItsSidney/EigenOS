/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "filesystem/fat32.h"
#include "kernel/mem/kheap.h"
#include "commands/commands.h"

typedef struct {
    uint8_t  boot_jmp[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  table_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t table_size_16;
    uint16_t sectors_per_track;
    uint16_t head_side_count;
    uint32_t hidden_sector_count;
    uint32_t total_sectors_32;
    // FAT32 Extended BPB
    uint32_t table_size_32;
    uint16_t extended_flags;
    uint16_t fat_version;
    uint32_t root_cluster;
    uint16_t fat_info;
    uint16_t backup_BS_sector;
    uint8_t  reserved_0[12];
    uint8_t  drive_number;
    uint8_t  reserved_1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fat_type_label[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct {
    block_device_t* dev;
    uint32_t partition_lba;
    fat32_bpb_t bpb;
    uint32_t first_data_sector;
    uint32_t fat_sector;
} fat32_t;

static int fat32_read(vfs_node_t* node, char* buf, uint32_t count, uint32_t offset) {
    return 0;
}

static vfs_ops_t fat32_ops = {
    .read = fat32_read
};

vfs_node_t* fat32_init(block_device_t* dev, uint32_t partition_lba) {
    if (!dev || !dev->read) return 0;

    uint8_t sector_buf[512];
    if (dev->read(dev, partition_lba, 1, sector_buf) != 0) return 0;
    
    fat32_bpb_t* bpb = (fat32_bpb_t*)sector_buf;
    
    // Check for FAT32 markers (Extended BPB markers at offset 66 are 0x28 or 0x29)
    if (bpb->boot_signature != 0x28 && bpb->boot_signature != 0x29) return 0;
    
    fat32_t* fs = (fat32_t*)kmalloc(sizeof(fat32_t));
    if (!fs) return 0;

    fs->dev = dev;
    fs->partition_lba = partition_lba;
    fs->bpb = *bpb;
    fs->fat_sector = partition_lba + bpb->reserved_sector_count;
    fs->first_data_sector = fs->fat_sector + (bpb->table_count * bpb->table_size_32);
    
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) { kfree(fs); return 0; }

    for(int i=0; i<63; i++) {
        node->name[i] = dev->name[i];
        if (!dev->name[i]) break;
    }
    node->name[63] = 0;

    node->type = VFS_DIRECTORY;
    node->ops = &fat32_ops;
    node->device = fs;
    node->ptr = 0;
    node->flags = 0;
    node->size = 0;
    
    return node;
}
