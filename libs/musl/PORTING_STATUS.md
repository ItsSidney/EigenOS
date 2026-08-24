# EigenOS musl port — continuation guide

## What's DONE
- musl 1.2.5 vendored to libs/musl/ (13MB full source)
- arch/eigen/syscall_arch.h written: int $0x80 bridge for __syscall0-6
- Directory structure ready

## Next steps (ordered)

### Phase 1: Minimal compile (~week 1)
1. Create `arch/eigen/bits/` directory with EigenOS-specific headers:
   - `alltypes.h` — typedefs (needs to match x86_64 layout)
   - `syscall.h.in` — syscall number mapping (use EIGEN_SYS_* from eigen.h)
   - `fcntl.h`, `ioctl.h`, `stat.h` — struct layouts
   Copy from `arch/x86_64/bits/` and adjust syscall numbers.
2. Compile just these musl dirs first:
   - src/string/*.c     (memcpy, strlen, strcmp, etc)
   - src/stdlib/*.c     (malloc via _sbrk, atoi, strtol, qsort)
   - src/stdio/*.c      (printf, fprintf, fopen, fread)
   - src/math/*.c       (sin, cos, pow, sqrt)
   - src/env/*.c        (getenv, setenv)
3. Skip for now:
   - src/linux/         (Linux-specific syscalls)
   - src/process/       (fork/exec — no fork on EigenOS)
   - src/thread/        (pthread — optional, skip v1)
   - src/mq/, src/sched/, src/ipc/  (not applicable)

### Phase 2: Syscall bridge (~week 2)
Map musl internal syscalls to EigenOS EIGEN_SYS_* numbers:

| Musl expects | EigenOS equivalent |
|---|---|
| SYS_open / openat | EIGEN_SYS_OPEN (0) |
| SYS_read | EIGEN_SYS_READ (2) |
| SYS_write | EIGEN_SYS_WRITE (1) |
| SYS_close | EIGEN_SYS_CLOSE (3) |
| SYS_lseek | EIGEN_SYS_LSEEK (22) |
| SYS_getpid | EIGEN_SYS_GETPID (4) |
| SYS_exit | EIGEN_SYS_EXIT (9) |
| mkdir | EIGEN_SYS_FS op=EIGEN_FS_MKDIR |

Create `src/internal/syscall.h` that maps musl's SYS_* constants
to EIGEN_SYS_* values.

### Phase 3: Link + test (~week 2-3)
1. Build libmusl.a ✓
2. Compile hello-world against it (no other libs needed) ✓
3. NetSurf removed entirely — musl is now the C runtime for the
   terminal/shell/busybox effort instead.
4. (n/a)

## Gotchas
- musl uses `syscall()` instruction on x86_64 natively; we use `int 0x80`.
  The syscall_arch.h replacement handles this.
- musl's malloc uses mmap for large allocations. Replace with eigen_malloc
  or provide a minimal mmap shim.
- musl's stdio assumes FILE* backed by fds. Our fd layer exists ✓
- No signals yet: stub kill/sigaction as return -1
- No fork: stub as -1 (spawn only)

## Key files to create next session
```
libs/musl/arch/eigen/
  bits/alltypes.h        ← copy from x86_64, adjust
  bits/fcntl.h           ← EigenOS fd constants
  bits/stat.h            ← struct stat layout matching kernel
  bits/syscall.h.in      ← SYS_* number mapping to EIGEN_SYS_*
  syscall_arch.h         ← DONE ✓
src/user/lib/musl/
  eigen_syscalls.c       ← the ~15 functions mapping to eigen_syscall
tools/build-musl.sh      ← batch compiler like build-netsurf.sh
```
