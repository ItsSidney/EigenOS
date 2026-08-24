/* imgui_impl_eigen.cpp — Dear ImGui platform + renderer backend for EigenOS.
 *
 * PATH A: the WM stays in ring 0. This backend turns an EigenOS ring-3
 * window into an ImGui-ready framebuffer + input source. The app owns one
 * EigenOS window (eigen_win_create), maps its content buffer (uint32_t*,
 * row-major, pitch == width), renders ImGui draw commands straight into
 * it, and presents with eigen_win_flush. Input comes from eigen_win_poll.
 *
 * No libstdc++ (compiled -fno-exceptions -fno-rtti). operator new/delete
 * live in imgui_eigen_compat.h / this TU.
 *
 * Compile flags (see build.sh build_imgui):
 *   g++ <freestanding ring-3 flags> -fno-exceptions -fno-rtti
 *        -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
 *        -include imgui_eigen_compat.h -c
 */
#include "imgui_eigen_compat.h"   // acosf shim, new/delete, math  [MUST be first]
#include "imgui.h"
#include "imgui_internal.h"

// EigenOS ring-3 userland API (declared in userlib.h, linked via lib/*.o)
extern "C" {
#include <user/eigen.h>
#include "userlib.h"
#include <stdint.h>
#include <string.h>
}

/* ── app state ────────────────────────────────────────────────────── */
struct EigenIOState {
    int     win_id;
    uint32_t* fb;       // window content buffer (uint32_t*, pitch == W)
    uint32_t fb_w;      // content width  (px)
    uint32_t fb_h;      // content height (px)
    ImGuiIO* io;
    bool    mouse_down[5];
};
static EigenIOState g_E;  // zero-initialized: win_id = -1 after Init

/* forward declarations (Render() calls NewFrame before its definition).
 * Must be extern "C" to match the definitions + header declarations. */
extern "C" void ImGui_ImplEigen_NewFrame(int win_id, uint32_t fbw, uint32_t fbh);

/* ── Memory ───────────────────────────────────────────────────────── */
/* Routed to EigenOS kmalloc (EIGEN_SYS_ALLOC). -fno-exceptions means
 * no libstdc++; these are the only alloc symbols ImGui needs.
 * The sized-dealloc variants are required by C++14; we discard the
 * size arg (eigen_free doesn't need it). */
void* operator new(unsigned long sz) {
    if (sz == 0) sz = 1;
    return eigen_malloc(sz);
}
void  operator delete(void* p)            { eigen_free(p); }
void  operator delete(void* p, unsigned long) { (void)p; }
void* operator new[](unsigned long sz)    { if (sz==0) sz=1; return eigen_malloc(sz); }
void  operator delete[](void* p)          { eigen_free(p); }
void  operator delete[](void* p, unsigned long) { (void)p; }

/* Gap 3 stubs: EigenOS _start does not run .init_array / global ctors.
 * ImGui's demo + internals reference these C++ ABI symbols; provide no-op
 * definitions so the archive links without libstdc++. */
extern "C" int __cxa_atexit(void* fn, void* arg, void* dso) {
    (void)fn; (void)arg; (void)dso;
    return 0;  /* success — register nothing */
}
extern "C" void* __dso_handle = (void*)0;

/* ── Renderer ─────────────────────────────────────────────────────── */
/* Software triangle/rect blit into the uint32_t framebuffer.
 * We reuse EigenOS' own blend math (see userui.c blend_px): src over dst. */
static inline void blend_pixel(uint32_t* dst, uint32_t rgb, uint8_t a) {
    if (a <= 0) return;
    if (a >= 255) { *dst = rgb; return; }
    uint32_t old = *dst;
    int sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    int dr = (old >> 16) & 0xFF, dg = (old >> 8) & 0xFF, db = old & 0xFF;
    int r = (sr * a + dr * (255 - a)) / 255;
    int g = (sg * a + dg * (255 - a)) / 255;
    int b = (sb * a + db * (255 - a)) / 255;
    *dst = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Textured quad: sample the ImFont atlas (or any atlas you attach).
 * UVs are [0,1]; we clamp and nearest-sample (pixel-perfect glyphs).
 * A future improvement: bilinear to match FreeType smoothness, but
 * nearest keeps the glyph edges crisp and matches your 8x16 aesthetic. */
static const ImTextureID TEX_FONT_ATLAS = (ImTextureID)1;

static void render_textured_tri(uint32_t* fb, uint32_t fbw, uint32_t fbh,
                                const ImVec2& a, const ImVec2& b, const ImVec2& c,
                                const ImVec2& ta, const ImVec2& tb, const ImVec2& tc,
                                uint32_t ca, uint32_t cb, uint32_t cc,
                                const uint32_t* tex, int texw, int texh,
                                const ImVec4& clip) {
    /* Barycentric via signed area (NO winding swap) — exactly mirrors
       render_colored_tri so each weight stays paired to its own vertex's
       uv and color (w0<->a<->ta<->ca, w1<->b<->tb<->cb, w2<->c<->tc<->cc).
       The old b<->c swap flipped the sub-triangle-area signs, splatting glyph
       ink texels to the bbox edges (the right/top white strip) and dropping
       the real glyph bodies. */
    float area = (b.x - c.x) * (a.y - c.y) - (a.x - c.x) * (b.y - c.y);
    if (ImFabs(area) < 1e-6f) return;                 // degenerate
    float inv_area = 1.0f / area;

    // bbox clipped to the scissor rect
    float minx = ImMin(ImMin(a.x, b.x), c.x);
    float miny = ImMin(ImMin(a.y, b.y), c.y);
    float maxx = ImMax(ImMax(a.x, b.x), c.x);
    float maxy = ImMax(ImMax(a.y, b.y), c.y);
    int cx0 = (int)ImMax(minx, clip.x), cy0 = (int)ImMax(miny, clip.y);
    int cx1 = (int)ImMin(maxx, clip.z), cy1 = (int)ImMin(maxy, clip.w);
    if (cx0 >= cx1 || cy0 >= cy1) return;
    if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0;
    if (cx1 > (int)fbw) cx1 = (int)fbw; if (cy1 > (int)fbh) cy1 = (int)fbh;

    // unpack per-vertex tint colors (ImGui col = 0xAABBGGRR: byte0=R .. byte3=A)
    int ar0=ca&0xFF, ag0=(ca>>8)&0xFF, ab0=(ca>>16)&0xFF, aa0=(ca>>24)&0xFF;
    int ar1=cb&0xFF, ag1=(cb>>8)&0xFF, ab1=(cb>>16)&0xFF, aa1=(cb>>24)&0xFF;
    int ar2=cc&0xFF, ag2=(cc>>8)&0xFF, ab2=(cc>>16)&0xFF, aa2=(cc>>24)&0xFF;

    for (int py = cy0; py < cy1; py++)
    for (int px = cx0; px < cx1; px++) {
        float px_f = (float)px + 0.5f, py_f = (float)py + 0.5f;
        float w0 = ((b.x - c.x) * (py_f - c.y) - (b.y - c.y) * (px_f - c.x)) * inv_area;
        float w1 = ((c.x - a.x) * (py_f - a.y) - (c.y - a.y) * (px_f - a.x)) * inv_area;
        float w2 = 1.0f - w0 - w1;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;   // strict inside (CCW)

        float u = w0*ta.x + w1*tb.x + w2*tc.x;
        float v = w0*ta.y + w1*tb.y + w2*tc.y;
        int tx = (int)(u * (texw - 1)), ty = (int)(v * (texh - 1));
        if (tx < 0) tx = 0; if (tx >= texw) tx = texw - 1;
        if (ty < 0) ty = 0; if (ty >= texh) ty = texh - 1;
        uint32_t tcol = tex[(uint32_t)ty * texw + tx];
        uint8_t ta_a = (uint8_t)((tcol >> 24) & 0xFF);
        if (ta_a == 0) continue;                            // transparent atlas texel (glyph bg)

        uint8_t A  = (uint8_t)(aa0*w0 + aa1*w1 + aa2*w2 + 0.5f);
        if (A == 0) continue;
        uint8_t final_a = (uint8_t)((A * ta_a) / 255);
        if (final_a == 0) continue;
        uint8_t R = (uint8_t)(ar0*w0 + ar1*w1 + ar2*w2 + 0.5f);
        uint8_t G = (uint8_t)(ag0*w0 + ag1*w1 + ag2*w2 + 0.5f);
        uint8_t B = (uint8_t)(ab0*w0 + ab1*w1 + ab2*w2 + 0.5f);
        /* Atlas texels are white (glyphs) or the reserved white pixel, so the
           output color is the interpolated VERTEX tint (text color / window
           color) — NOT a hardcoded white. This is what stops every window
           background from rendering white. */
        blend_pixel(&fb[(uint32_t)py * fbw + (uint32_t)px],
                    ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B, final_a);
    }
}

/* Per-vertex color interpolation across a triangle. ImGui packs
 * ImDrawVert.col as 0xAABBGGRR (uint32_t). We barycentrically blend the
 * three vertex colors per pixel — this is how buttons, frames, borders,
 * and every non-text widget shade get their gradients. */
static void render_colored_tri(uint32_t* fb, uint32_t fbw, uint32_t fbh,
                               const ImVec2& a, const ImVec2& b, const ImVec2& c,
                               uint32_t ca, uint32_t cb, uint32_t cc,
                               const ImVec4& clip) {
    int cx0 = (int)ImMax(ImMin(ImMin(a.x,b.x),c.x), clip.x);
    int cy0 = (int)ImMax(ImMin(ImMin(a.y,b.y),c.y), clip.y);
    int cx1 = (int)ImMin(ImMax(ImMax(a.x,b.x),c.x), clip.z);
    int cy1 = (int)ImMin(ImMax(ImMax(a.y,b.y),c.y), clip.w);
    if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0;
    if (cx1 > (int)fbw) cx1 = fbw; if (cy1 > (int)fbh) cy1 = fbh;
    if (cx0 >= cx1 || cy0 >= cy1) return;

    float area = (b.x-c.x)*(a.y-c.y)-(a.x-c.x)*(b.y-c.y);
    if (ImFabs(area) < 1e-6f) return;
    float inv_area = 1.0f/area;

    // unpack vertex colors. ImGui packs ImDrawVert.col as 0xAABBGGRR.
    // blend_pixel expects 0x00RRGGBB, so R=bits0-7, G=bits8-15, B=bits16-23.
    int ar0=ca&0xFF, ag0=(ca>>8)&0xFF, ab0=(ca>>16)&0xFF, aa0=(ca>>24)&0xFF;
    int ar1=cb&0xFF, ag1=(cb>>8)&0xFF, ab1=(cb>>16)&0xFF, aa1=(cb>>24)&0xFF;
    int ar2=cc&0xFF, ag2=(cc>>8)&0xFF, ab2=(cc>>16)&0xFF, aa2=(cc>>24)&0xFF;

    for (int py=cy0; py<cy1; py++)
    for (int px=cx0; px<cx1; px++) {
        float pf=(float)px+0.5f, qf=(float)py+0.5f;
        float w0=((b.x-c.x)*(qf-c.y)-(b.y-c.y)*(pf-c.x))*inv_area;
        float w1=((c.x-a.x)*(qf-c.y)-(c.y-a.y)*(pf-c.x))*inv_area;
        float w2=1.0f-w0-w1;
        if (w0<0||w1<0||w2<0) continue;
        uint8_t A = (uint8_t)(aa0*w0 + aa1*w1 + aa2*w2 + 0.5f);
        if (A == 0) continue;
        uint8_t R = (uint8_t)(ar0*w0 + ar1*w1 + ar2*w2 + 0.5f);
        uint8_t G = (uint8_t)(ag0*w0 + ag1*w1 + ag2*w2 + 0.5f);
        uint8_t B = (uint8_t)(ab0*w0 + ab1*w1 + ab2*w2 + 0.5f);
        uint32_t rgb = ((uint32_t)R<<16)|((uint32_t)G<<8)|(uint32_t)B;
        blend_pixel(&fb[(uint32_t)py*fbw+(uint32_t)px], rgb, A);
    }
}

extern "C" void ImGui_ImplEigen_Render(ImDrawData* draw_data,
                            uint32_t* fb, uint32_t fbw, uint32_t fbh,
                            const uint32_t* atlas_tex, int atlas_w, int atlas_h) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplEigen_NewFrame(g_E.win_id, fbw, fbh);

    static int _rdbg = 0;
    if (_rdbg < 2) {
        _rdbg++;
        ImFont* f = GImGui ? GImGui->Font : NULL;
        ImFontBaked* b = f ? f->GetFontBaked(f->LegacySize > 0.0f ? f->LegacySize : 18.0f) : NULL;
        printf("[r-dbg] Font=%p LastBaked=%p GetFontBaked=%p size=%.2f dens=%.2f Glyphs=%d FallbackIdx=%d\n",
               (void*)f, f?(void*)f->LastBaked:0, (void*)b,
               b?b->Size:0, b?b->RasterizerDensity:0, b?b->Glyphs.Size:0,
               b?b->FallbackGlyphIndex:-99);
        if (b) {
            const ImFontGlyph* gh = b->FindGlyph('H');
            printf("[r-dbg] 'H': uv=(%.4f,%.4f,%.4f,%.4f) gxy=(%.1f,%.1f,%.1f,%.1f) adv=%.2f pack=%u vis=%d\n",
                   gh?gh->U0:0, gh?gh->V0:0, gh?gh->U1:0, gh?gh->V1:0,
                   gh?gh->X0:0, gh?gh->Y0:0, gh?gh->X1:0, gh?gh->Y1:0,
                   gh?gh->AdvanceX:0, gh?(unsigned)gh->PackId:0, gh?gh->Visible:0);
            for (int k = 0; k < b->Glyphs.Size && k < 4; k++) {
                const ImFontGlyph& g = b->Glyphs[k];
                printf("[r-dbg] glyph[%d] cp=%u uv=(%.4f,%.4f,%.4f,%.4f) pack=%u\n",
                       k, g.Codepoint, g.U0, g.V0, g.U1, g.V1, g.PackId);
            }
            if (b->FallbackGlyphIndex >= 0 && b->FallbackGlyphIndex < b->Glyphs.Size) {
                const ImFontGlyph& fg = b->Glyphs[b->FallbackGlyphIndex];
                printf("[r-dbg] fallback cp=%u uv=(%.4f,%.4f,%.4f,%.4f) pack=%u\n",
                       fg.Codepoint, fg.U0, fg.V0, fg.U1, fg.V1, fg.PackId);
            }
        }
        if (f && f->OwnerAtlas && f->OwnerAtlas->Builder) {
            printf("[r-dbg] BakedPool=%d:", f->OwnerAtlas->Builder->BakedPool.Size);
            for (int k = 0; k < f->OwnerAtlas->Builder->BakedPool.Size; k++) {
                const ImFontBaked& bk = f->OwnerAtlas->Builder->BakedPool[k];
                printf(" [%d:%.1fpx/%.2f/%d]%s", k, bk.Size, bk.RasterizerDensity, bk.Glyphs.Size, bk.WantDestroy?"!":"");
            }
            printf("\n");
        }
        printf("[r-dbg] FontSize=%.2f LegacySize=%.2f\n", GImGui?GImGui->FontSize:0, f?f->LegacySize:0);
    }

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx = (const ImDrawVert*)cmd_list->VtxBuffer.Data;
        const ImDrawIdx*  idx = cmd_list->IdxBuffer.Data;
        if (!vtx || !idx) continue;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* cmd = &cmd_list->CmdBuffer[cmd_i];
            if (cmd->ElemCount == 0) continue;
            ImVec4 clip = cmd->ClipRect;

            /* In a real backend we'd compare GetTexID() against each registered
             * ImTextureData*. Here we have exactly ONE texture (the font atlas,
             * texid=1), so any non-zero texid is the font atlas. */
            ImU64 tid = cmd->GetTexID();
            if (tid != 0 && atlas_tex) {
                // Every ImGui cmd carries the font-atlas TexID (even colored
                // geometry), so all rendering goes through the textured pass.
                // Tint per-vertex color (window bg, text color, etc.) so dark
                // elements render dark instead of white.
                for (int i = 0; i < cmd->ElemCount; i += 3) {
                    unsigned a = idx[i+0], b = idx[i+1], c = idx[i+2];
                    const ImDrawVert& va = vtx[a], vb = vtx[b], vc = vtx[c];
                    render_textured_tri(fb, fbw, fbh,
                        va.pos, vb.pos, vc.pos,
                        va.uv, vb.uv, vc.uv,
                        va.col, vb.col, vc.col,
                        atlas_tex, atlas_w, atlas_h, clip);
                }
            } else {
                // Colored: no texture. Interpolate per-vertex color
                // (ImDrawVert.col, ABGR-packed by ImGui) across the tri.
                for (int i = 0; i < cmd->ElemCount; i += 3) {
                    unsigned a = idx[i+0], b = idx[i+1], c = idx[i+2];
                    const ImDrawVert& va = vtx[a], vb = vtx[b], vc = vtx[c];
                    render_colored_tri(fb, fbw, fbh,
                                       va.pos, vb.pos, vc.pos,
                                       va.col, vb.col, vc.col, clip);
                }
            }
            idx += cmd->ElemCount;
        }
    }
}

/* ── NewFrame ───────────────────────────────────────────────────── */
extern "C" void ImGui_ImplEigen_NewFrame(int win_id, uint32_t fbw, uint32_t fbh) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)fbw, (float)fbh);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

/* ── Init / Shutdown ────────────────────────────────────────────── */
extern "C" bool ImGui_ImplEigen_Init(int win_id) {
    g_E.win_id = win_id;
    return true;
}

extern "C" void ImGui_ImplEigen_Shutdown(void) {
    g_E.win_id = -1;
    g_E.fb = nullptr;
}

/* ── Input feed (called by the app loop from eigen_win_poll results) */
extern "C" void ImGui_ImplEigen_ProcessEvent(const eigen_ev_t* ev) {
    ImGuiIO& io = ImGui::GetIO();
    switch (ev->type) {
        case EIGEN_EV_MMOVE:
            io.AddMousePosEvent((float)ev->a, (float)ev->b);
            break;
        case EIGEN_EV_MDOWN:
            io.AddMousePosEvent((float)ev->a, (float)ev->b);
            if (ev->c >= 1 && ev->c <= 3) io.AddMouseButtonEvent(ev->c - 1, true);
            break;
        case EIGEN_EV_MUP:
            io.AddMousePosEvent((float)ev->a, (float)ev->b);
            if (ev->c >= 1 && ev->c <= 3) io.AddMouseButtonEvent(ev->c - 1, false);
            break;
        case EIGEN_EV_KEY: {
            int sc = ev->a & 0xFF;
            int down = !(ev->a & 0x100);
            // Map common scancodes -> ImGuiKey. ImGui 1.92 uses io.AddKeyEvent
            // with the new ImGuiKey enum; ImGuiKey_None == invalid.
            ImGuiKey key = ImGuiKey_None;
            if (sc >= 'A' && sc <= 'Z') key = (ImGuiKey)(ImGuiKey_A + (sc - 'A'));
            else if (sc >= '0' && sc <= '9') key = (ImGuiKey)(ImGuiKey_0 + (sc - '0'));
            else if (sc == 79) key = ImGuiKey_Enter;
            else if (sc == 1) 	key = ImGuiKey_Escape;
            else if (sc == 73) 	key = ImGuiKey_Space;
            else if (sc == 75) 	key = ImGuiKey_Backspace;
            if (sc == 128 && down) io.AddMouseWheelEvent(0, -1.0f);
            if (sc == 129 && down) io.AddMouseWheelEvent(0, +1.0f);
            if (key != ImGuiKey_None)
                io.AddKeyEvent(key, down);
            if (down && sc >= 32 && sc < 127)
                io.AddInputCharacter((ImWchar)sc);
            break;
        }
        default:
            break;
    }
}
