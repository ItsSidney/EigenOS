#!/usr/bin/env bash
# Direct engine-compile harness for the DOOM port (debugging only).
set -u
cd "$(dirname "$0")/.." || exit 1
CC=${CC:-gcc}
DCFLAGS=(-ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -fno-PIE -fno-PIC -fcf-protection=none -m64 -march=x86-64 -mno-mmx -mno-red-zone -mcmodel=kernel -DOOMGENERIC_RESX=640 -DOOMGENERIC_RESY=400 -Isrc/user/doom/inc -Iinclude -Isrc/user/doom/engine -Isrc/userlib -Isrc/userui -c)
EXCLUDE=(i_system.c i_input.c i_sound.c i_scale.c i_joystick.c i_cdmus.c i_allegromusic.c i_allegrosound.c i_endoom.c gusconf.c icon.c w_file.c w_file_stdc.c d_iwad.c doomgeneric_sdl.c doomgeneric_linuxvt.c doomgeneric_win.c doomgeneric_xlib.c doomgeneric_allegro.c doomgeneric_emscripten.c doomgeneric_soso.c doomgeneric_sosox.c i_sdlmusic.c i_sdlsound.c mus2mid.c)
mkdir -p bin/obj/doom
n=0; failed=0
while IFS= read -r src; do
    b=$(basename "$src")
    skip=0
    for e in "${EXCLUDE[@]}"; do [ "$b" = "$e" ] && skip=1 && break; done
    [ $skip -eq 1 ] && continue
    n=$((n+1))
    obj="bin/obj/doom/$(echo "$src" | tr '/' '_').o"
    if ! "$CC" "${DCFLAGS[@]}" "$src" -o "$obj" 2> /tmp/cc.err; then
        echo "FAIL: $b"
        sed -n '1,12p' /tmp/cc.err
        failed=1
        break
    fi
done < <(find src/user/doom/engine -name '*.c' | sort)
# compile glue too
for g in src/user/doom/doomgeneric_eigen.c src/user/doom/doom_libc.c src/user/doom/doom_iwad.c; do
    obj="bin/obj/doom/$(basename "$g" .c).o"
    if ! "$CC" "${DCFLAGS[@]}" "$g" -o "$obj" 2> /tmp/cc.err; then
        echo "FAIL(glue): $g"; sed -n '1,12p' /tmp/cc.err; failed=1
    fi
done
echo "compiled=$n failed=$failed total_objs=$(ls bin/obj/doom/*.o | wc -l)"
