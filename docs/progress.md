# EigenOS Progress

_Last updated: 2026-08-24_

Living document: what's done, what's broken, and the prioritized roadmap
(easy → hard). Items marked ✅ are complete and verified in-tree.

---

## ✅ Recently completed (this cycle)

### EFL removal — full purge
- Removed all EFL sources (`eina/eo/ecore/eet/emile/ecore-con/ecore-file/embryo`,
  `libs/{elm,evas,edje}-upstream`), EFL test apps (`eintest…edjetest`, `conntest`,
  `filetest`), build scripts, `build.sh` functions/subcommands, and stale Limine
  module entries. NetSurf untouched.
- Verified: full kernel+userland build green (pre-netsurf), zero EFL refs,
  ISO contains only native apps.

### Desktop / shell polish (ring 0)
- **Taskbar**: ultra-slim 20px polybar style; digit workspaces; text CPU/RAM
  modules; pinned-app strip merged with running windows (dedup); right-click
  context menu (Open / Pin⇄Unpin / Close) with Lucide glyphs; no bottom outline.
- **Start menu**: Haiku-style cascade dropdown — λ button, compact banner,
  categories with Lucide icons, fly-out app submenus; keyboard nav
  (↑↓ ←→ Enter Esc); searchable grid launcher still on Super+S.
- **Window chrome**: layout‑0 buttons (min far-left, max/close far-right),
  vector chevron glyphs, 24px titlebar / 16px buttons, square corners.
- **Theme**: "Nord" factory default (polar-night surfaces, platinum-snow
  accent) replacing white Mist; all popovers/menus de-rounded.
- **Desktop icons**: 64px Nordzy tiles, tiny labels, chrome-less hover wash.
- **Start-button icon import**: drop `.bmp` into `/home/user/icons`, pick via
  right-click picker; persisted to `cfg/starticon.cfg`.

### Icon system
- Kernel-side **libpng + zlib** build (`build_pngk`/`build_zlibk`) with
  kmalloc-backed allocators and libc-gap shims (`kmath.h`, `kzcalloc.c`).
- `tools/pack_icons.py` renders/packs theme PNGs into a C blob
  (`src/gui/nordzy_icons.h`); runtime decoder + RGBA cache
  (`src/gui/icon_theme.c`); procedural glyphs remain as fallback.
- 40 packed icons incl. upstream NetSurf logo (`NetSurf.ico[3]`),
  Chocolate-DOOM icon, power/reboot, category glyphs.

### Wallpaper — root-caused & fixed
- Shipped packs load **directly from Limine modules** (`mode==2`,
  `blit_module_named`) — immune to VFS seed races (the old failure).
- Default order wp5→wp1; on decode failure walks every registered pack;
  boot self-heal retries every 2s. Gradient only when nothing decodes.

### POSIX signals — tier 1
- Syscalls `SYS_SIGNAL(32)/KILL(33)/SIGRETURN(34)`; int80 stub records frame RSP.
- Lazy delivery in `schedule()` post-CR3 switch: handler trampoline injection
  onto user stack (`mov eax,34; int 0x80`) or default-kill teardown
  (exit_code=128+sig, stacks freed, DEAD).
- SIGWINCH delivered by `wm_user_mark_resize`.
- libc `signal()` is real (returns prev handler); userlib adds `kill/raise`;
  Terminal tracks `fg_pid` and **Ctrl+C works** on `run` children.

### termios — tier 1
- Full `<termios.h>` (flags, c_cc, bauds, TCSA*/TC*), per-process stateful
  `tcgetattr/tcsetattr/cfmakeraw/cfget·setspeed/tcflush(real input drain via
  SYS_INPUT DRAIN)/tcdrain/tcsendbreak/tcgetsid`.
- `isatty()` fixed: fds 0–2 now report true (was hardcoded 0).

### Stability fixes
- **User heap**: `SYS_FREE` now unmaps + returns physical pages to the PMM
  (was leaking frames every free/realloc → ImGui apps crashed after churn:
  "Page Fault at 0xFFFFFFFFFFFFFFFF" in deck = exhausted PMM).
- ImGui backend moved off legacy `io.MouseDown/MousePos/Wheel` to the 1.92
  event queue (`AddMousePos/Button/WheelEvent`) + `AddInputCharacter`
  (clicks and typing work); first-click focus no longer swallowed
  (kernel forwards button edges outside content rect too).
- Font baking: oversample 2×2, removed glyph-offset hacks (no more clipped
  bottoms).

### Stability hardening pass (asm / kernel core)
- **IST stacks** for #DF(IST1)/NMI(IST2)#MC(IST3): static 16 KB per-vector
  stacks wired into IDT gates — a fault on an overflowed kernel stack is now
  catchable instead of triple-fault rebooting the machine.
- **Kernel-stack canaries**: every task stack plants `0xC0DEC0DEFEEDFACE` at
  its base; scheduler verifies each tick and halts with task/name dump on smash.
- **Atomic signal pending bits** (`__atomic_fetch_or/and`) vs timer preemption.
- **FPU/SSE context switching implemented**: `cpu_save_fpu/restore` were
  exported but *never called* — every preemption leaked live XMM state into
  the next task (silent corruption for ImGui/DOOM/TinyGL). Scheduler now does
  eager FXSAVE/FXRSTOR per task with a pristine master image for new tasks.
- **SMP AP-parking**: Limine SMP request now registered; every non-BSP
  processor is pointed at a `cli/hlt` park loop before heavy init — APs can
  never race early init again (fixes doubled init lines + KVM boot stalls
  on `-smp >= 2`). Real per-CPU startup replaces this when SMP lands.
- **IDE waits no longer redraw the boot screen**: `draw_boot_log()` was being
  called from inside polling loops (every ~256 iterations) — full-screen
  redraws inside every disk wait made KVM disk IO crawl. Waits are silent now.

- **ROOT CAUSE FOUND & FIXED**: `rsdp_request` was missing
  `__attribute__((used, section(".requests")))` — Limine never saw it,
  so ACPI response was always NULL → cascading null derefs in every
  subsystem that depends on ACPI tables (PCI, GPU, net). One-line fix.
- **Known issue (parked)**: with `-smp >1` under KVM, secondary CPUs race into
  early kernel init (`[FS] init begin` printed twice, then hang). TCG/-smp 1
  unaffected. Fix = park APs until tasking starts.

- **ELF loader hardened**: rejects non-x86_64, bad `e_phentsize`, phnum>1024,
  PT_INTERP (static-only), segments >256 MB or images >512 MB, vaddr<4 MiB,
  wrap/overflow ranges.

### New app
- **EigenDeck** (`graphics/deck`, global_idx 62): Dashboard (live SYS_SYSINFO +
  history plots), Calculator (expression parser + tape), Paint canvas, Plots
  signal lab, Style Lab, About. Registered in launcher + terminal + Limine.

---

## ✅ Persistence infrastructure
- **virtio-blk driver** (`src/drivers/storage/virtio_blk.c`): legacy PCI
  virtio (1AF4:1001), polling mode, BAR0 I/O space, correct offsets per
  spec §4.1.4. Registered as `vblk0` block device.
- **persist.c**: two-slot CRC-protected A/B snapshots of the entire
  `files[]` table (nodes + payloads) written to raw disk LBAs.
- **Dirty tracking**: SYS_FS mutating ops + fd writes mark dirty;
  `persistd` ring-0 task autosaves every 15 s; shutdown/reboot sync too.
- Build.sh attaches `eigen_disk.img` as `-drive if=virtio` on q35.

## ⚠️ Known issues / WIP
| Issue | Owner / note |
|---|---|
| NetSurf link fails (`regerror/_ALIGNED/setlocale` dupes, `filter_parse`) | **user WIP** |
| Taskbar pins not persisted across reboot | needs disk persistence or cfg write |
| `[WP]` serial breadcrumbs compiled in | cosmetic; strip later |
| Legacy `TASKBAR_H` constant still referenced in a few spots | cleanup |
| Terminal has no VT escape parsing yet | blocks TUI ports (see #7) |
| Signals tier-1: no masks/RT-frames/restart; SIGSEGV bypasses handlers | tier-2 |

---

## 🗺️ Roadmap (easy → hard)

### 🟢 Easy (minutes – 1 day)
| # | Task | Unlocks |
|---|---|---|
| ~~1~~ | ✅ **DONE** — `[WP]` serial diagnostics stripped | — |
| 2 | Persist taskbar pins → `cfg/pins.cfg` | pins survive reboot |
| 3 | Runtime app registry (`cfg/apps.cfg` read by cascade menu + terminal) | install-once-run-anywhere |
| 4 | Path-spawn syscall (`SYS_SPAWN` accepts VFS path; reuse `elf.c`) | sideload ELFs w/o Limine modules — prerequisite for epm |
| 5 | `TIOCGWINSZ` ioctl returning Terminal grid size | TUI apps size correctly |
| 6 | Purge legacy `TASKBAR_H` uses | single work-area truth |

### 🟡 Medium (days – ~2 weeks)
| # | Task | Effort | Unlocks |
|---|---|---|---|
| 7 | VT100/xterm emulator pass on Terminal (cursor addressing, SGR, alt-screen, scroll regions) | ~1k LOC | every future TUI port |
| 8 | busybox vi port (~5k LOC) | days | modal editor; validates #7 |
| 9 | epm client + repo format (`manifest.json` + `.epk` over SYS_NET HTTP) + first 5 native packages | ~week | users install apps |
| 10 | LVGL port (flush/read/tick glue) | ~150 LOC glue | modern themed apps |
| 11 | PDCurses eigen backend | ~600 LOC | Rogue-class TUIs as windows |
| 12 | Disk persistence (IDE/AHCI write path; `/home`,`/cfg` backed) | 1–2 wks | kills ramfs-wipe class; epm installs stick |
| 13 | Signals tier‑2 (`rt_sigaction` RT frames, `sigprocmask`, restart) | ~wk | ncurses-era ports |
| 14 | Ring‑3 shell hybrid (taskbar/menus/desktop out of ring 0; kernel keeps compositor) | 1–2 wks | crash-proof desktop |
| 15 | MikMod/libmad → AC97 | small shims | music playback |

### 🔴 Hard (weeks – months)
| # | Task | Effort | Payoff |
|---|---|---|---|
| 16 | `mmap/munmap/brk` over vmm | 1–2 wks | modern-libc territory |
| 17 | `clone/fork` (eager copy → COW) | ~2 wks | proper POSIX process model |
| 18 | `getdents64` + mini-procfs | ~wk | directory iteration, /proc/self |
| 19 | musl phase‑1 (Linux-compat dispatch + ~50 shims, static) | 2–4 wks | busybox/coreutils run |
| 20 | Dynamic linking + ld-musl + Alpine apk repos via epm | +2–3 wks | `epm install htop` literal |
| 21 | VirGL over virtio-gpu | 1–3 mo | accelerated OpenGL |
| 22 | Full ring‑3 compositor (WM leaves ring 0) | months | microkernel-style display server |
| 23 | SMP scheduler (per-CPU runqueues) | months | multicore |

---

## Recommended sequence
1. Batch **#1–6** in one sitting (foundation hygiene).
2. **#7 + #8** — Terminal becomes a real VT; ship a modal editor.
3. **#9 + #12** together — ecosystem plus persistence (epm installs that *stick*).
4. POSIX-depth arc: **#13 → #16 → #17 → #18 → #19**, then **#20** when ready
   to inherit the Alpine ecosystem.
5. Keep **#10 (LVGL)** as the standing side-quest; endgame picks (#21–23)
   by taste: compatibility vs graphics vs architecture purity.

---

## Build quick-reference
```bash
./build.sh build        # full build (currently stops at netsurf link — WIP)
bash -c 'set -e; source ./build.sh help >/dev/null 2>&1 || true; build_iso'
./build.sh run          # QEMU + VNC :1
./build.sh log          # headless serial capture → eigen_qemu.log
```

Tool notes: `tools/pack_icons.py` regenerates the icon blob (needs
`rsvg-convert`; Nordzy + Lucide assets under `/tmp/opencode/`).
