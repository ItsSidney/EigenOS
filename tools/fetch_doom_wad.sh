#!/usr/bin/env bash
# ============================================================
#  Eigen OS — fetch_doom_wad.sh : supply the DOOM shareware WAD
#
#  The DOOM port is engine-only. It needs an IWAD (doom1.wad,
#  the shareware DOOM) to actually run. That file is copyrighted
#  and is NOT bundled in this repo.
#
#  To play:
#    1. Obtain a legal copy of doom1.wad (e.g. the free shareware
#       release from the original 1993 upload, or your own backup).
#    2. Place it next to this script (or pass the path as $1).
#    3. Run:  ./tools/fetch_doom_wad.sh /path/to/doom1.wad
#    4. Rebuild:  ./build.sh build
#
#  The script copies the WAD into bin/iso_root/user/ and adds the
#  boot module line to config/limine.conf so the kernel can hand it
#  to the DOOM ring-3 app via EIGEN_SYS_MODLOAD.
# ============================================================
set -e
SRC="${1:-doom1.wad}"
DST="bin/iso_root/user/doom1.wad"
CONF="config/limine.conf"

if [ ! -f "$SRC" ]; then
    echo "error: $SRC not found"
    echo "usage: $0 /path/to/doom1.wad"
    exit 1
fi

mkdir -p bin/iso_root/user
cp "$SRC" "$DST"
echo "[doom] copied $(stat -c%s "$DST") bytes -> $DST"

if ! grep -q "boot():/user/doom1.wad" "$CONF"; then
    # Insert right after the doom.elf module line if present, else append.
    if grep -q "boot():/user/doom.elf" "$CONF"; then
        sed -i "s|module_path: boot():/user/doom.elf|&\\n    module_path: boot():/user/doom1.wad|" "$CONF"
    else
        sed -i "s|display_mode: auto|display_mode: auto\\n    module_path: boot():/user/doom1.wad|" "$CONF"
    fi
    echo "[doom] registered doom1.wad boot module in $CONF"
fi
echo "[doom] done. Rebuild with ./build.sh build"
