# Eigen — Comprehensive Documentary

<img src="../misc/logo.png" alt="Eigen Logo" width="200"/>

```
```
███████╗██╗ ██████╗ ███████╗███╗   ██╗
██╔════╝██║██╔════╝ ██╔════╝████╗  ██║
█████╗  ██║██║  ███╗█████╗  ██╔██╗ ██║
██╔══╝  ██║██║   ██║██╔══╝  ██║╚██╗██║
███████╗██║╚██████╔╝███████╗██║ ╚████║
╚══════╝╚═╝ ╚═════╝ ╚══════╝╚═╝  ╚═══╝
```
```

> **Eigen** — A modern, low‑level x86_64 operating system built from scratch with C, Rust, and pure hardware passion.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Philosophy & Design Goals](#philosophy--design-goals)
3. [Boot Process](#boot-process)
4. [Architecture Overview](#architecture-overview)
5. [Memory Management](#memory-management)
6. [Kernel Subsystems](#kernel-subsystems)
   1. [Task Scheduler / Multitasking](#task-scheduler--multitasking)
   2. [Interrupts & Exceptions (IDT/GDT/TSS)](#interrupts--exceptions-idtgdttss)
   3. [Time & Timers](#time--timers)
   4. [Security & Users](#security--users)
   5. [Logging & Debug](#logging--debug)
7. [Graphics & Display](#graphics--display)
   1. [Framebuffer & GPU Drivers](#framebuffer--gpu-drivers)
   2. [Window Manager (WM)](#window-manager-wm)
   3. [GUI Shell & Theme System](#gui-shell--theme-system)
   4. [Widget Toolkit & UI Primitives](#widget-toolkit--ui-primitives)
8. [Input Subsystem](#input-subsystem)
9. [Filesystem Stack](#filesystem-stack)
   1. [VFS Layer](#vfs-layer)
   2. [RAM Filesystem (ramfs)](#ram-filesystem-ramfs)
   3. [FAT32 Driver & Disk I/O](#fat32-driver--disk-io)
10. [Networking](#networking)
    1. [Driver Layer (e1000 & virtio-net)](#driver-layer-e1000--virtio-net)
    2. [BSD-style IP/Ethernet Stack](#bsd-style-ipehernet-stack)
    3. [DNS / ICMP / UDP](#dns--icmp--udp)
    4. [Socket API & Apps](#socket-api--apps)
11. [Audio](#audio)
12. [Mathematics Engine](#mathematics-engine)
13. [Applications](#applications)
14. [Scripting — Eigen C](#scripting--eigen-c)
15. [Vision & Roadmap](#vision--roadmap)
16. [Hardware Compatibility](#hardware-compatibility)
17. [Build & Development](#build--development)
18. [Credits & License](#credits--license)

---

## Introduction

Eigen is an experimental, hobby‑grade x86_64 operating system designed to explore low‑level systems programming, real‑time graphics, and mathematical computation. It boots via the **Limine** bootloader, targets the **x86_64** architecture, and features a full desktop GUI, multithreaded kernel, networking stack, and a Rust/C hybrid codebase.

The project grew from a simple kernel tutorial into a complete GUI environment with virtual desktops, an app ecosystem, pluggable filesystems, and an embedded math engine capable of evaluating expressions, solving equations, and rendering plots.

---

## Philosophy & Design Goals

- **Low‑level fidelity**: Direct hardware access, freestanding kernel, no standard library dependencies.
- **Hybrid language kernel**: C for drivers/syscalls, Rust for memory-safe subsystems (process registry, allocator, games).
- **Flat, modern UI**: Adwaita‑inspired icons, plain blue (0x003366) default theme, compact taskbar, virtual desktop support.
- **Developer friendly**: Live build scripts, serial debug output, QEMU‑first workflow, and a custom scripting runtime (Eigen C).
- **Extensible**: VFS abstracts storage, WM abstracts windows, theme system abstracts colors.

---

## Boot Process

1. **Firmware (UEFI / BIOS)** loads Limine.
2. Limine loads `bin/eigen.bin` and optional kernel modules (wallpapers, assets).
3. `_start_c` in `src/kernel/kernel.c` runs:
   - Serial debug init
   - Framebuffer & HHDM setup via Limine protocols
   - IDT, FPU, GDT, TSS initialization
   - Memory map parsing
4. `kmain()` executes:
   - Kernel heap (`kheap_init`)
   - Virtual memory manager (`vmm_init`)
   - PIT timer @ 1000 Hz
   - Tasking / scheduler
   - Security subsystem (users, roles, session CR3)
   - Rust bridge init (`rust_kernel_init`)
   - Audio, Filesystem, PCI, ACPI, GPU
   - Network stack + mouse
   - VFS mount (RAM + FAT32)
   - Launch GUI shell

---

## Architecture Overview

```
┌─────────────────────────────────────────────────┐
│                  User Applications               │
│  Terminal · Text Editor · Calculator · Games     │
│  Weather · HTTP Viewer · File Explorer · Piano   │
├─────────────────────────────────────────────────┤
│                Window Manager (WM)               │
│  Z-order · Drag/Resize · Taskbar · Desktops      │
├─────────────────────────────────────────────────┤
│              GUI Shell / Theme System            │
│  Personalization · Wallpapers · Icons            │
├─────────────────────────────────────────────────┤
│           Widget Toolkit & UI Primitives         │
│  Buttons · Menus · Input fields · Scrollbars     │
├─────────────────────────────────────────────────┤
│                  Kernel Core (C + Rust)          │
│  VMM · SMP · Syscalls · Security · Scheduler     │
├─────────────────────────────────────────────────┤
│               Driver Layer                       │
│  Video (FB/GPU) · Input (Kbd/Mouse) · Audio      │
│  Storage (IDE) · Network (e1000/virtio) · PCI    │
├─────────────────────────────────────────────────┤
│               Filesystem Layer                   │
│  VFS · ramfs · FAT32 · Block devices             │
├─────────────────────────────────────────────────┤
│               Hardware Abstraction               │
│  Limine boot protocol · ACPI · RTC · PIT         │
└─────────────────────────────────────────────────┘
```

---

## Memory Management

- **Physical**: `kernel/mem/pmm.c` — page frame allocator.
- **Virtual**: `kernel/mem/vmm.c` — 4-level paging (PML4 → PDPT → PD → PT).
- **Heap**: `kernel/mem/kheap.c` — bump/block allocator for kernel objects.
- **Higher-half direct map (HHDM)**: Limine maps all physical memory at a fixed offset for easy access.
- **Page directories**: Per-session CR3 switching for user isolation.

---

## Kernel Subsystems

### Task Scheduler / Multitasking
- Preemptive tasking in `kernel/task/task.c`.
- Cooperative sleeping (`sleep_task`) and background workers.
- Rust process registry for cross-language task tracking.

### Interrupts & Exceptions (IDT/GDT/TSS)
- Full x86_64 IDT with 256 entries.
- GDT with kernel/user code/data segments.
- TSS for privileged stack switching.

### Time & Timers
- PIT (Programmable Interval Timer) at 1000 Hz.
- RTC (Real-Time Clock) for wall-clock time.
- Tick counters exposed to Rust.

### Security & Users
- Multi-user model with up to 16 accounts.
- Privilege levels: `ADMIN`, `USER`, `GUEST`.
- Password hashing (simple hash for demo).
- Session tracking with per-session page directories.
- Ring 3 transition support (`switch_to_user_mode`).

### Logging & Debug
- Early boot log rendered on framebuffer.
- Serial port (COM1) debug output.
- Boot progress bar with 31 discrete steps.

---

## Graphics & Display

### Framebuffer & GPU Drivers
- **Limine framebuffer** provides a linear framebuffer mapped into kernel space.
- **GPU abstraction** in `drivers/video/gpu.c`:
  - `gpu_init()`, `gpu_clear()`, `gpu_present()`
  - Backends for VirtIO GPU and Intel GMA.
- **GFX primitives** in `drivers/video/gfx.c`: rectangles, lines, text, blitting.

### Window Manager (WM)
- Fixed-size slot table: up to 8 simultaneous windows.
- Features: drag, resize, z-order, minimize, close.
- Per-window viewport scrolling (`view_x`, `view_y`).
- Virtual desktop support — windows persist per desktop.
- Callbacks: render, key, mouse, resize.
- Background thumbnail / live preview (via wallpaper redraw).

### GUI Shell & Theme System
- Monolithic theme roles (40+ roles) per `gui/theme.c`.
- Personalization settings:
  - Theme: Light / Dark / Blue Default
  - Accent color, font size, corner radius (flat = 0)
  - Saturation, contrast, transparency
  - Mouse sensitivity
- Wallpaper engine with pattern scaling (1×, 2×, 4×).

### Widget Toolkit & UI Primitives
- Buttons with hover/active states.
- Menus, scrollbars, input fields.
- Adwaita-style icon set baked into `gui/adwaita_icons.h`.
- Taskbar with inline status items.

---

## Input Subsystem

- **Keyboard**: scancode → ASCII translation, event queue.
- **Mouse**: IntelliMouse 4-byte packet support.
  - Wheel delta via `mouse_get_wheel_delta()` / `mouse_clear_wheel_delta()`.
  - WM coordinates vs render coordinate alignment (subtract `WM_TITLEBAR_H`).
  - Bound to framebuffer dimensions.

---

## Filesystem Stack

### VFS Layer
- `filesystem/vfs.c` implements a mount point tree.
- Unified path resolution (`/`, relative, absolute).
- Node reference counting for caching.

### RAM Filesystem (ramfs)
- In-memory file storage (256 files, 8 KB each).
- Supports create, read, write, delete, mkdir, trash/restore.
- Home and trash directories.

### FAT32 Driver & Disk I/O
- `filesystem/fat32.c` reads FAT32 partitions.
- Block device abstraction (`drivers/storage/storage.c`, `ide.c`).
- Auto-mount on boot; up to 6 storage devices probed.

---

## Networking

### Driver Layer (e1000 & virtio-net)
- PCI enumeration for NICs.
- e1000 (`drivers/net/e1000.c`) and VirtIO net (`drivers/net/virtio_net.c`).
- Memory-mapped I/O + descriptor rings.

### BSD-style IP/Ethernet Stack
- Ethernet II framing (`kernel/net/ethernet.c`).
- ARP resolution (`kernel/net/arp.c`).
- IPv4 input/output with checksum offload logic (`kernel/net/ip.c`).
- ICMP echo handling (ping) (`kernel/net/icmp.c`).
- UDP sockets (`kernel/net/udp.c`).
- Socket API exposed to apps (`kernel/net/socket.c`).
- DNS resolver (`kernel/net/dns.c`).

### DNS / ICMP / UDP
- DNS query/response parser.
- ICMP echo request/reply loopback + user-space hook.
- UDP datagram socket support for high-level apps.

### Socket API & Apps
- High-level apps: **HTTP Viewer** (`apps/httpviewer.c`), **Net Debug** (`apps/netdebug.c`), **Weather** (`apps/weather.c`).

---

## Audio

- PC speaker + AC97/PCM abstractions.
- `drivers/audio/engine.c` and `drivers/audio/speaker.c`.
- PCM sample playback pipeline.
- Apps: **Piano** (`apps/piano.c`).

---

## Mathematics Engine

Located in `engine/math_engine.c` and linked as `libmath_engine.a`.

### Vector & Matrix Algebra
- `vec2_t`, `vec3_t`, `vec4_t` with full arithmetic, dot, cross, norm.
- `mat4_t`: identity, multiply, translate, scale, rotate (X/Y/Z), perspective, look-at.
- `quat_t`: identity, multiply, rotation axis-angle, apply to vector, convert to mat4.

### Expression Parser
- `expr_eval(const char* expr, double x)` evaluates mathematical expressions.
- Supported tokens: `+ - * / ^ ( ) sin cos tan sqrt abs log ln exp`.
- Variable: `x`.
- Thread-safe error reporting via `expr_error()`.

### Numerical Solvers
- Newton’s method (`newton`) for root finding.
- Numerical derivative (`deriv`).
- Simpson’s rule integration (`integrate`).

> **This engine powers the graphing calculator, mandelbrot renderer, and experimental physics simulations.**

---

## Applications

| Category | App | Source File |
|----------|-----|-------------|
| Productivity | **Terminal** | `apps/terminal_app.c` |
| Productivity | **Text Editor** | `apps/text_editor.c` |
| Productivity | **File Explorer** | `apps/file_explorer.c` |
| Productivity | **Hex Dump** | `apps/hexdump.c` |
| Productivity | **Bitmap Maker** | `apps/bitmap_maker.c` |
| Productivity | **Save / Load Dialogs** | `apps/save_dialog.c`, `apps/load_dialog.c` |
| System | **Process Viewer** | `apps/process_viewer.c` |
| System | **Performance Monitor** | `apps/perfmon.c` |
| System | **Kernel Log** | `apps/kernellog.c` |
| System | **PCI Scanner** | `apps/pci_scanner.c` |
| System | **Personalization** | `apps/personalization.c` |
| System | **Sound Settings** | (via GUI) |
| Science | **Calculator** | `apps/calculator.c` |
| Science | **Graphing Calculator** | `apps/graphing.c` |
| Science | **Mandelbrot Set** | `apps/mandelbrot.c` |
| Games | **Snake** | `apps/games/snake.c` |
| Games | **Tetris** | (Rust + C bridge) |
| Games | **2048** | `apps/games/game_2048.c` |
| Games | **Minesweeper** | `apps/games/mines.c` |
| Games | **Pairs (Memory)** | `apps/games/pairs.c` |
| Games | **Sudoku** | `apps/games/sudoku.c` |
| Games | **Flappy Bird** | `apps/games/flappy.c` |
| Media | **Image Viewer** | `apps/imgview.c` |
| Media | **Piano** | `apps/piano.c` |
| Media | **Colour Wheel** | `apps/colour_wheel.c` |
| Network | **HTTP Viewer** | `apps/httpviewer.c` |
| Network | **Network Debug** | `apps/netdebug.c` |
| Network | **Weather** | `apps/weather.c` |
| Utilities | **Calendar** | `apps/calendar.c` |
| Utilities | **Clock** | `apps/clock.c` |
| Utilities | **On-Screen Keyboard** | `apps/osk.c` |

---

## Scripting — Eigen C

Eigen C (`eigenc`) is a lightweight scripting language built into the OS.

- **Types**: `int`, `double`/`float`, `string`, `bool`
- **Functions**: user-defined `int`/`double`/`bool`/`string`/`void` functions with typed, checked parameters
- **Arrays**: fixed-size `type name[size]`, zero-based `arr[index]` read/write, bounds-checked at runtime
- **Control Flow**: `if-else`, `while`, `for`
- **Compile & Run**: `ecc <file.ec>` → `erun <file.bin>`
- **REPL**: Terminal app supports inline scripting.
- **Standard Library**: `print`, `input`, `endl`, `exit`.

Full reference: [`docs/eigenc_tutorial.md`](eigenc_tutorial.md)

---

## Vision & Roadmap

> *"Simulate low-level graphics, mathematical expressions, and quantum calculations — and make them all work together."*

Eigen was born from a vision of building a machine that does not just run software, but **understands** it. The three pillars of that vision are actively being pursued:

### 1. Low-Level Graphics Simulation
- Hardware-accelerated 2D and 3D pipelines with clean, inspectable code.
- Software rasterizer with perspective-correct texture mapping, depth buffering, and back-face culling.
- Fractal generation (Mandelbrot, Julia sets) rendered natively in kernel space.
- Widget toolkit that behaves like a real GPU-driven compositor, not a toy framebuffer.
- **Current status**: Stable. 2D is production-grade; 3D software rasterizer is functional and expanding.

### 2. Mathematical Expressions & Computation
- The embedded `math_engine` is the backbone of scientific apps.
- Plans to expand to:
  - Symbolic algebra (simplification, differentiation, integration).
  - Complex number support.
  - Arbitrary-precision arithmetic (soft-float).
  - Plotting engine for multi-variable functions.
- **Current status**: Expression parser and numerical solvers are complete; symbolic math is in early design.

### 3. Quantum Calculations
- A long-term research thrust: simulate quantum circuits and algorithms directly on bare metal.
- Goals:
  - Qubit state-vector and density-matrix simulation.
  - Gates library (Hadamard, CNOT, Toffoli, phase gates).
  - Shor’s and Grover’s algorithm as native OS libraries.
  - Expose quantum primitives to Eigen C scripts and C/Rust apps.
- **Current status**: Conceptual architecture. Design docs and prototype code are in development. This is the farthest-out item on the roadmap, but it guides the math engine’s design today.

### Other Active Work
- **Network hardening**: TCP congestion control, TLS hooks.
- **Audio**: MIDI support and low-latency audio mixing.
- **Multiprocessor**: SMP bring-up for multi-core machines.
- **Userland POSIX layer**: `fork()`, `exec()`, signals.

We are working hard to turn every vision into a shipping, runnable feature.

---

## Hardware Compatibility

### Supported Architectures
- **x86_64** (primary)
  - UEFI (x64)
  - BIOS (legacy, via Limine)

### Verified Environments
| Environment | Status |
|-------------|--------|
| QEMU (`q35`, KVM or TCG) | ✅ Stable |
| GNOME Boxes | ✅ Compatible |
| HP EliteBook (hardware) | ✅ Tested |

### Emulated Hardware Targets
- QEMU Standard PC (i440FX + ICH9)
- Q35 Chipset (PCIe)
- Intel HDA / AC97 audio
- e1000 or virtio-net NICs
- IDE or virtio-blk storage

### Known Limitations
- No ACPI = degraded power management (halts fine).
- Requires at least 4 GB RAM recommended for GUI + builds inside OS.
- virtio-gpu preferred for best graphics performance.

---

## Build & Development

### Prerequisites
- `gcc` (x86_64-elf or host cross-compiler accepted via flags)
- `nasm`
- `ld` (GNU linker)
- `cargo` + `rustc` (x86_64-unknown-none target)
- `xorriso`

### Build Commands

```bash
# Full clean + build
./build.sh clean
./build.sh build

# Run in QEMU
./build.sh run

# Run with serial log
./build.sh log
```

### Directory Layout

| Path | Purpose |
|------|---------|
| `src/` | C + ASM kernel and driver sources |
| `src/rust/` | Rust kernel module (allocator, tetris, process registry) |
| `engine/` | Standalone math engine library |
| `include/` | Shared headers |
| `config/` | Linker script + Limine config |
| `tools/` | Bootloader binaries and helper scripts |
| `docs/` | Documentation and tutorials |
| `misc/` | Logo and banner assets |

---

## Credits & License

- **Author / Maintainer**: Sidney (Bedi)
- **License**: MIT (see `LICENSE`)
**Branding**: Eigen banner and logo assets are original works.

---

*Eigen — Low-level graphics. Mathematical expressions. Quantum calculations. Built from scratch.*
