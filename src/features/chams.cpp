#include <stdio.h>
#include <windows.h>
#include <gl/gl.h>

#include "../include/util.h"
#include "../include/entityutil.h"
#include "../include/globals.h"
#include "../include/cvars.h"
#include "features.h"

enum chams_settings {
    DISABLED     = 0,
    PLAYER_CHAMS = 1,
    HAND_CHAMS   = 2,
};

static inline float cvar_color(cvar_t* cv) {
    return cv->value / 255.0f;
}

void chams_init(void) {}
void chams_restore(void) {}
void chams_unhook_hw(void) {}

typedef void (__thiscall *RenderFinal_fn)(void* this_ptr);

static void __fastcall noop_hw(void* this_ptr, void* edx) {
    (void)this_ptr; (void)edx;
}

static void render_colored(void* this_ptr, float r, float g, float b) {
    void** vtbl = (void**)i_studiomodelrenderer;
    RenderFinal_fn real_final = (RenderFinal_fn)vtbl[20];
    RenderFinal_fn real_hw    = (RenderFinal_fn)vtbl[21];

    /* Step 1: Replace HW with no-op, call RenderFinal for setup */
    DWORD old_prot;
    VirtualProtect(&vtbl[21], sizeof(void*), PAGE_EXECUTE_READWRITE, &old_prot);
    vtbl[21] = (void*)noop_hw;
    VirtualProtect(&vtbl[21], sizeof(void*), old_prot, &old_prot);

    real_final(this_ptr);

    /* Step 2: Restore HW */
    VirtualProtect(&vtbl[21], sizeof(void*), PAGE_EXECUTE_READWRITE, &old_prot);
    vtbl[21] = (void*)real_hw;
    VirtualProtect(&vtbl[21], sizeof(void*), old_prot, &old_prot);

    /* Step 3: Set material color via glMaterialfv.
     * glColor4f gets overwritten by StudioRenderFinal_Hardware,
     * but glMaterialfv is NOT overwritten — it sticks.
     * With GL_COLOR_MATERIAL disabled, the material properties
     * are the sole source of color in the lighting pipeline.
     * With GL_LIGHTING enabled and GL_TEXTURE_2D disabled,
     * the output = lighting * material = our color. */
    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);

    GLfloat mat_color[] = { r, g, b, 1.0f };
    GLfloat mat_black[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_black);

    /* Step 4: Draw with real HW */
    real_hw(this_ptr);

    glPopAttrib();
}

bool chams(void* this_ptr) {
    const int setting = cv_chams->value == 5.0f ? 7 : (int)cv_chams->value;
    if (setting == DISABLED)
        return false;

    cl_entity_t* ent = i_enginestudio->GetCurrentEntity();

    if (ent->index == localplayer->index && setting & HAND_CHAMS) {
        render_colored(this_ptr,
            cvar_color(cv_chams_hands_r),
            cvar_color(cv_chams_hands_g),
            cvar_color(cv_chams_hands_b));
        return true;
    } else if (!(setting & PLAYER_CHAMS) || !valid_player(ent) ||
               !is_alive(ent)) {
        return false;
    }

    const bool friendly = is_friend(ent);

    /* Pass 1: behind walls */
    glDisable(GL_DEPTH_TEST);
    if (friendly) {
        render_colored(this_ptr,
            cvar_color(cv_chams_friend_invis_r),
            cvar_color(cv_chams_friend_invis_g),
            cvar_color(cv_chams_friend_invis_b));
    } else {
        render_colored(this_ptr,
            cvar_color(cv_chams_enemy_invis_r),
            cvar_color(cv_chams_enemy_invis_g),
            cvar_color(cv_chams_enemy_invis_b));
    }

    /* Pass 2: visible */
    glEnable(GL_DEPTH_TEST);
    if (friendly) {
        render_colored(this_ptr,
            cvar_color(cv_chams_friend_vis_r),
            cvar_color(cv_chams_friend_vis_g),
            cvar_color(cv_chams_friend_vis_b));
    } else {
        render_colored(this_ptr,
            cvar_color(cv_chams_enemy_vis_r),
            cvar_color(cv_chams_enemy_vis_g),
            cvar_color(cv_chams_enemy_vis_b));
    }

    return true;
}
