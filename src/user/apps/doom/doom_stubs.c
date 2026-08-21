/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/

/* doom_stubs.c — DOOM-specific platform stubs for the Eigen port.
 * These were originally bundled with the shared ring-3 libc; they
 * belong to the game, not the libc. They replace bits that normally
 * live in the engine's i_sound.c / i_system.c / i_joystick.c /
 * i_endoom.c, which the Eigen build excludes. */
#include "i_sound.h"

void I_Tactile(int on, int off, int total) { (void)on;(void)off;(void)total; }
int  I_GetMemoryValue(int offset, void* value, int size) { (void)offset;(void)value;(void)size; return 0; }
int snd_MusicDevice = 0;
int snd_SfxDevice  = 0;
int snd_DesiredMusicDevice = 0;
int snd_DesiredSfxDevice  = 0;
/* Some engine files reference the lowercase form. */
int snd_musicdevice = 0;
int snd_sfxdevice  = 0;
/* Joystick / endoom stubs (normally in i_joystick.c / i_endoom.c). */
void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}
void I_Endoom(byte* data) { (void)data; }
/* mkdir used by m_misc save-dir code (no FS in ring-3). */
int mkdir(const char* path, ...) { (void)path; return -1; }