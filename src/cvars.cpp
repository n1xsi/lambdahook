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
DECL_CVAR(chams_enemy_vis_r);
DECL_CVAR(chams_enemy_vis_g);
DECL_CVAR(chams_enemy_vis_b);
DECL_CVAR(chams_enemy_invis_r);
DECL_CVAR(chams_enemy_invis_g);
DECL_CVAR(chams_enemy_invis_b);
DECL_CVAR(chams_friend_vis_r);
DECL_CVAR(chams_friend_vis_g);
DECL_CVAR(chams_friend_vis_b);
DECL_CVAR(chams_friend_invis_r);
DECL_CVAR(chams_friend_invis_g);
DECL_CVAR(chams_friend_invis_b);
DECL_CVAR(chams_hands_r);
DECL_CVAR(chams_hands_g);
DECL_CVAR(chams_hands_b);
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
    /* Chams colors: 0-255 RGB. Defaults: enemy vis=green, enemy invis=red,
     * friend vis=cyan, friend invis=blue, hands=pink */
    REGISTER_CVAR(chams_enemy_vis_r, 100);
    REGISTER_CVAR(chams_enemy_vis_g, 185);
    REGISTER_CVAR(chams_enemy_vis_b, 105);
    REGISTER_CVAR(chams_enemy_invis_r, 230);
    REGISTER_CVAR(chams_enemy_invis_g, 18);
    REGISTER_CVAR(chams_enemy_invis_b, 70);
    REGISTER_CVAR(chams_friend_vis_r, 40);
    REGISTER_CVAR(chams_friend_vis_g, 180);
    REGISTER_CVAR(chams_friend_vis_b, 245);
    REGISTER_CVAR(chams_friend_invis_r, 25);
    REGISTER_CVAR(chams_friend_invis_g, 50);
    REGISTER_CVAR(chams_friend_invis_b, 180);
    REGISTER_CVAR(chams_hands_r, 240);
    REGISTER_CVAR(chams_hands_g, 170);
    REGISTER_CVAR(chams_hands_b, 240);
    REGISTER_CVAR(crosshair, 0);
    REGISTER_CVAR(tracers, 0);
    REGISTER_CVAR(norecoil, 0);
    REGISTER_CVAR(clmove, 0);
    REGISTER_CVAR(fov_circle, 0);

    return true;
}
