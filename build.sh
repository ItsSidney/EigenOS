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

CFLAGS="-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIE -fno-PIC          -fcf-protection=none -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-red-zone          -mcmodel=kernel -Iinclude -Iinclude/libs/tinygl -Isrc -Isrc/libs/bearssl/inc -Ilibs/wolfssl -DWOLFSSL_USER_SETTINGS -c"
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
    # src/user/ is RING-3 code — built by build_userland() / build_eina() etc.
    # edim is excluded: the old ring-0 text editor is dead code.
    C_SOURCES=$(find src -name "*.c" -not -path "*/user/*" -not -path "*/apps/system/edim.c" -not -path "*/libs/bearssl/*")
    for src in $C_SOURCES; do
        rel_path=${src#src/}
        obj="$OBJ_DIR/${rel_path%.c}.o"
        mkdir -p "$(dirname "$obj")"

        echo "  CC $src -> $obj"
        if [[ "$src" == *"net"* ]]; then
            $CC $CFLAGS -O2 -mno-sse -mno-sse2 $src -o $obj
        elif [[ "$src" == *"ring3_test"* ]]; then
            $CC $CFLAGS -O0 $src -o $obj
        else
            $CC $CFLAGS -O2 $src -o $obj
        fi
    done
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
    build_wolfssl

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
UCFLAGS="-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIE -fno-PIC -fcf-protection=none -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-red-zone -mcmodel=kernel $USER_NOSTDINC -Isrc/user/lib/libc/inc -Iinclude -Iinclude/user -Isrc/user/lib/userlib -Isrc/user/lib/userui -Isrc/user/lib/tinygl -Isrc/user/lib -c"
ULDFLAGS="-nostdlib -no-pie -m elf_x86_64"
UCC="$CC"

function build_userland() {
    echo "[BUILD] Building userland apps..."
    mkdir -p bin/userapp $ISO_ROOT/user bin/obj/user/lib

    # Shared ring-3 runtime compiled ONCE and linked into every app ELF.
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


    # FreeType
    local FT_SRCS="base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbbox.c base/ftglyph.c base/ftbdf.c base/ftbitmap.c base/ftmm.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c raster/raster.c psnames/psnames.c psaux/psaux.c pshinter/pshinter.c"
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

    # Single-file apps under src/user/apps/
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        base=$(basename "$src" .c)
        echo "  CC user/$base.c -> bin/userapp/$base.o"
        local app_cflags="$UCFLAGS"
        # EFL-using apps need the Eina/Eo header directories on the include path.
        case "$base" in
            eintest) app_cflags="$app_cflags -Isrc/user/lib/eina" ;;
            eotest)  app_cflags="$app_cflags -Isrc/user/lib/eina -Isrc/user/lib/eo -DEAPI= -DEAPI_WEAK= -DEOAPI=" ;;
            zlibtest) app_cflags="$app_cflags -Isrc/user/lib/zlib" ;;
            eettest) app_cflags="$app_cflags -Isrc/user/lib/eina -Isrc/user/lib/emile -Isrc/user/lib/eet -Isrc/user/lib/zlib" ;;
            pngtest) app_cflags="$app_cflags -Isrc/user/lib/png -Isrc/user/lib/zlib" ;;
            jpegtest) app_cflags="$app_cflags -Isrc/user/lib/jpeg" ;;
            ecoretest) app_cflags="$app_cflags -Isrc/user/lib/ecore" ;;
        esac
        $UCC $app_cflags "$src" -o "bin/userapp/$base.o"

        echo "  LD user/$base.elf"
        $LD $ULDFLAGS -o "bin/userapp/$base.elf" "bin/userapp/$base.o"              bin/obj/user/lib/*.o bin/obj/user/libeina.a bin/obj/user/libeo.a bin/obj/user/libeet.a bin/obj/user/libemile.a bin/obj/user/libpng.a bin/obj/user/libjpeg.a bin/obj/user/libecore.a bin/obj/user/libezlib.a

        echo "  CP user/$base.elf -> $ISO_ROOT/user/"
        cp "bin/userapp/$base.elf" "$ISO_ROOT/user/"
    done < <(find src/user/apps -maxdepth 1 -name '*.c' | sort)

    # Folder-form ring-3 apps (multi-file, described by build.conf)
    build_folders
}

# ──────────────────────────────────────────────────────────────────────────────
# Eina (EFL core library) — freestanding port
# ──────────────────────────────────────────────────────────────────────────────
function build_eina() {
    echo "[BUILD] Building Eina (EFL core, ring-3 port)..."
    mkdir -p bin/obj/user/eina

    # Eina-specific flags layered on top of the common user flags.
    # -nostdinc is inherited from UCFLAGS; eina's own dir is added last so
    # its internal headers take priority over any same-named file elsewhere.
    local EINA_DIR="src/user/lib/eina"
    local EINA_CFLAGS="$UCFLAGS -I$EINA_DIR -DEINA_BUILD -DEFL_BUILD -DEFL_BETA_API_SUPPORT -DHAVE_CLOCK_GETTIME -UHAVE_MMAP -UHAVE_EXECINFO_H -UHAVE_VALGRIND -DNVALGRIND -w"
    # NOTE: -UHAVE_MMAP and -UHAVE_EXECINFO_H suppress mmap/libunwind paths
    # which are OS-specific and we do not need for the freestanding port.

    # Files that use POSIX-only APIs we cannot stub:
    #   eina_file_posix.c  — mmap-based file loader (replaced by our VFS)
    #   eina_file_win32.c  — Windows only
    #   eina_thread_win32.c — Windows only
    #   eina_win32_dllmain.c — Windows only
    #   eina_debug_bt*.c   — libunwind / backtrace (not available ring-3)
    #   eina_debug_cpu.c   — CPU profiling (POSIX signals)
    #   eina_debug_thread.c— pthread internals we stub
    #   eina_mmap.c        — mmap wrapper
    #   eina_xattr.c       — Linux extended attributes (xattr syscall)
    local EXCLUDE="eina_file_posix.c eina_file_win32.c eina_thread_win32.c \\
                   eina_win32_dllmain.c eina_debug.c eina_debug_bt.c eina_debug_bt_file.c \\
                   eina_debug_cpu.c eina_debug_thread.c eina_mmap.c eina_xattr.c \\
                   eina_module.c eina_evlog.c eina_benchmark.c eina_hamster.c"

    local EINA_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        local skip=0
        for e in $EXCLUDE; do [ "$b" = "$e" ] && skip=1 && break; done
        [ $skip -eq 1 ] && echo "  [SKIP] eina/$b (excluded)" && continue

        local obj="bin/obj/user/eina/${b%.c}.o"
        echo "  CC eina/$b"
        $UCC $EINA_CFLAGS "$src" -o "$obj" || { echo "[EINA] build failed on $b"; return 1; }
        EINA_OBJS="$EINA_OBJS $obj"
    done < <(find "$EINA_DIR" -maxdepth 1 -name '*.c' | sort)

    # Archive Eina into a static lib so it links cleanly into any EFL app.
    echo "  AR bin/obj/user/libeina.a"
    ar rcs bin/obj/user/libeina.a $EINA_OBJS
    echo "[SUCCESS] Eina library built: bin/obj/user/libeina.a"
}

# ──────────────────────────────────────────────────────────────────────────────
# Eo (EFL object system, ring-3 port) — depends on Eina.
# Generated .eo.h/.eo.c come from host eolian_gen (see notes in eo/ dir).
# ──────────────────────────────────────────────────────────────────────────────
function build_eo() {
    echo "[BUILD] Building Eo (EFL object system, ring-3 port)..."
    mkdir -p bin/obj/user/eo

    local EO_DIR="src/user/lib/eo"
    local EINA_DIR="src/user/lib/eina"
    local EO_CFLAGS="$UCFLAGS -I$EO_DIR -I$EINA_DIR -DEINA_BUILD -DEFL_BUILD -DEFL_BETA_API_SUPPORT -DEAPI= -DEAPI_WEAK= -DEOAPI= -DHAVE_CLOCK_GETTIME -UHAVE_MMAP -UHAVE_EXECINFO_H -UHAVE_VALGRIND -DNVALGRIND -w"

    local EO_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        # Generated .eo.c files are #include'd by their hand-written .c
        # counterparts (eo_base_class.c, eo.c, eo_class_class.c) — they must
        # not be compiled as standalone translation units.
        case "$b" in *.eo.c) continue ;; esac
        local obj="bin/obj/user/eo/${b%.c}.o"
        echo "  CC eo/$b"
        $UCC $EO_CFLAGS "$src" -o "$obj" || { echo "[EO] build failed on $b"; return 1; }
        EO_OBJS="$EO_OBJS $obj"
    done < <(find "$EO_DIR" -maxdepth 1 -name '*.c' | sort)

    echo "  AR bin/obj/user/libeo.a"
    ar rcs bin/obj/user/libeo.a $EO_OBJS
    echo "[SUCCESS] Eo library built: bin/obj/user/libeo.a"
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
# Emile (EFL serialization / compression, ring-3 port) — depends on Eina + zlib.
# Cipher backends (OpenSSL/GnuTLS) are stubbed (see emile_cipher_stub.c).
# ──────────────────────────────────────────────────────────────────────────────
function build_emile() {
    echo "[BUILD] Building Emile (EFL serialization/compression, ring-3 port)..."
    mkdir -p bin/obj/user/emile

    local EMILE_DIR="src/user/lib/emile"
    local EMILE_CFLAGS="$UCFLAGS -I$EMILE_DIR -Isrc/user/lib/eina -Isrc/user/lib/zlib -DEFL_BUILD -DEFL_BETA_API_SUPPORT -w"

    local EMILE_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        local obj="bin/obj/user/emile/${b%.c}.o"
        echo "  CC emile/$b"
        $UCC $EMILE_CFLAGS "$src" -o "$obj" || { echo "[EMILE] build failed on $b"; return 1; }
        EMILE_OBJS="$EMILE_OBJS $obj"
    done < <(find "$EMILE_DIR" -maxdepth 1 -name '*.c' | sort)

    echo "  AR bin/obj/user/libemile.a"
    ar rcs bin/obj/user/libemile.a $EMILE_OBJS
    echo "[SUCCESS] Emile library built: bin/obj/user/libemile.a"
}

# ──────────────────────────────────────────────────────────────────────────────
# Eet (EFL serialization library, ring-3 port) — depends on Eina + Emile + zlib.
# The container I/O layer is a fresh eet_lib.c (see that file); the serialization
# core (eet_data.c, eet_dictionary.c, eet_node.c, eet_alloc.c) is upstream.
# ──────────────────────────────────────────────────────────────────────────────
function build_eet() {
    echo "[BUILD] Building Eet (EFL serialization, ring-3 port)..."
    mkdir -p bin/obj/user/eet

    local EET_DIR="src/user/lib/eet"
    local EET_CFLAGS="$UCFLAGS -I$EET_DIR -Isrc/user/lib/eina -Isrc/user/lib/emile -Isrc/user/lib/zlib -DEFL_BUILD -DEFL_BETA_API_SUPPORT -w"

    local EET_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        local obj="bin/obj/user/eet/${b%.c}.o"
        echo "  CC eet/$b"
        $UCC $EET_CFLAGS "$src" -o "$obj" || { echo "[EET] build failed on $b"; return 1; }
        EET_OBJS="$EET_OBJS $obj"
    done < <(find "$EET_DIR" -maxdepth 1 -name '*.c' | sort)

    echo "  AR bin/obj/user/libeet.a"
    ar rcs bin/obj/user/libeet.a $EET_OBJS
    echo "[SUCCESS] Eet library built: bin/obj/user/libeet.a"
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
# Ecore (EFL main-loop, ring-3 port) — classic scheduling API without the
# Efl interface layer (eolian codegen unavailable in this freestanding port).
# ──────────────────────────────────────────────────────────────────────────────
function build_ecore() {
    echo "[BUILD] Building Ecore (main-loop, freestanding port)..."
    mkdir -p bin/obj/user/ecore

    local ECORE_DIR="src/user/lib/ecore"
    local ECORE_CFLAGS="$UCFLAGS -I$ECORE_DIR -w"

    local ECORE_OBJS=""
    while IFS= read -r src; do
        [ -f "$src" ] || continue
        local b=$(basename "$src")
        local obj="bin/obj/user/ecore/${b%.c}.o"
        echo "  CC ecore/$b"
        $UCC $ECORE_CFLAGS "$src" -o "$obj" || { echo "[ECORE] build failed on $b"; return 1; }
        ECORE_OBJS="$ECORE_OBJS $obj"
    done < <(find "$ECORE_DIR" -maxdepth 1 -name '*.c' | sort)

    echo "  AR bin/obj/user/libecore.a"
    ar rcs bin/obj/user/libecore.a $ECORE_OBJS
    echo "[SUCCESS] Ecore built: bin/obj/user/libecore.a"
}

# ──────────────────────────────────────────────────────────────────────────────
# Read a KEY=VALUE pair out of a build.conf; echoes the value (or "").
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
        $LD $ULDFLAGS -o "bin/userapp/$NAME.elf" $OBJS              bin/obj/user/lib/*.o              bin/obj/user/libpng.a bin/obj/user/libjpeg.a bin/obj/user/libecore.a bin/obj/user/libezlib.a              || { echo "[FOLDER] link failed for $NAME"; return 1; }

        echo "  CP user/$NAME.elf -> $ISO_ROOT/user/"
        cp "bin/userapp/$NAME.elf" "$ISO_ROOT/user/"

        # Register as a Limine module so it can be spawned (idempotent).
        if ! grep -qF "boot():/user/$NAME.elf" config/limine.conf; then
            printf '    module_path: boot():/user/%s.elf\n' "$NAME"                  >> config/limine.conf
        fi
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
    build_eina
    build_eo
    build_zlib
    build_emile
    build_eet
    build_png
    build_jpeg
    build_ecore
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

    QEMU_FLAGS="-cdrom $ISO_NAME -m 4G -smp 4 -machine q35 -serial stdio                  -net nic,model=e1000 -net user -rtc base=localtime                  -vga std -display vnc=:1                  -device AC97"

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
    "eina")
        # Build only the Eina library — useful during the EFL port.
        # Requires userland libs to already be compiled.
        build_eina
        ;;
    "eo")
        # Build only the Eo library — useful during the EFL port.
        # Requires userland libs (and Eina) to already be compiled.
        build_eo
        ;;
    "zlib")
        # Build only the zlib library — useful during the EFL port (Eet needs it).
        # Requires userland libs to already be compiled.
        build_zlib
        ;;
    "emile")
        # Build only the Emile library — useful during the EFL port.
        build_eina
        build_zlib
        build_emile
        ;;
    "eet")
        # Build only the Eet library — useful during the EFL port.
        build_eina
        build_zlib
        build_emile
        build_eet
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
    "ecore")
        # Build only the Ecore library — useful during the EFL UI stack port.
        build_ecore
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
        echo "Usage: $0 {build|clean|run|eina|log|help}"
        echo "  build: Compiles kernel + all ring-3 apps + ISO"
        echo "  clean: Removes build artifacts"
        echo "  run:   Builds (if needed) and runs in QEMU"
        echo "  eina:  Compile only the Eina EFL library (for iteration)"
        echo "  log:   Run headless and save serial log to eigen_qemu.log"
        echo "  help:  Shows this help"
        ;;
esac
