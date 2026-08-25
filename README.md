# EigenOS λ

A from-scratch **64-bit hobby operating system** with a modern twist: a custom kernel, a real POSIX userspace (musl), **289 BusyBox commands**, a compositing GUI with Papirus icons — and DOOM.

```
EigenOS 1.0.0-eigen x86_64
```

![EigenOS](misc/logo.png)

## What makes it interesting

Most hobby OSes stop at "prints text to VGA". EigenOS boots into a full desktop where you can open a terminal, run `ls -la`, edit files in `vi`, play DOOM, and browse your filesystem with real icons — all on a kernel and libc that were built for this OS specifically.

## Feature matrix

### Kernel (x86_64)
| Subsystem | Details |
|---|---|
| Boot | Limine protocol, SMP bring-up with AP parking, KVM/q35 + multi-vendor tested |
| Memory | PMM + VMM, per-process page tables, hhdm, 256MB user heap with owner tracking (no cross-process leaks) |
| Scheduler | Round-robin, preemption, sleeping, FPU/SSE state save (eager FXSAVE), kernel stack canaries |
| Processes | `fork` (eager AS copy), `execve` (in-place image swap), `wait4`, process groups, SIGINT forwarding |
| Signals | Tier-1 POSIX signals with trampoline injection + `sigreturn` |
| TLS | Per-task `IA32_FS_BASE`, `arch_prctl(ARCH_SET_FS)`, pthread-ready layout |
| FS | Inode-style RAM FS (node 0 = `/`), per-task cwd, FHS layout (`/bin /user /etc /dev /home`), **disk persistence** (two-slot CRC A/B snapshots on virtio-blk) |
| Drivers | virtio-blk, e1000 NIC + TCP/IP, AC97/HDA audio, PS/2 + USB mice, RTC, ext-ish flat + VFS mounts |
| Syscalls | 59-entry Eigen ABI + **Linux-compatible layer** for musl |

### Userspace
| Piece | Details |
|---|---|
| **musl libc (real)** | Full build: process, linux, sched, regex, passwd, termios, legacy, signal-asm — 1000+ objects. No shim shortcuts for new ports. |
| **BusyBox 1.36** | **289 applets** compiled against our musl: `ls cat grep sed awk vi top ps wget-ready...` dispatched via argv[0] |
| **Terminal** | Kernel PTY subsystem (cooked + raw line discipline, ^C→SIGINT, winsize) + VT100/xterm emulator: 16-color SGR, scrollback, cursor addressing |
| **Shell** | `sh.elf`: prompt, builtins, PATH lookup, BusyBox fallback, job-friendly ^C relay |
| **GUI** | Compositing window manager: drag/resize/snap/maximize, 20px top taskbar, cascade app menu, Nordzy + **Papirus** icons, λ logo with 6 color variants (incl. rainbow) |
| **Apps** | File Explorer (icon toolbar, list/grid, Papirus type icons), Terminal, Settings, Calculator, Clock, Calendar, Text Editor, Image Viewer, Process Viewer, Graphing, EigenDeck, **DOOM (Chocolate-DOOM port)**, glgears (TinyGL) |
| **Media** | AC97/HDA audio, BMP/PNG/JPEG decoders (kernel + user), FreeType text rendering |

### Developer experience
- One-command build: `./build.sh build` → bootable ISO
- Incremental, cached; BusyBox/musl pipelines fully automated (`tools/build-musl.sh`, `tools/build-busybox.sh`)
- Serial breadcrumbs + in-kernel fault screens with register dumps and user-stack backtraces
- Test apps for every subsystem: `forktest`, `posixtest`, `pthreadtest`, `polltest`...

## Quick start

```bash
git clone https://github.com/ItsSidney/EigenOS.git
cd EigenOS
./build.sh build          # produces eigen-x86_64.iso
```

Run it:

```bash
qemu-system-x86_64 -enable-kvm -cpu host -m 4G -smp 4 -machine q35 \
  -cdrom eigen-x86_64.iso -drive file=disk.img,format=raw,if=virtio \
  -net nic,model=e1000 -net user
```

In the desktop: open **Terminal** → try `uname -a`, `ls`, `cat /etc/passwd`, `vi`, `top` — all real BusyBox.

## Architecture snapshot

```
┌─────────────────────────────────────────────┐
│ Apps: sh · busybox(289) · explorer · DOOM   │
├─────────────────────────────────────────────┤
│ musl libc (real POSIX) + Eigen shim         │
├─────────────────────────────────────────────┤
│ Syscall ABI (59): fork/execve/wait4/stat/   │
│ getdents64/pty/arch_prctl/ioctl/uname...    │
├─────────────────────────────────────────────┤
│ Kernel: sched · VMM · signals · VFS+persist │
│ drivers: virtio-blk e1000 AC97 PS/2 RTC     │
├─────────────────────────────────────────────┤
│ GUI: WM · taskbar · icon themes · FreeType  │
└─────────────────────────────────────────────┘
```

## Roadmap
- [ ] Haiku-style top menu bar (Deskbar)
- [ ] Shared widget kit (buttons/sliders/graphs) across all apps
- [ ] Socket syscall bridge → `wget`, `ping`, `nc` online
- [ ] BusyBox `ash` as alternative shell
- [ ] Copy-on-write fork, dynamic linking (`ld-musl`)

## License
GPL — see headers. Third-party components (musl, BusyBox, DOOM, FreeType, libpng/libjpeg/zlib, wolfSSL, TinyGL) keep their own licenses in-tree.

---
*Built with too much coffee and an unreasonable number of page-fault dumps.*
