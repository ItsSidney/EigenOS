/* Minimal freestanding sys/types.h for the DOOM port.
 * Guarded against host headers that may also define these. */
#ifndef EIGEN_SHIM_SYSTYPES_H
#define EIGEN_SHIM_SYSTYPES_H
#ifndef _SSIZE_T_DEFINED
typedef long ssize_t;
#define _SSIZE_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _OFF_T_DEFINED
typedef long off_t;
#define _OFF_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _MODE_T_DEFINED
typedef unsigned long mode_t;
#define _MODE_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _PID_T_DEFINED
typedef int pid_t;
#define _PID_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _DEV_T_DEFINED
typedef unsigned long dev_t;
#define _DEV_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _INO_T_DEFINED
typedef unsigned long ino_t;
#define _INO_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _NLINK_T_DEFINED
typedef unsigned int nlink_t;
#define _NLINK_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _UID_T_DEFINED
typedef unsigned int uid_t;
#define _UID_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _GID_T_DEFINED
typedef unsigned int gid_t;
#define _GID_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _TIME_T_DEFINED
typedef long time_t;
#define _TIME_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _LOCALE_T_DEFINED
typedef void* locale_t;      /* EigenOS has no locale; opaque handle for ports (Eina). */
#define _LOCALE_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef _ID_T_DEFINED
typedef unsigned int id_t;
#define _ID_T_DEFINED
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
#ifndef EIGEN_USECONDS_T
#define EIGEN_USECONDS_T
typedef unsigned int useconds_t;
#endif
#endif
