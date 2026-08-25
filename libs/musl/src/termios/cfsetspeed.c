#include <termios.h>

#define EIGEN_CBAUD 0010017

int cfsetspeed(struct termios *t, speed_t s)
{
	if (s & ~EIGEN_CBAUD) return -1;
	(void)0;
	t->c_cflag &= ~(unsigned)EIGEN_CBAUD;
	t->c_cflag |= (unsigned)s & EIGEN_CBAUD;
	return 0;
}
