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
/* StudioRenderModel is a __thiscall vtable function.
 * We hook it as __fastcall (this in ecx, unused edx).
 * Cannot use DECL_HOOK/ORIGINAL macros because they assume cdecl. */
typedef void (__thiscall *StudioRenderModel_fn)(void* this_ptr);
static StudioRenderModel_fn ho_StudioRenderModel_real = NULL;

void __fastcall h_StudioRenderModel_thiscall(void* this_ptr, void* /* edx */) {
    static int smr_log = 0;
    if (smr_log < 5) {
        smr_log++;
        cl_entity_t* ent = i_enginestudio->GetCurrentEntity();
        printf("  StudioRenderModel #%d: ent=%p idx=%d player=%d chams=%d\n",
               smr_log, (void*)ent, ent ? ent->index : -1,
               ent ? ent->player : 0, (int)cv_chams->value);
    }
    if (!chams(this_ptr))
        ho_StudioRenderModel_real(this_ptr);
}
DECL_HOOK(CalcRefdef);
DECL_HOOK(HUD_PostRunCmd);

/* Detour hooks */
DECL_HOOK(CL_Move);

detour_data_t detour_data_clmove;
DECL_DETOUR_TYPE(void, clmove_type);

static bool clmove_hooked = false;

static float* g_cl_viewangles_ptr = NULL;

/* norecoil: track SetViewAngles calls within CL_CreateMove */
static bool nr_in_createmove = false;
static int nr_sva_call_count = 0;    /* how many SVA calls this frame */
static float nr_sva_log[8][2];       /* log first 8 calls: pitch,yaw */
static float nr_pre_createmove[3];   /* cl.viewangles before CL_CreateMove */

/* Hardware breakpoint on cl.viewangles to catch who writes recoil */
static LONG WINAPI nr_veh_handler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    /* DR6 bit 0 = DR0 triggered */
    DWORD dr6 = ep->ContextRecord->Dr6;
    if (!(dr6 & 1))
        return EXCEPTION_CONTINUE_SEARCH;

    /* Clear DR6 */
    ep->ContextRecord->Dr6 = 0;

    /* Log who wrote to cl.viewangles[0] */
    static int veh_log = 0;
    if (veh_log < 100) {
        veh_log++;
        void* eip = (void*)ep->ContextRecord->Eip;

        /* Identify module */
        HMODULE mod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)eip, &mod);
        char modname[128] = "?";
        if (mod) GetModuleFileNameA(mod, modname, sizeof(modname));

        /* Get just the filename */
        char* slash = strrchr(modname, '\\');
        if (!slash) slash = strrchr(modname, '/');
        const char* shortname = slash ? slash + 1 : modname;

        printf("  veh#%d: EIP=%p (%s+0x%x) val=%.4f\n",
               veh_log, eip, shortname,
               (unsigned)((byte*)eip - (byte*)mod),
               g_cl_viewangles_ptr ? g_cl_viewangles_ptr[0] : 0.f);
    }

    /* Resume with RF flag to avoid re-triggering */
    ep->ContextRecord->EFlags |= 0x10000; /* RF */
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void* g_veh_handle = NULL;

static void nr_set_hwbp(void* addr) {
    DWORD tid = GetCurrentThreadId();
    HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, tid);
    if (!thread) {
        printf("  hwbp: OpenThread failed err=%lu\n", GetLastError());
        return;
    }

    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &ctx)) {
        printf("  hwbp: GetThreadContext failed err=%lu\n", GetLastError());
        CloseHandle(thread);
        return;
    }

    ctx.Dr0 = (DWORD)(uintptr_t)addr;
    ctx.Dr7 &= ~(0xF << 16);
    ctx.Dr7 |= (1 << 0);     /* DR0 local enable */
    ctx.Dr7 |= (1 << 16);    /* condition = write (01) */
    ctx.Dr7 |= (3 << 18);    /* len = 4 bytes (11) */
    ctx.Dr6 = 0;

    if (!SetThreadContext(thread, &ctx)) {
        printf("  hwbp: SetThreadContext failed err=%lu\n", GetLastError());
    }

    /* Verify */
    CONTEXT ctx2;
    memset(&ctx2, 0, sizeof(ctx2));
    ctx2.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(thread, &ctx2);
    printf("  hwbp: set DR0=%p DR7=0x%08x (verify DR0=%p DR7=0x%08x)\n",
           (void*)(uintptr_t)ctx.Dr0, (unsigned)ctx.Dr7,
           (void*)(uintptr_t)ctx2.Dr0, (unsigned)ctx2.Dr7);

    CloseHandle(thread);
}

typedef void (*SetViewAngles_fn)(float*);
typedef void (*GetViewAngles_fn)(float*);
static SetViewAngles_fn orig_SetViewAngles = NULL;
static GetViewAngles_fn orig_GetViewAngles = NULL;

/* Return-address offset (within client.dll) of the SetViewAngles call-site
 * that applies weapon recoil. Identified from sva_diag logs: this call-site
 * writes the systematic recoil kick (pitch climbing, DoD left/right spread).
 * The mouse/input call-site is +0x238dd and must be left alone. */
#define NR_RECOIL_CALLSITE_OFF 0x3cf2b

static void hooked_SetViewAngles(float* angles) {
    if (CVAR_ON(norecoil)) {
        void* retaddr = __builtin_return_address(0);
        HMODULE mod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)retaddr, &mod);
        unsigned off = mod ? (unsigned)((byte*)retaddr - (byte*)mod) : 0;

        if (off == NR_RECOIL_CALLSITE_OFF) {
            /* Recoil call-site: block the write so recoil never enters
             * cl.viewangles. Mouse (a separate call-site) is untouched. */
            static int nr_block_log = 0;
            if (nr_block_log < 40) {
                nr_block_log++;
                float cur[3] = {0};
                if (orig_GetViewAngles) orig_GetViewAngles(cur);
                printf("  nr_block_recoil#%d: blocked set %.4f,%.4f (cur=%.4f,%.4f)\n",
                       nr_block_log, angles[0], angles[1], cur[0], cur[1]);
            }
            return;
        }
        orig_SetViewAngles(angles);
        return;
    }
    orig_SetViewAngles(angles);
}

/*----------------------------------------------------------------------------*/

bool hooks_init(void) {
    printf("lambdahook: hooks_init()\n");

    /* Phase 1: VMT hooks on i_client */
    printf("  hooking CL_CreateMove...\n");
    HOOK(i_client, CL_CreateMove);
    printf("  hooking HUD_Redraw...\n");
    HOOK(i_client, HUD_Redraw);
    printf("  hooking CalcRefdef...\n");
    HOOK(i_client, CalcRefdef);
    printf("  hooking HUD_PostRunCmd...\n");
    HOOK(i_client, HUD_PostRunCmd);

    /* Phase 1b: Hook SetViewAngles/GetViewAngles in engine func table
     * to intercept DoD weapon event recoil */
    orig_SetViewAngles = (SetViewAngles_fn)i_engine->SetViewAngles;
    orig_GetViewAngles = (GetViewAngles_fn)i_engine->GetViewAngles;
    i_engine->SetViewAngles = hooked_SetViewAngles;
    printf("  hooked SetViewAngles: orig=%p new=%p\n",
           (void*)orig_SetViewAngles, (void*)hooked_SetViewAngles);

    /* Dump SetViewAngles code more deeply to find the REAL cl.viewangles */
    {
        byte* fn = (byte*)orig_GetViewAngles;
        byte* fn2 = (byte*)orig_SetViewAngles;
        printf("  GetViewAngles at %p bytes: ", (void*)fn);
        for (int i = 0; i < 32; i++)
            printf("%02X ", fn[i]);
        printf("\n");

        printf("  SetViewAngles at %p bytes: ", (void*)fn2);
        for (int i = 0; i < 64; i++)
            printf("%02X ", fn2[i]);
        printf("\n");

        /* SetViewAngles calls through [imm32] at offset +8 (FF 15 xx xx xx xx).
         * That indirect call might be the REAL setter. Dump that address. */
        for (int i = 0; i < 32; i++) {
            if (fn2[i] == 0xFF && fn2[i+1] == 0x15) {
                void** call_ptr = *(void***)(fn2 + i + 2);
                printf("  SVA indirect call at +%d: [%p] = %p\n",
                       i, (void*)call_ptr, *call_ptr);
                /* Dump the target function */
                byte* target = (byte*)*call_ptr;
                if (target) {
                    printf("  SVA target bytes: ");
                    for (int j = 0; j < 64; j++)
                        printf("%02X ", target[j]);
                    printf("\n");
                }
                break;
            }
        }

        /* Similarly for GetViewAngles */
        for (int i = 0; i < 32; i++) {
            if (fn[i] == 0xFF && fn[i+1] == 0x15) {
                void** call_ptr = *(void***)(fn + i + 2);
                printf("  GVA indirect call at +%d: [%p] = %p\n",
                       i, (void*)call_ptr, *call_ptr);
                byte* target = (byte*)*call_ptr;
                if (target) {
                    printf("  GVA target bytes: ");
                    for (int j = 0; j < 64; j++)
                        printf("%02X ", target[j]);
                    printf("\n");
                }
                break;
            }
        }

        /* Extract cl.viewangles address from SetViewAngles:
         * Pattern: F3 0F 11 05 XX XX XX XX  (movss [addr], xmm0)
         * The address is at offset +17 in the function bytes we see. */
        float* cl_viewangles = NULL;
        for (int i = 0; i < 48; i++) {
            if (fn2[i] == 0xF3 && fn2[i+1] == 0x0F && fn2[i+2] == 0x11 && fn2[i+3] == 0x05) {
                cl_viewangles = *(float**)(fn2 + i + 4);
                printf("  cl.viewangles found at %p\n", (void*)cl_viewangles);
                break;
            }
        }

        if (cl_viewangles) {
            printf("  cl.viewangles value: %.2f, %.2f, %.2f\n",
                   cl_viewangles[0], cl_viewangles[1], cl_viewangles[2]);
            g_cl_viewangles_ptr = cl_viewangles;

            /* Install hardware breakpoint on cl.viewangles[0] to find who writes recoil */
            g_veh_handle = AddVectoredExceptionHandler(1, nr_veh_handler);
            if (g_veh_handle) {
                nr_set_hwbp((void*)cl_viewangles);
                printf("  hwbp installed on cl.viewangles[0] at %p\n", (void*)cl_viewangles);
            } else {
                printf("  FAILED to install VEH handler\n");
            }
        }
    }

    /* Phase 2: StudioRenderModel flat struct hook.
     * g_StudioRenderer is a flat struct (r_studio_interface_t), not a C++
     * object. Hook the function pointer directly in the struct. */
    if (i_studiomodelrenderer) {
        printf("  hooking StudioRenderModel (vtable thiscall)...\n");
        printf("    vtable at %p\n", (void*)i_studiomodelrenderer);
        printf("    StudioRenderModel=%p StudioRenderFinal=%p\n",
               (void*)i_studiomodelrenderer->StudioRenderModel,
               (void*)i_studiomodelrenderer->StudioRenderFinal);

        ho_StudioRenderModel_real = (StudioRenderModel_fn)i_studiomodelrenderer->StudioRenderModel;

        DWORD old_prot;
        VirtualProtect(&i_studiomodelrenderer->StudioRenderModel, sizeof(void*),
                       PAGE_EXECUTE_READWRITE, &old_prot);
        i_studiomodelrenderer->StudioRenderModel = (void (*)(void*))h_StudioRenderModel_thiscall;
        VirtualProtect(&i_studiomodelrenderer->StudioRenderModel, sizeof(void*),
                       old_prot, &old_prot);

        printf("    hooked -> %p\n", (void*)i_studiomodelrenderer->StudioRenderModel);
    } else {
        printf("  SKIP StudioRenderModel (no i_studiomodelrenderer)\n");
    }

    /* Phase 3: glColor4f detour for chams coloring */
    chams_init();

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

    printf("lambdahook: hooks_init() done (minimal mode)\n");
    return true;
}

void hooks_restore(void) {
    chams_unhook_hw();
    chams_restore();

    if (clmove_hooked)
        detour_del(&detour_data_clmove);

    /* Restore vtable hook */
    if (i_studiomodelrenderer && ho_StudioRenderModel_real) {
        DWORD old_prot;
        VirtualProtect(&i_studiomodelrenderer->StudioRenderModel, sizeof(void*),
                       PAGE_EXECUTE_READWRITE, &old_prot);
        i_studiomodelrenderer->StudioRenderModel = (void (*)(void*))ho_StudioRenderModel_real;
        VirtualProtect(&i_studiomodelrenderer->StudioRenderModel, sizeof(void*),
                       old_prot, &old_prot);
    }

    /* Restore SetViewAngles */
    if (orig_SetViewAngles)
        i_engine->SetViewAngles = (void (*)(float*))orig_SetViewAngles;

    /* Remove hardware breakpoint and VEH */
    if (g_veh_handle) {
        CONTEXT ctx;
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        GetThreadContext(GetCurrentThread(), &ctx);
        ctx.Dr0 = 0;
        ctx.Dr7 &= ~((0xF << 16) | (1 << 0));
        ctx.Dr6 = 0;
        SetThreadContext(GetCurrentThread(), &ctx);
        RemoveVectoredExceptionHandler(g_veh_handle);
        g_veh_handle = NULL;
    }
}

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/

void h_CL_CreateMove(float frametime, usercmd_t* cmd, int active) {
    bool do_nr = CVAR_ON(norecoil) && g_cl_viewangles_ptr;

    float va_before[3] = {0};
    if (do_nr) {
        va_before[0] = g_cl_viewangles_ptr[0];
        va_before[1] = g_cl_viewangles_ptr[1];
        va_before[2] = g_cl_viewangles_ptr[2];
    }

    /* Do NOT allow SetViewAngles during CL_CreateMove either —
     * DoD applies recoil via gEngfuncs.SetViewAngles inside weapon events.
     * Mouse input writes to cl.viewangles directly, not through SetViewAngles. */
    nr_sva_call_count = 0;
    nr_in_createmove = true;
    ORIGINAL(CL_CreateMove, frametime, cmd, active);
    nr_in_createmove = false;

    if (do_nr) {
        /* diagnostics printed from hooked_SetViewAngles */
        (void)va_before;
    }

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


    correct_movement(cmd, old_angles);
    ang_clamp(&cmd->viewangles);
}

/*----------------------------------------------------------------------------*/

int h_HUD_Redraw(float time, int intermission) {
    int ret = ORIGINAL(HUD_Redraw, time, intermission);

    /* Watermark */
    /* Centered watermark */
    {
        SCREENINFO scr;
        scr.iSize = sizeof(SCREENINFO);
        i_engine->pfnGetScreenInfo(&scr);
        const char* wm = "lambdahook";
        int tw = 0;
        for (const char* p = wm; *p; p++)
            tw += scr.charWidths[(unsigned char)*p];
        engine_draw_text(scr.iWidth / 2 - tw / 2, 5, (char*)wm, (rgb_t){ 255, 255, 255 });
    }

    esp();
    custom_crosshair();
    draw_fov_circle();

    return ret;
}

/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/

void h_CalcRefdef(ref_params_t* params) {
    ORIGINAL(CalcRefdef, params);

    if (CVAR_ON(norecoil)) {
        static int cr_log = 0;
        bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (lmb && cr_log < 60) {
            cr_log++;
            printf("  cr#%d: pa=%.4f,%.4f,%.4f clva=%.4f,%.4f va=%.4f,%.4f\n",
                   cr_log,
                   params->punchangle.x, params->punchangle.y, params->punchangle.z,
                   params->cl_viewangles.x, params->cl_viewangles.y,
                   params->viewangles.x, params->viewangles.y);
        }

        params->viewangles.x -= params->punchangle.x * 2;
        params->viewangles.y -= params->punchangle.y * 2;
        params->viewangles.z -= params->punchangle.z * 2;
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

        vec_copy(g_punchAngles, to->client.punchangle);

        static int postrun_log = 0;
        if (CVAR_ON(norecoil) && postrun_log < 20) {
            float px = to->client.punchangle.x;
            float py = to->client.punchangle.y;
            if (px != 0 || py != 0) {
                postrun_log++;
                printf("  postrun_punch#%d: %.2f,%.2f\n", postrun_log, px, py);
            }
        }
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
