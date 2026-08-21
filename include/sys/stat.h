/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#ifndef _WIN32
#define S_IFDIR 0040000
#define S_IFREG 0100000

struct stat {
    unsigned short st_mode;
    int st_size;
};

int stat(const char* path, struct stat* buf);
int mkdir(const char* path);

static inline int mkdir_mode(const char* path, unsigned int mode) {
    (void)mode;
    return mkdir(path);
}
#define mkdir(path, mode) mkdir_mode(path, mode)
#endif

#endif
