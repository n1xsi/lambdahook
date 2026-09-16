#include <stdio.h>
#include <string.h>
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

typedef void (__thiscall *StudioRenderFinal_fn)(void* this_ptr);

static void call_StudioRenderFinal(void* this_ptr) {
    StudioRenderFinal_fn fn = (StudioRenderFinal_fn)i_studiomodelrenderer->StudioRenderFinal;
    fn(this_ptr);
}

static inline float cvar_color(cvar_t* cv) {
    return cv->value / 255.0f;
}

typedef void (APIENTRY *glColor4f_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
static glColor4f_fn real_glColor4f = NULL;

static bool chams_active = false;
static float chams_r = 1.0f, chams_g = 1.0f, chams_b = 1.0f;

static bool glcolor_hooked = false;
static uint8_t saved_bytes[5];

static void APIENTRY h_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (chams_active) {
        r = chams_r;
        g = chams_g;
        b = chams_b;
        /* Force textures off — StudioRenderFinal re-enables them */
        glDisable(GL_TEXTURE_2D);
    }
    /* Unhook, call original, rehook. */
    DWORD old_prot;
    VirtualProtect((void*)real_glColor4f, 5, PAGE_EXECUTE_READWRITE, &old_prot);
    memcpy((void*)real_glColor4f, saved_bytes, 5);
    VirtualProtect((void*)real_glColor4f, 5, old_prot, &old_prot);

    real_glColor4f(r, g, b, a);

    VirtualProtect((void*)real_glColor4f, 5, PAGE_EXECUTE_READWRITE, &old_prot);
    uint8_t jmp[5];
    jmp[0] = 0xE9;
    int32_t rel = (int32_t)((uint8_t*)h_glColor4f - ((uint8_t*)real_glColor4f + 5));
    memcpy(&jmp[1], &rel, 4);
    memcpy((void*)real_glColor4f, jmp, 5);
    VirtualProtect((void*)real_glColor4f, 5, old_prot, &old_prot);
}

void chams_init(void) {
    HMODULE opengl = GetModuleHandleA("opengl32.dll");
    if (!opengl) {
        printf("  chams_init: can't find opengl32.dll\n");
        return;
    }
    real_glColor4f = (glColor4f_fn)GetProcAddress(opengl, "glColor4f");
    if (!real_glColor4f) {
        printf("  chams_init: can't find glColor4f\n");
        return;
    }

    printf("  chams_init: glColor4f at %p\n", (void*)real_glColor4f);
    printf("  glColor4f bytes: ");
    for (int i = 0; i < 16; i++)
        printf("%02X ", ((uint8_t*)real_glColor4f)[i]);
    printf("\n");

    /* Save first 5 bytes */
    memcpy(saved_bytes, (void*)real_glColor4f, 5);

    /* Write relative jmp */
    DWORD old_prot;
    VirtualProtect((void*)real_glColor4f, 5, PAGE_EXECUTE_READWRITE, &old_prot);
    uint8_t jmp[5];
    jmp[0] = 0xE9;
    int32_t rel = (int32_t)((uint8_t*)h_glColor4f - ((uint8_t*)real_glColor4f + 5));
    memcpy(&jmp[1], &rel, 4);
    memcpy((void*)real_glColor4f, jmp, 5);
    VirtualProtect((void*)real_glColor4f, 5, old_prot, &old_prot);

    glcolor_hooked = true;
    printf("  chams_init: glColor4f hooked\n");
}

void chams_restore(void) {
    if (glcolor_hooked && real_glColor4f) {
        DWORD old_prot;
        VirtualProtect((void*)real_glColor4f, 5, PAGE_EXECUTE_READWRITE, &old_prot);
        memcpy((void*)real_glColor4f, saved_bytes, 5);
        VirtualProtect((void*)real_glColor4f, 5, old_prot, &old_prot);
        glcolor_hooked = false;
    }
}

bool chams(void* this_ptr) {
    const int setting = cv_chams->value == 5.0f ? 7 : (int)cv_chams->value;
    if (setting == DISABLED)
        return false;

    cl_entity_t* ent = i_enginestudio->GetCurrentEntity();

    if (ent->index == localplayer->index && setting & HAND_CHAMS) {
        chams_r = cvar_color(cv_chams_hands_r);
        chams_g = cvar_color(cv_chams_hands_g);
        chams_b = cvar_color(cv_chams_hands_b);
        chams_active = true;
        call_StudioRenderFinal(this_ptr);
        chams_active = false;
        return true;
    } else if (!(setting & PLAYER_CHAMS) || !valid_player(ent) ||
               !is_alive(ent)) {
        return false;
    }

    const bool friendly = is_friend(ent);

    /* Pass 1: behind walls */
    glDisable(GL_DEPTH_TEST);
    if (friendly) {
        chams_r = cvar_color(cv_chams_friend_invis_r);
        chams_g = cvar_color(cv_chams_friend_invis_g);
        chams_b = cvar_color(cv_chams_friend_invis_b);
    } else {
        chams_r = cvar_color(cv_chams_enemy_invis_r);
        chams_g = cvar_color(cv_chams_enemy_invis_g);
        chams_b = cvar_color(cv_chams_enemy_invis_b);
    }
    chams_active = true;
    call_StudioRenderFinal(this_ptr);
    chams_active = false;

    /* Pass 2: visible */
    glEnable(GL_DEPTH_TEST);
    if (friendly) {
        chams_r = cvar_color(cv_chams_friend_vis_r);
        chams_g = cvar_color(cv_chams_friend_vis_g);
        chams_b = cvar_color(cv_chams_friend_vis_b);
    } else {
        chams_r = cvar_color(cv_chams_enemy_vis_r);
        chams_g = cvar_color(cv_chams_enemy_vis_g);
        chams_b = cvar_color(cv_chams_enemy_vis_b);
    }
    chams_active = true;
    call_StudioRenderFinal(this_ptr);
    chams_active = false;

    return true;
}
