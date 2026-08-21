/* doom_iwad.c — minimal IWAD resolver for the Eigen DOOM port.
 * Replaces the engine's d_iwad.c (which searches a real filesystem).
 * Our WAD is loaded in-memory from the doom1.wad boot module, so the
 * WAD class opens any name we hand it; these functions just return the
 * requested name and pin the game to shareware DOOM (doom1.wad). */
#include <string.h>
#include "doomtype.h"
#include "d_mode.h"
#include "doomstat.h"
#include "d_iwad.h"
#include "m_argv.h"
#include "i_system.h"
#include "w_wad.h"

/* Returns the name unchanged: the in-memory WAD class resolves it. */
char* D_FindWADByName(char* name) { return name; }
char* D_TryFindWADByName(char* name) { return name; }

char* D_FindIWAD(int mask, GameMission_t* mission) {
    (void)mask;
    *mission = doom;                 /* shareware doom1.wad */
    return "doom1.wad";
}

const iwad_t** D_FindAllIWADs(int mask) { (void)mask; return 0; }
char* D_SaveGameIWADName(GameMission_t gamemission) { (void)gamemission; return "doom1.wad"; }
char* D_SuggestIWADName(GameMission_t mission, GameMode_t mode) { (void)mission;(void)mode; return "doom1.wad"; }
char* D_SuggestGameName(GameMission_t mission, GameMode_t mode) { (void)mission;(void)mode; return "Doom"; }

void D_CheckCorrectIWAD(GameMission_t mission) { (void)mission; }

/* NOTE: D_IdentifyVersion / InitGameVersion live in d_main.c, not d_iwad.c,
   in this engine version, so we don't redefine them here. */

