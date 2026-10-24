/*
 * Luigi's Mansion for the PlayStation 5 - what runs before the game's main.
 *
 * The game's main is its own (lm-recomp/src/recomp/main.cpp), so the console's start-up is a
 * constructor: standard error into klog, the shell's splash dismissed, the log file, and the
 * folder SDL derives its paths from. After PS5_Dusklight's ps5/src/startup.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <SDL3/SDL_stdinc.h>

#include "log.h"
#include "platform.h"

__attribute__((constructor)) static void
ps5_startup(void)
{
   platform_init(PS5_TITLE_NAME);
   log_start("/app0/userdata/log.txt");
   say("log: /app0/userdata/log.txt");
   /* SDL_GetPrefPath is $XDG_DATA_HOME/<org>/<app>/. A title sees its own folder (/app0, which
    * is /data/homebrew/<TITLE_ID>) and nothing else of /data. Set in SDL's own copy of the
    * environment, which is all SDL_getenv reads: the console's libc keeps what setenv sets apart
    * from the `environ` SDL copies. */
   SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME", "/app0/userdata", true);
   SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "HOME", "/app0/userdata", true);
   /* The window SDL reports is the screen, and the game renders at the window's size (its
    * resolution setting "match window"): 4K. Dawn presents to the TV's own mode (VK_KHR_display),
    * scaling if the TV is smaller. */
   SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_PS5_DISPLAY", "3840x2160", false);
}
