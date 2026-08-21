/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "drivers/storage/storage.h"
#include "commands/commands.h"

static block_device_t* devices[16];
static uint32_t device_count = 0;

void storage_register_device(block_device_t* dev) {
    if (device_count < 16) {
        devices[device_count++] = dev;
    }
}

uint32_t storage_get_device_count() { return device_count; }
block_device_t* storage_get_device(uint32_t index) { return (index < device_count) ? devices[index] : 0; }
