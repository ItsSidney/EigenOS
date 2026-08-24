/***************************************************************/
/*  EigenUI Demo — build with the EigenUI library                */
/*  Compile via the userland build (single-file apps path).      */
/***************************************************************/
#include "eigenui/eigenui.h"
#include <string.h>

static eui_widget* g_prog;
static eui_widget* g_status;

static void on_button(eui_widget* w, void* ud) { (void)w;(void)ud; eui_label_set_text(g_status, "Button clicked! ✓"); }
static void on_slider(eui_widget* w, void* ud) { (void)ud; eui_progress_set(g_prog, eui_slider_get(w)); }
static void on_toggle(eui_widget* w, void* ud) { (void)ud; eui_label_set_text(g_status, eui_toggle_get(w) ? "Toggle: ON" : "Toggle: OFF"); }
static void on_list(eui_widget* w, void* ud) {
    (void)ud; int sel = eui_list_get_sel(w);
    char buf[64]; strcpy(buf, "Selected row #"); buf[15] = '0' + (sel >= 0 ? sel : 0); buf[16] = 0;
    eui_label_set_text(g_status, buf);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    eui_widget* root = eui_box_new(1);          /* vertical */
    eui_box_set_spacing(root, 10);
    eui_box_set_padding(root, 14);

    eui_widget* title = eui_label_new("EigenUI — modern GUI toolkit");
    eui_label_set_align(title, 1);
    eui_box_add(root, title);

    /* button + toggle row */
    eui_widget* row = eui_box_new(0);            /* horizontal */
    eui_box_set_spacing(row, 8);
    eui_widget* b = eui_button_new("Click me");
    eui_button_set_icon(b, EUI_ICON_STARFILL);
    eui_widget_on_click(b, on_button, 0);
    eui_box_add(row, b);
    eui_widget* tg = eui_toggle_new(0, "Enable");
    eui_widget_on_change(tg, on_toggle, 0);
    eui_box_add(row, tg);
    eui_box_add(root, row);

    /* slider + progress */
    eui_widget* sl = eui_slider_new(0, 100, 40);
    eui_widget_on_change(sl, on_slider, 0);
    eui_box_add(root, sl);
    g_prog = eui_progress_new(100);
    eui_progress_set(g_prog, 40);
    eui_box_add(root, g_prog);

    /* text entry */
    eui_widget* en = eui_entry_new();
    eui_entry_set(en, "Type here...");
    eui_box_add(root, en);

    /* scrollable list */
    eui_widget* scroll = eui_scroll_new(1);     /* vertical scroll */
    eui_widget* list = eui_list_new();
    const char* items[] = { "Documents", "Downloads", "Pictures", "Music", "Videos",
                            "Projects", "Notes", "Archives", "Backups", "Trash",
                            "Reports", "Source" };
    for (int i = 0; i < 12; i++) eui_list_add(list, items[i]);
    eui_widget_on_change(list, on_list, 0);
    eui_scroll_set_content(scroll, list);
    eui_box_add(root, scroll);

    /* icons / custom shapes */
    eui_widget* icons = eui_box_new(0);
    eui_box_set_spacing(icons, 8);
    eui_box_add(icons, eui_icon_new(EUI_ICON_HOME, 0));
    eui_box_add(icons, eui_icon_new(EUI_ICON_TRIANGLE, 0));
    eui_box_add(icons, eui_icon_new(EUI_ICON_STARFILL, 0));
    eui_box_add(icons, eui_icon_new(EUI_ICON_HEART, 0));
    eui_box_add(icons, eui_icon_new(EUI_ICON_SETTINGS, 0));
    eui_box_add(root, icons);

    g_status = eui_label_new("Status: idle");
    eui_box_add(root, g_status);

    eui_app_run(root, "EigenUI Demo", 420, 580);
    return 0;
}
