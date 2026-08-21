/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

typedef struct block_device {
    char name[32];
    uint64_t size_sectors;
    uint32_t block_size;
    int (*read)(struct block_device* dev, uint64_t start_lba, uint32_t count, void* buf);
    int (*write)(struct block_device* dev, uint64_t start_lba, uint32_t count, const void* buf);
    void* priv; // Driver private data
} block_device_t;

void storage_register_device(block_device_t* dev);
uint32_t storage_get_device_count();
block_device_t* storage_get_device(uint32_t index);

#endif
