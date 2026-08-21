/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/*********************************************************************
 * imguitest — Hello ImGui on EigenOS.
 *
 * A minimal Dear ImGui app in a ring-3 EigenOS window:
 *   - Creates a normal EigenOS window (eigen_win_create).
 *   - Boots ImGui (CreateContext + the Eigen software backend in
 *     imgui_impl_eigen.cpp).
 *   - Uses ImGui's CANONICAL DEFAULT DARK THEME:
 *       ImGui::StyleColorsDark(), the look from the upstream repo
 *       you referenced (https://github.com/ocornut/imgui). No
 *       palette hijacking — the raw ImGui dark style ships as-is.
 *   - Bakes the font atlas from the DejaVuSans boot module via the
 *     bundled FreeType, so text is crisp at any glyph size.
 *   - Shows the ImGui demo window + a small themed panel, proving
 *     buttons, sliders, color editing, and rendering all work
 *     through the Eigen software rasterizer (into the uint32_t*
 *     window buffer).
 *
 * Build: see build.conf — links libimgui.a + imgui_impl_eigen.o +
 * your ring-3 libc/userui/freetype.
 *********************************************************************/

#include "imgui_eigen_compat.h"     // acosf shim + math  [MUST be first]

/* EigenOS' ring-3 libc headers (userlib.h, eigen.h, stdio.h, stdlib.h,
 * string.h) are C-only — they have no extern "C" guards. Without this
 * wrapper a .cpp file mangles printf/malloc/eigen_* as C++ symbols and
 * they won't link against your C-compiled libc.o (which exports
 * unmangled C names). This block MUST come BEFORE imgui.h, because
 * imgui.h pulls in <stdlib.h>/<string.h> which would otherwise claim
 * those symbols are C++-linked, causing "conflicting linkage" errors. */
extern "C" {
#include <user/eigen.h>
#include "userlib.h"
#include <stdint.h>
#include <stdio.h>    // eigen_* printf/scanf route to kernel log
#include <stdlib.h>   // malloc/free (C ABI)
#include <string.h>   // memcpy/memset/strlen (C ABI)
}

#include "imgui.h"
#include "imgui_internal.h"   // [DBG] ImGuiDebugLogFlags_EventFont (g.DebugLogFlags)
#include "imgui_impl_eigen.h"

#define WIN_X  120
#define WIN_Y  120
#define WIN_W  640
#define WIN_H  420
#define MAX_EVS 32
#define FONT_PIXELS 18

/* Converted font atlas (RGBA8 → uint32_t ARGB for the Eigen renderer). */
static uint32_t* g_atlas_px = NULL;
static int       g_atlas_w  = 0;
static int       g_atlas_h  = 0;

/* ── main ─────────────────────────────────────────────────────── */
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("[imguitest] booting ImGui -> EigenOS ring-3 window\n");

    /* 1: ImGui context + the CANONICAL DEFAULT DARK theme (your ask).
     *    Not a custom palette — ImGui's own StyleColorsDark(), the look
     *    straight off https://github.com/ocornut/imgui. */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // FontGlobalScale is gone in ImGui 1.92; scale is managed via the
    // texture UVs now. Leave io at defaults.
    io.IniFilename = NULL;                    // no .ini on this OS
    io.ConfigFlags = ImGuiConfigFlags_None;   // no docking needed here
    ImGui::StyleColorsDark();                 // <-- the upstream default dark
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f); // 1.92 legacy-backend hook
    io.FontGlobalScale = 1.0f;
    /* [DBG] Dump ImGui's built-in "[font]" event log (bake/pack/texture
     * create+update/repack/failures) to the serial console. OutputToTTY is
     * enabled by default; EventFont gates the font-specific events. */
    ImGui::GetCurrentContext()->DebugLogFlags |= ImGuiDebugLogFlags_EventFont;

    /* 2: window + framebuffer. One normal ring-3 window; the WM
     *    composites our buffer like any other app window. */
    int win = eigen_win_create(WIN_X, WIN_Y, WIN_W, WIN_H,
                               "imguitest — ImGui (dark default)");
    if (win < 0) {
        printf("[imguitest] eigen_win_create failed (%d)\n", win);
        return 1;
    }
    if (!ImGui_ImplEigen_Init(win)) {
        printf("[imguitest] backend init failed\n");
        return 1;
    }

    /* 3: font atlas. Load DejaVuSans (Limine module) + let ImGui bake it. */
    long fsize = 0;
    {
        long cap = 2 * 1024 * 1024;
        unsigned char* fdata = (unsigned char*)malloc((size_t)cap);
        if (!fdata) { printf("[imguitest] OOM font buffer\n"); goto done; }
        fsize = eigen_load_module("DejaVuSans", fdata, (uint64_t)cap);
        if (fsize <= 0) {
            printf("[imguitest] 'DejaVuSans' boot module not found\n");
            free(fdata);
            goto done;
        }
        printf("[imguitest] DejaVuSans: %ld bytes\n", fsize);

        ImFontConfig cfg = {};
        cfg.OversampleH = 1;  /* 1:1 display; 3x made the atlas 512x1024 and
                                 likely OOM'd the shared kernel heap mid-bake */
        cfg.OversampleV = 1;
        cfg.GlyphOffset.y = 2.0f;
        cfg.SizePixels = (float)FONT_PIXELS;
        cfg.RasterizerDensity = 1.0f;
        cfg.FontDataOwnedByAtlas = false;  /* atlas keeps our pointer, never copies */
        ImFont* font = io.Fonts->AddFontFromMemoryTTF(fdata, (int)fsize,
                                                      (float)FONT_PIXELS, &cfg);
        if (font) { font->LegacySize = (float)FONT_PIXELS; font->CurrentRasterizerDensity = 1.0f; }
        /* NOTE: must NOT free fdata here — ImGui 1.92 AddFontFromMemoryTTF keeps
         * a pointer to the buffer (used by stb_truetype for the whole atlas
         * lifetime). We keep it alive for the duration of the app. */
        (void)font;

        /* Bake the atlas with stb_truetype (compiled into imgui_draw.cpp). */
        unsigned char* pixels = NULL;
        int pw = 0, ph = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);
        if (pixels && pw && ph) {
            printf("[imguitest] ImGui baked atlas %dx%d\n", pw, ph);
            /* Convert RGBA8 bytes → uint32_t ARGB for the Eigen renderer
             * (which reads tex[ty*texw+tx] with alpha in bits 24-31). */
            g_atlas_w = pw; g_atlas_h = ph;
            g_atlas_px = (uint32_t*)malloc((size_t)pw * ph * sizeof(uint32_t));
            if (g_atlas_px) {
                for (int i = 0; i < pw * ph; i++) {
                    uint8_t r = pixels[i*4 + 0];
                    uint8_t g = pixels[i*4 + 1];
                    uint8_t b = pixels[i*4 + 2];
                    uint8_t a = pixels[i*4 + 3];
                    g_atlas_px[i] = ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
                }
            }
            /* Register as texture id 1 (TEX_FONT_ATLAS); the renderer
             * samples ImTextureID(1) for all textured draw commands. */
            io.Fonts->SetTexID((void*)1);
        } else {
            printf("[imguitest] font atlas bake failed\n");
        }
    }

    /* 4: main loop. Matches the calculator/ftglyph pattern: poll ->
     *    draw -> flush -> sleep 16ms. */
    eigen_ev_t evs[MAX_EVS];
    static float val = 0.5f;
    static float col[3] = { 0.20f, 0.40f, 0.80f };  // ImGui default-accent-ish
    static bool show_demo = true;
    static int   counter = 0;

    printf("[imguitest] entering render loop (WASD/arrows+click to play)\n");
    for (;;) {
        /* input */
        int n = eigen_win_poll(win, evs, MAX_EVS);
        for (int i = 0; i < n; i++) {
            if (evs[i].type == EIGEN_EV_CLOSE) goto done;
            ImGui_ImplEigen_ProcessEvent(&evs[i]);
        }

        /* resize handle */
        uint32_t W = WIN_W, H = WIN_H;
        eigen_win_getsize(win, &W, &H);
        uint32_t* buf = (uint32_t*)eigen_win_map(win);
        if (!buf) { eigen_sleep_ms(16); continue; }

        /* ImGui frame */
        ImGui_ImplEigen_NewFrame(win, W, H);
        ImGui::NewFrame();

        /* the demo window — proves everything renders */
        if (show_demo) {
            ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(560, 320), ImGuiCond_Always);
            ImGui::ShowDemoWindow(&show_demo);
        }

        /* a small themed panel: button + slider + color edit + counter */
        ImGui::SetNextWindowPos(ImVec2(40, 60), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_Always);
        ImGui::Begin("imguitest panel", &show_demo);   // title-bar "x" = quit check
        ImGui::Text("Hello EigenOS — ImGui default dark theme.");
        ImGui::Separator();
        if (ImGui::Button("press me")) counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);
        ImGui::SliderFloat("amplitude", &val, 0.0f, 1.0f);
        ImGui::ColorEdit3("accent (RGB)", col,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf);
        ImGui::End();

        /* render */
        ImGui::Render();
        ImDrawData* dd = ImGui::GetDrawData();

        /* ── per-cmd diagnostics (serial-only, first 3 frames) ── */
        static int dbg_f = 0;
        if (dbg_f < 3) {
            dbg_f++;
            printf("[imgui-dbg] f=%u DD: cmdlists=%d  DisplaySize=(%.0f,%.0f)\n",
                   dbg_f, dd ? dd->CmdListsCount : -1,
                   dd ? dd->DisplaySize.x : 0, dd ? dd->DisplaySize.y : 0);
            if (dd) for (int n = 0; n < dd->CmdListsCount; n++) {
                const ImDrawList* cl = dd->CmdLists[n];
                printf("[imgui-dbg] cl[%d] vtx=%d idx=%d cmds=%d\n",
                       n, cl->VtxBuffer.Size, cl->IdxBuffer.Size, cl->CmdBuffer.Size);
                for (int i = 0; i < cl->CmdBuffer.Size; i++) {
                    const ImDrawCmd* c = &cl->CmdBuffer[i];
                    ImU64 tid = (ImU64)(uintptr_t)c->GetTexID();
                    printf("[imgui-dbg]  cmd[%d.%d] texid=%llu elem=%u clip=(%.0f,%.0f,%.0f,%.0f)\n",
                           n, i, tid, c->ElemCount,
                           c->ClipRect.x, c->ClipRect.y, c->ClipRect.z, c->ClipRect.w);
                }
                const ImDrawVert* vv = cl->VtxBuffer.Data;
                int nsm = cl->VtxBuffer.Size < 24 ? cl->VtxBuffer.Size : 24;
                printf("[imgui-dbg]  vtxsample:");
                for (int k = 0; k < nsm; k++)
                    printf(" pos=(%.1f,%.1f) uv=(%.3f,%.3f) col=%08x",
                           vv[k].pos.x, vv[k].pos.y, vv[k].uv.x, vv[k].uv.y, vv[k].col);
                printf("\n");
            }
        }

        /* clear to the ImGui dark-window-bg color (#1E1E1E → 0x1E1E1E) */
        uint32_t bg = 0x1E1E1E;
        eigen_draw_fillrect(buf, (int)W, (int)H, 0, 0, (int)W, (int)H, bg);

        /* raster ImGui draw-list into our buffer */
        ImGui_ImplEigen_Render(dd, buf, W, H,
                               g_atlas_px, g_atlas_w, g_atlas_h);

        eigen_win_flush(win);

        /* ── diagnostics (serial-only, first 8 frames) ── */
        static int dbg_frame = 0;
        if (dbg_frame < 8) {
            dbg_frame++;
            unsigned ntri = 0, ntxt = 0, ncol = 0, ncmds = 0;
            for (int n = 0; n < dd->CmdListsCount; n++) {
                const ImDrawList* cl = dd->CmdLists[n];
                ncmds += cl->CmdBuffer.Size;
                for (int i = 0; i < cl->CmdBuffer.Size; i++) {
                    const ImDrawCmd* c = &cl->CmdBuffer[i];
                    ntri += c->ElemCount / 3;
                    if ((ImU64)c->GetTexID() != 0) ntxt++; else ncol++;
                }
            }
            /* count non-background pixels */
            unsigned nbg = 0;
            if (buf) for (unsigned y = 0; y < H; y++)
                for (unsigned x = 0; x < W; x++)
                    if (buf[y*W + x] != bg) nbg++;
            if (dbg_frame == 1) {
                unsigned ink = 0, wtex = 0;
                if (g_atlas_px && g_atlas_w>0 && g_atlas_h>0) {
                    for (int i = 0; i < g_atlas_w*g_atlas_h; i++) {
                        uint8_t a = (g_atlas_px[i]>>24)&0xff;
                        uint8_t r=(g_atlas_px[i]>>16)&0xff, g=(g_atlas_px[i]>>8)&0xff, b=g_atlas_px[i]&0xff;
                        if (a > 0) { ink++; if (r==255&&g==255&&b==255) wtex++; }
                    }
                }
                ImFont* f0 = io.Fonts->Fonts.Size ? io.Fonts->Fonts[0] : NULL;
                printf("[imgui-dbg] io.FontGlobalScale=%.2f io.DisplayFramebufferScale=(%.2f,%.2f) FontSizeBase=%.2f GetFontSize=%.2f\n",
                       io.FontGlobalScale, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y,
                       ImGui::GetStyle().FontSizeBase, ImGui::GetFontSize());
                printf("[imgui-dbg] font LegacySize=%.2f CurrentRasterizerDensity=%.3f\n",
                       f0 ? f0->LegacySize : -1.0f, f0 ? f0->CurrentRasterizerDensity : -1.0f);
                printf("[imgui-dbg] atlas %dx%d ink_px%d white_texel_px=%d tex0=%08x texmid=%08x white@35,0=%08x\n",
                       g_atlas_w, g_atlas_h, ink, wtex,
                       g_atlas_px? g_atlas_px[0]:0,
                       (g_atlas_px&&(g_atlas_w*g_atlas_h)) ? g_atlas_px[(g_atlas_w*g_atlas_h)/2]:0,
                       g_atlas_px ? g_atlas_px[0*512 + 35] : 0);
                printf("[imgui-dbg] texid check: cl0.0=%llu cl1.0=%llu cl1.1=%llu\n",
                       (unsigned long long)(uintptr_t)dd->CmdLists[0]->CmdBuffer[0].GetTexID(),
                       (unsigned long long)(uintptr_t)dd->CmdLists[1]->CmdBuffer[0].GetTexID(),
                       (unsigned long long)(uintptr_t)dd->CmdLists[1]->CmdBuffer[1].GetTexID());
            }
            if (dbg_frame == 2 && buf && W>400 && H>90) {
                /* dense scanline across the panel text row (y~85) and y~98 */
                printf("[imgui-dbg] scanline y=85:");
                for (unsigned x=50; x<336; x+=4) printf(" %08x", buf[85*W + x]);
                printf("\n[imgui-dbg] scanline y=98:");
                for (unsigned x=50; x<336; x+=4) printf(" %08x", buf[98*W + x]);
                printf("\n[imgui-dbg] scanline y=200:");
                for (unsigned x=440; x<630; x+=4) printf(" %08x", buf[200*W + x]);
                printf("  (demo window right edge text?)\n");
            }
            printf("[imgui-dbg] f=%u cmds=%u textured=%u colored=%u tris=%u nonbg_px=%u\n",
                   dbg_frame, ncmds, ntxt, ncol, ntri, nbg);
            if (buf && W > 12 && H > 10) {
                printf("[imgui-dbg] grid:");
                const unsigned gx[8] = {10, W/8, W/4, W/2, 3*W/4, W-12, W-4, W-2};
                const unsigned gy[6] = {10, H/6, H/3, H/2, 2*H/3, H-10};
                for (unsigned r = 0; r < 6; r++) {
                    printf(" y%u:", gy[r]);
                    for (unsigned c = 0; c < 8; c++)
                        printf(" %08x", buf[gy[r]*W + gx[c]]);
                }
                printf(" | Redge:");
                const unsigned rr[4] = {10, H/4, H/2, H-12};
                for (unsigned i = 0; i < 4; i++) printf(" %08x", buf[rr[i]*W + (W-2)]);
                printf("\n");
            }
        }

        eigen_win_flush(win);
        eigen_sleep_ms(16);
    }

done:
    ImGui::DestroyContext();
    if (g_atlas_px) free(g_atlas_px);
    eigen_win_close(win);
    printf("[imguitest] closed\n");
    return 0;
}
