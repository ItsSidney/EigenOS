/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "filesystem/ramfs.h"
#include "filesystem/filesystem.h"
#include "kernel/mem/kheap.h"
#include "commands/commands.h"

static int ramfs_read(vfs_node_t* node, char* buf, uint32_t count, uint32_t offset) {
    // Offset is ignored in current simple RAMFS
    int fd = fs_open(node->name, 0);
    if (fd < 0) return -1;
    int r = fs_read(fd, buf, count);
    fs_close(fd);
    return r;
}

static vfs_ops_t ramfs_ops = {
    .read = ramfs_read
};

vfs_node_t* ramfs_init() {
    vfs_node_t* root = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    strcpy(root->name, "/");
    root->type = VFS_DIRECTORY;
    root->ops = &ramfs_ops;
    return root;
}
