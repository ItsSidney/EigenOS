/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef FAT32_H
#define FAT32_H

#include "drivers/storage/storage.h"
#include "filesystem/vfs.h"

vfs_node_t* fat32_init(block_device_t* dev, uint32_t partition_lba);

#endif
