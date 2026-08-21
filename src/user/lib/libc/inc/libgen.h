/* Freestanding libgen.h shim for EigenOS ring-3 / EFL port. */
#ifndef EIGEN_SHIM_LIBGEN_H
#define EIGEN_SHIM_LIBGEN_H

/* POSIX basename/dirname — our libc.c provides these. */
char *basename(char *path);
char *dirname(char *path);

#endif /* EIGEN_SHIM_LIBGEN_H */
