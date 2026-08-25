#!/bin/bash
# tools/build-busybox.sh — BusyBox 1.36.1 against EigenOS real musl.
#
# Pipeline: Kbuild compile with EXTRA_CFLAGS -> per-dir lib.a archives
# (two passes: generated headers can land mid-build) -> Eigen link
# (eigen crt + shim + libmusl.a, -z muldefs), identical to every other
# ring-3 ELF.
set -e
cd "$(dirname "$0")/.."

ROOT="$(pwd)"
BB=libs/busybox
OUT="$ROOT/bin/obj/user/busybox"
MUSL="$ROOT/libs/musl"
mkdir -p "$OUT"

[ -d "$BB" ] || { echo "[BUSYBOX] $BB missing"; exit 1; }

EXTRA_CFLAGS="-nostdinc -ffreestanding -fno-stack-protector -fno-stack-check \
 -fno-lto -fno-PIE -fno-PIC -fcf-protection=none -m64 -march=x86-64 \
 -mno-80387 -mno-mmx -mno-red-zone -mcmodel=kernel -mlong-double-64 \
 -I$MUSL/src/include -I$MUSL/arch/eigen -I$MUSL/src/internal -I$MUSL/include \
 -I$ROOT/src/user/lib/libc/inc -I$ROOT/include -I$ROOT/include/user \
 -I$ROOT/include/bbstub -I$ROOT/include/bbstub/sys \
 -I$ROOT/include/bbstub/bits -I$ROOT/include/bbstub/asm -w"

# Applets needing /proc, kernel headers, clone-family or SysV IPC we lack.
# Re-applied after every kconfig pass — syncconfig races can re-set =y.
apply_kill_list() {
    local CFG="$1"
    for no in PING TRACEROUTE IP IFCONFIG ROUTE NETSTAT MOUNT UMOUNT \
              SWAPONOFF FDISK MODPROBE INSMOD RMMOD LSMOD DEPMOD ASH \
              INIT FEATURE_INITRD KBD_MODE LOADKMAP DUMPKMAP SETCONSOLE \
              SHOWKEY LOADFONT SETFONT SETKEYCODES DEALLOCVT OPENVT \
              CONSOLE_TOOLS FEATURE_SETPRIV CAPABILITY WATCHDOG NBDCLIENT \
              I2C_TOOLS I2CTRANSFER I2CGET I2CSET I2CDUMP I2CDETECT \
              HDPARM MKDOSFS MKFS_EXT2 MKFS_MINIX MKFS_REISER MKFS_VFAT \
              NANDWRITE NANDDUMP FLASHCP IFPLUGD IFENSLAVE FBSPLASH FBSET \
              BRCTL ARPING ETHER_WAKE FAKEIDENTD FTPGET FTPPUT HTTPD \
              IPADDR IPLINK IPNEIGH IPROUTE IPRULE IPTUNNEL NBD_SERVER NC \
              SLATTACH TC UDHCPC6 UDHCPC WGET ZCIP CONSPY SETSERIAL \
              RTCWAKE SETARCH CHRT TASKSET UEVENT MDEV NTPD NAMEIF \
              RAIDAUTORUN SEEDRNG ACPID BLKDISCARD IONICE TUNCTL \
              FEATURE_UTMP FEATURE_WTMP DEVMEM BEEP MODUTILS MODINFO \
              SETPRIV LOGREAD SYSLOGD LOGGER IPCRM IPCS HUSH UBIRENAME \
              UBIATTACH UBIDETACH UBIMKVOL UBIRMVOL UBIRSVOL UBIUPDATEVOL \
              FEATURE_SH_EMBEDDED_SCRIPTS FEATURE_SH_STANDALONE; do
        sed -i "s/^CONFIG_${no}=y/# CONFIG_${no} is not set/" "$CFG"
    done
    sed -i 's/^CONFIG_SH_IS_ASH=y/# CONFIG_SH_IS_ASH is not set/'   "$CFG"
    sed -i 's/^CONFIG_SH_IS_HUSH=y/# CONFIG_SH_IS_HUSH is not set/' "$CFG"
    sed -i 's/^# CONFIG_SH_IS_NONE is not set/CONFIG_SH_IS_NONE=y/' "$CFG"
}

# Configure ONCE; defconfig clobbers, so guard hard.
if [ ! -f "$OUT/.config" ]; then
    echo "[BUSYBOX] defconfig + Eigen tuning..."
    make -C "$BB" O="$OUT" defconfig > /dev/null
    apply_kill_list "$OUT/.config"
    sed -i "s|^CONFIG_EXTRA_CFLAGS=.*|CONFIG_EXTRA_CFLAGS=\"$EXTRA_CFLAGS\"|" "$OUT/.config"
    sed -i "s|^CONFIG_EXTRA_LDFLAGS=.*|CONFIG_EXTRA_LDFLAGS=\"-nostdlib -no-pie\"|" "$OUT/.config"
    yes "" | make -C "$BB" O="$OUT" oldconfig > /dev/null 2>&1 || true
    apply_kill_list "$OUT/.config"          # oldconfig may re-set symbols
    yes "" | make -C "$BB" O="$OUT" oldconfig > /dev/null 2>&1 || true
fi
apply_kill_list "$OUT/.config"

# Pin config artifacts: syncconfig inside -j makes RACES and re-enables
# default-y symbols. Sequential oldconfig here, then make artifacts newer
# than .config so parallel makes never re-enter kconfig.
yes "" | make -C "$BB" O="$OUT" oldconfig > /dev/null 2>&1 || true
apply_kill_list "$OUT/.config"
touch "$OUT/.config" -d "2 hours ago"
mkdir -p "$OUT/include/config"
yes "" | make -C "$BB" O="$OUT" oldconfig > /dev/null 2>&1 || true
touch "$OUT/include/config/auto.conf" "$OUT/include/autoconf.h"

# Purge NUL-corrupted .cmd files (left by interrupted builds; they make
# kbuild die with "missing separator" and silently skip whole dirs)
find "$OUT" -name "*.cmd" -type f | while read -r f; do
  if LC_ALL=C grep -q "$(printf '\x00')" "$f" 2>/dev/null; then rm -f "$f"; fi
done

# Always re-apply: the kill list grows; existing configs must honor it
apply_kill_list "$OUT/.config"

echo "[BUSYBOX] compiling (Kbuild)..."
make -C "$BB" O="$OUT" -j"$(nproc)" > "$OUT/build.log" 2>&1 || true

# Archive passes: generated headers (applet tables, embedded scripts) can
# regenerate mid-build leaving appletlib.o newer than its archive; two
# incremental passes converge. Targets are per-dir lib.a only — the Kbuild
# final gcc link would fail (glibc) and -j aborts sibling dir builds.
DIRS=$(cd "$BB" && ls -d */ | tr -d '/' | \
       grep -vE 'scripts|applets|applets_sh|arch|configs|docs|examples|testsuite|include' | tr '\n' ' ')
TARGETS=$(for d in $DIRS; do echo "$d/lib.a"; done)
make -k -C "$BB" O="$OUT" -j"$(nproc)" $TARGETS >> "$OUT/build.log" 2>&1 || true
make -k -C "$BB" O="$OUT" -j"$(nproc)" $TARGETS >> "$OUT/build.log" 2>&1 || true

# capability.o is config-gated out of libbb/lib.a but switch_root.o needs it
make -C "$BB" O="$OUT" libbb/capability.o >> "$OUT/build.log" 2>&1 || true

BUILTIN=$(find "$OUT" -name 'built-in.o' | tr '\n' ' ')
LIBS=$(find "$OUT" -name 'lib.a' | tr '\n' ' ')
[ -f "$OUT/applets/applets.o" ] || { echo "[BUSYBOX] no applets.o — see $OUT/build.log"; exit 1; }

echo "[BUSYBOX] linking bin/userapp/busybox.elf (Eigen link)..."
mkdir -p bin/userapp
ld -nostdlib -no-pie -m elf_x86_64 -z muldefs --start-group \
   -o bin/userapp/busybox.elf \
   bin/obj/user/lib/*.o $BUILTIN \
   "$OUT/libbb/capability.o" \
   "$OUT/applets/applets.o" $LIBS \
   bin/obj/user/libmusl.a --end-group

cp bin/userapp/busybox.elf bin/iso_root/user/busybox 2>/dev/null || true
echo "[SUCCESS] busybox.elf: $(ls -la bin/userapp/busybox.elf | awk '{print $5}') bytes"
