/* Minimal freestanding sys/stat.h for ring-3 apps. */
#ifndef EIGEN_SHIM_SYSSTAT_H
#define EIGEN_SHIM_SYSSTAT_H
#include <sys/types.h>
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFMT  0170000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

struct stat {
    dev_t     st_dev;
    ino_t     st_ino;
    mode_t    st_mode;
    nlink_t   st_nlink;
    uid_t     st_uid;
    gid_t     st_gid;
    dev_t     st_rdev;
    off_t     st_size;
    time_t    st_atime;
    time_t    st_mtime;
    time_t    st_ctime;
};

int stat(const char* path, struct stat* buf);
int fstat(int fd, struct stat* buf);
int mkdir(const char* path, mode_t mode);
#endif
