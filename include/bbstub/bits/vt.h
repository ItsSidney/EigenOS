/* stub vt */
#ifndef _STUB_BITS_VT_H
#define _STUB_BITS_VT_H
#define VT_RELDISP 1
#define VT_ACKACQ 2
#define VT_ACTIVATE 3
#define VT_WAITACTIVE 4
#define VT_GETMODE 5
#define VT_SETMODE 6
#define VT_OPENQRY 7
#define VT_GETSTATE 8
struct vt_mode { char mode, waitv, relsig, acqsig, frsig; };
struct vt_stat { unsigned short v_active, v_signal, v_state; };
#define VT_PROCESS 1
#define VT_AUTO 0
#endif
