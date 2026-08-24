#!/bin/bash
# Batch-compile vendored Lua 5.1.5 against EigenOS freestanding ports.
# Excludes: lua.c/luac.c/print.c (standalone), liolib/loslib/loadlib/lmathlib
# (need system IO/time/dlopen/libm which we stub or omit for the ring-3 port).
cd "$(dirname "$0")/.."
UCC=gcc
GCC_INC=$(gcc -print-file-name=include)
INC="-Ilibs/lua/src -Ilibs/lua/shim \
  -Isrc/user/lib/libc/inc -Iinclude -Iinclude/user"
PREFIX="-include libs/lua/shim/eigen_lua_shim.h"
FLAGS="-ffreestanding -fno-stack-protector -fno-lto -fno-PIE -fno-PIC -m64 -march=x86-64 \
  -nostdinc -I$GCC_INC -w -O1 -D_DEFAULT_SOURCE"
DEFER="lua.c luac.c print.c liolib.c loslib.c loadlib.c lmathlib.c"
OUT=bin/obj/user/lua
rm -rf $OUT && mkdir -p $OUT
ok=0; fail=0; FAILED=""
for f in $(find libs/lua/src -maxdepth 1 -name "*.c" | sort); do
  b=$(basename "$f" .c)
  case "$DEFER" in *"$b"*) continue ;; esac
  obj="$OUT/$b.o"
  if $UCC $FLAGS $PREFIX $INC -c "$f" -o "$obj" 2>"/tmp/lua_$b.err"; then
    ok=$((ok+1))
  else
    fail=$((fail+1)); FAILED="$FAILED\n$f"
  fi
done
# freestanding shims (stdio no-ops, fmod, etc.)
if $UCC $FLAGS $PREFIX $INC -c libs/lua/shim/eigen_lua_shim.c -o "$OUT/shim.o" 2>/tmp/lua_shim.err; then
  ok=$((ok+1))
else
  fail=$((fail+1)); FAILED="$FAILED\nlibs/lua/shim/eigen_lua_shim.c"
fi
echo "== lua compile ok=$ok fail=$fail =="
[ -n "$FAILED" ] && printf "FAILED:$FAILED\n"
exit 0
