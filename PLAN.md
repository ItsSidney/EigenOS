# Plan: Dear ImGui in a ring-3 EigenOS app (PATH A)

Goal
----
Port Dear ImGui to EigenOS as a ring-3 APPS, drawn into a normal
EigenOS window (same model as Settings / Calculator). The window
manager stays exactly where it is (ring 0). This is the smallest,
lowest-risk way to get ImGui working, and it leaves the door open to
either of the bigger paths (ImGui in ring 0, or a full ring-3 WM)
later.

Chosen path: PATH A ("ImGui as just another ring-3 app").
- The WM does NOT change.
- A ring-3 app creates a window with eigen_win_create, maps it with
  eigen_win_map (gets a uint32_t* framebuffer), lets ImGui render into
  that buffer, calls eigen_win_flush to present, and drains events via
  eigen_win_poll. Identical to how Settings/Calculator work today, but
  the renderer is ImGui instead of hand-rolled EigenUI calls.
- ImGui + FreeType give crisp, scalable, antialiased DejaVu Sans text.

What is NOT done here (explicit, so we don't creep):
- We do not move the WM to ring 0, do not touch wm.c, do not touch
  draw_taskbar / draw_start_menu, do not change the window flags.
- We do not make ImGui the shell desktop or replace the taskbar.

ABI surface we target (all verified in include/user/userlib.h +
include/user/eigen.h):
  int   eigen_win_create(int x,int y,int w,int h,const char* title);
  void* eigen_win_map(int id);                 // content buffer (uint32_t*)
  int   eigen_win_flush(int id);               // present one frame
  void  eigen_win_close(int id);
  int   eigen_win_poll(int id, eigen_ev_t* out, int max);
  int   eigen_win_getsize(int id, uint32_t* w, uint32_t* h);  // content px
  int   eigen_win_gettheme(uint32_t* out, int max);            // live palette
  long  eigen_load_module(const char* name, void* dest, uint64_t maxbytes);
  int   eigen_time_get(int out[6]);   // {h,m,s,day,month,year}

Window model: the WM composites each ring-3 app's content buffer into
the framebuffer per frame. So an ImGui app is a normal top-level
window — no special compositing needed.

Input model we will consume from eigen_win_poll:
  EIGEN_EV_MMOVE (a=x, b=y), EIGEN_EV_MDOWN (a=button0/1), EIGEN_EV_MUP,
  EIGEN_EV_KEY (a=scancode; bit 0x100 = release), EIGEN_EV_CLOSE,
  EIGEN_EV_RENDER (frame tick).

Build model (from build.sh):
  - UCC = gcc, UCFLAGS = freestanding ring-3 flags.
  - Folder-form apps live in src/user/apps/<cat>/<app>/build.conf with
    NAME / SRCS / INCS / FLAGS. The build system compiles .c and links
    bin/obj/user/lib/*.o + bin/obj/user/libfreetype.a.
  - We add C++ support: a new build_imgui() in build.sh that compiles
    .cpp with g++ under the same freestanding flags + -fno-exceptions
    -fno-rtti -fno-threadsafe-statics -D__IMGUIDROID (we supply our own
    mini-runtime), and links ImGui + FreeType + lib/*.o.

-----------------------------------------------------------------------
STAGE 1 — Vendor Dear ImGui (compile-only target)
-----------------------------------------------------------------------
1.1. Create src/user/lib/imgui/ with the Dear ImGui source tree
     (imgui.cpp, imgui_widgets.cpp, imgui_draw.cpp, imgui_tables.cpp,
     imgui_demo.cpp, imgui.h, imgui_draw.h, imgui_internal.h,
     imconfig.h, backends/imgui_impl_* stubbed).
     - Pull a known-tag release (e.g. v1.92) single-source drop; no git
       dependency at build time (a vendored copy).
     - imconfig.h: leave default; we will override styling at runtime
       in code, not via #defines, so the same tree can theme to match
       EigenOS palettes.
1.2. Add a build hook build_imgui() in build.sh:
     - Compile imgui*.cpp + backend .cpp with g++ + UCFLAGS +
       -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-extern-
       templates -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DISABLE_
       TEST_FOLDERS -DNDEBUG (release). Output into bin/obj/user/imgui/.
     - ar rcs bin/obj/user/libimgui.a from the objects.
1.3. Verification (no OS needed): host-compile under g++ with the same
     flags. Expect: imgui*.o + libimgui.a produced, link cleanly. Host
     link must fail gracefully (no main) — that's fine; we only need the
     compile+archive step to be reproducible. (Later stages give ImGui a
     real backend, so this step is just "is the source importable".)
     - Risk gate: if -fno-exceptions/-fno-rtti breaks a specific imgui
       file, the culprit is named and we patch imconfig or isolate.

-----------------------------------------------------------------------
STAGE 2 — EigenOS rendering backend (software framebuffer) + FreeType font
-----------------------------------------------------------------------
2.1. Create src/user/lib/imgui/backends/imgui_impl_eigen.cpp/h.
     Contract:
       void  ImGui_ImplEigen_Init(int win_id);
       void  ImGui_ImplEigen_NewFrame(int win_id, int fb_w, int fb_h);
       void  ImGui_ImplEigen_Render(ImDrawData* draw_data,
                                   uint32_t* fb, int fb_w, int fb_h);
     - NewFrame: read eigen_win_getsize for content dims; feed
       ImGuiIO DisplaySize / DisplayFramebufferScale (1.0).
     - Render: walk ImDrawCmd -> ImDrawIdx; software-rasterize:
         * solid-triangle fill with edge functions (integer),
         * textured triangle via nearest/affine UV sample,
         * scissor clip via the command's ClipRect (clip to buffer),
         * alpha blend (src over dst; 0xFF opaque short-circuit) using the
           same blend_px math from src/user/lib/userui/userui.c.
       Write directly into the uint32_t* the app got from eigen_win_map.
     - This is ~300-400 LOC. Reuse nothing from ImGui's GL/MSVC backends.
2.2. Font / FreeType:
     - At app startup, get DejaVuSans bytes via eigen_load_module
       ("DejaVuSans") into a heap buffer. FreeType is already linked
       (libfreetype.a). Build an ImGui font atlas:
         ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, size, pxSize,
           nullptr, glyphRanges);
       with GlyphOffset/GlyphExtraSpacing set for EigenOS baseline.
     - ImGui bakes the atlas to an RGBA32 texture (ImTextureID is an
       index into our local texture array, NOT a GL handle). Render
       samples it during textured-triangle rasterization.
     - Glyph range: default + Basic Latin; extend later if needed.
2.3. Platform/IO wiring in the app (not the backend):
     - io.DeltaTime from eigen_time_get (frame-to-frame delta).
     - io.MousePos / io.MouseDown from eigen_win_poll events.
     - io.KeyMap: map your scancodes to ImGuiKey (a-z, arrows, enter,
       space, esc, wheel 128/129 -> MouseWheel).
     - Back button / close: EIGEN_EV_CLOSE -> io.AddInputCharacter, and
       request app exit.
2.4. Verification: host-compile imgui_impl_eigen.cpp with -ffreestanding
     and a tiny stub header that declares the eigen_win_* symbols as
     externs. Expect: compiles, backend object in. No link yet.

-----------------------------------------------------------------------
STAGE 3 — The test app: imguitest
-----------------------------------------------------------------------
3.1. Scaffold src/user/apps/dev/imguitest/{main.cpp, build.conf}.
     main.cpp:
       - eigen_win_create(120, 80, 640, 420, "ImGui — DejaVu");
       - Theme: use ImGui's canonical default dark theme verbatim —
         ImGui::StyleColorsDark(). This is the "link ocornut/imgui default
         theme" you asked for: the fixed dark-gray palette shipped in
         imgui.cpp (WindowBg 0x0E0E0E-ish, ChildBg 0x111111, Border
         0x0E0E0EFF, Button 0x20202AFF, PlotLines 0x636380FF, etc.) —
         NOT EigenOS' palette. EigenOS theme fetch is skipped here so the
         ImGui app reads as stock upstream dark, easy to judge ImGui
         itself. (Later: a themed variant can swap a handful of
         StyleColorsDark slots to EigenOS accent.)
       - per-frame: poll events -> feed IO; ImGui::NewFrame();
         ImGui::Begin("Demo"); draw a few widgets (button, slider,
         checkbox, color button, a collapsible with the CPU graph demo);
         ImGui::End(); ImGui::Render(); ImGui_ImplEigen_Render(...).
       - eigen_win_flush; loop. Exit on EIGEN_EV_CLOSE.
3.2. build.conf:
       NAME=imguitest
       SRCS=main.cpp
       INCS=../../lib/imgui  ../../lib/imgui/backends  ../../../lib/freetype/include
       FLAGS=-fno-exceptions -fno-rtti -fno-threadsafe-statics -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS
     (build_imgui() already produced libimgui.a + the FreeType archive.)
3.3. Add imguitest to build.sh's folder-form scan (build_folders already
     globs build.conf under src/user/apps — so it is picked up
     automatically once build_imgui() has produced the prerequisites).
     - Add a new LIBIMGUIDIR / libimgui.a to the link line inside
       build_folders' LD step (detect .cpp in SRCS -> switch from UCC to
       g++-frontend-equivalent flags; or simpler: route any folder with
       a .cpp SRCS through build_imgui_app($conf) using g++).
3.4. Verification: `bash build.sh` from repo root. Expect:
     - build_imgui() builds libimgui.a (host g++, freestanding flags).
     - build_folders() sees build.conf, compiles main.cpp with g++, links
       bin/obj/user/libimgui.a + libfreetype.a + bin/obj/user/lib/*.o.
     - Output: bin/userapp/imguitest.elf copied to iso_root/user.
     - No symbol from libstdc++ (verify with nm: no __cxa_*, no
       std::string vtable leaks). new/delete resolved by our shim.
   - This is the host-compile gate; we confirm it builds to an ELF before
     any boot. Boot test (QEMU with your iso) is stage 4.

-----------------------------------------------------------------------
STAGE 4 — Runtime verification (QEMU only)
-----------------------------------------------------------------------
4.1. Boot the iso (your existing qemu path). Spawn imguitest from the
     app drawer / terminal. Expect: a 640x420 window titled "ImGui —
     DejaVu", dark theme, live widgets (slider moves, checkbox flips),
     text reading in DejaVu Sans (not the 8x16 font), window closes via
     the title-bar X.
4.2. Acceptance checks:
     - No ring-0 regressions (wm.c untouched; start/menu/graphs work).
     - ImGui text is sharper than eigen_draw_text's 8x16 (FreeType).
     - Theme colors match the live OS palette (resize the window / flip
       dark mode in Settings and confirm ImGui repaints with the theme).
     - Closing the window returns cleanly to the desktop.

-----------------------------------------------------------------------
WHAT "DONE" LOOKS LIKE (this task)
-----------------------------------------------------------------------
- src/user/lib/imgui/ vendored ImGui v1.92.6 source (single pinned version).
- src/user/lib/imgui/backends/imgui_impl_eigen.{cpp,h} — the fb driver.
  ✓ Done: Init/NewFrame/Render/Shutdown + ProcessEvent + blend_pixel.
- src/user/lib/imgui/imgui_eigen_compat.h — acosf shim, extern "C" libc
  wrappers, new/delete decl, __cxa_atexit/__dso_handle stubs.
- src/user/apps/dev/imguitest/imguitest.cpp + build.conf — the shipped
  demo app.
  ✓ Done: StyleColorsDark(), DejaVuSans atlas via FreeType (eigen_load_module),
  ShowDemoWindow + custom button/slider/color-edit pane, event loop.
- build.sh: $GPP/$UCCXXFLAGS, build_userland builds the backend object,
  build_folders handles .cpp via g++, links libimgui.a + libfreetype.a.
- bin/obj/user/libimgui.a (2.8MB) + libfreetype.a produced.
- bin/userapp/imguitest.elf linked — VERIFIED: valid 2.2MB x86-64 ELF,
  ZERO unresolved symbols on a full nm -u check.
- PLAN.md stays as the record. Changelog.md updated.

Out of scope (explicit): the ring-0 WM rewrite, the macOS-style top menu
bar, and any changes to draw_taskbar / wm.c hit testing. Those belong to
PATH B/C; they are deliberately deferred so this stage lands cleanly and
verifies.

-----------------------------------------------------------------------
RISKS / GATES
-----------------------------------------------------------------------
R1. C++ in the ring-3 toolchain: -ffreestanding has no libstdc++. We
    compile ImGui with -fno-exceptions/-fno-rtti/-fno-threadsafe-
    statics and provide operator new/delete shims. If ANY imgui path
    needs std:: (rare; it doesn't by default), we patch imconfig to
    IMGUI_STDLIB_DISABLE or stub the handful of symbols.
R2. Software rasterization of ImGui's texture triangles (font glyphs):
    must be fast enough for 60fps at 640x420 on a single core. Integer
    edge-walk fills are trivially OK here; textured fills only happen
    for glyphs/uv icons (small regions). Gate: time a full-screen clear
    + a glyph-heavy frame; if >16ms we drop to a simpler sampler.
R3. eigen_load_module("DejaVuSans") size limit vs the 8MB module cap
    used by DOOM. DejaVuSans.ttf is ~700KB, fine. Gate: print the byte
    count at startup; bail with a visible error if it fails.
R4. eigen_win_getsize returns CONTENT dims (not outer window). We must
    draw to content_w*content_h, not WIN_W*WIN_H. Gate: assert
    fb_w*fb_h matches the mapped size; mismatch -> clip to min.
R5. The WM composites our buffer once per poll. If ImGui's NewFrame
    timing and the WM's per-frame composite drift, expect input lag.
    Gate: ImGui::GetIO().DeltaTime derived from eigen_time_get delta,
    not from a host timer.

-----------------------------------------------------------------------
COMMAND SUMMARY (to reproduce the plan, stage by stage)
-----------------------------------------------------------------------
# Stage 1 (host only, no OS):
  g++ <freestanding flags> -ffreestanding -fno-exceptions -fno-rtti \
      -c src/user/lib/imgui/imgui.cpp -o bin/obj/user/imgui/imgui.o
  ar rcs bin/obj/user/libimgui.a bin/obj/user/imgui/*.o

# Stage 2 + 3 (host compile gate):
  bash build.sh              # builds kernel + all apps incl. imguitest

# Stage 4 (runtime — QEMU):
  <your existing qemu boot of eigen-x86_64.iso>, spawn imguitest
