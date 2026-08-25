/* stub SysV msg */
#ifndef _STUB_BITS_MSG_H
#define _STUB_BITS_MSG_H
#include <bits/ipc.h>
struct msqid_ds { struct ipc_perm msg_perm; };
#define MSG_NOERROR 010000
#define MSG_EXCEPT 020000
#endif
