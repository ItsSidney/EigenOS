/* stub: SysV IPC unsupported */
#ifndef _STUB_BITS_IPC_H
#define _STUB_BITS_IPC_H
struct ipc_perm { unsigned int key, uid, gid, cuid, cgid, mode, seq; };
#define IPC_CREAT 01000
#define IPC_EXCL 02000
#define IPC_NOWAIT 04000
#define IPC_PRIVATE 0
#define IPC_RMID 0
#define IPC_SET 1
#define IPC_STAT 2
#define IPC_INFO 3
#endif
