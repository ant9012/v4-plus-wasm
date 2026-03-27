#include "RetroEngine.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


#if !RETRO_USE_NETWORKING
float networkPing = 0.0f;
#endif

#if !RETRO_USE_ORIGINAL_CODE

#if RETRO_PLATFORM == RETRO_WIN
#include "Windows.h"
#endif

void parseArguments(int argc, char *argv[])
{
    for (int a = 0; a < argc; ++a) {
        const char *find = "";

        find = strstr(argv[a], "stage=");
        if (find) {
            int b = 0;
            int c = 6;
            while (find[c] && find[c] != ';') Engine.startSceneFolder[b++] = find[c++];
            Engine.startSceneFolder[b] = 0;
        }

        find = strstr(argv[a], "scene=");
        if (find) {
            int b = 0;
            int c = 6;
            while (find[c] && find[c] != ';') Engine.startSceneID[b++] = find[c++];
            Engine.startSceneID[b] = 0;
        }

        find = strstr(argv[a], "console=true");
        if (find) {
            engineDebugMode       = true;
            Engine.devMenu        = true;
            Engine.consoleEnabled = true;
#if RETRO_PLATFORM == RETRO_WIN
            AllocConsole();
            freopen_s((FILE **)stdin, "CONIN$", "w", stdin);
            freopen_s((FILE **)stdout, "CONOUT$", "w", stdout);
            freopen_s((FILE **)stderr, "CONOUT$", "w", stderr);
#endif
        }

        find = strstr(argv[a], "usingCWD=true");
        if (find) {
            usingCWD = true;
        }
    }
}
#endif

#ifdef NXLINK
#include <switch.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <arpa/inet.h>
#include <unistd.h>

static int s_nxlinkSock = -1;

static void initNxLink()
{
    if (R_FAILED(socketInitializeDefault()))
        return;

    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock >= 0)
        printf("printf output now goes to nxlink server\n");
    else
        socketExit();
}
#endif

#ifndef __EMSCRIPTEN__
// ========== NATIVE MAIN (Desktop / Consoles) ==========
int main(int argc, char *argv[])
{
#ifdef NXLINK
    initNxLink();
#endif

#if !RETRO_USE_ORIGINAL_CODE
    parseArguments(argc, argv);
#endif

    SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");
    Engine.Init();
    Engine.Run();

#if !RETRO_USE_ORIGINAL_CODE
    if (Engine.consoleEnabled) {
#if RETRO_PLATFORM == RETRO_WIN
        FreeConsole();
#endif
    }
#endif

#ifdef NXLINK
    socketExit();
#endif

    return 0;
}

#if RETRO_PLATFORM == RETRO_UWP
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return SDL_WinRTRunApp(main, NULL); }
#endif

#else
// ========== EMSCRIPTEN (WebAssembly) ==========
#include <emscripten.h>
#include <stdio.h>

// main() is a no-op on Emscripten.
// The JS wrapper calls RSDK_Configure + RSDK_Initialize instead once the filesystem is ready.
int main(int argc, char *argv[])
{
    printf("=== Emscripten main() bypassed. Waiting for JS wrapper... ===\n");
    fflush(stdout);
    return 0;
}

extern "C" {
    void EMSCRIPTEN_KEEPALIVE RSDK_Configure(int value, int type)
    {
        printf("=== RSDK_Configure(%d, %d) ===\n", value, type);
        fflush(stdout);

        if (type == 1) {
            if (value == 1) {
                Engine.gameDeviceType = RETRO_MOBILE;
                Engine.gamePlatform   = "MOBILE";
            }
            else {
                Engine.gameDeviceType = RETRO_STANDARD;
                Engine.gamePlatform   = "STANDARD";
            }
        }
    }

    void EMSCRIPTEN_KEEPALIVE RSDK_Initialize()
    {
        printf("=== RSDK_Initialize() called by JS! ===\n");
        fflush(stdout);

        // FORCE the engine to look in the current virtual directory for Data.rsdk
        // Since we bypass parseArguments() entirely in WebAssembly
        usingCWD = true; 

        SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");

        printf("=== Calling Engine.Init() ===\n");
        fflush(stdout);
        Engine.Init();

        printf("=== IS ENGINE RUNNING? %d ===\n", Engine.running);

        printf("=== Calling Engine.Run() ===\n");
        fflush(stdout);
        
        // Engine.Run() handles its own requestAnimationFrame binding internally.
        // We do not define emscripten_set_main_loop here!
        Engine.Run(); 

        printf("=== Engine.Run() returned. Main loop is active via browser! ===\n");
        fflush(stdout);
    }
}

#endif // __EMSCRIPTEN__