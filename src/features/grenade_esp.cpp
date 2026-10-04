#include "../include/sdk.h"
#include "../include/cvars.h"
#include "../include/globals.h"
#include "../include/mathutil.h"
#include "../include/util.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <GL/gl.h>

/*----------------------------------------------------------------------------*/
/* Constants                                                                  */

#define GE_MAX_ENTS       2048
#define GE_MAX_TRACKED    32
#define GE_MAX_TRAJ       256
#define GE_SIM_DT         0.025f
#define GE_DEFAULT_FUSE   4.0f
#define GE_DEFAULT_GRAVITY 800.0f
#define GE_STOP_SPEED     60.0f
#define GE_CIRCLE_RADIUS  120.0f
#define GE_CIRCLE_SEGS    24
#define GE_PI             3.14159265358979f
#define GE_DMG_BASE       150.0f
#define GE_DMG_RADIUS     500.0f

/* GoldSrc movetypes */
#define MOVETYPE_NONE    0
#define MOVETYPE_TOSS    6
#define MOVETYPE_BOUNCE  10

/*----------------------------------------------------------------------------*/
/* Model name matching via engine studio API                                  */

static const char* ge_get_model_name(cl_entity_t* ent) {
    if (!ent)
        return NULL;
    struct model_s* mdl = i_enginestudio->GetModelByIndex(ent->curstate.modelindex);
    if (!mdl)
        return NULL;
    return (const char*)mdl;
}

static bool ge_is_grenade_model(const char* name) {
    if (!name || !name[0])
        return false;
    /* Only match actual grenade world models, not sprites/effects */
    if (strstr(name, "sprites/") != NULL)
        return false;
    return (strstr(name, "grenade") != NULL ||
            strstr(name, "gren")    != NULL ||
            strstr(name, "stick")   != NULL ||
            strstr(name, "mills")   != NULL ||
            strstr(name, "smoke")   != NULL ||
            strstr(name, "splode")  != NULL ||
            strstr(name, "frag")    != NULL);
}

/*----------------------------------------------------------------------------*/
/* Vector helpers                                                             */

static inline float ge_dot(vec3_t a, vec3_t b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline float ge_vec_len(vec3_t v) {
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

/*----------------------------------------------------------------------------*/
/* Grenade tracking — each frame we scan entities and build a fresh draw list */
/* No persistent tracking across frames — just use what the engine gives us.  */

static float g_last_recon = 0.0f;

/*----------------------------------------------------------------------------*/
/* Trajectory prediction                                                      */

static int ge_predict(vec3_t start, vec3_t startvel, float sim_time,
                      float ent_gravity, float ent_friction,
                      vec3_t* out, int max_pts) {
    vec3_t pos, vel;
    vec_copy(pos, start);
    vec_copy(vel, startvel);

    int n = 0;
    vec_copy(out[n], pos);
    n++;

    float sv_grav = GE_DEFAULT_GRAVITY;
    cvar_t* g = i_engine->pfnGetCvarPointer("sv_gravity");
    if (g)
        sv_grav = g->value;

    float gravity = sv_grav;
    if (ent_gravity > 0.0f)
        gravity *= ent_gravity;

    float overbounce = 1.0f;
    if (ent_friction != 0.0f)
        overbounce = 2.0f - ent_friction;

    bool stopped = false;
    float t = 0.0f;

    i_engine->pEventAPI->EV_PushPMStates();
    i_engine->pEventAPI->EV_SetSolidPlayers(-1);
    i_engine->pEventAPI->EV_SetTraceHull(2);

    while (t < sim_time && n < max_pts && !stopped) {
        float dt = GE_SIM_DT;
        if (t + dt > sim_time)
            dt = sim_time - t;

        vel[2] -= gravity * dt;

        vec3_t end;
        end[0] = pos[0] + vel[0] * dt;
        end[1] = pos[1] + vel[1] * dt;
        end[2] = pos[2] + vel[2] * dt;

        pmtrace_t tr;
        i_engine->pEventAPI->EV_PlayerTrace(pos, end, 0, -1, &tr);

        if (tr.fraction < 1.0f) {
            vec_copy(pos, tr.endpos);

            float backoff = ge_dot(vel, tr.plane.normal) * overbounce;
            vel[0] -= backoff * tr.plane.normal[0];
            vel[1] -= backoff * tr.plane.normal[1];
            vel[2] -= backoff * tr.plane.normal[2];

            float spd = ge_vec_len(vel);
            if (spd < GE_STOP_SPEED) {
                vel[0] = vel[1] = vel[2] = 0.0f;
                stopped = true;
            }
        } else {
            vec_copy(pos, end);
        }

        vec_copy(out[n], pos);
        n++;
        t += dt;
    }

    i_engine->pEventAPI->EV_PopPMStates();
    return n;
}

/*----------------------------------------------------------------------------*/
/* Damage estimation — simple distance falloff, no trace                      */
/* The predicted detonation point is approximate anyway; a trace from a       */
/* future point that doesn't exist yet is unreliable. Just show distance dmg. */

static float ge_estimate_damage(vec3_t grenade_pos, vec3_t player_pos) {
    vec3_t diff = vec_sub(grenade_pos, player_pos);
    float dist = ge_vec_len(diff);

    if (dist > GE_DMG_RADIUS)
        return 0.0f;

    float dmg = GE_DMG_BASE * (1.0f - dist / GE_DMG_RADIUS);
    if (dmg < 0.0f)
        dmg = 0.0f;
    return dmg;
}

/*----------------------------------------------------------------------------*/
/* Drawing                                                                    */

static void ge_draw_circle_3d(vec3_t center, float radius) {
    vec2_t prev_scr;
    bool had_prev = false;
    const rgb_t col = { 255, 40, 40 };

    for (int i = 0; i <= GE_CIRCLE_SEGS; i++) {
        float a = 2.0f * GE_PI * (float)i / (float)GE_CIRCLE_SEGS;
        vec3_t pt = vec3(center[0] + cosf(a) * radius,
                         center[1] + sinf(a) * radius,
                         center[2]);

        vec2_t scr;
        if (world_to_screen(pt, scr)) {
            if (had_prev)
                gl_drawline(prev_scr[0], prev_scr[1], scr[0], scr[1],
                            2.0f, col);
            prev_scr[0] = scr[0];
            prev_scr[1] = scr[1];
            had_prev = true;
        } else {
            had_prev = false;
        }
    }
}

static void ge_draw_warning(vec3_t det_pos, float dmg, float remain) {
    vec3_t above = vec3(det_pos[0], det_pos[1], det_pos[2] + 40.0f);

    vec2_t scr;
    if (!world_to_screen(above, scr))
        return;

    int sx = (int)scr[0];
    int sy = (int)scr[1];

    char buf[64];

    /* Timer */
    rgb_t time_col;
    if (remain < 1.5f)
        time_col = (rgb_t){ 255, 40, 40 };
    else if (remain < 3.0f)
        time_col = (rgb_t){ 255, 200, 40 };
    else
        time_col = (rgb_t){ 40, 255, 40 };

    snprintf(buf, sizeof(buf), "%.1fs", remain);
    engine_draw_text(sx - 12, sy - 28, buf, time_col);

    /* Damage — always show, even if 0 */
    rgb_t dmg_col;
    if (dmg > 80.0f)
        dmg_col = (rgb_t){ 255, 40, 40 };
    else if (dmg > 40.0f)
        dmg_col = (rgb_t){ 255, 200, 40 };
    else
        dmg_col = (rgb_t){ 200, 200, 200 };

    snprintf(buf, sizeof(buf), "~%.0f dmg", dmg);
    engine_draw_text(sx - 20, sy - 14, buf, dmg_col);

    /* Warning exclamation */
    rgb_t warn_col = { 255, 40, 40 };
    engine_draw_text(sx - 4, sy, (char*)"!", warn_col);
}

/*----------------------------------------------------------------------------*/
/* Saved origin for velocity estimation (per entity index)                    */

static vec3_t g_prev_origin[GE_MAX_ENTS];
static float  g_prev_time[GE_MAX_ENTS];
static bool   g_prev_valid[GE_MAX_ENTS];
static float  g_first_seen[GE_MAX_ENTS];
static int    g_last_msgnum[GE_MAX_ENTS];
static bool   g_was_bounce[GE_MAX_ENTS];

/*----------------------------------------------------------------------------*/
/* Recon / debug mode                                                         */

static void ge_recon(float curtime) {
    if (curtime - g_last_recon < 0.5f)
        return;
    g_last_recon = curtime;

    int maxcl = i_engine->GetMaxClients();

    for (int i = maxcl + 1; i < GE_MAX_ENTS; i++) {
        cl_entity_t* ent = i_engine->GetEntityByIndex(i);
        if (!ent || !ent->model)
            continue;

        const char* mdl_name = ge_get_model_name(ent);
        if (!mdl_name || !mdl_name[0])
            continue;

        /* Only log grenade-like or recently tracked entities */
        bool is_gren = ge_is_grenade_model(mdl_name);
        bool was_tracked = g_prev_valid[i];
        if (!is_gren && !was_tracked)
            continue;

        i_engine->Con_Printf(
            "[ge] #%d  mdl=%-30s  mt=%d  msg=%d  "
            "org=(%.0f %.0f %.0f)  midx=%d  ef=%d\n",
            i, mdl_name, ent->curstate.movetype,
            ent->curstate.messagenum,
            ent->origin[0], ent->origin[1], ent->origin[2],
            ent->curstate.modelindex, ent->curstate.effects);
    }
}

/*----------------------------------------------------------------------------*/
/* Main entry point — called from h_HUD_Redraw                               */

void grenade_esp(void) {
    if (!cv_grenade_esp || cv_grenade_esp->value == 0.0f)
        return;

    float curtime = i_engine->GetClientTime();

    if (cv_grenade_esp->value >= 2.0f) {
        ge_recon(curtime);
        if (cv_grenade_esp->value >= 3.0f)
            return;
    }

    cl_entity_t* local = i_engine->GetLocalPlayer();
    if (!local)
        return;
    if (!i_engine->pEventAPI)
        return;

    int maxcl = i_engine->GetMaxClients();

    /* Mark all slots as "not seen this frame" */
    static bool g_seen_this_frame[GE_MAX_ENTS];
    memset(g_seen_this_frame, 0, sizeof(g_seen_this_frame));

    for (int i = maxcl + 1; i < GE_MAX_ENTS; i++) {
        cl_entity_t* ent = i_engine->GetEntityByIndex(i);
        if (!ent || !ent->model)
            continue;

        const char* mdl_name = ge_get_model_name(ent);
        if (!ge_is_grenade_model(mdl_name))
            continue;

        vec3_t origin;
        vec_copy(origin, ent->origin);

        if (vec_is_zero(origin))
            continue;

        int mt = ent->curstate.movetype;
        if (mt == MOVETYPE_NONE)
            continue;

        /* Remember if we ever saw this entity as BOUNCE (thrown grenade).
         * Only track grenades that were thrown, not pickups/drops. */
        if (mt == MOVETYPE_BOUNCE)
            g_was_bounce[i] = true;
        if (!g_was_bounce[i])
            continue;

        g_seen_this_frame[i] = true;

        /* First time seeing this grenade? */
        if (!g_prev_valid[i]) {
            vec_copy(g_prev_origin[i], origin);
            g_prev_time[i]    = curtime;
            g_prev_valid[i]   = true;
            g_first_seen[i]   = curtime;
            g_last_msgnum[i]  = ent->curstate.messagenum;
            continue; /* need at least 2 frames for velocity */
        }

        /* Kill grenade after fuse expires + 1s grace period */
        float age = curtime - g_first_seen[i];
        if (age > GE_DEFAULT_FUSE + 1.0f) {
            g_prev_valid[i] = false;
            g_was_bounce[i] = false;
            continue;
        }

        /* Velocity from curstate, or estimate from position delta */
        vec3_t velocity;
        vec_copy(velocity, ent->curstate.velocity);

        if (velocity[0] == 0.0f && velocity[1] == 0.0f && velocity[2] == 0.0f) {
            float dt = curtime - g_prev_time[i];
            if (dt > 0.001f) {
                velocity[0] = (origin[0] - g_prev_origin[i][0]) / dt;
                velocity[1] = (origin[1] - g_prev_origin[i][1]) / dt;
                velocity[2] = (origin[2] - g_prev_origin[i][2]) / dt;
            }
        }

        vec_copy(g_prev_origin[i], origin);
        g_prev_time[i] = curtime;

        float remain = GE_DEFAULT_FUSE - age;
        if (remain <= 0.0f)
            remain = 0.1f;

        float ent_grav = ent->curstate.gravity;
        float ent_fric = ent->curstate.friction;

        /* Predict trajectory */
        vec3_t traj[GE_MAX_TRAJ];
        int npts = ge_predict(origin, velocity, remain,
                              ent_grav, ent_fric, traj, GE_MAX_TRAJ);

        /* Draw trajectory line (red) */
        const rgb_t traj_col = { 255, 40, 40 };
        for (int j = 0; j < npts - 1; j++) {
            vec2_t s1, s2;
            bool v1 = world_to_screen(traj[j], s1);
            bool v2 = world_to_screen(traj[j + 1], s2);
            if (v1 && v2)
                gl_drawline(s1[0], s1[1], s2[0], s2[1], 2.0f, traj_col);
        }

        if (npts < 1)
            continue;

        vec3_t det;
        vec_copy(det, traj[npts - 1]);

        /* Draw 3-D circle at detonation point */
        ge_draw_circle_3d(det, GE_CIRCLE_RADIUS);

        /* Damage estimation from current grenade position to player */
        float dmg = ge_estimate_damage(origin, local->origin);

        /* Warning marker + timer */
        ge_draw_warning(det, dmg, remain);
    }

    /* Clear state for grenades no longer in the entity list this frame */
    for (int i = maxcl + 1; i < GE_MAX_ENTS; i++) {
        if (!g_seen_this_frame[i] && g_prev_valid[i])
            g_prev_valid[i] = false;
    }
}
