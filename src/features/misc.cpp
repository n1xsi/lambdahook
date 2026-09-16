#include <windows.h>

#include "../include/sdk.h"
#include "../include/util.h"
#include "../include/mathutil.h"
#include "../include/entityutil.h"
#include "../include/globals.h"
#include "../include/cvars.h"
#include "features.h"

void custom_crosshair(void) {
    if (!CVAR_ON(crosshair))
        return;

    /* Get window size via engine API */
    SCREENINFO scr;
    scr.iSize = sizeof(SCREENINFO);
    i_engine->pfnGetScreenInfo(&scr);
    int mx = scr.iWidth / 2;
    int my = scr.iHeight / 2;

    /* The real length is sqrt(2 * (len^2)) */
    const int len   = cv_crosshair->value;
    const int gap   = 1;
    const float w   = 1;
    const rgb_t col = { 255, 255, 255 };

    /*
     *   1\ /2
     *     X
     *   3/ \4
     */
    gl_drawline(mx - gap, my - gap, mx - gap - len, my - gap - len, w, col);
    gl_drawline(mx + gap, my - gap, mx + gap + len, my - gap - len, w, col);
    gl_drawline(mx - gap, my + gap, mx - gap - len, my + gap + len, w, col);
    gl_drawline(mx + gap, my + gap, mx + gap + len, my + gap + len, w, col);
}

void draw_fov_circle(void) {
    if (!CVAR_ON(fov_circle) || !CVAR_ON(aimbot))
        return;

    float fov_deg = cv_aimbot_fov->value;
    if (fov_deg <= 0.0f)
        return;

    SCREENINFO scr;
    scr.iSize = sizeof(SCREENINFO);
    i_engine->pfnGetScreenInfo(&scr);
    int mx = scr.iWidth / 2;
    int my = scr.iHeight / 2;

    /* Convert FOV degrees to screen pixels.
     * The game's horizontal FOV is ~90 degrees covering scr.iWidth pixels.
     * So: pixels_per_degree = scr.iWidth / 90.0
     * Radius in pixels = fov_deg * pixels_per_degree */
    float pixels_per_deg = (float)scr.iWidth / 90.0f;
    float radius = fov_deg * pixels_per_deg;

    const int segments = 64;
    const float line_w = 1.0f;
    const rgb_t col = { 255, 255, 255 };

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(col.r, col.g, col.b, 180);
    glLineWidth(line_w);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * 3.14159265f * (float)i / (float)segments;
        float px = mx + radius * cosf(angle);
        float py = my + radius * sinf(angle);
        glVertex2f(px, py);
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void bullet_tracers(usercmd_t* cmd) {
    /* Check physical mouse state — engine overwrites cmd->buttons */
    bool attacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (!CVAR_ON(tracers) || !attacking || !can_shoot() ||
        !is_alive(localplayer))
        return;

    /* Get player eye pos, start of tracer */
    vec3_t view_height;
    i_engine->pEventAPI->EV_LocalPlayerViewheight(view_height);
    vec3_t local_eyes = vec_add(localplayer->origin, view_height);

    /* Get forward vector from viewangles */
    vec3_t fwd;
    i_engine->pfnAngleVectors(cmd->viewangles, fwd, NULL, NULL);

    const int tracer_len = 3000;
    vec3_t end;
    end.x = local_eyes.x + fwd.x * tracer_len;
    end.y = local_eyes.y + fwd.y * tracer_len;
    end.z = local_eyes.z + fwd.z * tracer_len;

    /* NOTE: Change tracer settings here */
    const float w    = 0.8;
    const float time = 2;
    draw_tracer(local_eyes, end, (rgb_t){ 66, 165, 245 }, 1, w, time);
}
