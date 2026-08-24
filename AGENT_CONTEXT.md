# EigenOS — Agent Session Context

## What EigenOS Is
Hobby x86-64 OS built from scratch by Sidney (one person). Limine boot, ring-3 ELF userland, compositing window manager, TCP/IP + DHCP + DNS + TLS 1.2, FreeType text everywhere (kernel + ring-3), PS/2 input, IDE storage, AC97 audio. ~20 apps including terminal, browser (edrowser), DOOM, games, editors.

## Build
```bash
bash build.sh build        # full build → eigen-x86_64.iso at repo root
bash build.sh run          # QEMU (user tests on real hardware / VirtualBox / KVM)
```
Requires: gcc, nasm, xorriso. User tests in Oracle VirtualBox and KVM.

## Critical Rules
- **NEVER run `bash build.sh build` if user is also building** — zombie processes wipe bin/obj/user/ mid-build causing phantom failures. Always `pkill -9 -f "build.sh"` first.
- Kernel syscall ABI: `int 0x80`, EIGEN_SYS_* numbers in include/user/eigen.h
- Window pixel format: 0xRRGGBB (opaque 32-bit)
- Ring-3 compiled with: `-ffreestanding -mno-80387 -mno-mmx -mno-red-zone -mcmodel=kernel -nostdinc`
- Shared ring-3 libs: bin/obj/user/lib/*.o (libc.o posix.o math.o setjmp.o pthread.o userlib.o userui.o vector_icons.o)
- Folder apps auto-discovered: src/user/apps/<cat>/<app>/build.conf → auto-linked + registered

## Architecture (key paths)
```
src/kernel/           kernel (syscalls, task, mem, net, acpi)
src/gui/              WM, taskbar_mac.c, theme.c, wallpaper_mgr.c, ftfont.c
src/user/lib/
  libc/               hand-written libc (INCOMPLETE — being replaced by musl)
  freetype/           FreeType 2.13.2 port (ring-3)
  eigengui/           REMOVED — was too incomplete
libs/
  wolfssl/            TLS 1.2
include/user/eigen.h  syscall numbers + structs
tools/build-musl.sh       batch compiler for the musl libc (libmusl.a)
tools/build-lua54.sh      Lua 5.4.4 compiler
```

## Syscall Table (EIGEN_SYS_*)
0=open 1=write 2=read 3=close 4=getpid 5=sleep 6=gettime 7=alloc 8=free 9=exit
10=spawn 11=wait 12=sysinfo 13=gfx 14=input 15=beep 16=win 17=time 18=fontmap
19=modload 20=fs 21=gettheme 22=lseek 23=settings 24=net 25=thread 26=futex
27=poll 28=epoll 29=pipe 30=fcntl 31=spawn_fds

## Recent Completions
- ✅ Spawn fd redirection (EIGEN_SYS_SPAWN_FDS=31): child inherits parent pipe/file fds as stdin/stdout/stderr
- ✅ Terminal `run <app>` command: spawns app with stdout→pipe, drains output live
- ✅ FreeType taskbar + launcher (kernel-side eg font engine)
- ✅ Terminal crash fix (apps[] NULL terminator)
- ✅ Per-entry Limine module registration (register_boot_module)
- ✅ Wallpaper pipeline fixed (dynamic wp* module seeding, mkdir stub bypassed)
- ✅ Key modifiers packed into WM key events (c bits: 1=shift 2=ctrl 4=alt)

## IN PROGRESS: musl libc port
**This replaces the hand-written libc entirely. Fixes all shim problems permanently.**

State (as of this session):
- musl 1.2.5 vendored to libs/musl/
- arch/eigen/ fully written: syscall_arch.h (int $0x80 bridge, __syscall0-6),
  bits/syscall.h (EIGEN_SYS_* map; unsupported → 0x7fffffff sentinel; SYS_brk=0x7ff1
  bridged locally to __eigen_brk), bits/{errno,mman,poll,resource,fcntl,termios,
  ioctl,ioctl_fix,stat,dirent,statfs}.h copied/derived from x86_64 + generic,
  atomic_arch/pthread_arch/fp_arch/reloc.h from x86_64, kstat.h from x86_64.
- arch/eigen/eigen_mmap.c (mmap/munmap/mremap/madvise/mprotect over
  EIGEN_SYS_ALLOC=7/FREE=8 + internal __mmap/__madvise/__mremap), eigen_bridge.c
  (__syscall_cp, thread-independent __errno_location, no-op __lock/__unlock,
  malloc→__libc_malloc_impl, __eigen_brk, __libc_malloc), eigen_crt.c (_start),
  eigen_stubs.c (UTC tz + strerror/strsignal + thread/process/signal stubs).
- tools/build-musl.sh builds **libmusl.a** (787 objects, ~738 KB). It merges ALL
  objects into ONE relocatable module via `ld -r --allow-multiple-definition`
  (required because musl's internal symbols are `hidden` and only resolve inside a
  single module), then wraps in an archive.
- **hello-world using write/malloc/strlen/free AND printf/malloc/free links cleanly
  against bin/obj/user/libmusl.a** (verified: ld exits 0). Steps 1-7 DONE.
- Skipped (unsupported single-threaded env): src/thread/*, src/process/spawn & popen
  (posix_spawn), real TLS init, vdso, dynamic-link shims, the x87 long-double math
  (use -mlong-double-64), and a few macro-quirk files (__stdio_seek/freopen/pclose,
  time/__tz.c — tz provided via eigen_stubs UTC stubs).

### Next steps for musl:
8. NetSurf REMOVED entirely (vendored tree + build integration deleted). musl
   libc itself is retained as the C runtime for the terminal/shell/busybox effort.
9. Relink all other apps against musl → delete old libc shims
- (Optional) Port real threads/pthread, posix_spawn, signals, locale catalogs
  (currently stubbed → callers get -ENOSYS).

### Musl include paths that WORK (use EXACTLY this order):
```bash
gcc -ffreestanding -nostdinc -fno-stack-protector -fno-PIC -fno-PIE \
  -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-red-zone -mcmodel=kernel \
  -mlong-double-64 \
  -Ilibs/musl/src/include -Ilibs/musl/arch/eigen -Ilibs/musl/src/internal \
  -Ilibs/musl/include -w -O1 -c <file.c>
```
- Do NOT add `.../arch/eigen/bits` to the include path — `<stdint.h>` then resolves
  to `bits/stdint.h` and the whole chain breaks.
- `src/internal` MUST precede `include` so `#include "syscall.h"` picks the internal
  version, not the public one.
- Do NOT add GCC's include dir (-I$(gcc -print-file-name=include)) — conflicts with musl's stdint.
- -mlong-double-64 so long double == double (avoids forbidden x87).

### Musl gotchas discovered:
- alltypes.h generated by: cat arch_defines + include/alltypes.h.in | sed -f tools/mkalltypes.sed
- bits/stdint.h references int32_t which comes from alltypes.h conditionals (__NEED_int32_t)
- Don't define int_fast16_t/32_t in alltypes.h (bits/stdint.h does it)
- Don't define intptr_t/uintptr_t in alltypes.h (GCC provides them, conflicts)
- malloc uses mmap internally → bridged to EIGEN_SYS_ALLOC/FREE (see eigen_mmap.c).
- syscall_arch.h guard MUST be renamed (e.g. _EIGEN_SYSCALL_ARCH_H); the generic
  internal/syscall.h also uses _INTERNAL_SYSCALL_H and the collision silently drops
  every __syscallN.
- multibyte funcs are in src/multibyte/ (not src/locale/); __randname is in src/temp/;
  the real allocator is src/malloc/mallocng/; setjmp/longjmp are x86_64 .s under
  src/setjmp/x86_64/ (must assemble .s, not just .c).
- The merged relocatable object triggers a harmless "requires executable stack"
  warning from setjmp.s; can be suppressed later with --noexecstack if desired.

## REMOVED (intentionally)
- EigenGUI toolkit (was too incomplete, user decided to cut losses)
- Lite XL port (worked partially but abandoned)
- EFL/Evas/Edje/Elementary (removed before this session)

## File Locations
- Changelog.md — updated each session
- misc/logo-modern.svg — lambda icon design
- config/limine.conf — boot entries (Default + Safe Graphics), modules registered per-entry
- src/user/lib/userlib/userlib.{h,c} — ring-3 syscall wrappers
- libs/musl/PORTING_STATUS.md — musl libc port continuation guide
