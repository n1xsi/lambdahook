#ifndef GLOBALS_H_
#define GLOBALS_H_

#include "sdk.h"
#include <windows.h>

/*----------------------------------------------------------------------------*/

#define DECL_INTF(type, name) \
    type* i_##name = NULL;    \
    type o_##name;

#define DECL_INTF_EXTERN(type, name) \
    extern type* i_##name;           \
    extern type o_##name;

/*----------------------------------------------------------------------------*/

extern void* hw;
extern HMODULE client_dll;

extern vec3_t g_punchAngles;
extern float g_flNextAttack, g_flNextPrimaryAttack;
extern int g_iClip;

DECL_INTF_EXTERN(cl_enginefunc_t, engine);
DECL_INTF_EXTERN(cl_clientfunc_t, client);
DECL_INTF_EXTERN(playermove_t, pmove);
extern playermove_t** pp_pmove;
DECL_INTF_EXTERN(engine_studio_api_t, enginestudio);
DECL_INTF_EXTERN(StudioModelRenderer_t, studiomodelrenderer);
extern r_studio_interface_t* g_pStudioAPI;

extern cl_entity_t* localplayer;

/*----------------------------------------------------------------------------*/

bool globals_init(void);
void globals_store(void);
void globals_restore(void);

#endif /* GLOBALS_H_ */
