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

typedef void (__thiscall *RenderFinal_fn)(void* this_ptr);

static void call_StudioRenderFinal(void* this_ptr) {
    void** vtbl = (void**)i_studiomodelrenderer;
    RenderFinal_fn fn = (RenderFinal_fn)vtbl[20];
    fn(this_ptr);
}

/*==========================================================================
 * Inline hook on opengl32!glColor4f via trampoline.
 * One-time patch at init (single VirtualProtect), no per-call overhead.
 * The trampoline holds the original first bytes + jmp back.
 *==========================================================================*/

typedef void (APIENTRY *glColor4f_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
static glColor4f_fn trampoline_glColor4f = NULL;

static bool chams_color_active = false;
static float chams_r = 1, chams_g = 1, chams_b = 1;

/* We need a naked-style hook to avoid stack issues.
 * Since MSVC/MinGW x86 doesn't reliably support naked+C code,
 * we use a normal APIENTRY function. The trampoline approach
 * means this is called via jmp, not unhook/rehook. */
static void APIENTRY hook_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    if (chams_color_active) {
        trampoline_glColor4f(chams_r, chams_g, chams_b, 1.0f);
    } else {
        trampoline_glColor4f(r, g, b, a);
    }
}

/* Also hook glColor3f — GoldSrc may use this instead */
typedef void (APIENTRY *glColor3f_fn)(GLfloat, GLfloat, GLfloat);
static glColor3f_fn trampoline_glColor3f = NULL;

static void APIENTRY hook_glColor3f(GLfloat r, GLfloat g, GLfloat b) {
    if (chams_color_active) {
        trampoline_glColor3f(chams_r, chams_g, chams_b);
    } else {
        trampoline_glColor3f(r, g, b);
    }
}

typedef void (APIENTRY *glColor4ub_fn)(GLubyte, GLubyte, GLubyte, GLubyte);
static glColor4ub_fn trampoline_glColor4ub = NULL;

static void APIENTRY hook_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    if (chams_color_active) {
        trampoline_glColor4ub(
            (GLubyte)(chams_r * 255),
            (GLubyte)(chams_g * 255),
            (GLubyte)(chams_b * 255), 255);
    } else {
        trampoline_glColor4ub(r, g, b, a);
    }
}

/* Build a trampoline: copy first N bytes of target, append jmp back.
 * Returns executable trampoline, or NULL on failure.
 * stolen_bytes must be >= 5 and must not split an instruction. */
static void* make_trampoline(void* target, int stolen_bytes, void* hook_fn) {
    /* Allocate RWX buffer for trampoline */
    byte* tramp = (byte*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    if (!tramp) return NULL;

    /* Copy stolen bytes */
    memcpy(tramp, target, stolen_bytes);

    /* Append jmp back to target + stolen_bytes */
    tramp[stolen_bytes] = 0xE9;
    *(int32_t*)(tramp + stolen_bytes + 1) =
        (int32_t)((byte*)target + stolen_bytes - (tramp + stolen_bytes + 5));

    /* Patch target: jmp to hook_fn */
    DWORD old_prot;
    VirtualProtect(target, stolen_bytes, PAGE_EXECUTE_READWRITE, &old_prot);

    ((byte*)target)[0] = 0xE9;
    *(int32_t*)((byte*)target + 1) =
        (int32_t)((byte*)hook_fn - ((byte*)target + 5));

    /* NOP remaining stolen bytes */
    for (int i = 5; i < stolen_bytes; i++)
        ((byte*)target)[i] = 0x90;

    VirtualProtect(target, stolen_bytes, old_prot, &old_prot);

    return tramp;
}

static bool hooks_installed = false;

static void install_gl_hooks(void) {
    if (hooks_installed) return;

    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (!gl) {
        printf("  chams: opengl32.dll not found!\n");
        return;
    }

    /* glColor4f: typically starts with mov edi,edi / push ebp / mov ebp,esp
     * or push ebp / mov ebp,esp — 5+ bytes we can safely steal.
     * We'll examine the first bytes to determine safe steal count. */
    void* p4f = (void*)GetProcAddress(gl, "glColor4f");
    void* p3f = (void*)GetProcAddress(gl, "glColor3f");
    void* p4ub = (void*)GetProcAddress(gl, "glColor4ub");

    printf("  chams: glColor4f=%p glColor3f=%p glColor4ub=%p\n", p4f, p3f, p4ub);

    if (p4f) {
        byte* b = (byte*)p4f;
        printf("  chams: glColor4f bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
               b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]);

        /* Determine stolen bytes count based on prologue.
         * Common prologues:
         *   8B FF 55 8B EC  (mov edi,edi / push ebp / mov ebp,esp) = 5 bytes
         *   55 8B EC ...     (push ebp / mov ebp,esp / ...) = need to find 5+ boundary
         *   sub esp,N / push regs — varies */
        int steal = 0;
        if (b[0] == 0x8B && b[1] == 0xFF && b[2] == 0x55 && b[3] == 0x8B && b[4] == 0xEC) {
            steal = 5; /* mov edi,edi / push ebp / mov ebp,esp */
        } else if (b[0] == 0x55 && b[1] == 0x8B && b[2] == 0xEC) {
            /* push ebp / mov ebp,esp — 3 bytes, need more */
            if (b[3] == 0x83 && b[4] == 0xEC) steal = 6; /* sub esp, imm8 */
            else if (b[3] == 0x56) steal = 7; /* push esi + next */
            else steal = 7; /* safe guess: take 7 */
        } else {
            steal = 7; /* fallback */
        }

        printf("  chams: glColor4f stealing %d bytes\n", steal);
        trampoline_glColor4f = (glColor4f_fn)make_trampoline(p4f, steal, (void*)hook_glColor4f);
        if (trampoline_glColor4f)
            printf("  chams: glColor4f hooked (trampoline=%p)\n", (void*)trampoline_glColor4f);
    }

    if (p3f) {
        byte* b = (byte*)p3f;
        printf("  chams: glColor3f bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
               b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]);
        int steal = 0;
        if (b[0] == 0x8B && b[1] == 0xFF) steal = 5;
        else if (b[0] == 0x55 && b[1] == 0x8B && b[2] == 0xEC) steal = 7;
        else steal = 7;
        trampoline_glColor3f = (glColor3f_fn)make_trampoline(p3f, steal, (void*)hook_glColor3f);
        if (trampoline_glColor3f)
            printf("  chams: glColor3f hooked (trampoline=%p)\n", (void*)trampoline_glColor3f);
    }

    if (p4ub) {
        byte* b = (byte*)p4ub;
        printf("  chams: glColor4ub bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
               b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]);
        int steal = 0;
        if (b[0] == 0x8B && b[1] == 0xFF) steal = 5;
        else if (b[0] == 0x55 && b[1] == 0x8B && b[2] == 0xEC) steal = 7;
        else steal = 7;
        trampoline_glColor4ub = (glColor4ub_fn)make_trampoline(p4ub, steal, (void*)hook_glColor4ub);
        if (trampoline_glColor4ub)
            printf("  chams: glColor4ub hooked (trampoline=%p)\n", (void*)trampoline_glColor4ub);
    }

    hooks_installed = true;
}

void chams_init(void) {
    install_gl_hooks();
}

void chams_restore(void) {
    /* Restoring inline hooks would require unpatching.
     * For now, just disable the color override. */
    chams_color_active = false;
}

void chams_unhook_hw(void) {
    chams_color_active = false;
}

/*==========================================================================*/

static void render_colored(void* this_ptr, float r, float g, float b) {
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT);
    glDisable(GL_TEXTURE_2D);

    chams_r = r;
    chams_g = g;
    chams_b = b;
    chams_color_active = true;

    call_StudioRenderFinal(this_ptr);

    chams_color_active = false;
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
