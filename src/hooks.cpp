#include <windows.h>
#include <stdio.h>

#include "include/hooks.h"
#include "include/sdk.h"
#include "include/globals.h"
#include "include/util.h"
#include "include/mathutil.h"
#include "include/cvars.h"
#include "include/detour.h"    /* 8dcc/detour-lib */
#include "features/features.h" /* bhop(), esp(), etc. */

/* Normal VMT hooks */
DECL_HOOK(CL_CreateMove);
DECL_HOOK(HUD_Redraw);
DECL_HOOK(StudioRenderModel);
DECL_HOOK(CalcRefdef);
DECL_HOOK(HUD_PostRunCmd);

/* Detour hooks */
DECL_HOOK(CL_Move);

detour_data_t detour_data_clmove;
DECL_DETOUR_TYPE(void, clmove_type);

static bool clmove_hooked = false;

/* VTable hook for StudioRenderModel */
static void** smr_vtable = NULL;
static void*  smr_orig_rendermodel = NULL;

typedef void (__thiscall *StudioRenderModel_vtfn)(void* this_ptr);

void __fastcall h_StudioRenderModel_vt(void* this_ptr, void* /* edx */) {
    static int smr_vt_log = 0;
    if (smr_vt_log < 5) {
        smr_vt_log++;
        cl_entity_t* ent = i_enginestudio->GetCurrentEntity();
        printf("  StudioRenderModel_VT #%d: ent=%p idx=%d player=%d\n",
               smr_vt_log, (void*)ent, ent ? ent->index : -1,
               ent ? ent->player : 0);
    }
    if (!chams(this_ptr)) {
        ((StudioRenderModel_vtfn)smr_orig_rendermodel)(this_ptr);
    }
}

/*----------------------------------------------------------------------------*/

bool hooks_init(void) {
    printf("dod-cheat: hooks_init()\n");

    /* Phase 1: VMT hooks on i_client */
    printf("  hooking CL_CreateMove...\n");
    HOOK(i_client, CL_CreateMove);
    printf("  hooking HUD_Redraw...\n");
    HOOK(i_client, HUD_Redraw);
    printf("  hooking CalcRefdef...\n");
    HOOK(i_client, CalcRefdef);
    printf("  hooking HUD_PostRunCmd...\n");
    HOOK(i_client, HUD_PostRunCmd);

    /* Phase 2: StudioRenderModel flat struct hook.
     * g_StudioRenderer is a flat struct (r_studio_interface_t), not a C++
     * object. Hook the function pointer directly in the struct. */
    if (i_studiomodelrenderer) {
        printf("  hooking StudioRenderModel (flat struct)...\n");
        printf("    g_StudioRenderer=%p\n", (void*)i_studiomodelrenderer);
        printf("    StudioRenderModel=%p StudioRenderFinal=%p\n",
               (void*)i_studiomodelrenderer->StudioRenderModel,
               (void*)i_studiomodelrenderer->StudioRenderFinal);
        HOOK(i_studiomodelrenderer, StudioRenderModel);
        printf("    hooked -> %p\n", (void*)i_studiomodelrenderer->StudioRenderModel);
    } else {
        printf("  SKIP StudioRenderModel (no i_studiomodelrenderer)\n");
    }

    /* Phase 3: glColor4f detour removed — chams call glColor4f directly */

    /* Phase 4: CL_Move detour — scan hw.dll for the function */
    {
        byte* hw_base = (byte*)hw;
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)hw_base;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(hw_base + dos->e_lfanew);
        IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        byte* text_start = NULL;
        size_t text_size = 0;
        byte* rdata_start = NULL;
        size_t rdata_size = 0;

        for (WORD si = 0; si < nt->FileHeader.NumberOfSections; si++) {
            if (sec[si].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
                if (!text_start) {
                    text_start = hw_base + sec[si].VirtualAddress;
                    text_size = sec[si].Misc.VirtualSize;
                }
            }
            if (sec[si].Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA &&
                !(sec[si].Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
                if (!rdata_start) {
                    rdata_start = hw_base + sec[si].VirtualAddress;
                    rdata_size = sec[si].Misc.VirtualSize;
                }
            }
        }

        void* clmove_addr = NULL;

        if (text_start && rdata_start) {
            /* Find "CL_Move\0" string in rdata */
            byte* str_addr = NULL;
            for (size_t i = 0; i + 8 <= rdata_size; i++) {
                if (memcmp(rdata_start + i, "CL_Move\0", 8) == 0) {
                    str_addr = rdata_start + i;
                    printf("  found \"CL_Move\" string at %p\n", str_addr);
                    break;
                }
            }

            if (str_addr) {
                /* Find push <str_addr> in .text: 68 xx xx xx xx */
                for (size_t i = 0; i + 5 <= text_size; i++) {
                    if (text_start[i] == 0x68 &&
                        *(void**)(text_start + i + 1) == (void*)str_addr) {
                        printf("  found push \"CL_Move\" at %p\n",
                               text_start + i);
                        /* Walk backwards to find function prologue */
                        for (int j = (int)i - 1; j >= (int)i - 128 && j >= 0;
                             j--) {
                            /* 55 = push ebp; or CC = int3 padding */
                            if (text_start[j] == 0x55 &&
                                text_start[j + 1] == 0x8B &&
                                text_start[j + 2] == 0xEC) {
                                clmove_addr = text_start + j;
                                printf("  found CL_Move at %p\n",
                                       clmove_addr);
                                break;
                            }
                            if (text_start[j] == 0xCC) {
                                clmove_addr = text_start + j + 1;
                                printf("  found CL_Move at %p (after CC)\n",
                                       clmove_addr);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        if (clmove_addr) {
            printf("  hooking CL_Move at %p...\n", clmove_addr);
            detour_init(&detour_data_clmove, clmove_addr, (void*)h_CL_Move);
            detour_add(&detour_data_clmove);
            clmove_hooked = true;
        } else {
            printf("  SKIP CL_Move (not found, speedhack disabled)\n");
        }
    }

    printf("dod-cheat: hooks_init() done (minimal mode)\n");
    return true;
}

void hooks_restore(void) {
    if (clmove_hooked)
        detour_del(&detour_data_clmove);

    /* Restore vtable hook */
    if (smr_vtable && smr_orig_rendermodel) {
        const int SMR_RENDERMODEL_IDX = 19;
        DWORD old_prot;
        VirtualProtect(&smr_vtable[SMR_RENDERMODEL_IDX], sizeof(void*),
                       PAGE_READWRITE, &old_prot);
        smr_vtable[SMR_RENDERMODEL_IDX] = smr_orig_rendermodel;
        VirtualProtect(&smr_vtable[SMR_RENDERMODEL_IDX], sizeof(void*),
                       old_prot, &old_prot);
    }
}

/*----------------------------------------------------------------------------*/

/* kbutton_t — engine's internal key state struct */
typedef struct {
    int down[2];
    int state;
} kbutton_t;

static kbutton_t* kb_jump = NULL;

void h_CL_CreateMove(float frametime, usercmd_t* cmd, int active) {
    ORIGINAL(CL_CreateMove, frametime, cmd, active);

    vec3_t old_angles = cmd->viewangles;

    localplayer = i_engine->GetLocalPlayer();

    /* Re-read pmove every frame */
    if (pp_pmove)
        i_pmove = *pp_pmove;

    /* Bhop: use engine command instead of modifying cmd->buttons.
     * The engine ignores our changes to cmd->buttons (it overwrites them
     * after CL_CreateMove returns). Instead, send +jump/-jump commands
     * directly to the engine. */
    if (CVAR_ON(bhop) && i_pmove && i_pmove->movetype == MOVETYPE_WALK) {
        bool space_down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        bool on_ground = (i_pmove->flags & FL_ONGROUND) != 0;

        static bool sent_jump = false;
        static int bhop_log = 0;

        if (space_down) {
            if (on_ground && !sent_jump) {
                i_engine->pfnClientCmd((char*)"+jump\n");
                sent_jump = true;
            } else if (!on_ground && sent_jump) {
                i_engine->pfnClientCmd((char*)"-jump\n");
                sent_jump = false;
            }
        } else {
            if (sent_jump) {
                i_engine->pfnClientCmd((char*)"-jump\n");
                sent_jump = false;
            }
        }

        if (bhop_log < 30) {
            bhop_log++;
            printf("  bhop#%d: space=%d ground=%d sent=%d btn=0x%x\n",
                   bhop_log, space_down, on_ground, sent_jump, cmd->buttons);
        }
    }

    aimbot(cmd);
    bullet_tracers(cmd);

    /* Norecoil: compensate view punch by counter-rotating.
     * CalcRefdef punchangle is always 0 in this engine version,
     * so we read punchangle from the playermove struct instead. */
    if (CVAR_ON(norecoil) && i_pmove) {
        static int nr_log = 0;
        vec3_t engine_angles;
        i_engine->GetViewAngles(engine_angles);

        /* The engine applies punchangle * 2 to the view */
        float px = i_pmove->punchangle.x;
        float py = i_pmove->punchangle.y;
        if (nr_log < 20 && (px != 0 || py != 0)) {
            nr_log++;
            printf("  norecoil_cm#%d: pmove_punch=%.2f,%.2f\n", nr_log, px, py);
        }
        if (px != 0 || py != 0) {
            engine_angles.x -= px * 2;
            engine_angles.y -= py * 2;
            i_engine->SetViewAngles(engine_angles);
        }
    }

    correct_movement(cmd, old_angles);
    ang_clamp(&cmd->viewangles);
}

/*----------------------------------------------------------------------------*/

int h_HUD_Redraw(float time, int intermission) {
    int ret = ORIGINAL(HUD_Redraw, time, intermission);

    /* Watermark */
    engine_draw_text(5, 5, "dod-cheat", (rgb_t){ 255, 255, 255 });

    esp();
    custom_crosshair();
    draw_fov_circle();

    return ret;
}

/*----------------------------------------------------------------------------*/

void h_StudioRenderModel(void* this_ptr) {
    static int smr_log = 0;
    if (smr_log < 5) {
        smr_log++;
        cl_entity_t* ent = i_enginestudio->GetCurrentEntity();
        printf("  StudioRenderModel #%d: ent=%p idx=%d player=%d chams=%d\n",
               smr_log, (void*)ent, ent ? ent->index : -1,
               ent ? ent->player : 0, (int)cv_chams->value);
    }
    if (!chams(this_ptr))
        ORIGINAL(StudioRenderModel, this_ptr);
}

/*----------------------------------------------------------------------------*/

void h_CalcRefdef(ref_params_t* params) {
    static int norecoil_log = 0;
    if (CVAR_ON(norecoil) && norecoil_log < 20) {
        norecoil_log++;
        printf("  norecoil#%d BEFORE: punch=%.2f,%.2f,%.2f\n",
               norecoil_log, params->punchangle.x, params->punchangle.y, params->punchangle.z);
    }

    vec_copy(g_punchAngles, params->punchangle);

    ORIGINAL(CalcRefdef, params);

    if (CVAR_ON(norecoil) && norecoil_log <= 20) {
        printf("  norecoil#%d AFTER:  punch=%.2f,%.2f,%.2f\n",
               norecoil_log, params->punchangle.x, params->punchangle.y, params->punchangle.z);
    }

    if (CVAR_ON(norecoil)) {
        params->punchangle.x = 0;
        params->punchangle.y = 0;
        params->punchangle.z = 0;
    }
}

/*----------------------------------------------------------------------------*/

void h_HUD_PostRunCmd(struct local_state_s* from, struct local_state_s* to,
                      struct usercmd_s* cmd, int runfuncs, double time,
                      unsigned int random_seed) {
    ORIGINAL(HUD_PostRunCmd, from, to, cmd, runfuncs, time, random_seed);

    if (runfuncs) {
        g_flNextAttack = to->client.m_flNextAttack;
        g_flNextPrimaryAttack =
          to->weapondata[to->client.m_iId].m_flNextPrimaryAttack;
        g_iClip = to->weapondata[to->client.m_iId].m_iClip;
    }
}

/*----------------------------------------------------------------------------*/

void h_CL_Move() {
    if (cv_clmove && cv_clmove->value != 0) {
        for (int i = 0; i < (int)cv_clmove->value; i++)
            CALL_ORIGINAL(detour_data_clmove, clmove_type);
    }

    CALL_ORIGINAL(detour_data_clmove, clmove_type);
}
