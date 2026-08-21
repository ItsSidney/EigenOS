# Ring-3 App Tree (src/user/apps)

This is where **user-mode** (ring 3) apps live. The OS is being migrated
here "one app at a time" — kernel-side apps under `src/apps/<category>/`
move into matching folders here as they are ported.

## Layout

    src/user/apps/
      README.md                 (this file)
      <category>/              e.g. productivity, system, graphics,
                               debug, accessibility, customization,
                               networking, games
        <app>/                ONE folder per app (multi-file safe)
          build.conf          declares how to build it
          main.c              entry point (defines main())
          ...                 any other .c / .h the app needs

Single-file apps (`src/user/apps/*.c`, maxdepth 1) are still auto-built by
`build.sh` for backwards compatibility, but **new apps should be folders**.

## build.conf format

Each folder app carries a `build.conf` (key = value, no quotes):

    NAME=my_app          # ELF base name -> bin/iso_root/user/my_app.elf
    SRCS=main.c ui.c     # space-separated sources, relative to the folder
    INCS=. include       # extra -I dirs, relative to the folder (optional)
    FLAGS=-O2            # extra CFLAGS (optional, e.g. -DOOMGENERIC_RESX=640)

`build.sh` auto-discovers every `src/user/apps/**/build.conf`, builds the ELF,
copies it to the ISO, and registers it as a Limine module so it can be
launched with `spawn my_app` (or a Start-menu entry).

## Why a folder per app?

Kernel ports are usually multi-file (engine + glue). A dedicated folder keeps
each app self-contained, gives it a clean `build.conf` (so exclude-lists,
per-app defines and include paths live with the code), and lets a port land
without touching `build.sh` or the menu code.

## Reference: a minimal main()

    int main(void) {
        int id = eigen_win_create(120, 80, 420, 320, "My App");
        if (id < 0) return 1;
        ui_sync_theme();                       // match the shell palette
        uint32_t* buf = eigen_win_map(id);
        ui_t ui = {0};
        while (1) {
            eigen_ev_t evs[16];
            int got = eigen_win_poll(id, evs, 16);
            int W = 420, H = 320; eigen_win_getsize(id, (uint32_t*)&W, (uint32_t*)&H);
            buf = (uint32_t*)eigen_win_map(id);   // re-map (resize-safe)
            ui_begin(&ui, buf, W, H);
            ui_feed(&ui, evs, got);
            /* draw widgets here */
            ui_end(&ui);
            eigen_win_flush(id);
            eigen_sleep_ms(16);
        }
        return 0;
    }

See `graphics/ring3_template/` for a complete, working example.
