#ifndef HOOKS_H_
#define HOOKS_H_

/*----------------------------------------------------------------------------*/

#include "sdk.h"
#include "globals.h"
#include "detour.h"

#include <windows.h>
#include <gl/gl.h>

/*
 * Table of prefixes:
 *   prefix | meaning
 *   -------+----------------------------
 *   *_t    | typedef (function type)
 *   h_*    | hook function (ours)
 *   ho_*   | hook original (ptr to orig)
 */
#define DECL_HOOK_EXTERN(TYPE, NAME, ...)  \
    typedef TYPE (*NAME##_t)(__VA_ARGS__); \
    extern NAME##_t ho_##NAME;             \
    TYPE h_##NAME(__VA_ARGS__);

#define DECL_HOOK(NAME) NAME##_t ho_##NAME = NULL;

#define HOOK(INTERFACE, NAME)          \
    ho_##NAME       = INTERFACE->NAME; \
    INTERFACE->NAME = h_##NAME;

#define ORIGINAL(NAME, ...) ho_##NAME(__VA_ARGS__);

/*----------------------------------------------------------------------------*/

bool hooks_init(void);
void hooks_restore(void);

/* Detour data — exposed for cleanup */
extern detour_data_t detour_data_clmove;

/* VMT hooks */
DECL_HOOK_EXTERN(void, CL_CreateMove, float, usercmd_t*, int);
DECL_HOOK_EXTERN(int, HUD_Redraw, float, int);
DECL_HOOK_EXTERN(void, StudioRenderModel, void*);
DECL_HOOK_EXTERN(void, CalcRefdef, ref_params_t*);
DECL_HOOK_EXTERN(void, HUD_PostRunCmd, struct local_state_s*,
                 struct local_state_s*, struct usercmd_s*, int, double,
                 unsigned int);

/* Detour hooks */
DECL_HOOK_EXTERN(void, CL_Move);

#endif /* HOOKS_H_ */
