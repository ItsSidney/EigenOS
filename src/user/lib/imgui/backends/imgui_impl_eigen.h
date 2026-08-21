/* imgui_impl_eigen.h — EigenOS backend declarations for Dear ImGui.
 *
 * Header-only declarations; the app calls these from inside its ring-3
 * event loop. See imgui_impl_eigen.cpp for the implementation. */
#ifndef IMGUI_IMPL_EIGEN_H
#define IMGUI_IMPL_EIGEN_H

#include "imgui.h"
#include <user/eigen.h>   // eigen_ev_t, EIGEN_EV_*

#ifdef __cplusplus
extern "C" {
#endif

/* Render an ImGui frame into your mapped window buffer.
 *   fb      — pointer returned by eigen_win_map()
 *   fbw,fbh — content dims from eigen_win_getsize()
 *   atlas   — RGBA8 font atlas pixels (ImDrawData texid == 1)
 *   atlas_w,h — atlas dims in px
 * Call once per frame AFTER ImGui::Render(), BEFORE eigen_win_flush(). */
void  ImGui_ImplEigen_Render(ImDrawData* draw_data,
                              uint32_t* fb, uint32_t fbw, uint32_t fbh,
                              const uint32_t* atlas, int atlas_w, int atlas_h);

/* Frame bookkeeping. Called by Render() itself, but exposed for clarity. */
void  ImGui_ImplEigen_NewFrame(int win_id, uint32_t fbw, uint32_t fbh);

/* Lifecycle. win_id = the int returned by eigen_win_create(). */
bool  ImGui_ImplEigen_Init(int win_id);
void  ImGui_ImplEigen_Shutdown(void);

/* Feed one event from eigen_win_poll() into ImGui's IO. */
void  ImGui_ImplEigen_ProcessEvent(const eigen_ev_t* ev);

#ifdef __cplusplus
}
#endif
#endif /* IMGUI_IMPL_EIGEN_H */
