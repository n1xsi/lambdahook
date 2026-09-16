#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "include/globals.h"
#include "include/sdk.h"
#include "include/util.h"

/*----------------------------------------------------------------------------*/

/* Handlers used globally */
void* hw = NULL;
HMODULE client_dll = NULL;

vec3_t g_punchAngles = { 0, 0, 0 };

/* Weapon info */
float g_flNextAttack = 0.f, g_flNextPrimaryAttack = 0.f;
int g_iClip = 0;

DECL_INTF(cl_enginefunc_t, engine);
DECL_INTF(cl_clientfunc_t, client);
DECL_INTF(playermove_t, pmove);
playermove_t** pp_pmove = NULL;
DECL_INTF(engine_studio_api_t, enginestudio);
DECL_INTF(StudioModelRenderer_t, studiomodelrenderer);
r_studio_interface_t* g_pStudioAPI = NULL;

/* Updated in CL_CreateMove hook */
cl_entity_t* localplayer = NULL;

/*----------------------------------------------------------------------------*/

/*
 * Find the engine's cl_clientfunc_t table in hw.dll by scanning writable
 * sections for known client.dll export addresses at the correct struct offsets.
 * Zero byte-pattern signatures — survives engine updates as long as the engine
 * still stores a function table with pointers to client.dll exports.
 */
static cl_clientfunc_t* find_client_funcs(void) {
    void* real_Initialize = (void*)GetProcAddress(client_dll, "Initialize");
    void* real_HUD_Init   = (void*)GetProcAddress(client_dll, "HUD_Init");
    void* real_CL_CreateMove = (void*)GetProcAddress(client_dll, "CL_CreateMove");
    void* real_HUD_Redraw = (void*)GetProcAddress(client_dll, "HUD_Redraw");

    if (!real_Initialize || !real_HUD_Init || !real_CL_CreateMove || !real_HUD_Redraw) {
        ERR("Missing critical client.dll exports");
        return NULL;
    }

    printf("  client exports: Initialize=%p HUD_Init=%p CL_CreateMove=%p HUD_Redraw=%p\n",
           real_Initialize, real_HUD_Init, real_CL_CreateMove, real_HUD_Redraw);

    /* Scan hw.dll's writable sections for the struct */
    byte* base = (byte*)hw;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    size_t init_off    = offsetof(cl_clientfunc_t, Initialize);
    size_t hudinit_off = offsetof(cl_clientfunc_t, HUD_Init);
    size_t clcm_off    = offsetof(cl_clientfunc_t, CL_CreateMove);
    size_t redraw_off  = offsetof(cl_clientfunc_t, HUD_Redraw);
    size_t struct_size = sizeof(cl_clientfunc_t);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD chars = sec[i].Characteristics;
        if (!(chars & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA)))
            continue;

        byte* start = base + sec[i].VirtualAddress;
        size_t size = sec[i].Misc.VirtualSize;

        if (size < struct_size)
            continue;

        for (size_t off = 0; off + struct_size <= size; off += 4) {
            byte* candidate = start + off;

            if (*(void**)(candidate + init_off) != real_Initialize)
                continue;
            if (*(void**)(candidate + hudinit_off) != real_HUD_Init)
                continue;
            if (*(void**)(candidate + clcm_off) != real_CL_CreateMove)
                continue;
            if (*(void**)(candidate + redraw_off) != real_HUD_Redraw)
                continue;

            printf("  found cl_funcs at %p\n", candidate);
            return (cl_clientfunc_t*)candidate;
        }
    }

    return NULL;
}

/*
 * Find client.dll's copy of the engine function table (gEngfuncs).
 * Scans the Initialize() export for: mov edi, <imm32> ; mov ecx, <imm32> ; rep movsd
 * The first imm32 is the destination address (gEngfuncs).
 *
 * Also handles the alternate pattern: push esi; mov esi,[esp+8]; mov edi,<imm32>;
 * or a direct: mov ecx,<count>; mov edi,<addr>; rep movsd
 */
static cl_enginefunc_t* find_engine_funcs(void) {
    byte* func = (byte*)GetProcAddress(client_dll, "Initialize");
    if (!func) {
        ERR("Can't find Initialize export");
        return NULL;
    }

    printf("  Initialize at %p, scanning for gEngfuncs...\n", func);

    /* Scan first 128 bytes for rep movsd (F3 A5).
     * The mov edi,<imm32> (BF xx xx xx xx) before it is the destination. */
    for (int i = 0; i < 128 - 2; i++) {
        if (func[i] == 0xF3 && func[i + 1] == 0xA5) {
            /* Walk backwards to find BF (mov edi, imm32) */
            for (int j = i - 1; j >= i - 20 && j >= 0; j--) {
                if (func[j] == 0xBF) {
                    void* addr = *(void**)(func + j + 1);
                    printf("  found gEngfuncs at %p\n", addr);
                    return (cl_enginefunc_t*)addr;
                }
            }
        }
    }

    /* Fallback: scan for a mov [imm32], eax pattern (A3 xx xx xx xx)
     * which would be a direct store to a global pointer */
    for (int i = 0; i < 64; i++) {
        if (func[i] == 0xA3) {
            void** pptr = *(void***)(func + i + 1);
            printf("  found gEngfuncs ptr at %p -> %p\n", pptr, *pptr);
            return (cl_enginefunc_t*)*pptr;
        }
    }

    return NULL;
}

/*
 * Find client.dll's stored playermove_t pointer.
 * Scans PM_Init/HUD_PlayerMoveInit/HUD_PlayerMove for a store to a global.
 * Returns the ADDRESS of the global pointer (playermove_t**) so we can
 * re-read it every frame — the value is NULL until the engine calls PM_Move.
 */
static playermove_t** find_pmove_ptr(void) {
    /* Try exports in order of likelihood of having a direct store */
    const char* exports[] = {
        "HUD_PlayerMoveInit", "HUD_PlayerMove", NULL
    };

    for (int e = 0; exports[e]; e++) {
        byte* func = (byte*)GetProcAddress(client_dll, exports[e]);
        if (!func)
            continue;

        printf("  scanning %s at %p for pmove global...\n", exports[e], func);
        printf("  %s bytes: ", exports[e]);
        for (int d = 0; d < 64; d++)
            printf("%02X ", func[d]);
        printf("\n");

        /* Scan for store patterns, stop at function boundary */
        for (int i = 0; i < 64; i++) {
            if (func[i] == 0xC3) break;
            if (func[i] == 0x90 && i + 1 < 64 && func[i + 1] == 0x90) break;
            if (func[i] == 0xE9) break;

            playermove_t** pptr = NULL;

            if (func[i] == 0xA3)
                pptr = *(playermove_t***)(func + i + 1);
            else if (func[i] == 0x89 && i + 5 < 64 &&
                     (func[i+1] == 0x0D || func[i+1] == 0x15 ||
                      func[i+1] == 0x35 || func[i+1] == 0x3D ||
                      func[i+1] == 0x1D))
                pptr = *(playermove_t***)(func + i + 2);

            if (pptr) {
                printf("  candidate at %s+%d: addr=%p val=%p\n",
                       exports[e], i, pptr, *pptr);
                printf("  found pp_pmove at %p\n", pptr);
                return pptr;
            }
        }
    }

    /* Last resort: HUD_PlayerMove uses mov edx,[imm32] to load an object.
     * The first dword at +0 is the address it reads from — that's the
     * pmove global we want (accessed indirectly via vtable call).
     * Pattern: 8B 15 xx xx xx xx = mov edx, [imm32] */
    byte* func = (byte*)GetProcAddress(client_dll, "HUD_PlayerMove");
    if (func && func[0] == 0x8B && func[1] == 0x15) {
        void** obj_ptr_addr = *(void***)(func + 2);
        printf("  HUD_PlayerMove uses indirect obj at %p (val=%p)\n",
               obj_ptr_addr, *obj_ptr_addr);
        /* The object pointer itself isn't pmove, but the function passes
         * [esp+4] (first arg = pmove) through to the virtual call.
         * We need a different approach — scan the target of the jmp. */

        /* At +22 there's E9 (jmp) to a function that likely stores pmove.
         * Follow it. */
        byte* cbase_pm = (byte*)client_dll;
        IMAGE_DOS_HEADER* cdos_pm = (IMAGE_DOS_HEADER*)cbase_pm;
        IMAGE_NT_HEADERS* cnt_pm = (IMAGE_NT_HEADERS*)(cbase_pm + cdos_pm->e_lfanew);
        size_t csize_pm = cnt_pm->OptionalHeader.SizeOfImage;

        for (int i = 0; i < 32; i++) {
            if (func[i] == 0xE9) {
                int32_t rel = *(int32_t*)(func + i + 1);
                byte* target = func + i + 5 + rel;
                printf("  following jmp at +%d to %p\n", i, target);

                /* Validate target is within client.dll */
                if (target < cbase_pm || target >= cbase_pm + csize_pm) {
                    printf("  jmp target outside client.dll, skipping\n");
                    break;
                }

                printf("  target bytes: ");
                for (int d = 0; d < 64; d++)
                    printf("%02X ", target[d]);
                printf("\n");

                /* Scan the target function for a pmove store */
                for (int j = 0; j < 128; j++) {
                    if (target[j] == 0xC3) break;

                    playermove_t** pptr = NULL;
                    if (target[j] == 0xA3)
                        pptr = *(playermove_t***)(target + j + 1);
                    else if (target[j] == 0x89 && j + 5 < 128 &&
                             (target[j+1] == 0x0D || target[j+1] == 0x15 ||
                              target[j+1] == 0x35 || target[j+1] == 0x3D ||
                              target[j+1] == 0x1D))
                        pptr = *(playermove_t***)(target + j + 2);

                    if (pptr) {
                        printf("  candidate at jmp_target+%d: addr=%p val=%p\n",
                               j, pptr, *pptr);
                        printf("  found pp_pmove at %p\n", pptr);
                        return pptr;
                    }
                }
                break;
            }
        }
    }

    return NULL;
}

/*
 * Find engine_studio_api and g_StudioRenderer from HUD_GetStudioModelInterface.
 *
 * Signature: int HUD_GetStudioModelInterface(int version,
 *                struct r_studio_interface_s **ppinterface,
 *                engine_studio_api_t *pstudio)
 *
 * The function stores pstudio to a global (IEngineStudio) and writes
 * *ppinterface = &g_StudioRenderer.
 */
static bool find_studio_interfaces(engine_studio_api_t** out_enginestudio,
                                    StudioModelRenderer_t** out_smr) {
    byte* func = (byte*)GetProcAddress(client_dll, "HUD_GetStudioModelInterface");
    if (!func) {
        ERR("Can't find HUD_GetStudioModelInterface export");
        return false;
    }

    printf("  HUD_GetStudioModelInterface at %p\n", func);
    printf("  bytes: ");
    for (int d = 0; d < 96; d++)
        printf("%02X ", func[d]);
    printf("\n");

    engine_studio_api_t* found_studio = NULL;
    StudioModelRenderer_t* found_smr = NULL;

    for (int i = 0; i < 128; i++) {
        /* rep movsd — destination (edi/BF) is IEngineStudio */
        if (func[i] == 0xF3 && func[i + 1] == 0xA5) {
            for (int j = i - 1; j >= i - 20 && j >= 0; j--) {
                if (func[j] == 0xBF) {
                    void* addr = *(void**)(func + j + 1);
                    if (!found_studio) {
                        found_studio = (engine_studio_api_t*)addr;
                        printf("  found IEngineStudio copy at %p\n", addr);
                    }
                    break;
                }
            }
        }

        /* A3 = mov [imm32], eax — pstudio stored directly */
        if (!found_studio && func[i] == 0xA3) {
            void** pptr = *(void***)(func + i + 1);
            byte* cbase = (byte*)client_dll;
            IMAGE_DOS_HEADER* cdos = (IMAGE_DOS_HEADER*)cbase;
            IMAGE_NT_HEADERS* cnt = (IMAGE_NT_HEADERS*)(cbase + cdos->e_lfanew);
            size_t csize = cnt->OptionalHeader.SizeOfImage;
            if ((byte*)pptr >= cbase && (byte*)pptr < cbase + csize) {
                if (*pptr) {
                    found_studio = (engine_studio_api_t*)*pptr;
                    printf("  found IEngineStudio via A3 at %p -> %p\n", pptr, *pptr);
                }
            }
        }
    }

    if (found_studio)
        *out_enginestudio = found_studio;

    /* Find CStudioModelRenderer by following the E8 call in
     * HUD_GetStudioModelInterface. That call goes to a small init
     * function which references the global CStudioModelRenderer
     * object via mov ecx, <imm32> (B9). The object's first dword
     * is a vtable pointer containing 20+ code pointers. */
    byte* cbase = (byte*)client_dll;
    IMAGE_DOS_HEADER* cdos = (IMAGE_DOS_HEADER*)cbase;
    IMAGE_NT_HEADERS* cnt = (IMAGE_NT_HEADERS*)(cbase + cdos->e_lfanew);
    size_t csize = cnt->OptionalHeader.SizeOfImage;

    for (int i = 0; i < 96; i++) {
        if (func[i] != 0xE8)
            continue;

        int32_t rel = *(int32_t*)(func + i + 1);
        byte* target = func + i + 5 + rel;

        if (target < cbase || target >= cbase + csize)
            continue;

        printf("  E8 call at +%d -> %p\n", i, target);
        printf("  target bytes: ");
        for (int d = 0; d < 64; d++)
            printf("%02X ", target[d]);
        printf("\n");

        /* Scan target for B9 (mov ecx, imm32) — this ptr */
        for (int j = 0; j < 64; j++) {
            if (target[j] != 0xB9)
                continue;

            uint32_t candidate = *(uint32_t*)(target + j + 1);
            byte* cand_ptr = (byte*)(uintptr_t)candidate;
            if (cand_ptr < cbase || cand_ptr >= cbase + csize)
                continue;

            uint32_t maybe_vtbl = *(uint32_t*)cand_ptr;
            byte* vtbl_ptr = (byte*)(uintptr_t)maybe_vtbl;
            if (vtbl_ptr < cbase || vtbl_ptr >= cbase + csize)
                continue;

            /* Verify vtable: count code pointers in client.dll */
            uint32_t* vtbl = (uint32_t*)(uintptr_t)maybe_vtbl;
            int code_ptrs = 0;
            for (int v = 0; v < 24; v++) {
                byte* vp = (byte*)(uintptr_t)vtbl[v];
                if (vp >= cbase && vp < cbase + csize)
                    code_ptrs++;
            }

            printf("  B9 at target+%d: obj=%p vtbl=%p code_ptrs=%d\n",
                   j, cand_ptr, vtbl_ptr, code_ptrs);

            if (code_ptrs >= 15) {
                printf("  FOUND CStudioModelRenderer vtable at %p\n", vtbl_ptr);
                printf("  vtable entries:\n");
                for (int v = 0; v < 24; v++)
                    printf("    [%d] = %08X\n", v, vtbl[v]);
                found_smr = (StudioModelRenderer_t*)vtbl_ptr;
                break;
            }
        }

        if (found_smr)
            break;

        /* Also try E8 calls within the target function (nested call) */
        for (int j = 0; j < 48; j++) {
            if (target[j] != 0xE8)
                continue;

            int32_t rel2 = *(int32_t*)(target + j + 1);
            byte* target2 = target + j + 5 + rel2;
            if (target2 < cbase || target2 >= cbase + csize)
                continue;

            printf("  nested E8 at target+%d -> %p\n", j, target2);
            printf("  target2 bytes: ");
            for (int d = 0; d < 64; d++)
                printf("%02X ", target2[d]);
            printf("\n");

            for (int k = 0; k < 64; k++) {
                if (target2[k] != 0xB9)
                    continue;

                uint32_t candidate = *(uint32_t*)(target2 + k + 1);
                byte* cand_ptr = (byte*)(uintptr_t)candidate;
                if (cand_ptr < cbase || cand_ptr >= cbase + csize)
                    continue;

                uint32_t maybe_vtbl = *(uint32_t*)cand_ptr;
                byte* vtbl_ptr = (byte*)(uintptr_t)maybe_vtbl;
                if (vtbl_ptr < cbase || vtbl_ptr >= cbase + csize)
                    continue;

                uint32_t* vtbl = (uint32_t*)(uintptr_t)maybe_vtbl;
                int code_ptrs = 0;
                for (int v = 0; v < 24; v++) {
                    byte* vp = (byte*)(uintptr_t)vtbl[v];
                    if (vp >= cbase && vp < cbase + csize)
                        code_ptrs++;
                }

                printf("  B9 at target2+%d: obj=%p vtbl=%p code_ptrs=%d\n",
                       k, cand_ptr, vtbl_ptr, code_ptrs);

                if (code_ptrs >= 15) {
                    printf("  FOUND CStudioModelRenderer vtable at %p (nested)\n", vtbl_ptr);
                    for (int v = 0; v < 24; v++)
                        printf("    [%d] = %08X\n", v, vtbl[v]);
                    found_smr = (StudioModelRenderer_t*)vtbl_ptr;
                    break;
                }
            }
            if (found_smr)
                break;
        }
        if (found_smr)
            break;
    }

    if (found_smr)
        *out_smr = found_smr;

    return found_studio && found_smr;
}

/*----------------------------------------------------------------------------*/

bool globals_init(void) {
    printf("dod-cheat: globals_init()\n");

    /* Phase A: Module handles */
    hw = (void*)GetModuleHandleA("hw.dll");
    if (!hw) {
        ERR("Can't find hw.dll");
        return false;
    }
    printf("  hw.dll at %p\n", hw);

    client_dll = GetModuleHandleA("client.dll");
    if (!client_dll) {
        ERR("Can't find client.dll");
        return false;
    }
    printf("  client.dll at %p\n", (void*)client_dll);

    /* Phase B: Find engine's cl_clientfunc_t table via data-reference scan */
    printf("dod-cheat: Phase B - finding cl_funcs...\n");
    i_client = find_client_funcs();
    if (!i_client) {
        ERR("Can't find cl_funcs table in hw.dll");
        return false;
    }

    /* Phase C: Find engine function table from client.dll's Initialize */
    printf("dod-cheat: Phase C - finding engine funcs...\n");
    i_engine = find_engine_funcs();
    if (!i_engine) {
        ERR("Can't find engine function table");
        return false;
    }

    /* Phase D: Find pmove pointer location from PM_Move/HUD_PlayerMove */
    printf("dod-cheat: Phase D - finding pmove...\n");
    pp_pmove = find_pmove_ptr();
    if (pp_pmove) {
        i_pmove = *pp_pmove;
        printf("  pp_pmove=%p, i_pmove=%p (may be NULL until game starts)\n",
               (void*)pp_pmove, (void*)i_pmove);
    } else {
        ERR("Can't find pmove pointer (non-fatal, some features disabled)");
    }

    /* Phase E: Find studio interfaces from HUD_GetStudioModelInterface */
    printf("dod-cheat: Phase E - finding studio interfaces...\n");
    engine_studio_api_t* studio_ptr = NULL;
    StudioModelRenderer_t* smr_ptr = NULL;
    if (find_studio_interfaces(&studio_ptr, &smr_ptr)) {
        i_enginestudio = studio_ptr;
        i_studiomodelrenderer = smr_ptr;
    } else {
        ERR("Can't find studio interfaces (chams disabled)");
        /* Non-fatal: chams won't work but ESP/aimbot will */
    }

    /* Validate critical interfaces */
    if (!i_engine || !i_client) {
        ERR("Critical interfaces missing, aborting");
        return false;
    }

    printf("dod-cheat: All interfaces resolved:\n");
    printf("  i_engine = %p\n", (void*)i_engine);
    printf("  i_client = %p\n", (void*)i_client);
    printf("  i_pmove  = %p\n", (void*)i_pmove);
    printf("  i_enginestudio = %p\n", (void*)i_enginestudio);
    printf("  i_studiomodelrenderer = %p\n", (void*)i_studiomodelrenderer);

    if (i_studiomodelrenderer) {
        if (!protect_addr(i_studiomodelrenderer, PAGE_READWRITE)) {
            ERR("Couldn't unprotect address of SMR");
            return false;
        }
    }

    globals_store();

    return true;
}

void globals_store(void) {
    if (i_engine)
        memcpy(&o_engine, i_engine, sizeof(cl_enginefunc_t));
    if (i_client)
        memcpy(&o_client, i_client, sizeof(cl_clientfunc_t));
    if (i_enginestudio)
        memcpy(&o_enginestudio, i_enginestudio, sizeof(engine_studio_api_t));
}

void globals_restore(void) {
    if (i_engine)
        memcpy(i_engine, &o_engine, sizeof(cl_enginefunc_t));
    if (i_client)
        memcpy(i_client, &o_client, sizeof(cl_clientfunc_t));
    if (i_enginestudio)
        memcpy(i_enginestudio, &o_enginestudio, sizeof(engine_studio_api_t));
}
