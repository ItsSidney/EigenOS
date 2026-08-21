/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_MOUNT_POINTS 8
#define MAX_FILESYSTEMS 4

typedef enum {
    VFS_FILE,
    VFS_DIRECTORY
} vfs_type_t;

struct vfs_node;

typedef struct {
    int (*open)(struct vfs_node* node, uint32_t flags);
    int (*read)(struct vfs_node* node, char* buf, uint32_t count, uint32_t offset);
    int (*write)(struct vfs_node* node, const char* buf, uint32_t count, uint32_t offset);
    int (*close)(struct vfs_node* node);
    int (*readdir)(struct vfs_node* node, uint32_t index, char* name, vfs_type_t* type);
    struct vfs_node* (*finddir)(struct vfs_node* node, const char* name);
} vfs_ops_t;

typedef struct vfs_node {
    char name[64];
    uint32_t flags;
    uint32_t size;
    vfs_type_t type;
    vfs_ops_t* ops;
    void* device; // Optional pointer to device-specific data
    struct vfs_node* ptr; // Pointer to another node (for mount points)
} vfs_node_t;

extern vfs_node_t* vfs_root;

void vfs_init();
int vfs_mount(const char* path, vfs_node_t* local_root);
vfs_node_t* vfs_open_path(const char* path);

// Standard VFS operations
int vfs_read(vfs_node_t* node, char* buf, uint32_t count, uint32_t offset);
int vfs_write(vfs_node_t* node, const char* buf, uint32_t count, uint32_t offset);
void vfs_close(vfs_node_t* node);

#endif
