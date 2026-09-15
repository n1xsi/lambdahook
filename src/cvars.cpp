#include "include/cvars.h"
#include "include/sdk.h"
#include "include/globals.h"

DECL_CVAR(bhop);
DECL_CVAR(autostrafe);
DECL_CVAR(aimbot);
DECL_CVAR(aimbot_fov);
DECL_CVAR(autoshoot);
DECL_CVAR(esp);
DECL_CVAR(esp_enemy_only);
DECL_CVAR(chams);
DECL_CVAR(crosshair);
DECL_CVAR(tracers);
DECL_CVAR(norecoil);
DECL_CVAR(clmove);
DECL_CVAR(fov_circle);

bool cvars_init(void) {
    REGISTER_CVAR(bhop, 0);
    REGISTER_CVAR(autostrafe, 0);
    REGISTER_CVAR(aimbot, 0);
    REGISTER_CVAR(aimbot_fov, 10);
    REGISTER_CVAR(autoshoot, 0);
    REGISTER_CVAR(esp, 3);
    REGISTER_CVAR(esp_enemy_only, 0);
    REGISTER_CVAR(chams, 0);
    REGISTER_CVAR(crosshair, 0);
    REGISTER_CVAR(tracers, 0);
    REGISTER_CVAR(norecoil, 0);
    REGISTER_CVAR(clmove, 0);
    REGISTER_CVAR(fov_circle, 0);

    return true;
}
