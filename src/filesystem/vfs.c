/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#include "filesystem/vfs.h"
#include "commands/commands.h"

vfs_node_t* vfs_root = 0;
static vfs_node_t mount_points[MAX_MOUNT_POINTS];
static uint32_t mount_count = 0;

void vfs_init() {
    vfs_root = 0;
    mount_count = 0;
}

int vfs_mount(const char* path, vfs_node_t* local_root) {
    if (strcmp(path, "/") == 0) {
        vfs_root = local_root;
        return 0;
    }
    
    if (mount_count >= MAX_MOUNT_POINTS) return -1;
    
    vfs_node_t* mnt = &mount_points[mount_count++];
    strcpy(mnt->name, path);
    mnt->ptr = local_root;
    mnt->type = VFS_DIRECTORY;
    mnt->ops = 0;
    
    return 0;
}

vfs_node_t* vfs_open_path(const char* path) {
    if (!path || path[0] != '/') return vfs_root;
    
    // Check for /mnt/ prefix
    if (strncmp(path, "/mnt/", 5) == 0) {
        char part_name[64];
        int i = 5, j = 0;
        while (path[i] && path[i] != '/' && j < 63) part_name[j++] = path[i++];
        part_name[j] = 0;
        
        for (uint32_t m = 0; m < mount_count; m++) {
            if (strcmp(mount_points[m].name, part_name) == 0) {
                // Should traverse subpath in mount_points[m].ptr
                return mount_points[m].ptr;
            }
        }
    }
    
    return vfs_root;
}

int vfs_read(vfs_node_t* node, char* buf, uint32_t count, uint32_t offset) {
    if (node && node->ops && node->ops->read)
        return node->ops->read(node, buf, count, offset);
    return -1;
}

int vfs_write(vfs_node_t* node, const char* buf, uint32_t count, uint32_t offset) {
    if (node && node->ops && node->ops->write)
        return node->ops->write(node, buf, count, offset);
    return -1;
}

void vfs_close(vfs_node_t* node) {
    if (node && node->ops && node->ops->close)
        node->ops->close(node);
}
