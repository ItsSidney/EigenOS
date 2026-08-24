#!/bin/bash
# Copyright (C) Sidney 2024-2026. All rights reserved.
# Written by Sidney.
# Distributed under terms of the GNU General Public License.

set -e

KERNEL_BIN="bin/eigen.bin"
ISO_NAME="eigen-x86_64.iso"
ISO_ROOT="bin/iso_root"
OBJ_DIR="bin/obj"

CC="gcc"
AS="nasm"
LD="ld"

# GCC's own built-in include dir (stdint.h, stddef.h, stdarg.h, etc.)
GCC_INC="$(gcc -print-file-name=include)"

CFLAGS="-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIE -fno-PIC          -fcf-protection=none -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-red-zone          -mcmodel=kernel -Iinclude -Iinclude/libs/tinygl -Isrc -Isrc/libs/bearssl/inc -Ilibs/wolfssl -DWOLFSSL_USER_SETTINGS -Isrc/user/lib/png -Isrc/user/lib/zlib -c"
ASFLAGS="-f elf64"
LDFLAGS="-Map=bin/kernel.map -T config/linker.ld -static -nostdlib -no-pie -z max-page-size=0x1000 -m elf_x86_64"

function clean() {
    echo "[CLEAN] Removing build artifacts..."
    rm -rf bin
    rm -f $ISO_NAME
}

function check_tools() {
    local missing=0
    for tool in $CC $AS $LD; do
        if ! command -v $tool &> /dev/null; then
            echo "[ERROR] $tool is not installed."
            missing=1
        fi
    done
    if ! command -v xorriso &> /dev/null; then
        echo "[WARN] xorriso not found — ISO image will not be created (kernel binary still built)."
    fi
    if [ $missing -eq 1 ]; then
        exit 1
    fi
}

function build_c() {
    echo "[BUILD] Compiling C source files..."
    # src/user/ is RING-3 code — built by build_userland() et al.
    # edim is excluded: the old ring-0 text editor is dead code.
    C_SOURCES=$(find src -name "*.c" -not -path "*/user/*" -not -path "*/apps/system/edim.c" -not -path "*/libs/bearssl/*" -not -path "*/kzcalloc.c")
    for src in $C_SOURCES; do
        rel_path=${src#src/}
        obj="$OBJ_DIR/${rel_path%.c}.o"
        mkdir -p "$(dirname "$obj")"

        echo "  CC $src -> $obj"
        if [[ "$src" == *"net"* ]]; then
            $CC $CFLAGS -O2 -mno-sse -mno-sse2 $src -o $obj
        elif [[ "$src" == *"ftfont.c" || "$src" == *"ftsystem_kernel.c" ]]; then
            # Kernel FreeType text engine: needs the vendored FreeType headers.
            $CC $CFLAGS -O2 -mno-sse -mno-sse2 -DFT2_BUILD_LIBRARY -Isrc/user/lib/freetype/include $src -o $obj
        elif [[ "$src" == *"ring3_test"* ]]; then
            $CC $CFLAGS -O0 $src -o $obj
        else
            $CC $CFLAGS -O2 $src -o $obj
        fi
    done
}

# ──────────────────────────────────────────────────────────────────────────────
# FreeType (kernel side) — the SAME vendored sources the ring-3 port uses,
# compiled into ring-0 objects so gui/ftfont.c can rasterize DejaVuSans for
# the shell taskbar. ftsystem.c is REPLACED by src/kernel/lib/ftsystem_kernel.c
# (kmalloc-backed memory manager, no file streams). Objects land OUTSIDE
# bin/obj/user/ so the kernel link picks them up.
# ──────────────────────────────────────────────────────────────────────────────
function build_ftkernel() {
    echo "[BUILD] Building FreeType (kernel text engine)..."
    local FT_SRCS="base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbbox.c base/ftglyph.c base/ftbdf.c base/ftbitmap.c base/ftmm.c cache/ftcache.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c raster/raster.c psnames/psnames.c psaux/psaux.c pshinter/pshinter.c"
    mkdir -p "$OBJ_DIR/libs/freetype"
    local src base obj
    for f in $FT_SRCS; do
        src="src/user/lib/freetype/src/$f"
        [ -f "$src" ] || { echo "  [FTKERNEL] missing $src"; return 1; }
        base=$(basename "$f" .c)
        obj="$OBJ_DIR/libs/freetype/kft_$base.o"
        echo "  CC freetype(kernel)/$f"
        $CC $CFLAGS -O2 -mno-sse -mno-sse2 -DFT2_BUILD_LIBRARY -Isrc/user/lib/freetype/include "$src" -o "$obj" || { echo "[FTKERNEL] build failed on $src"; return 1; }
    done
    echo "[SUCCESS] FreeType kernel objects built"
}

function build_asm() {
    echo "[BUILD] Compiling ASM source files..."
    ASM_SOURCES=$(find src/kernel -name "*.asm")
    for src in $ASM_SOURCES; do
        rel_path=${src#src/}
        obj="$OBJ_DIR/${rel_path%.asm}.o"
        mkdir -p "$(dirname "$obj")"

        echo "  AS $src -> $obj"
        $AS $ASFLAGS $src -o $obj
    done
}

function build_assets() {
    echo "[BUILD] Preparing wallpaper packages (Limine modules)..."
    mkdir -p "$ISO_ROOT/wallpapers"
    for bmp in src/assets/wallpapers/*.bmp; do
        [ -f "$bmp" ] || continue
        echo "  CP $bmp -> $ISO_ROOT/wallpapers/$(basename "$bmp")"
        cp "$bmp" "$ISO_ROOT/wallpapers/"
    done
    echo "[BUILD] Preparing vendor logo packages (Limine modules)..."
    mkdir -p "$ISO_ROOT/vendors"
    for bmp in src/assets/vendors/*.bmp; do
        [ -f "$bmp" ] || continue
        echo "  CP $bmp -> $ISO_ROOT/vendors/$(basename "$bmp")"
        cp "$bmp" "$ISO_ROOT/vendors/"
    done

    # Auto-register EVERY wallpaper as a Limine module in BOTH boot entries:
    # a new module line is inserted after each existing wallpapers/wp1 line,
    # so dropping wpw1.bmp (or anything else) into src/assets/wallpapers/
    # is enough for the kernel to see it as a boot module.
    for bmp in src/assets/wallpapers/*.bmp; do
        [ -f "$bmp" ] || continue
        local wname=$(basename "$bmp"); wname="${wname%.*}"
        if ! grep -qF "boot():/wallpapers/$wname.bmp" config/limine.conf; then
            echo "  [WALLPAPER] registering boot module: $wname.bmp"
            awk -v ins="    module_path: boot():/wallpapers/$wname.bmp" '
                { print }
                /module_path: boot\(\):\/wallpapers\/wp1\.bmp/ { print ins }
            ' config/limine.conf > config/limine.conf.tmp && mv config/limine.conf.tmp config/limine.conf
        fi
    done

    # Same guarantee for the kernel font: EVERY boot entry needs the
    # DejaVuSans module or the shell silently falls back to the 8x16 font.
    # Per-entry check (entries start with '/' at column 0); if an entry has
    # no font line, one is inserted right after that entry's wp5 wallpaper.
    awk -v ins="    module_path: boot():/fonts/DejaVuSans.ttf" '
        { L[NR]=$0 }
        /^[^ ].*EigenOS/ { if (anchor && !seenfont) mark[anchor]=1; anchor=0; seenfont=0 }
        /module_path: boot\(\):\/wallpapers\/wp5\.bmp/ { anchor=NR }
        /module_path: boot\(\):\/fonts\/DejaVuSans\.ttf/ { seenfont=1 }
        END {
            if (anchor && !seenfont) mark[anchor]=1
            for (i=1; i<=NR; i++) { print L[i]; if (mark[i]) print ins }
        }
    ' config/limine.conf > config/limine.conf.tmp
    if ! cmp -s config/limine.conf config/limine.conf.tmp; then
        echo "  [WALLPAPER] registering boot module: fonts/DejaVuSans.ttf (missing from some boot entries)"
        mv config/limine.conf.tmp config/limine.conf
    else
        rm -f config/limine.conf.tmp
    fi
}

# ──────────────────────────────────────────────────────────────────────────────
# WolfSSL (freestanding TLS client) — vendored under libs/wolfssl/.
# Compiled OUTSIDE the src/ glob (libs/ is not under src/) with a curated
# source list; objects go to $OBJ_DIR/libs/wolfssl/* and are picked up by
# the kernel link (find $OBJ_DIR -name '*.o' -not -path '*/user/*').
# ──────────────────────────────────────────────────────────────────────────────
function build_wolfssl() {
    echo "[BUILD] Building WolfSSL (freestanding TLS client)..."
    local WDIR="libs/wolfssl"
    [ -d "$WDIR" ] || { echo "[WOLFSSL] vendored tree not found at $WDIR"; return 0; }
    mkdir -p "$OBJ_DIR/libs/wolfssl"

    # Crypto (wolfcrypt) — minimal TLS-client set.
    # NOTE: SP math (sp_int.c) is wolfSSL 5.9's default bignum backend;
    # integer.c is the legacy backend and would duplicate mp_* symbols.
    local WOLFCRYPT_SRCS="aes.c asn.c chacha20_poly1305.c chacha.c coding.c ecc.c error.c hash.c \
kdf.c hmac.c memory.c misc.c poly1305.c pwdbased.c rsa.c sha.c sha256.c sha512.c random.c \
sp_int.c wc_encrypt.c wolfmath.c"
    # TLS engine (src/) — client path only (NO_DTLS/No server handled via user_settings).
    local ENGINE_SRCS="ssl.c internal.c tls.c x509.c ssl_certman.c ssl_crypto.c ssl_api_cert.c \
ssl_api_ext.c ssl_load.c ssl_misc.c ssl_sess.c ssl_bn.c wolfio.c ssl_sk.c ssl_asn1.c pk.c pk_ec.c pk_rsa.c keys.c bio.c"

    local src base obj
    for s in $WOLFCRYPT_SRCS; do
        src="$WDIR/wolfcrypt/src/$s"
        [ -f "$src" ] || { echo "  [WOLFSSL] missing $src"; return 1; }
        base=$(basename "$s" .c)
        obj="$OBJ_DIR/libs/wolfssl/wc_$base.o"
        echo "  CC wolfssl/wolfcrypt/$s"
        $CC $CFLAGS $WOLFCRYPT_EXTRA $src -o "$obj" || { echo "[WOLFSSL] build failed on $src"; return 1; }
    done
    for s in $ENGINE_SRCS; do
        src="$WDIR/src/$s"
        [ -f "$src" ] || { echo "  [WOLFSSL] missing $src"; return 1; }
        base=$(basename "$s" .c)
        obj="$OBJ_DIR/libs/wolfssl/wl_$base.o"
        echo "  CC wolfssl/$s"
        $CC $CFLAGS $src -o "$obj" || { echo "[WOLFSSL] build failed on $src"; return 1; }
    done
    echo "[SUCCESS] WolfSSL built ($(( $(echo $WOLFCRYPT_SRCS | wc -w) + $(echo $ENGINE_SRCS | wc -w) )) objects)"
}

function build_kernel() {
    mkdir -p $OBJ_DIR

    # Ensure no userland objects contaminate the kernel link.
    rm -rf $OBJ_DIR/user $OBJ_DIR/doom $OBJ_DIR/kilo $OBJ_DIR/userlib

    build_c
    build_asm
    build_assets

    # Prune stale objects whose source has been deleted.
    STALE_COUNT=0
    while IFS= read -r obj; do
        rel="${obj#$OBJ_DIR/}"
        src="src/${rel%.o}.c"
        [ -f "$src" ] || src="src/${rel%.o}.asm"
        if [ ! -f "$src" ] || [ "$src" = "src/apps/system/edim.c" ]; then
            echo "  [PRUNE] stale object (source removed): $obj"
            rm -f "$obj"
            STALE_COUNT=$((STALE_COUNT + 1))
        fi
    done < <(find $OBJ_DIR -name "*.o" -not -path "*/user/*" | sort)
    [ "$STALE_COUNT" -gt 0 ] && echo "[BUILD] Removed $STALE_COUNT stale object(s)."

    # WolfSSL objects are vendored under libs/ (outside src/), so they must
    # be built AFTER the prune pass above (prune would delete them otherwise).
    # Same for the kernel-side FreeType objects — and the zlib/libpng pair
    # that powers the Nordzy icon theme decoder.
    build_wolfssl
    build_ftkernel
    build_zlibk
    build_pngk

    echo "[BUILD] Linking kernel..."
    mkdir -p "$(dirname "$KERNEL_BIN")"
    $LD $LDFLAGS -o $KERNEL_BIN $(find $OBJ_DIR -name "*.o" -not -path "*/user/*" | sort)

    echo "[SUCCESS] Kernel binary: $KERNEL_BIN"
}

# ──────────────────────────────────────────────────────────────────────────────
# Ring-3 userland shared flags
# ──────────────────────────────────────────────────────────────────────────────
# NOTE: -nostdinc prevents ANY host system header from leaking in.
# We explicitly add back:
#   $GCC_INC   — GCC's built-in headers (stddef, stdarg, stdint, etc.)
#   libc/inc   — our freestanding libc shims (stdio, string, stdlib …)
#   userlib    — ring-3 ABI wrappers
#   userui     — ring-3 UI toolkit

USER_NOSTDINC="-nostdinc -I$GCC_INC"
UCFLAGS="-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIE -fno-PIC -fcf-protection=none -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-red-zone -mcmodel=kernel $USER_NOSTDINC -Isrc/user/lib/libc/inc -Iinclude -Iinclude/user -Isrc/user/lib/userlib -Isrc/user/lib/userui -Isrc/user/lib/tinygl -Isrc/user/lib/freetype/include -Isrc/user/lib -Isrc/user/lib/eigenui -c"
ULDFLAGS="-nostdlib -no-pie -m elf_x86_64 -z muldefs"
UCC="$CC"

# ──────────────────────────────────────────────────────────────────────────────
# Register a boot module in EVERY Limine boot entry (entries are delimited by
# '/'-prefixed headers; each anchors on its fonts/DejaVuSans.ttf module line,
# guaranteed present by build_assets' font fixer). Existing copies of the
# exact line are deduped first, so re-running stays idempotent and modules
# appended at EOF by older builds get normalized into both entries.
# ──────────────────────────────────────────────────────────────────────────────
function register_boot_module() {
    local path="$1"                     # e.g. boot():/user/doom.elf
    [ -z "$path" ] && return 0
    local line="    module_path: $path"
    grep -vF "$line" config/limine.conf > config/limine.conf.tmp
    awk -v ins="$line" '
        { print }
        index($0, "module_path: boot():/fonts/DejaVuSans.ttf") > 0 { print ins }
    ' config/limine.conf.tmp > config/limine.conf && rm -f config/limine.conf.tmp
}

function build_userland() {
    # Real musl (process/linux/sched included) — must precede all apps.
    if [ ! -f bin/obj/user/libmusl.a ]; then
        echo "[BUILD] Building musl (libc archive)..."
        bash tools/build-musl.sh || { echo "[MUSL] build failed"; return 1; }
        mkdir -p bin/libs && cp -f bin/obj/user/libmusl.a bin/libs/libmusl.a
    elif [ -f bin/libs/libmusl.a ] && [ bin/libs/libmusl.a -nt tools/build-musl.sh ]; then
        cp -f bin/libs/libmusl.a bin/obj/user/libmusl.a
    fi

    echo "[BUILD] Building userland apps..."
    mkdir -p bin/userapp $ISO_ROOT/user bin/obj/user/lib

    # Shared ring-3 runtime compiled ONCE and linked into every app ELF.
    mkdir -p bin/obj/user/lib bin/userapp
    for lib in userlib/userlib userui/userui userui/vector_icons userui/file_dialog                 libc/libc libc/posix libc/math libc/setjmp pthread/pthread; do
        if [ -f "src/user/lib/$lib.c" ]; then

            echo "  CC lib/$lib.c -> bin/obj/user/lib/$(basename $lib).o"
            $UCC $UCFLAGS "src/user/lib/$lib.c" -o "bin/obj/user/lib/$(basename $lib).o"
        fi
    done

    # TinyGL (software 3D rasterizer)
    for f in src/user/lib/tinygl/*.c; do
        if [ -f "$f" ]; then
            base=$(basename "$f" .c)
            echo "  CC lib/tinygl/$base.c -> bin/obj/user/lib/tgl_$base.o"
            $UCC $UCFLAGS "$f" -o "bin/obj/user/lib/tgl_$base.o"
        fi
    done

    # EigenUI (modern GUI toolkit — layered EFL/Elementary-style)
    for f in src/user/lib/eigenui/*.c; do
        if [ -f "$f" ]; then
            base=$(basename "$f" .c)
            echo "  CC lib/eigenui/$base.c -> bin/obj/user/lib/eui_$base.o"
            $UCC $UCFLAGS "$f" -o "bin/obj/user/lib/eui_$base.o"
        fi
    done


    # FreeType
    local FT_SRCS="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbbox.c base/ftglyph.c base/ftbdf.c base/ftbitmap.c base/ftmm.c cache/ftcache.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c raster/raster.c psnames/psnames.c psaux/psaux.c pshinter/pshinter.c"
    for f in $FT_SRCS; do
        if [ -f "src/user/lib/freetype/src/$f" ]; then
            local base=$(basename "$f" .c)
            echo "  CC lib/freetype/src/$f -> bin/obj/user/lib/ft_$base.o"
            $UCC $UCFLAGS -DFT2_BUILD_LIBRARY -Isrc/user/lib/freetype/include "src/user/lib/freetype/src/$f" -o "bin/obj/user/lib/ft_$base.o"
        fi
    done

    # ImGui
    for f in src/user/lib/imgui/*.cpp src/user/lib/imgui/backends/*.cpp; do
        if [ -f "$f" ]; then
            local base=$(basename "$f" .cpp)
            echo "  CXX lib/imgui/$base.cpp -> bin/obj/user/lib/ig_$base.o"
            g++ $UCFLAGS -fno-exceptions -fno-rtti -fno-threadsafe-statics -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS -include src/user/lib/imgui/imgui_eigen_compat.h -Isrc/user/lib/imgui -Isrc/user/lib/imgui/backends "$f" -o "bin/obj/user/lib/ig_$base.o"
        fi
    done

    # Lua 5.1.5: restore archive or build it fresh. Must happen BEFORE apps.
    if [ -f bin/libs/liblua.a ]; then
        cp bin/libs/liblua.a bin/obj/user/liblua.a
    elif [ -d libs/lua ] && [ ! -f bin/obj/user/liblua.a ]; then
        bash tools/build-lua.sh > /dev/null 2>&1 || true
        ar rcs bin/obj/user/liblua.a bin/obj/user/lua/*.o 2>/dev/null || true
        mkdir -p bin/libs && cp -f bin/obj/user/liblua.a bin/libs/liblua.a
        echo "[BUILD] Lua archived: $(ls -la bin/obj/user/liblua.a 2>/dev/null | awk '{print $5}') bytes"
    fi
    [ -f bin/obj/user/liblua.a ] || ar rcs bin/obj/user/liblua.a

    # Single-file apps under src/user/apps/
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .c)
        echo "  CC user/$base.c -> bin/userapp/$base.o"
        local app_cflags="$UCFLAGS"
        # Apps that need extra include paths (non-EFL codecs / VM).
        case "$base" in
            zlibtest) app_cflags="$app_cflags -Isrc/user/lib/zlib" ;;
            pngtest) app_cflags="$app_cflags -Isrc/user/lib/png -Isrc/user/lib/zlib" ;;
            jpegtest) app_cflags="$app_cflags -Isrc/user/lib/jpeg" ;;
            luatest) app_cflags="$UCFLAGS -Ilibs/lua/src" ;;
        esac
        $UCC $app_cflags "$src" -o "bin/userapp/$base.o"

        echo "  LD user/$base.elf"
        if [ "$base" = "luatest" ]; then
          $LD $ULDFLAGS -o "bin/userapp/$base.elf" "bin/userapp/$base.o" \
            bin/obj/user/lib/*.o bin/obj/user/liblua.a
        else
          $LD $ULDFLAGS -o "bin/userapp/$base.elf" "bin/userapp/$base.o" \
            bin/obj/user/lib/*.o bin/obj/user/libmusl.a bin/obj/user/libpng.a bin/obj/user/libjpeg.a bin/obj/user/libezlib.a
        fi

        echo "  CP user/$base.elf -> $ISO_ROOT/user/"
        cp "bin/userapp/$base.elf" "$ISO_ROOT/user/"
    done < <(find src/user/apps -maxdepth 1 -name '*.c' | sort)

    # Folder-form ring-3 apps (multi-file, described by build.conf)
    build_folders
}

function build_zlibk() {
    echo "[BUILD] Building zlib (kernel icon-decoder support)..."
    mkdir -p "$OBJ_DIR/libs/zlib"
    local ZSRCS="adler32.c crc32.c inflate.c inffast.c inftrees.c zutil.c compress.c uncompr.c infback.c deflate.c trees.c"
    local s base obj
    for s in $ZSRCS; do
        src="src/user/lib/zlib/$s"; base=$(basename "$s" .c)
        obj="$OBJ_DIR/libs/zlib/kz_$base.o"
        echo "  CC zlib-kernel/$s"
        $CC $CFLAGS -DZ_SOLO -DMY_ZCALLOC -w "$src" -o "$obj" || { echo "[ZLIB-K] failed on $s"; return 1; }
    done
    src="src/libs/kzcalloc.c"
    echo "  CC libs/kzcalloc.c"
    $CC $CFLAGS -DZ_SOLO -DMY_ZCALLOC -w "$src" -o "$OBJ_DIR/libs/zlib/kz_kzcalloc.o" || return 1
    echo "[SUCCESS] kernel zlib built"
}

function build_pngk() {
    echo "[BUILD] Building libpng (kernel icon-decoder support)..."
    mkdir -p "$OBJ_DIR/libs/png"
    local PSRCS="png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwrite.c pngwio.c pngwtran.c pngwutil.c"
    local s base obj
    for s in $PSRCS; do
        src="src/user/lib/png/$s"; base=$(basename "$s" .c)
        obj="$OBJ_DIR/libs/png/kp_$base.o"
        echo "  CC png-kernel/$s"
        $CC $CFLAGS -DPNG_NO_STDIO -include src/libs/kmath.h -w "$src" -o "$obj" || { echo "[PNG-K] failed on $s"; return 1; }
    done
    echo "[SUCCESS] kernel libpng built"
}

function build_zlib() {
    echo "[BUILD] Building zlib (compression, ring-3 port, Z_SOLO)..."
    mkdir -p bin/obj/user/zlib

    local ZLIB_DIR="src/user/lib/zlib"
    local ZLIB_CFLAGS="$UCFLAGS -I$ZLIB_DIR -DZ_SOLO -w"

    local ZLIB_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        local obj="bin/obj/user/zlib/${b%.c}.o"
        echo "  CC zlib/$b"
        $UCC $ZLIB_CFLAGS "$src" -o "$obj" || { echo "[ZLIB] build failed on $b"; return 1; }
        ZLIB_OBJS="$ZLIB_OBJS $obj"
    done < <(find "$ZLIB_DIR" -maxdepth 1 -name '*.c' | sort)

    echo "  AR bin/obj/user/libezlib.a"
    ar rcs bin/obj/user/libezlib.a $ZLIB_OBJS
    echo "[SUCCESS] zlib library built: bin/obj/user/libezlib.a"
}

# ──────────────────────────────────────────────────────────────────────────────
# libpng (PNG codec, ring-3 port) — depends on zlib.
# Config is the prebuilt pnglibconf.h; SIMD paths are disabled (pure C filters).
# ──────────────────────────────────────────────────────────────────────────────
function build_png() {
    echo "[BUILD] Building libpng (PNG codec, ring-3 port)..."
    mkdir -p bin/obj/user/png

    local PNG_DIR="src/user/lib/png"
    local PNG_CFLAGS="$UCFLAGS -I$PNG_DIR -Isrc/user/lib/zlib \
        -DPNG_INTEL_SSE_OPT=0 -DPNG_ARM_NEON_OPT=0 -DPNG_MIPS_MSA_OPT=0 \
        -DPNG_POWERPC_VSX_OPT=0 -DPNG_LOONGARCH_LSX_OPT=0 -w"

    local PNG_SRCS="png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c \
        pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c \
        pngwrite.c pngwtran.c pngwutil.c"
    local PNG_OBJS=""
    for f in $PNG_SRCS; do
        [ -f "$PNG_DIR/$f" ] || { echo "[PNG] missing source $f"; return 1; }
        local obj="bin/obj/user/png/${f%.c}.o"
        echo "  CC png/$f"
        $UCC $PNG_CFLAGS "$PNG_DIR/$f" -o "$obj" || { echo "[PNG] build failed on $f"; return 1; }
        PNG_OBJS="$PNG_OBJS $obj"
    done

    echo "  AR bin/obj/user/libpng.a"
    ar rcs bin/obj/user/libpng.a $PNG_OBJS
    echo "[SUCCESS] libpng built: bin/obj/user/libpng.a"
}

# ──────────────────────────────────────────────────────────────────────────────
# libjpeg (IJG 9e, PNG codec sibling) — ring-3 port, pure C (no SIMD).
# Config is a hand-written jconfig.h; memory manager is jmemnobs (malloc only).
# ──────────────────────────────────────────────────────────────────────────────
function build_jpeg() {
    echo "[BUILD] Building libjpeg (IJG 9e, ring-3 port)..."
    mkdir -p bin/obj/user/jpeg

    local JPEG_DIR="src/user/lib/jpeg"
    local JPEG_CFLAGS="$UCFLAGS -I$JPEG_DIR -w"

    local JPEG_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        local obj="bin/obj/user/jpeg/${b%.c}.o"
        echo "  CC jpeg/$b"
        $UCC $JPEG_CFLAGS "$src" -o "$obj" || { echo "[JPEG] build failed on $b"; return 1; }
        JPEG_OBJS="$JPEG_OBJS $obj"
    done < <(find "$JPEG_DIR" -maxdepth 1 -name '*.c' | sort)

    echo "  AR bin/obj/user/libjpeg.a"
    ar rcs bin/obj/user/libjpeg.a $JPEG_OBJS
    echo "[SUCCESS] libjpeg built: bin/obj/user/libjpeg.a"
}

# ──────────────────────────────────────────────────────────────────────────────
function conf_get() {
    local file="$1" key="$2"
    [ -f "$file" ] || { echo ""; return; }
    local line
    while IFS= read -r line; do
        case "$line" in
            "$key="*)
                echo "${line#$key=}"
                return
                ;;
        esac
    done < "$file"
    echo ""
}

function build_folders() {
    local conf_list
    conf_list=$(find src/user/apps -mindepth 3 -maxdepth 3 -name build.conf | sort)
    [ -z "$conf_list" ] && return
    echo "[BUILD] Building folder-form ring-3 apps..."
    while IFS= read -r conf; do
        [ -f "$conf" ] || continue
        [ -z "$conf" ] && continue
        local dir; dir=$(dirname "$conf")
        local NAME;   NAME=$(conf_get "$conf" NAME)
        local SRCS;   SRCS=$(conf_get "$conf" SRCS)
        local INCS;   INCS=$(conf_get "$conf" INCS)
        local FLAGS;  FLAGS=$(conf_get "$conf" FLAGS)
        [ -z "$NAME" ] && { echo "  [SKIP] $conf: missing NAME"; continue; }
        [ -z "$SRCS" ] && { echo "  [SKIP] $conf: missing SRCS"; continue; }

        local OBJS=""
        for s in $SRCS; do
            local src="$dir/$s"
            [ -f "$src" ] || { echo "  [SKIP] $conf: source $s missing"; OBJS=""; break; }
            local obj="bin/userapp/$(echo "$dir" | tr '/' '_')_$(basename "$s" .c).o"
            echo "  CC ${dir#src/user/apps/}/$s"
            local inc_flags=""
            for inc in $INCS; do inc_flags="$inc_flags -I$dir/$inc"; done

            if [[ "$src" == *.cpp ]]; then
                echo "  CXX ${dir#src/user/apps/}/$s"
                g++ $UCFLAGS -fno-exceptions -fno-rtti $inc_flags $FLAGS "$src" -o "$obj" || { echo "[FOLDER] build failed on $src"; return 1; }
            else
                $UCC $UCFLAGS $inc_flags $FLAGS "$src" -o "$obj" || { echo "[FOLDER] build failed on $src"; return 1; }
            fi
            OBJS="$OBJS $obj"
        done
        [ -z "$OBJS" ] && continue

        echo "  LD user/$NAME.elf"
        $LD $ULDFLAGS -o "bin/userapp/$NAME.elf" $OBJS              bin/obj/user/lib/*.o              bin/obj/user/libpng.a bin/obj/user/libjpeg.a bin/obj/user/libezlib.a              || { echo "[FOLDER] link failed for $NAME"; return 1; }

        echo "  CP user/$NAME.elf -> $ISO_ROOT/user/"
        cp "bin/userapp/$NAME.elf" "$ISO_ROOT/user/"

        # Register as a Limine module in EVERY boot entry (idempotent).
        register_boot_module "boot():/user/$NAME.elf"
    done < <(find src/user/apps -mindepth 3 -maxdepth 3 -name build.conf | sort)
}

function build_doom() {
    echo "[BUILD] Building DOOM (ring-3 port)..."
    mkdir -p bin/userapp "$ISO_ROOT/user" bin/userapp/doom_objs

    local DCC="$CC"
    # DOOM uses float/double trig tables — allow x87 FPU.
    local DCFLAGS="-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto          -fno-PIE -fno-PIC -fcf-protection=none -m64 -march=x86-64 -mno-mmx -mno-red-zone          -mcmodel=kernel -nostdinc -I$GCC_INC          -DOOMGENERIC_RESX=640 -DOOMGENERIC_RESY=400          -Isrc/user/lib/libc/inc -Iinclude -Isrc/user/apps/doom/engine          -Isrc/user/lib/userlib -Isrc/user/lib/userui -c"
    local DLDFLAGS="-nostdlib -no-pie -m elf_x86_64"

    local EXCLUDE="i_system.c i_sound.c i_scale.c i_joystick.c          i_cdmus.c i_allegromusic.c i_allegrosound.c i_endoom.c gusconf.c icon.c          w_file.c w_file_stdc.c d_iwad.c          doomgeneric_sdl.c doomgeneric_linuxvt.c doomgeneric_win.c          doomgeneric_xlib.c doomgeneric_allegro.c doomgeneric_emscripten.c          doomgeneric_soso.c doomgeneric_sosox.c          i_sdlmusic.c i_sdlsound.c mus2mid.c"
    local ENGINE_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        local skip=0
        for e in $EXCLUDE; do [ "$b" = "$e" ] && skip=1 && break; done
        [ $skip -eq 1 ] && continue
        local obj="bin/userapp/doom_objs/$(echo "$src" | tr '/' '_').o"
        echo "  CC doom/$b"
        $DCC $DCFLAGS "$src" -o "$obj" || { echo "[DOOM] build failed on $src"; return 1; }
        ENGINE_OBJS="$ENGINE_OBJS $obj"
    done < <(find src/user/apps/doom/engine -name '*.c' | sort)

    local GLUE="src/user/apps/doom/doomgeneric_eigen.c src/user/apps/doom/doom_iwad.c src/user/apps/doom/doom_stubs.c"
    for g in $GLUE; do
        local obj="bin/userapp/doom_objs/$(basename $g .c).o"
        echo "  CC doom/$(basename $g)"
        $DCC $DCFLAGS "$g" -o "$obj" || { echo "[DOOM] build failed on $g"; return 1; }
        ENGINE_OBJS="$ENGINE_OBJS $obj"
    done

    echo "  LD user/doom.elf"
    $LD $DLDFLAGS -o "bin/userapp/doom.elf" $ENGINE_OBJS          bin/obj/user/lib/userlib.o bin/obj/user/lib/userui.o bin/obj/user/lib/vector_icons.o          bin/obj/user/lib/libc.o bin/obj/user/lib/posix.o bin/obj/user/lib/pthread.o bin/obj/user/lib/math.o bin/obj/user/lib/setjmp.o          || { echo "[DOOM] link failed"; return 1; }

    echo "  CP user/doom.elf -> $ISO_ROOT/user/"
    cp "bin/userapp/doom.elf" "$ISO_ROOT/user/"
    echo "[SUCCESS] DOOM ELF built."
}

function build_awk() {
    echo "[BUILD] Building awk (ring-3 port)..."
    mkdir -p bin/userapp "$ISO_ROOT/user" bin/userapp/awk_objs

    local ACC="$CC"
    local ACFLAGS="-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto          -fno-PIE -fno-PIC -fcf-protection=none -m64 -march=x86-64 -mno-mmx -mno-red-zone          -mcmodel=kernel -nostdinc -I$GCC_INC          -Isrc/user/lib/libc/inc -Iinclude -Iinclude/user          -Isrc/user/apps/awk          -Isrc/user/lib/userlib -Isrc/user/lib/userui -c"
    local ALDFLAGS="-nostdlib -no-pie -m elf_x86_64"

    local AWK_OBJS=""
    local SRCS="awkgram.tab.c proctab.c lex.c b.c main.c parse.c lib.c run.c tran.c"
    for s in $SRCS; do
        local obj="bin/userapp/awk_objs/$s.o"
        echo "  CC awk/$s"
        $ACC $ACFLAGS "src/user/apps/awk/$s" -o "$obj" || { echo "[AWK] build failed on $s"; return 1; }
        AWK_OBJS="$AWK_OBJS $obj"
    done

    echo "  LD user/awk.elf"
    $LD $ALDFLAGS -o "bin/userapp/awk.elf" $AWK_OBJS          bin/obj/user/lib/userlib.o bin/obj/user/lib/userui.o bin/obj/user/lib/vector_icons.o          bin/obj/user/lib/libc.o bin/obj/user/lib/posix.o bin/obj/user/lib/pthread.o bin/obj/user/lib/math.o bin/obj/user/lib/setjmp.o          || { echo "[AWK] link failed"; return 1; }

    echo "  CP user/awk.elf -> $ISO_ROOT/user/"
    cp "bin/userapp/awk.elf" "$ISO_ROOT/user/"
    echo "[SUCCESS] awk ELF built."
}


function build_iso() {
    echo "[BUILD] Creating ISO image..."
    mkdir -p $ISO_ROOT/boot
    cp $KERNEL_BIN $ISO_ROOT/
    cp config/limine.conf $ISO_ROOT/boot/

    LIMINE_DIR="tools/bootloader"
    if [ ! -f "$LIMINE_DIR/limine-bios.sys" ]; then
        echo "[ERROR] Limine binaries not found in $LIMINE_DIR!"
        exit 1
    fi

    cp "$LIMINE_DIR/limine-bios.sys"   $ISO_ROOT/boot/
    cp "$LIMINE_DIR/limine-bios-cd.bin" $ISO_ROOT/boot/
    cp "$LIMINE_DIR/limine-uefi-cd.bin" $ISO_ROOT/boot/

    mkdir -p $ISO_ROOT/EFI/BOOT
    cp "$LIMINE_DIR/BOOTX64.EFI"  $ISO_ROOT/EFI/BOOT/
    cp "$LIMINE_DIR/BOOTIA32.EFI" $ISO_ROOT/EFI/BOOT/

    if command -v xorriso &> /dev/null; then
        echo "[BUILD] Using xorriso to create ISO..."
        xorriso -as mkisofs -b boot/limine-bios-cd.bin                  -no-emul-boot -boot-load-size 4 -boot-info-table                  --efi-boot boot/limine-uefi-cd.bin                  -efi-boot-part --efi-boot-image                  $ISO_ROOT -o $ISO_NAME 2>/dev/null
    elif command -v genisoimage &> /dev/null; then
        echo "[BUILD] Using genisoimage to create ISO..."
        genisoimage -b boot/limine-bios-cd.bin                      -no-emul-boot -boot-load-size 4 -boot-info-table                      --efi-boot boot/limine-uefi-cd.bin                      -efi-boot-part --efi-boot-image                      -o $ISO_NAME $ISO_ROOT 2>/dev/null
    else
        echo "[ERROR] No ISO creation tool found (xorriso or genisoimage required)."
        exit 1
    fi

    if [ -x "./tools/bootloader/limine" ]; then
        ./tools/bootloader/limine bios-install $ISO_NAME &>/dev/null || true
    fi

    echo "[SUCCESS] Eigen build complete: $ISO_NAME"
}

function build() {
    check_tools
    echo "[BUILD] Starting Eigen build process..."
    build_kernel
    build_zlib
    build_png
    build_jpeg
    build_userland
    build_doom
    build_awk
    build_iso
}

function run() {
    if [ ! -f $KERNEL_BIN ] && [ ! -f $ISO_NAME ]; then
        build
    fi

    if [ ! -f $ISO_NAME ]; then
        echo "[ERROR] No ISO image found. Run './build.sh build' first."
        exit 1
    fi

    QEMU="qemu-system-x86_64"
    if ! command -v $QEMU &> /dev/null; then
        echo "[ERROR] QEMU is not installed."
        exit 1
    fi

    QEMU_FLAGS="-cdrom $ISO_NAME -m 4G -smp 4 -machine q35 -serial stdio                  -net nic,model=e1000 -net user -rtc base=localtime                  -vga std -display vnc=:1                  -device AC97 -drive file=eigen_disk.img,format=raw,if=virtio"

    if [ -e /dev/kvm ]; then
        QEMU_FLAGS="$QEMU_FLAGS -cpu host -enable-kvm"
    else
        echo "[INFO] KVM not available, using default CPU emulation."
        QEMU_FLAGS="$QEMU_FLAGS -cpu qemu64"
    fi

    # Auto-clear any stale QEMU instance still holding the VNC port.
    echo "[RUN] Clearing any process holding VNC port 5901..."
    if command -v ss >/dev/null 2>&1; then
        for p in $( (ss -tlnp 2>/dev/null | grep -E ':5901($|[[:space:]])' | grep -oP 'pid=\K[0-9]+' | sort -u) || true); do
            echo "[RUN]   killing stale listener on 5901 (pid $p)"
            kill -9 "$p" 2>/dev/null
        done
    fi
    pkill -9 -f qemu-system 2>/dev/null || true
    # Wait for the port to actually free up.
    for i in $(seq 1 20); do
        if command -v ss >/dev/null 2>&1; then
            ss -tln 2>/dev/null | grep -qE ':5901($|[[:space:]])' || break
        fi
        sleep 0.5
    done

    echo "[RUN] Launching QEMU (VNC :1)..."
    $QEMU $QEMU_FLAGS &
    QPID=$!

    if command -v vncviewer &> /dev/null; then
        echo "[RUN] Waiting for VNC port 5901..."
        for i in $(seq 1 20); do
            ss -tln | grep -q ":5901 " && break
            sleep 0.5
        done
        vncviewer :1 &
    else
        echo "[INFO] Install net-misc/tigervnc for a graphical viewer."
        echo "[INFO] Connect manually: vncviewer :1"
    fi

    wait $QPID
}

case "$1" in
    "build")
        build
        ;;
    "clean")
        clean
        ;;
    "run"|"-r"|"--run")
        run
        ;;
    "zlib")
        # Build only the zlib library.
        build_zlib
        ;;
    "png")
        # Build only the libpng library — useful during the libpng port.
        build_zlib
        build_png
        ;;
    "jpeg")
        # Build only the libjpeg library — useful during the libjpeg port.
        build_jpeg
        ;;
    "lua")
        # Build vendored Lua 5.1.5 (ring-3 lib) + freestanding shims.
        bash tools/build-lua.sh
        mkdir -p bin/obj/user bin/libs
        AR_OBJS="$(ls bin/obj/user/lua/*.o 2>/dev/null)"
        ar rcs bin/obj/user/liblua.a $AR_OBJS 2>/dev/null || true
        cp -f bin/obj/user/liblua.a bin/libs/liblua.a
        echo "[SUCCESS] Lua 5.1.5 archived"
        ;;
    "log")
        if [ ! -f $ISO_NAME ]; then build; fi
        echo "[RUN] Launching QEMU with serial log to eigen_qemu.log..."
        QEMU="qemu-system-x86_64"
        QFLAGS="-cdrom $ISO_NAME -m 2G -smp 4 -serial file:eigen_qemu.log -display none"
        QFLAGS="$QFLAGS -net nic,model=e1000 -net user -device AC97"
        if [ -e /dev/kvm ]; then QFLAGS="$QFLAGS -cpu host -enable-kvm"; else QFLAGS="$QFLAGS -cpu qemu64"; fi
        $QEMU $QFLAGS &
        QPID=$!
        echo "[RUN] PID $QPID — logging..."
        sleep 25
        kill $QPID 2>/dev/null; wait $QPID 2>/dev/null
        echo "[RUN] Log saved to eigen_qemu.log (lines: $(wc -l < eigen_qemu.log))"
        ;;
    "help"|*)
        echo "Usage: $0 {build|clean|run|zlib|png|jpeg|lua|log|help}"
        echo "  build: Compiles kernel + all ring-3 apps + ISO"
        echo "  clean: Removes build artifacts"
        echo "  run:   Builds (if needed) and runs in QEMU"
        echo "  zlib:  Compile only the zlib library"
        echo "  png:   Compile zlib + libpng"
        echo "  jpeg:  Compile only libjpeg"
        echo "  lua:   Build vendored Lua 5.1.5"
        echo "  log:   Run headless and save serial log to eigen_qemu.log"
        echo "  help:  Shows this help"
        ;;
esac
