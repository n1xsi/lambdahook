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

enum visible_flags {
    NONE               = 0,
    ENEMY_VISIBLE      = 1,
    ENEMY_NOT_VISIBLE  = 2,
    FRIEND_VISIBLE     = 3,
    FRIEND_NOT_VISIBLE = 4,
    HANDS              = 5,
};

static void set_chams_color(visible_flags mode) {
    switch (mode) {
        case ENEMY_VISIBLE:       glColor4f(0.40f, 0.73f, 0.41f, 1.0f); break;
        case ENEMY_NOT_VISIBLE:   glColor4f(0.90f, 0.07f, 0.27f, 1.0f); break;
        case FRIEND_VISIBLE:      glColor4f(0.16f, 0.71f, 0.96f, 1.0f); break;
        case FRIEND_NOT_VISIBLE:  glColor4f(0.10f, 0.20f, 0.70f, 1.0f); break;
        case HANDS:               glColor4f(0.94f, 0.66f, 0.94f, 1.0f); break;
        default: break;
    }
}

bool chams(void* this_ptr) {
    const int setting = cv_chams->value == 5.0f ? 7 : (int)cv_chams->value;
    if (setting == DISABLED)
        return false;

    cl_entity_t* ent = i_enginestudio->GetCurrentEntity();

    if (ent->index == localplayer->index && setting & HAND_CHAMS) {
        glDisable(GL_TEXTURE_2D);
        set_chams_color(HANDS);
        i_studiomodelrenderer->StudioRenderFinal(this_ptr);
        glEnable(GL_TEXTURE_2D);
        return true;
    } else if (!(setting & PLAYER_CHAMS) || !valid_player(ent) ||
               !is_alive(ent)) {
        return false;
    }

    const bool friendly = is_friend(ent);

    glDisable(GL_TEXTURE_2D);

    glDisable(GL_DEPTH_TEST);
    set_chams_color(friendly ? FRIEND_NOT_VISIBLE : ENEMY_NOT_VISIBLE);
    i_studiomodelrenderer->StudioRenderFinal(this_ptr);

    glEnable(GL_DEPTH_TEST);
    set_chams_color(friendly ? FRIEND_VISIBLE : ENEMY_VISIBLE);
    i_studiomodelrenderer->StudioRenderFinal(this_ptr);

    glEnable(GL_TEXTURE_2D);

    return true;
}
