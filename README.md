<p align="center">
  <img src="misc/logo.svg" width="110" alt="EigenOS lambda mark">
</p>

# EigenOS

An x86_64 operating system written from scratch in C and a bit of assembly.
It boots with Limine, runs ring-3 ELF programs out of an in-memory filesystem,
draws its own desktop, drives an e1000 NIC with its own TCP/IP stack, and as
of recently speaks TLS from inside the kernel.

Every matrix has eigenvalues, Av = λv. Hence the logo.

This is a one-person project. It builds in about two minutes, fits on a
bootable ISO, and is meant to be read as much as run.

---

## Status

Things that work, verified in QEMU (and mostly on the boot log you see every
time it starts):

- BIOS and UEFI boot through Limine, with the boot progress drawn on screen
- A desktop: window manager, taskbar, desktop switcher, themes, wallpapers
- Ring-3 ELF userland loaded straight from Limine modules — no initrd, no ELF
  loader tricks, the bootloader *is* the package manager
- RAM filesystem on `/`, FAT32 for anything that lands on a real disk
- PS/2 keyboard and mouse, plus USB HID
- e1000 networking with DHCP, DNS, ICMP ping, TCP and UDP sockets
- A TLS 1.2 client inside the kernel (wolfSSL) with the Mozilla CA bundle, so
  the bundled browser can actually open https:// pages
- AC97 audio, PC speaker, a piano app because why not
- DOOM runs. This was non-negotiable.

Things that are rough:

- The TCP stack works but hasn't survived a hostile network. Treat it as demo grade.
- TLS is 1.2 only for now, and there's a single global TLS connection at a time.
- Without KVM everything runs under TCG emulation, which is slow but honest.
- The EFL ports cover the foundation (see below); Evas and everything above
  them are still on the wishlist.

---

## Building and running

You need gcc, nasm, xorriso, and qemu-system-x86_64:

```bash
sudo apt install build-essential nasm xorriso qemu-system-x86
```

Then:

```bash
./build.sh build    # kernel + userland + ISO  ->  eigen-x86_64.iso
./build.sh run      # boots it in QEMU, VNC on :1
```

`run` opens a VNC server on port 5901; if `vncviewer` is installed it will
connect for you, otherwise point your viewer at `:1`. It also clears stale
QEMU instances that would otherwise sit on that port, which sounds aggressive
but saves typing.

KVM is used automatically when `/dev/kvm` is accessible. If you're not in the
`kvm` group (I wasn't), you get pure TCG emulation — slower, entirely usable.

Other targets:

```bash
./build.sh log      # headless boot, serial output -> eigen_qemu.log
./build.sh clean    # remove build artifacts
```

There are also single-library targets left over from porting work, handy when
touching one of them: `eina`, `eo`, `zlib`, `emile`, `eet`, `png`, `jpeg`,
`ecore`. Each rebuilds just that library and its dependencies.

---

## What's inside

```
src/
  boot/         early assembly, GDT, entry
  kernel/       memory, tasks, syscalls, net stack, TLS glue
  drivers/      video, input, audio, storage, network, PCI, ACPI
  filesystem/   VFS, ramfs, FAT32
  gui/          window manager, widgets, icons, theme
  gfx/          framebuffer helpers, splash
  engine/       animation engine
  libs/         tinygl, tiny png/jpeg helpers
  user/         everything ring-3:
    lib/          libc shims, userlib/userui, pthread stubs,
                  ports: eina eo eet emile zlib png jpeg ecore freetype imgui
    apps/         terminal, browser, games, viewers, test programs
libs/wolfssl/   vendored wolfSSL, trimmed to what we compile
include/        kernel + userland headers
config/         limine.conf, linker script
tools/          vendored Limine binaries, app scaffolders
docs/           notes, including an old scripting-language tutorial
```

The kernel is compiled `-ffreestanding -mcmodel=kernel`, static linked,
no PIE. Userland is plain ELF64 linked `-nostdlib` against our own libc
shims — which includes software floating point math, because the userland
is built `-mno-80387` and there is no FPU state saving in the scheduler yet.
Yes, `sqrt()` is a function call into our own libm. It's fine. Mostly.

---

## Networking and TLS

The IP stack is homegrown: Ethernet + ARP, IPv4, ICMP, UDP, TCP, DNS resolver,
DHCP lease at boot. Drivers: e1000 (QEMU's default NIC). Everything above the
driver lives in `src/kernel/net/`.

TLS used to be BearSSL. It's wolfSSL now — same job, but upstream is actively
maintained and I wanted PEM-formatted CA bundles instead of hand-rolled trust
anchors. The swap kept the old kernel API (`br_tls_*`) so nothing else had to
change; only the implementation in `src/kernel/net/tls.c` moved.

How it's wired for a kernel with no OS underneath:

- `WOLFSSL_USER_IO` — record I/O goes through callbacks that talk to kernel
  sockets directly. No BSD socket layer, no `select()`.
- RNG seeding uses `rdrand`, but only after CPUID says the CPU has it. QEMU's
  default `qemu64` model doesn't, and executing `rdrand` anyway raises #UD,
  which cascades into a triple fault. That was a fun afternoon. The fallback
  mixes RTC fields with timer ticks — weak entropy, acceptable for a demo OS,
  and it never faults.
- `USER_TIME` hooks feed certificate validity checks from the RTC.
- The CA store is generated from the Mozilla bundle (121 roots) into
  `src/kernel/net/ca_roots.h` as a PEM blob.

At boot the kernel runs a self-test and prints:

```
[WOLFSSL] self-test ok (CAs loaded, RNG ok)
```

If that line shows up, the whole crypto stack came up before any network
existed. **edrowser** (the bundled browser) then uses it for https:// pages.

---

## Ports

A running tally of libraries made to live freestanding in this environment.
Each one has a matching test program that prints `[NAME] ALL PASS`:

| Library        | Version  | Notes                                            |
|----------------|----------|--------------------------------------------------|
| zlib           | 1.3.1    | Z_SOLO, custom allocator                          |
| libpng         | 1.6.43   | SIMD off, software-only                           |
| libjpeg (IJG)  | 9e       | jmemnobs backend, encode+decode verified          |
EFL (Eina, Eo, Emile, Eet, Ecore, Evas, Edje, Elementary) was previously
ported here as the ring-3 GUI toolkit and has since been removed entirely.
The OS now uses its own native compositing desktop under `src/gui/`
(framebuffer + GFX/GPU drivers, Freetype text, input stack); see
`src/gui/wm.c` and `src/drivers/video/`. No EFL code remains in the tree.

---

## Applications

GUI apps (ring-3): terminal, edrowser (web), weather, file explorer,
calculator, calendar, clock, settings, process viewer, image viewer,
paint studio, bitmap maker, graphing calculator, julia and mandelbrot
renderers, colour wheel, on-screen keyboard, hex dump, kernel log, three
TinyGL demos including glgears, imgui playground, FreeType glyph viewer,
and DOOM.

Terminal utilities: `awk` (the one true awk, ported), `kilo`, plus the test
 suite — `posixtest ftglyph pthreadtest setjmptest polltest
zlibtest pngtest jpegtest`. After any library work, run the
matching test in the VM. If it ends in ALL PASS, ship it.

Small ritual worth knowing: `imgview` generates `/demo.png` and `/demo.jpg`
itself at startup — encoding a gradient through libpng and libjpeg, writing
both to the ramfs, then decoding them back to the screen. Press P and J to
flip between lossless and lossy. The whole codec stack, exercised by one key.

---

## Adding an application

```bash
tools/new_app.sh myapp           # single-file app
tools/new_folder_app.sh myapp    # multi-file app with build.conf
```

Folder apps register themselves as Limine modules at build time; single-file
apps need a line in `config/limine.conf` and an entry in the terminal's app
list (`src/user/apps/system/terminal/main.c`). Rebuild, boot, type the name.

---

## Notes on the tree

- `bin/` and the ISO are never committed. `git status` staying clean means
  source state, not build state.
- The Limine binaries are vendored under `tools/bootloader/` (~4 MB) so the
  build works offline.
- `libs/wolfssl/` is vendored and trimmed: docs, examples, tests, benchmarks,
  architecture-specific assembly, post-quantum experiments, and ciphers we
  don't negotiate are removed. What remains is exactly what `build.sh`
  compiles, roughly 29 MB instead of 127 MB.
- `docs/eigenc_tutorial.md` documents a small scripting language that existed
  in an earlier incarnation. The interpreter is gone; the tutorial survives
  as a fossil.

---

## FAQ

**Why the lambda?**
Eigenvalues. Also it looks good stroked in green.

**Why x86_64 only?**
Because the GDT/TSS/paging code is ISA-specific and rewriting it for another
target is a project, not a patch. Never say never.

**Daily driver?**
No. It's a toy in the affectionate sense — a thing built to understand every
layer, where "it crashed" always means "I get to find out why."

---

## License

Source is GPL-3.0, matching the headers on every file. Vendored components
keep their own licenses: Limine (BSD-2-clause), wolfSSL (GPLv2/GPLv3 or
commercial, per its COPYING), zlib, libpng, libjpeg, EFL parts (LGPL-2.1),
FreeType (FTL/GPL-2.0), and the DOOM port's assorted heritage.
