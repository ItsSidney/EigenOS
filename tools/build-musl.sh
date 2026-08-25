#!/bin/bash
# tools/build-musl.sh — compile a usable musl subset for EigenOS ring-3.
#
# Produces object files under bin/obj/user/musl/ and archives them into
# bin/obj/user/libmusl.a. This is NOT a full musl build: thread/linux/process/
# ipc/mq/sched subsystems are intentionally skipped (no kernel support).
#
# Bridge files (arch/eigen/eigen_*.c) supply: mmap/munmap/mremap/madvise/
# mprotect over EIGEN_SYS_ALLOC, __syscall_cp, a thread-independent errno,
# and the _start bootstrap.

set -e
cd "$(dirname "$0")/.."

MUSL=libs/musl
OUT=bin/obj/user/musl
mkdir -p "$OUT"

CC="gcc"
# NOTE: do NOT add arch/eigen/bits to the include path — that makes
# <stdint.h> resolve to bits/stdint.h and breaks the include chain.
# <bits/...> is reached via -Iarch/eigen.
CFLAGS="-ffreestanding -nostdinc -fno-stack-protector -fno-PIC -fno-PIE \
  -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-red-zone -mcmodel=kernel \
  -mlong-double-64 \
  -I$MUSL/src/include -I$MUSL/arch/eigen -I$MUSL/src/internal -I$MUSL/include \
  -w -O1 -c"

# Files we never compile (no kernel support or need locale/threads).
SKIP=(
  "libs/musl/src/string/strsignal.c"           # locale-dependent
  "libs/musl/src/ctype/__ctype_get_mb_cur_max.c"  # pulls locale/pthread
  "libs/musl/src/env/__libc_start_main.c"      # musl bootstrap (we use eigen_crt)
  "libs/musl/src/env/__init_tls.c"             # TLS init (no TLS in static eigen)
  "libs/musl/src/env/__reset_tls.c"            # TLS reset
  "libs/musl/src/env/__stack_chk_fail.c"       # stack-protector (disabled)
  "libs/musl/src/env/__libc_csu_fini.c"        # csu
  "libs/musl/src/env/__libc_csu_init.c"        # csu
  "libs/musl/src/env/__progname.c"             # progname (needs auxv)
  "libs/musl/src/internal/emulate_wait4.c"      # wait4 emulation (no fork)
  "libs/musl/src/internal/vdso.c"               # vdso (none on eigen)
  "libs/musl/src/internal/version.c"            # generated version.h
  "libs/musl/src/errno/strerror.c"              # locale message tables
  "libs/musl/src/math/x86_64/fabsl.c"           # x87 asm (use generic)
  "libs/musl/src/math/x86_64/fmodl.c"           # x87 asm
  "libs/musl/src/math/x86_64/llrintl.c"         # x87 asm
  "libs/musl/src/math/x86_64/lrintl.c"          # x87 asm
  "libs/musl/src/math/x86_64/remainderl.c"      # x87 asm
  "libs/musl/src/math/x86_64/remquol.c"         # x87 asm
  "libs/musl/src/math/x86_64/rintl.c"           # x87 asm
  "libs/musl/src/math/x86_64/sqrtl.c"          # x87 asm
  "libs/musl/src/malloc/lite_malloc.c"         # alt allocator (use mallocng)
  "libs/musl/src/malloc/replaced.c"            # dynamic-link shim
  "libs/musl/src/stdio/__stdio_seek.c"         # syscall.h macro redefinition quirk
  "libs/musl/src/stdio/freopen.c"              # same quirk
  "libs/musl/src/stdio/pclose.c"               # same quirk (popen is unsupported)
  "libs/musl/src/time/__tz.c"                  # index/strchr macro clash (tzdata unused)
)

skip_match() {
  local f="$1"
  for s in "${SKIP[@]}"; do
    [ "$f" = "$s" ] && return 0
  done
  return 1
}

compile() {
  local src="$1"
  skip_match "$src" && return 0
  [ -f "$src" ] || return 0
  local obj="$OUT/${src#libs/musl/src/}"
  obj="${obj%.c}.o"; obj="${obj%.s}.o"
  mkdir -p "$(dirname "$obj")"
  if [[ "$src" == *.s ]]; then
    echo "  AS $src"
    gcc -c "$src" -o "$obj" || { echo "  [FAIL] $src"; return 1; }
    return 0
  fi
  echo "  CC $src"
  $CC $CFLAGS "$src" -o "$obj" || {
    echo "  [FAIL] $src"
    return 1
  }
}

FAIL=0

# 1) Core dependency-light directories.
for f in $(find $MUSL/src/string -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/ctype -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/math -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/math/x86_64 -maxdepth 1 -name '*.c' 2>/dev/null | sort) \
         $(find $MUSL/src/env -maxdepth 1 -name '*.c' | sort); do
  compile "$f" || FAIL=1
done

# 1b) stdlib + unistd + malloc + exit (malloc/exit + write/read/close).
for f in $(find $MUSL/src/stdlib -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/unistd -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/malloc -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/malloc/mallocng -maxdepth 1 -name '*.c' 2>/dev/null | sort) \
         $(find $MUSL/src/exit -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/fenv -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/stdio -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/signal -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/stat -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/fcntl -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/dirent -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/time -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/setjmp -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/setjmp/x86_64 -name '*.s' | sort) \
          $(find $MUSL/src/signal/x86_64 -name '*.s' | sort) \
          $(find $MUSL/src/termios -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/locale -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/multibyte -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/temp -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/prng -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/misc -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/network -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/select -maxdepth 1 -name '*.c' | sort) \
          $(find $MUSL/src/malloc/mallocng -maxdepth 1 -name '*.c' | sort); do
  compile "$f" || FAIL=1
done

# 2) Internal helpers (syscall_ret, etc.) — but NOT the thread/pthread ones.
for f in $(find $MUSL/src/internal -name '*.c' | sort); do
  compile "$f" || FAIL=1
done

# 3) errno (skip __errno_location — we provide our own bridge version).
for f in $(find $MUSL/src/errno -name '*.c' | sort); do
  case "$f" in
    */__errno_location.c) continue ;;
  esac
  compile "$f" || FAIL=1
done

# 3b) Process / linux / threads subsystems (real fork/execve/pthread).
for f in $(find $MUSL/src/process -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/regex -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/conf -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/passwd -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/legacy -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/time -name 'tzset.c' | sort) \
         $(find $MUSL/src/linux -maxdepth 1 -name '*.c' | sort) \
         $(find $MUSL/src/sched -maxdepth 1 -name '*.c' | sort); do
  case "$f" in
    */clone.c|*/posix_spawn*.c|*/fexecve.c|*/system.c|*/popen.c|*/ftw.c)
      continue ;;   # need __clone asm / spawn-family we bridge separately
  esac
  compile "$f" || FAIL=1
done

# 4) Our bridge + bootstrap object(s).
for f in $MUSL/arch/eigen/eigen_mmap.c $MUSL/arch/eigen/eigen_bridge.c \
          $MUSL/arch/eigen/eigen_crt.c $MUSL/arch/eigen/eigen_stubs.c; do
  compile "$f" || FAIL=1
done

echo "==== core compile done (FAIL=$FAIL) ===="

# Combine everything into ONE relocatable object. musl's internal symbols are
# `hidden`, so they only resolve when all objects live in a single module (as
# musl expects). Linking with `ld -r` collapses them; the public API symbols
# (malloc, write, printf, ...) stay exported.
OBJLIST=$(find bin/obj/user/musl -name '*.o')
ld -r -o bin/obj/user/musl_rel.o $OBJLIST --allow-multiple-definition
# Wrap the relocatable object in an archive for convenient linking.
ar rcs bin/obj/user/libmusl.a bin/obj/user/musl_rel.o
echo "[MUSL] built libmusl.a: $(ls -la bin/obj/user/libmusl.a 2>/dev/null | awk '{print $5}') bytes ($(echo "$OBJLIST" | wc -l) objects)"

exit $FAIL
