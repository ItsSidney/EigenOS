/* termios.h — POSIX terminal control for EigenOS (tier 1).
 *
 * The kernel delivers raw keyboard bytes to ring-3 fds already, so there
 * is no kernel line discipline to program: these calls keep REAL per-
 * process state (so raw/cooked bookkeeping works inside your app) and
 * route queue-flushing to the console driver. Canonical-mode echo/edit
 * is NOT emulated by the kernel — raw-mode programs (kilo/dte-class)
 * are fully functional; cooked readers should do their own line edit. */

#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <stdint.h>
#include <sys/types.h>

typedef unsigned char cc_t;
typedef unsigned int  speed_t;
typedef unsigned int  tcflag_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

/* c_iflag */
#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IUCLC   0001000
#define IXON    0002000
#define IXANY   0004000
#define IXOFF   0010000
#define IMAXBEL 0020000
#define IUTF8   0040000

/* c_oflag */
#define OPOST   0000001
#define OLCUC   0000002
#define ONLCR   0000004
#define OCRNL   0000010
#define ONOCR   0000020
#define ONLRET  0000040

/* c_cflag */
#define CSIZE   0000060
#define CS5     0000000
#define CS6     0000020
#define CS7     0000040
#define CS8     0000060
#define CSTOPB  0000100
#define CREAD   0000200
#define PARENB  0000400
#define PARODD  0001000
#define HUPCL   0002000
#define CLOCAL  0004000

/* c_lflag */
#define ISIG    0000001
#define ICANON  0000002
#define ECHO    0000010
#define ECHOE   0000020
#define ECHOK   0000040
#define ECHONL  0000100
#define NOFLSH  0000200
#define TOSTOP  0000400
#define ECHOCTL 0001000
#define ECHOPRT 0002000
#define ECHOKE  0004000
#define FLUSHO  0020000
#define PENDIN  0040000
#define IEXTEN  0100000
#define EXTPROC 0200000

/* c_cc subscript names */
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP   10
#define VEOL    11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT  15
#define _POSIX_VDISABLE 0

/* baud */
#define B0       0000000
#define B50      0000001
#define B75      0000002
#define B110     0000003
#define B134     0000004
#define B150     0000005
#define B200     0000006
#define B300     0000007
#define B600     0000010
#define B1200    0000011
#define B1800    0000012
#define B2400    0000013
#define B4800    0000014
#define B9600    0000015
#define B19200   0000016
#define B38400   0000017
#define B57600   0010001
#define B115200  0010002
#define B230400  0010003
#define B460800  0010004

/* setattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* flush queue selectors */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

int     tcgetattr(int fd, struct termios* t);
int     tcsetattr(int fd, int action, const struct termios* t);
void    cfmakeraw(struct termios* t);
void    cfmakeraw_nux(struct termios* t);
speed_t cfgetispeed(const struct termios* t);
speed_t cfgetospeed(const struct termios* t);
int     cfsetispeed(struct termios* t, speed_t s);
int     cfsetospeed(struct termios* t, speed_t s);
int     tcflush(int fd, int queue);
int     tcdrain(int fd);
int     tcsendbreak(int fd, int dur);
pid_t   tcgetsid(int fd);

#endif /* _TERMIOS_H */
