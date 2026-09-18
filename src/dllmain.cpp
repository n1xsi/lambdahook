#include <stdio.h>
#include <windows.h>

#include "include/main.h"
#include "include/sdk.h"
#include "include/globals.h"
#include "include/cvars.h"
#include "include/hooks.h"
#include "include/util.h"

static bool loaded = false;
static HMODULE g_hModule = NULL;

void load(void) {
    /* Log to file instead of console to avoid stealing hl.exe's window */
    freopen("C:\\lambdahook-log.txt", "w", stdout);
    freopen("C:\\lambdahook-log.txt", "a", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("lambdahook: Injected.\n");
    fflush(stdout);

    /* Initialize globals/interfaces */
    if (!globals_init()) {
        ERR("Error loading globals, aborting");
        self_unload();
        return;
    }

    /* Create cvars for settings */
    if (!cvars_init()) {
        ERR("Error creating cvars, aborting");
        self_unload();
        return;
    }

    /* Hook functions */
    if (!hooks_init()) {
        ERR("Error hooking functions, aborting");
        self_unload();
        return;
    }

    /* DoD-only — no multi-game detection needed */

    i_engine->pfnClientCmd("echo \"lambdahook loaded successfully!\"");

    loaded = true;
}

void unload(void) {
    if (loaded) {
        /* TODO: Remove our cvars */

        globals_restore();
        hooks_restore();
    }

    printf("lambdahook: Unloaded.\n\n");
}

void self_unload(void) {
    if (g_hModule) {
        /* FreeLibraryAndExitThread atomically frees the DLL and exits the
         * thread, avoiding the race condition of FreeLibrary returning into
         * unloaded code. */
        FreeLibraryAndExitThread(g_hModule, 0);
    }
}

/*----------------------------------------------------------------------------*/

static DWORD WINAPI load_thread(LPVOID lpParam) {
    (void)lpParam;
    load();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
    (void)lpReserved;

    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hModule;
            DisableThreadLibraryCalls(hModule);
            CreateThread(NULL, 0, load_thread, NULL, 0, NULL);
            break;

        case DLL_PROCESS_DETACH:
            unload();
            break;
    }

    return TRUE;
}
