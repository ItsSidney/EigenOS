/* On-device Lua 5.1.5 smoke test (windowed + klog checkpoints).
 *
 * Boots a Lua state, opens the standard libs (base/table/string/debug),
 * runs a snippet (recursive fib + string.rep) and verifies the returned
 * values. Output goes both to the kernel log (unbuffered) and to a window.
 */
#include <stdio.h>
#include <string.h>

#include "userlib.h"
#include "user/eigen.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static uint32_t g_win;
static uint32_t* g_fb;
static int g_w, g_h, g_y;

#define C_OK    0x55FF77
#define C_FAIL  0xFF5555
#define C_NORM  0xCCCCCC
#define C_TITLE 0x66CCFF

static void wline(const char* s, uint32_t c) {
    if (g_fb) { eigen_draw_text(g_fb, g_w, g_h, 8, g_y, s, c); g_y += 18; }
}
static void ck(const char* s, uint32_t c) {
    eigen_puts(s);
    wline(s, c);
    if (g_fb) eigen_win_flush(g_win);
}

int
main(void)
{
   ck("[LUA] main entered", C_TITLE);

   g_win = eigen_win_create(140, 110, 520, 340, "Lua 5.1 Test");
   if (g_win < 0) { ck("[LUA] win_create failed", C_FAIL); return 1; }
   g_fb = (uint32_t*)eigen_win_map(g_win);
   if (!g_fb) { ck("[LUA] win_map failed", C_FAIL); return 1; }
   eigen_win_getsize(g_win, (uint32_t*)&g_w, (uint32_t*)&g_h);
   eigen_draw_fillrect(g_fb, g_w, g_h, 0, 0, g_w, g_h, 0x0C0C0C);
   g_y = 10;
   ck("[LUA] Lua 5.1.5 smoke test", C_TITLE);

   lua_State *L = luaL_newstate();
   if (!L) { ck("[LUA] FAIL: luaL_newstate returned NULL", C_FAIL); goto wait; }
   ck("[LUA] luaL_newstate OK", C_NORM);

   luaL_openlibs(L);
   ck("[LUA] luaL_openlibs OK", C_NORM);

   const char *code =
       "local function fib(n) if n < 2 then return n end"
       " return fib(n-1) + fib(n-2) end\n"
       "local r = fib(20)\n"
       "local s = string.rep('ab', 5)\n"
       "return r, s\n";

   int rc = 0;
   if (luaL_dostring(L, code) != 0) {
        const char *err = lua_tostring(L, -1);
        char buf[160];
        snprintf(buf, sizeof buf, "[LUA] FAIL: luaL_dostring: %s", err ? err : "(null)");
        ck(buf, C_FAIL);
        rc = 1;
   } else {
        char buf[160];
        int n = lua_gettop(L);
        snprintf(buf, sizeof buf, "[LUA] returned %d value(s)", n);
        ck(buf, C_NORM);

        long fib = (n >= 1) ? (long)lua_tointeger(L, 1) : -1;
        const char *s = (n >= 2) ? lua_tostring(L, 2) : "";
        snprintf(buf, sizeof buf, "[LUA] fib(20) = %ld (expect 6765)", fib);
        ck(buf, (fib == 6765) ? C_OK : C_FAIL);
        if (fib != 6765) rc = 1;

        snprintf(buf, sizeof buf, "[LUA] string.rep = '%s' (expect ababababab)",
                 s ? s : "(null)");
        ck(buf, (s && strcmp(s, "ababababab") == 0) ? C_OK : C_FAIL);
        if (!s || strcmp(s, "ababababab") != 0) rc = 1;
        lua_pop(L, n);
   }

   lua_close(L);
   ck("[LUA] lua_close OK", C_NORM);
   if (rc == 0) ck("[LUA] ALL PASS", C_OK);
   else ck("[LUA] FAILURES PRESENT", C_FAIL);

wait:
   for (;;) {
        eigen_ev_t evs[8];
        int n = eigen_win_poll(g_win, evs, 8);
        int closed = 0;
        for (int i = 0; i < n; i++)
            if (evs[i].type == EIGEN_EV_CLOSE) closed = 1;
        if (closed) break;
        eigen_sleep_ms(50);
   }
   return rc;
}
