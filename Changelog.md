# Changelog

All notable changes to **EigenOS** (formerly BEDI_OS), a bare-metal x86-64
GUI operating system. Written by Sidney.

## 2026 — ImGui Port (PATH A: ring-3 app, WM untouched)
- Vendored Dear ImGui v1.92.6 into `src/user/lib/imgui/` (imgui.cpp, imgui_draw.cpp,
  imgui_widgets.cpp, imgui_tables.cpp, imgui_demo.cpp + stb) — compiles clean with
  ring-3 flags (`-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics`,
  `-mno-mmx`, freestanding, NO libstdc++). Verified `nm`: zero cxa_*/std::* exports.
- `imgui_eigen_compat.h` shims (included via `-include` in build flags):
  (a) acosf/acos via atan2(√(1-x²),x) — your libc has no acos.
  (b) `extern "C"` wrapper on stdio.h/stdlib.h/string.h so C++-mangled calls
  (memcpy/_Z6memcpy, cos/_Z3cosd, printf) resolve against your C-compiled libc.o.
  (c) `__cxa_atexit`/`__dso_handle` no-op stubs so the demo window links without
  libstdc++ (EigenOS `_start` skips `.init_array`).
- Backend: `src/user/lib/imgui/backends/imgui_impl_eigen.cpp` + `.h`:
  platform+renderer dual backend. Renderer = software barycentric triangle fill
  (per-vertex color interp), font atlas textured path, scissor rect, src-over
  alpha blend — into `uint32_t* fb` from `eigen_win_map`. Input maps
  eigen_ev_t (MMOVE→MousePos, MDOWN/MUP→MouseDown[0], KEY→AddKeyEvent,
  MOUSEWHEEL). All public funcs `extern "C"`.
- FIX: your libc headers (math.h, string.h, stdlib.h, assert.h) had no
  `extern "C"` guards — C++ apps mangled printf/memcpy/cos and failed to link.
  Added the guards (same one-line pattern as the wctype.h wchar_t fix).
- FIX: `memchr` declared in string.h but missing from libc.c — removed from
  libc.c (posix.c already provides it) to avoid duplicate-definition.
- Test app: `src/user/apps/dev/imguitest/` — `StyleColorsDark()` (default ImGui
  dark theme, NOT EigenOS-palette), DejaVuSans font baked via FreeType
  (`eigen_load_module("DejaVuSans")` → `FT_New_Memory_Face` per ftglyph.c pattern).
- build.sh integration: added `$GPP`/`$UCCXXFLAGS` (with `-mno-80387 -mno-mmx`
  ring-3 restrictions), backend build step in `build_userland`, C++ folder-app
  compile+link path in `build_folders` (auto-detects `.cpp`).
- VERIFIED: host link-test produced a valid 2.2MB ELF with ZERO unresolved symbols.
- VERIFIED: `bash build.sh build` compiles libimgui.a (5 C++ TUs) + backend + imguitest.elf,
  links with zero errors, and auto-registers `module_path: boot():/user/imguitest.elf`
  in config/limine.conf. ISO artifact: bin/iso_root/user/imguitest.elf (1.7MB).
- BUG FIX: `AddFontFromMemoryTTF(NULL, 0)` assertion crash fixed. Was passing placeholder
  NULL data; now passes real DejaVuSans TTF bytes via `eigen_load_module`. Removed
  `-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS` (disables legacy `GetTexDataAsRGBA32`/`SetTexID`
  which the app needs). App uses ImGui's stb_truetype atlas baking, converts RGBA8→ARGB
  uint32_t for the Eigen renderer. Removed manual FreeType baking (build_font_atlas).
- Registered imguitest in the Start menu (`menu_app_entries[]` + `all_items[]` in
  src/gui/gui.c) as a ring-3 app entry (`ring3_app`, category=Debug, user_elf="imguitest").
- Fixed build.sh line-continuation backslash doubling (`file_dialog \\` → `file_dialog \`)
  that caused a syntax error breaking `build_userland`.
- BUG FIX (empty window): clip-rect y-components were swapped in BOTH
  `render_textured_tri` and `render_colored_tri` (imgui_impl_eigen.cpp).
  ImGui's ClipRect uses (x0,y0,x1,y1) = (xmin,ymin,xmax,ymax) mapped to
  (clip.x, clip.y, clip.z, clip.w), but the code used clip.w for y-min
  and clip.y for y-max → `cy0 >= cy1` → every triangle was clipped away.
  Swapped to `clip.y` for cy0 and `clip.w` for cy1.
- BUG FIX (no text): texid comparison was `GetTexID() == TEX_FONT_ATLAS`
  (which is 1). In ImGui 1.92 `ImTextureID` is `ImU64` (not void*), and
  `SetTexID((void*)1)` stores into TexRef._TexData->TexID which wasn't
  properly initialized by the legacy `GetTexDataAsRGBA32` path → the
  comparison failed → font glyphs fell into the colored-tri branch
  (drawn without texture → invisible). Changed renderer to treat ANY
  non-zero `GetTexID()` as the single font atlas texture (robust for a
  software backend with one texture).
- BUG FIX (R/B color swap): `render_colored_tri` unpacked ImGui vertex
  colors (`0xAABBGGRR`) with R and B swapped — `(ca>>16)&0xFF` gets BLUE
  (bits 16-23), not RED. Caused all colored primitives to render with
  channels reversed → looked wrong. Fixed: R=bits0-7, G=bits8-15, B=bits16-23.
- Font crash fix: replaced `AddFontFromMemoryTTF(NULL, 0, ...)` (assertion:
  font_data_size > 100) with real DejaVuSans TTF bytes via eigen_load_module.
  Removed `-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS` (disables legacy
  GetTexDataAsRGBA32/SetTexID needed by this app). Uses stb_truetype
  (ImGui default), converts RGBA8→uint32t ARGB for the renderer.

- Mac-Style Taskbar
- `src/gui/taskbar_mac.c`: top-only, 26px tall, always visible thin bar
  with "EigenOS" text label + "Apps" dropdown (categories → apps).
  The dropdown is a REAL menu — click "Apps" → see 8 categories → click
  a category → see that category's apps from `menu_app_entries[]`.
  Clicking an app launches it via `launch_func()`.
- The old complex start menu (search bar, notification bell, CPU/RAM
  graphs, clock, desktop switcher, pinned icons) in gui.c is now dead
  code (not called) — replaced by this minimal Apps dropdown.
- Moved `app_item_t` struct + `menu_app_entries[]` to `include/gui/gui.h`
  so they remain accessible to gui.c's app-launching logic.
- Updated `gui_get_work_area()` to reserve the top 26px for windows.

## 2026 — Kernel
- 16-bit boot chain (boot.asm, gdt.asm, print_16) + Limine x86-64 entry
- GDT / IDT / TSS, interrupts, FPU state handling, SSE-safe ISR paths
- Physical memory manager (PMM), virtual memory manager (VMM, paging)
- Task scheduler, kernel tasks, sleep/wait primitives, FPU/SSE reset guards
- Ring 3 userland: ELF loader, syscall table (19 syscalls), userlib
  (file I/O, windows, gfx, input, spawn/wait, beep, fontmap)
- Heap allocator (kheap), freestanding libm (math_stubs.c — correct
  double-precision sin/cos/tan/atan/atan2/asin/acos/exp/log/pow/sqrt/
  sinh/cosh/tanh/fmod/floor/ceil, ~1e-9 to 1e-12 vs glibc)
- New EIGEN_SYS_MODLOAD (19) syscall: copies a named Limine boot module's
  bytes into a user buffer (ring-3 data loading — prerequisite for the
  DOOM/WAD port). userlib wrapper `eigen_load_module(name, dest, max)`.
  Added `src/user/winpixel.c` ring-3 window smoke test (proves the
  user-window 32bpp draw + event path) and a "Window Test" launcher entry.
- BUG FIX (WM): ring-3 (user) windows leaked a clip-rect on the global
  gfx clip stack (render_window returned before popping the window-rect
  clip), which confined the taskbar/start-menu/search to the window rect
  and broke the shell once a user window opened/moved. Now balanced.
- PERF FIX (spawn): create_user_process_elf deep-cloned the whole kernel
  pml4 (recursive copy of every boot-module/framebuffer mapping) with IRQs
  disabled, causing a ~5s mouse/keyboard freeze on ring-3 app launch.
  New vmm_new_user_pml4() builds a minimal address space (kernel half
  shared, user half empty) — O(512) instead of recursive — so launch is
  instant. Benefits all ring-3 apps (incl. the future DOOM port).
- RING-3 VISUAL PARITY: added EIGEN_SYS_GET_THEME (21) syscall that copies
  the live OS theme palette into a user array. userui's ui_sync_theme()
  pulls it so ring-3 apps (EigenFiles, etc.) draw with the SAME colours as
  the shell (theme-aware) instead of a hard-coded off-brand palette.
- RING-3 FLICKER FIX: ring-3 apps wrote their content buffer asynchronously
  while the WM composited it, producing torn/garbage frames. Added a commit
  buffer (user_cbuf): the app draws into user_buf, eigen_win_flush() copies
  user_buf -> user_cbuf, and the WM composites only user_cbuf. The WM now
  always reads a fully-rendered frame, eliminating ring-3 window flicker.

## 2026 — DOOM (ring-3 port)
- Ported **DOOM** (doomgeneric engine, upstream ozkl/doomgeneric) as a
  self-contained ring-3 ELF — `src/user/doom/`. Builds via a dedicated
  `build_doom()` path in `build.sh` (the engine needs its own include
  order + x87 FPU enabled for the trig tables), independent of the per-file
  `src/user` ring-3 ELF rule.
- Engine tree vendored at `src/user/doom/engine/` (upstream `doomgeneric/`).
  Platform files replaced with Eigen glue: `doomgeneric_eigen.c` (DG_* hooks,
  in-memory WAD class, scancode-set-1 -> DOOM key map, I_* stubs),
  `doom_libc.c` (self-contained freestanding libc + DOOM stubs; the kernel's
  userlib only provides syscall wrappers, not a libc), `doom_iwad.c`
  (IWAD resolver — no filesystem, pins shareware doom1.wad).
- WAD loading: `DG_Init` pulls `doom1.wad` from a Limine boot module via the
  EIGEN_SYS_MODLOAD (19) syscall and exposes it through an in-memory
  `wad_file_class_t`, so the engine's `W_OpenFile/W_Read` work with no disk.
  If the WAD module is absent the app prints a clear message and exits (no
  crash). `tools/fetch_doom_wad.sh` copies a user-supplied `doom1.wad` into
  the ISO and registers the boot module line.
- Display: 320x200 internal res upscaled x2 to a 640x400 ring-3 window
  (DOOMGENERIC_RESX/Y), palette-mapped into the window's 0xRRGGBB buffer
  (matches the WM's pixel format). Input via `eigen_win_poll` + the same
  scancode-set-1 codes the WM already enqueues. Timer via
  `eigen_gettime_ms`/`eigen_sleep_ms`. Audio is stubbed (FEATURE_SOUND off).
- BUG FIX (nk_demo / ring-3 heap exhaustion): the nuklear demo (and any
  ring-3 app that realloc/free) closed immediately on open. Root cause was the
  ring-3 user heap in `syscall.c`: a pure bump allocator whose `eigen_free`
  (SYS_FREE, 8) was a no-op. Nuklear's allocator reuses/frees blocks every
  frame, so each frame leaked its previous command-buffer + pool pages into the
  unfreeable 64 MB region until `k_sys_alloc_user` returned -1 (NULL); nuklear
  dereferences that return unchecked, faulting the ring-3 task
  (`idt.c` -> `exit_task`, which frees the window = "closes immediately"). DOOM
  masked this because its loop barely allocates. Replaced the bump allocator
  with a free-list heap: every live allocation is tracked in a block list
  (allocated + free), `k_sys_free_user` finds the exact block by base, marks
  it free, and coalesces neighbours, and `k_sys_alloc_user` first-fits a free
  span (splitting the tail) before bumping. Verified by a host harness that
  reproduces the exact crash with a faithful bump-allocator stub and survives
  3000 frames once a real freeing allocator is used; full build (kernel + all
  ring-3 apps, including nk_demo.elf) passes and the ISO links.
- BUG FIX (DOOM blank window): the WM allocates the ring-3 content buffer
  at (win_w) x (win_h - WM_TITLEBAR_H=32), so a 640x400 window request
  yielded a 640x368 content buffer. The engine's I_FinishUpdate writes a
  full 640x400 frame, overflowing the buffer and producing a blank window.
  Fix: DOOM now owns a fixed 640x400 engine framebuffer (DG_ScreenBuffer),
  requests the window at height 400+32 so the content buffer is exactly
  640x400, and DG_DrawFrame blits frame->content with a fit (immune to the
  titlebar subtraction / any WM clamping). Pixel format is 0x00RRGGBB on
  both sides, matching the WM's composite expectation.
- NOTE: `build.sh`'s kernel link now excludes `bin/obj/doom/` (the DOOM
  objects are ring-3, not kernel) so they don't get pulled into `eigen.bin`.

## 2026 — Graphics & GUI
- Window manager (windows, taskbar, start menu, dock, desktop icons)
- Theme engine (dark/light), wallpapers, screensaver, font rendering
- Software GL: TinyGL port (LGPL-2.1) with GLTeapot, GLCube, GLGears,
  Periodic Table; ZBuffer 5R6G5B rendering with two-sided lighting
- BMP decoding, image viewer, paint studio, bitmap maker

## 2026 — Networking
- e1000 NIC driver, ARP / IP / DHCP / DNS, TCP sockets
- BearSSL TLS (MIT) — real HTTPS in-kernel
- Weather app (Open-Meteo over HTTPS), Edrowser (HTML browser)

## 2026 — Applications
- Productivity: calculator, scientific calculator, calendar, clock,
  text editor, file explorer, open/save dialogs
- System: process viewer, PCI scanner, system info, Bluetooth & ports,
  personalization, taskbar layout studio, animation manager
- Personalization rebuilt: theme-aware (no hard-coded white), 5 sections
  (Wallpaper / Theme / Accent / Dock / Desktop), sliding sidebar indicator,
  continuously-animating live desktop preview (real clock, sweeping sheen,
  breathing window, pulsing dock + taskbar), accent swatches with pulse,
  apply ripples. Added theme_set_accent() + cfg/accent.cfg persistence.
- Taskbar Layout Studio heavily reworked for maneuverability + no overlap:
  control panel is now a fixed-height scroll area with its own scrollbar (so
  all controls + Apply/Save/Random/Reset/Close always stay on-screen), design
  library and pinned-apps list both have visible scrollbars and wheel support;
  fixed a latent bug where tapping the already-active Position button cycled
  the position; preview + pins now re-skin with the live theme instead of
  hard-coded greys.
- Taskbar Layout Studio (follow-up fix): the Action/Save/Random/Reset/Close
  buttons were previously drawn inside the scroll-clipped panel and pushed
  off-screen (also content_h undercounted rows), so they were unreachable.
  Buttons are now a pinned action bar at the window bottom — always visible.
- WM window chrome now follows the active theme's window roles
  (THEME_ROLE_WINDOW_TITLE / _BG / _BORDER / _PRIMARY) instead of a
  hard-coded grey title bar (was 0x1E1E22), eliminating the washed-out
  "white strip" at the top of every window. Benefits all apps.
- Removed the kernel diagnostic phase HUD ("tls: done" and friends) that was
  drawn in the top-right corner of the desktop (gui.c render loop). The
  g_diag_phase variable is kept for serial debug only — no longer painted.
- Chess + Checkers upgraded to the new app standard: chrome palette
  (background, surface, outline, primary/secondary/tertiary text) is now
  resolved from the active theme each frame instead of hard-coded near-white /
  dark values, so both games re-skin with the desktop theme. Removed hard-coded
  white button text (0xF8FAFC). Piece/board semantic colors are unchanged.
- TinyGL demos (GLGears / GLCube / GLTeapot) heavily upgraded to the new app
  standard:
  • Haiku-style top menu bar (Scene / Render / View / Help) with dropdowns:
    - Scene ...... render multiple objects (gears 1/3/6, cubes 1/2/4, teapots 1/2/3)
    - Render ..... wireframe toggle, pause/resume, reset view, fit window
    - View ...... zoom in / out, fullscreen, FPS-counter toggle
    - Help ...... controls overlay
  • Menu chrome resolves from the active theme (THEME_ROLE_* + accent) so the
    bar re-skins instead of hard-coding dark/near-white colors.
  • Resize FIX: each window registers an on_resize callback that resizes the
    ZBuffer target to the new content size, so the 3D scene reallocates and
    rescales to the window instead of staying stagnant.
  • Scroll wheel now zooms in/out (camera dolly, clamped 0.3x–4x) and left-drag
    inside the viewport rotates the view (suppressed over the menu bar). Mouse
    drag-rotate and wheel-zoom were previously non-functional.
- GL Demos (formerly GLCube) rewrite + GL ecosystem polish:
  • Renamed GLCube → "GL Demos". Scene menu is now a shape picker
    (Cube / Sphere / Torus / Cone / Pyramid) rendered one at a time,
    default Cube. All five shapes are multi-colored using a shared
    multi-color palette (coral/amber/teal/blue/magenta/slate) applied
    per latitude-band (sphere), per ring (torus), per segment (cone),
    per face (cube/pyramid) — so every shape reads as a solid colored
    object, not a single flat tone.
  • Registered all three TinyGL demos (GLGears / GL Demos / GLTeapot)
    in the Start-menu search list (all_items[]) so they appear in search.
  • Heavily improved the three GL app icons in app_icons.c: interlocking
    metallic gears (proper teeth, hub, gloss), a glossy shaded isometric
    cube, and a cleaner glossy red teapot — all on the premium dark tile
    with accent outline, matching the rest of the icon engine.
  • Single-GL-context stagnation FIX: TinyGL keeps ONE global GLContext,
    so opening a second GL demo window clobbered the shared context and
    froze the first window's render. Added apps/graphics/gl_single.c/h to
    track the single active GL demo window; opening any GL demo now closes
    any other open one first (gears ⇄ demos ⇄ teapot), so only one renders
    at a time and nothing stagnates. Each demo registers/unregisters via
    gl_demo_set_active on open/close.
  • Help overlay FIX: clicking anywhere now dismisses the controls overlay
    in all three GL demos (previously the click was swallowed by the menu
    hit-test and the overlay could only be cleared by reopening it).
- Games: snake, dvd, piano, checkers and more
- Accessibility: on-screen keyboard, alarm
- Userland samples (src/user) demonstrating ring-3 ELF apps

## 2026 — Tooling
- build.sh (C+ASM → ISO), QEMU run target, Rust module removed
|- Vendored libraries split into standalone repos for licensing:
|  BearSSL (MIT), TinyGL (LGPL-2.1)

### Networking deep-dive read-through (this session)
| Comprehensive read of the full in-kernel network stack:
| - **Data path**: e1000.c (PCI MMIO, RX/TX descriptor rings, interrupt
|   handler `e1000_intr`) -> ether_input() (Ethernet frame demultiplex:
|   ETHERTYPE_IP -> ip_input, ETHERTYPE_ARP -> arp_input) -> ip_input()
|   (IPv4 header parse, protocol demux: TCP/UDP/ICMP) -> tcp_input/udp_input/
|   icmp_input. Outbound: app builds mbuf -> ip_output (checksum, routing via
|   if_netmask/if_gateway) -> arp_resolve (cache + pending queue) ->
|   ether_output -> if_output -> driver TX ring.
| - **ARP** (arp.c): 16-entry cache, 4-slot pending queue with broadcast bypass
|   for bcast/directed-bcast traffic (fixed saturation bug), per-entry static flag.
| - **UDP** (udp.c/udp.h): static 32-slot RX ring buffer with round-robin slot
|   allocation; udp_output() inline-assembles IP+UDP headers into an mbuf cluster.
| - **TCP** (tcp.h): `struct tcp_socket` with BSD-style send/recv buffers,
|   sequence numbers, state machine (LISTEN/SYN_SENT/ESTABLISHED/FIN_WAIT etc.),
|   `tcp_connect`/`tcp_send`/`tcp_recv` API.
| - **ICMP** (icmp.c): Echo request/reply with `icmp_get_echo_reply()` for
|   userspace ping.
| - **DHCP** (dhcp.c): full DORA state machine (DISCOVER/OFFER/REQUEST/ACK),
|   parses subnet mask, router/gateway, DNS server from options; sets if_ip,
|   if_netmask, if_gateway, if_dns on the interface.
| - **DNS** (dns.c): UDP-based resolver with simple cache, builds RFC 1035
|   queries, parses A-record responses.
| - **Socket layer** (socket.c/socket.h): kernel-level socket API
|   (sys_socket, sys_connect, sys_send, sys_recv, sys_socket_close,
|   sys_socket_closed) wrapping TCP. File-descriptor-style handles.
| - **Interface mgmt** (if.c/if.h): `struct ifnet` with `if_output` function
|   pointer per-driver; `if_find_primary()`, `if_find_by_name()`,
|   `if_attach()`, `if_addr()` accessor.
| - **mTLS/BearSSL** (tls.c/tls.h): static single-connection TLS client.
|   br_tls_begin() inits `br_ssl_client_init_full` (all cipher suites,
|   ECDHE-first ordering), `br_x509_minimal_context` with 23 auto-generated
|   trust anchors (ca_roots.h, gens'd from tools/gen_ca_roots.py).
|   CPUID-gated RDRAND entropy seeder with RTC+PIT fallback (avoids #UD
|   on QEMU default CPU). Handshake deferred to first br_sslio_write_all()
|   (lazy drive). X.509 validation time via RTC->days_from_civil() with
|   +719528 epoch offset. The `br_sslio_flush()` after write is critical:
|   br_sslio_write_all only buffers; flush pushes records.
| - **net_selftest.c**: headless TLS selftest task (tls_https_test_task)
|   that reproduces edrowser's HTTPS flow against example.com without GUI,
|   driving all phases (dhcp-wait -> dns -> tcp-connect -> tls handshake ->
|   http-read) with serial markers for crash capture.
| - **Userspace apps**: edrowser.c (the full HTTP browser: URL bar, fetch
|   over TCP or TLS, HTTP parser, HTML tag stripper, search, history,
|   scrollbar, top-bar status), weather.c (Open-Meteo HTTPS API), netdebug.c
|   (netstat-style interface with NIC stats, socket list, ARP/route tables).

## 2026 — Ring-3 app tree (porting framework)
- New categorized ring-3 app tree at `src/user/apps/<category>/` mirroring the
  kernel `src/apps/` categories (productivity, system, graphics, debug,
  accessibility, customization, networking, games). Paves the way for moving
  apps into ring 3 one at a time; each app lives in its own folder so
  multi-file ports (engine + glue) stay self-contained.
- New folder-app build convention: any `src/user/apps/<cat>/<app>/build.conf`
  (NAME / SRCS / INCS / FLAGS) is auto-discovered and built by
  `build_folders()` in build.sh — no edits to build.sh or gui.c needed. The ELF
  is copied to the ISO and registered as a Limine module (idempotent) so it can
  be launched via `spawn <name>` / a Start-menu entry. See `src/user/apps/README.md`.
- Added `src/user/apps/graphics/ring3_template/` — a complete, working
  multi-file reference app (main.c + ui.c/ui.h + build.conf) proving the
  folder convention compiles, links and launches. It re-maps the window buffer
  every frame (resize/maximize safe) and is theme-aware via ui_sync_theme().
- Added `tools/new_folder_app.sh "App Name" [category]` scaffolder that writes
  a ready-to-build folder app (build.conf + main.c + ui.c/ui.h) for categories
  1-8.
## 2026 - Nuklear: resize crash fix + UI modernization
- FIXED (critical): ring-3 windows DISTORTED then CRASHED on resize/maximize.
  Root cause in src/gui/wm.c: wm_user_grow_buffer() reallocated the content
  buffer and called vmm_map_range() to remap the per-slot VMA to the new
  physical pages. vmm_map_range maps into active_pml4(), which during a WM/
  kernel operation is the KERNEL pml4, NOT the owning ring-3 app's pml4 - so the
  remap never reached the app. The app's next eigen_win_map() then aliased to
  stale/garbage pages while eigen_win_getsize() reported the new size, so the
  app drew into the wrong buffer with the new width (bend/noise) and then
  page-faulted (crash). Fix: allocate the FULL 4 MB slot buffer ONCE at window
  creation (USER_WIN_MAX_BUF == USER_WIN_VMA_STRIDE) and map the whole slot;
  wm_user_grow_buffer now ONLY updates the logical content size (clamped to the
  budget) and clears newly-exposed pixels - it never reallocates or remaps. The
  app's buffer pointer stays valid across any resize. Resize-safe end to end.
- Nuklear driver (src/user/lib/nuklear/nuklear_eigen.c) UI modernization:
  - Sliders: modern pill track, accent-filled value portion, taller rounded
    accent thumb (the EigenOS zip cursor) that grows on hover/active.
  - Dropdown (combo): rounded 8px, accent-tinted open state, accent trigger.
  - Buttons: 8px radius to match the OS shell, accent text on hover, accent fill
    on press with white label for legibility (was 4px radius, flat text).
  - Light/Dark theme API: added nk_eigen_set_mode(int dark) /
    nk_eigen_get_mode(void). Switching re-derives the entire widget skin from
    the live OS palette - dark uses OS surface/text roles, light uses a bright
    canvas with light panels and dark ink while keeping the OS accent.
- nk_demo (src/user/apps/nk_demo/nk_demo.c) rewritten to the new standard:
  resize-safe (re-map + re-size every frame, panel rect clamped to the live
  buffer), Dark/Light toggle button + Ctrl+T, grouped cards for
  buttons/sliders/checkboxes+dropdown/text+property+progress/tree/colour
  picker. Builds and links into the ISO.
## 2026 - Nuklear/UI polish + first ring-3 port (Calculator)
- FIXED (visual): rounded widgets rendered as FOUR SEPARATE CORNER CIRCLES
  on press. Root cause in src/user/lib/userui/userui.c: ui_fill_round /
  ui_draw_round composited a rounded rect from two fill rects + four
  eigen_draw_fillcircle calls; when rounding exceeded half the control height
  (short buttons) the central strips vanished and only the four corner arcs
  showed. Rewrote both with a correct per-pixel rounded-rect test (inside if
  within radius r of the nearest corner centre). Fixes buttons, combo, tabs
  and window chrome everywhere (nuklear demo + userui apps).
- Nuklear light theme reworked: clean white panels on a soft grey canvas,
  darker ink, hairline blue-grey borders, and dark-ink (not accent) text on
  button hover so labels stay legible on white. No more washed-out clash.
- Nuklear slider: handle is now a plain RECTANGLE grip (rounding 0) with a
  filled accent track, instead of the tall rounded 'zip' cursor.
- Nuklear buttons: hover text = accent (dark mode) / dim ink (light mode).
- FIRST APP PORTED TO RING 3: Calculator (src/user/apps/productivity/calculator/).
  Folder-form ring-3 app (build.conf + main.c + ui.c) using the userui
  toolkit. Functional 4-col grid (digits, + - * /, =, C, neg, %, .), running
  expression display, keyboard entry, resize-safe. Built + registered as a
  Limine module (spawn calculator). It is the first app to demonstrate the
  ring-3 porting path end to end (after ring3_template proved the convention).
