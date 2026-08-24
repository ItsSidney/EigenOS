#!/usr/bin/env python3
"""Pack Nordzy icon PNGs into an embeddable C header for EigenOS.

Renders selected SVGs from the Nordzy-dark theme at 48px with rsvg-convert,
concatenates the PNG bytes into one blob, and generates src/gui/nordzy_icons.h
containing the blob plus an entry table keyed by EigenOS application name.
"""
import subprocess, sys, os

THEME = "/tmp/opencode/nz/Nordzy-dark"
OUT = "/home/larping-c/EigenOS/src/gui/nordzy_icons.h"
SIZE = 48

# EigenOS app name -> candidate icon paths (relative to theme root, .svg tried)
MAP = {
    "Calculator":         ["apps/scalable/accessories-calculator"],
    "File Explorer":      ["apps/scalable/system-file-manager"],
    "Text Editor":        ["apps/scalable/accessories-text-editor",
                           "apps/scalable/utilities-text-editor"],
    "DOOM":               ["/tmp/opencode/appicons/doom.png"],
    "Edrowser":           ["apps/scalable/web-browser",
                           "apps/scalable/internet-web-browser",
                           "apps/scalable/firefox",
                           "apps/scalable/org.gnome.Epiphany"],
    "Calendar":           ["apps/scalable/office-calendar"],
    "Process Viewer":     ["apps/scalable/utilities-system-monitor"],
    "Clock":              ["apps/scalable/gnome-clocks",
                           "apps/scalable/alarm-clock"],
    "Terminal":           ["apps/scalable/utilities-terminal",
                           "apps/scalable/Etermutilities-terminal"],
    "Julia":              ["apps/scalable/graphics-viewer-document"],
    "Mandelbrot":         ["apps/scalable/graphics-viewer-document"],
    "Colour Wheel":       ["apps/scalable/preferences-desktop-color"],
    "Image Viewer":       ["apps/scalable/accessories-image-viewer"],
    "Bitmap Maker":       ["apps/scalable/gimp"],
    "Paint Studio":       ["apps/scalable/gimp"],
    "Graphing Calculator":["apps/scalable/accessories-calculator"],
    "GLGears":            ["apps/scalable/preferences-desktop-display"],
    "GL Demos":           ["apps/scalable/preferences-desktop-display"],
    "GLTeapot":           ["apps/scalable/preferences-desktop-display"],
    "Kernel Log":         ["apps/scalable/utilities-log-viewer",
                           "apps/scalable/gnome-logs"],
    "Weather":            ["status/scalable/weather-clear",
                           "status/scalable/weather-few-clouds"],
    "Hex Viewer":         ["apps/scalable/org.gnome.GHex"],
    "On-Screen Keyboard": ["apps/scalable/input-keyboard"],
    "Settings":           ["apps/scalable/org.gnome.Settings",
                           "apps/scalable/gnome-control-center",
                           "apps/scalable/preferences-desktop-default-applications"],
    "File I/O Test":      ["apps/scalable/application-default-icon",
                           "apps/scalable/system-file-manager"],

    # ── special keys (UI chrome, not apps) ──
    "#cat-all":           ["/tmp/opencode/lucide/layout-grid"],
    "#cat-productivity":  ["/tmp/opencode/lucide/briefcase"],
    "#cat-system":        ["/tmp/opencode/lucide/cpu"],
    "#cat-games":         ["/tmp/opencode/lucide/gamepad-2"],
    "#cat-graphics":      ["/tmp/opencode/lucide/palette"],
    "#cat-debug":         ["/tmp/opencode/lucide/bug"],
    "#cat-accessibility": ["/tmp/opencode/lucide/accessibility"],
    "#cat-networking":    ["/tmp/opencode/lucide/globe"],
    "#power":             ["/tmp/opencode/lucide/power",
                           "apps/scalable/system-shutdown",
                           "apps/scalable/xfsm-shutdown"],
    "#reboot":            ["/tmp/opencode/lucide/rotate-cw",
                           "apps/scalable/system-reboot",
                           "apps/scalable/xfsm-reboot"],
    "#pin":               ["/tmp/opencode/lucide/pin"],
    "#unpin":             ["/tmp/opencode/lucide/pin-off"],
    "#plus":              ["/tmp/opencode/lucide/plus"],
    "#close":             ["/tmp/opencode/lucide/x"],
}

def find_svg(cands):
    for c in cands:
        opts = [c] if c.startswith("/") and c.endswith(".png") else                ([c + ".svg"] if c.startswith("/") else [os.path.join(THEME, c + ".svg")])
        for p2 in opts:
            if os.path.isfile(p2):
                return p2
    return None

def render(svg):
    r = subprocess.run(["rsvg-convert", "-w", str(SIZE), "-h", str(SIZE), svg],
                       capture_output=True)
    if r.returncode != 0 or len(r.stdout) < 8:
        return None
    return r.stdout

def main():
    blob = bytearray()
    entries = []   # (appname, off, len)
    missing = []
    for app, cands in MAP.items():
        src = find_svg(cands)
        if not src:
            missing.append(app); continue
        if src.endswith(".png"):
            with open(src, "rb") as f: png = f.read()   # pre-rasterized asset
        else:
            png = render(src)
        if png is None:
            missing.append(app); continue
        entries.append((app, len(blob), len(png)))
        blob += png
    with open(OUT, "w") as f:
        f.write("/* Nordzy icon theme subset (https://github.com/MolassesLover/Nordzy-icon)\n")
        f.write(" * GPL-3.0 — rendered at %dpx, packed by tools/pack_icons.py */\n" % SIZE)
        f.write("#ifndef NORDZY_ICONS_H\n#define NORDZY_ICONS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("typedef struct { const char* name; uint32_t off, len; } nz_icon_t;\n\n")
        f.write("static const unsigned char nz_blob[] = {")
        for i, b in enumerate(blob):
            if i % 20 == 0: f.write("\n    ")
            f.write("%d," % b)
        f.write("\n};\n\n")
        f.write("static const nz_icon_t nz_icons[] = {\n")
        for app, off, ln in entries:
            f.write('    { "%s", %d, %d },\n' % (app, off, ln))
        f.write("};\n\n#endif\n")
    total = sum(ln for _, _, ln in entries)
    print(f"packed {len(entries)} icons, {len(blob)} blob bytes ({total} png)")
    if missing:
        print("missing (procedural fallback):", ", ".join(missing))

if __name__ == "__main__":
    main()
