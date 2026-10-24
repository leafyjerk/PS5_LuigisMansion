/*
 * Dusklight for the PlayStation 5 - SDL3's video driver for the console.
 *
 * The console has one output and no windows: this gives SDL one display and
 * lets it keep its window bookkeeping (size, flags, events) for a window that
 * is the whole screen. Nothing is drawn through SDL; the title's renderer
 * presents through VK_KHR_display on its own.
 *
 * SDL_PS5_DISPLAY=WIDTHxHEIGHT in the environment sets the size (default
 * 1920x1080).
 *
 * SPDX-License-Identifier: Zlib
 */
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_PS5

#include "video/SDL_sysvideo.h"
#include "events/SDL_events_c.h"

static bool PS5_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    int w = 1920, h = 1080;
    const char *size = SDL_getenv("SDL_PS5_DISPLAY");

    if (size && SDL_sscanf(size, "%dx%d", &w, &h) != 2) {
        w = 1920;
        h = 1080;
    }
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = w;
    mode.h = h;
    mode.refresh_rate = 60.0f;
    return SDL_AddBasicVideoDisplay(&mode) != 0;
}

static void PS5_VideoQuit(SDL_VideoDevice *_this)
{
}

static void PS5_PumpEvents(SDL_VideoDevice *_this)
{
}

static bool PS5_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    /* The window is the screen, and always has the pad's focus */
    SDL_SetKeyboardFocus(window);
    return true;
}

static bool PS5_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window)
{
    return true;
}

static void PS5_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    /* The size stays the display's */
}

static SDL_FullscreenResult PS5_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen)
{
    return SDL_FULLSCREEN_SUCCEEDED;
}

static void PS5_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *PS5_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        return NULL;
    }
    device->VideoInit = PS5_VideoInit;
    device->VideoQuit = PS5_VideoQuit;
    device->PumpEvents = PS5_PumpEvents;
    device->CreateSDLWindow = PS5_CreateWindow;
    device->SetWindowPosition = PS5_SetWindowPosition;
    device->SetWindowSize = PS5_SetWindowSize;
    device->SetWindowFullscreen = PS5_SetWindowFullscreen;
    device->free = PS5_DeleteDevice;
    device->device_caps = VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY;
    return device;
}

VideoBootStrap PS5_bootstrap = {
    "ps5", "PlayStation 5 video driver",
    PS5_CreateDevice,
    NULL, // no ShowMessageBox implementation
    false
};

#endif // SDL_VIDEO_DRIVER_PS5
