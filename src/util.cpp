#include <cstdio>
#include <cstring>
#include <cmath>
#include <windows.h>
#include <gl/gl.h>

#include "include/util.h"
#include "include/sdk.h"
#include "include/globals.h"

/*----------------------------------------------------------------------------*/

void engine_draw_text(int x, int y, char* s, rgb_t c) {
    /* Convert to 0..1 range */
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;

    i_engine->pfnDrawSetTextColor(r, g, b);
    i_engine->pfnDrawConsoleString(x, y, s);
}

void draw_tracer(vec3_t start, vec3_t end, rgb_t c, float a, float w,
                 float time) {
    static const char* MDL_STR = "sprites/laserbeam.spr";
    static int beam_idx = i_engine->pEventAPI->EV_FindModelIndex(MDL_STR);

    float r = c.r / 255.f;
    float g = c.g / 255.f;
    float b = c.b / 255.f;

    i_engine->pEfxAPI
      ->R_BeamPoints(start, end, beam_idx, time, w, 0, a, 0, 0, 0, r, g, b);
}

void gl_drawbox(int x, int y, int w, int h, rgb_t c) {
    /* Line width */
    const int lw = 1;

    /*
     *     1
     *   +----+
     * 2 |    | 3
     *   |    |
     *   +----+
     *     4
     */
    gl_drawline(x, y, x + w, y, lw, c);
    gl_drawline(x, y, x, y + h, lw, c);
    gl_drawline(x + w, y, x + w, y + h, lw, c);
    gl_drawline(x, y + h, x + w, y + h, lw, c);
}

void gl_drawline(int x0, int y0, int x1, int y1, float w, rgb_t col) {
    const int alpha = 255;

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(col.r, col.g, col.b, alpha); /* Set colors + alpha */
    glLineWidth(w);                         /* Set line width */
    glBegin(GL_LINES);                      /* Interpret vertices as lines */
    glVertex2i(x0, y0);                     /* Start */
    glVertex2i(x1, y1);                     /* End */
    glEnd();                                /* Stop glBegin, end line mode */
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/

static bool get_section_bounds(void* module, const char* name, byte** start, size_t* size) {
    byte* base = (byte*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (strncmp((const char*)sec[i].Name, name, 8) == 0) {
            *start = base + sec[i].VirtualAddress;
            *size = sec[i].Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

void* find_pattern(void* module, const byte* pattern, const char* mask, size_t len) {
    byte* start = NULL;
    size_t size = 0;

    if (!get_section_bounds(module, ".text", &start, &size))
        return NULL;

    for (size_t i = 0; i <= size - len; i++) {
        bool found = true;
        for (size_t j = 0; j < len; j++) {
            if (mask[j] == 'x' && start[i + j] != pattern[j]) {
                found = false;
                break;
            }
        }
        if (found)
            return (void*)(start + i);
    }
    return NULL;
}

void* find_data_ref(void* module, void* value) {
    byte* base = (byte*)module;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uint32_t needle = (uint32_t)(uintptr_t)value;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        DWORD chars = sec[i].Characteristics;
        /* Scan writable or initialized data sections */
        if (!(chars & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA)))
            continue;

        byte* start = base + sec[i].VirtualAddress;
        size_t size = sec[i].Misc.VirtualSize;

        for (size_t off = 0; off + 4 <= size; off += 4) {
            if (*(uint32_t*)(start + off) == needle)
                return (void*)(start + off);
        }
    }
    return NULL;
}

/*----------------------------------------------------------------------------*/

bool protect_addr(void* ptr, int new_flags) {
    DWORD old_protect;
    /* On Windows we always use PAGE_EXECUTE_READWRITE for full access.
     * The new_flags parameter is kept for API compatibility but ignored. */
    DWORD win_flags = PAGE_EXECUTE_READWRITE;

    if (VirtualProtect(ptr, 4096, win_flags, &old_protect) == 0) {
        ERR("Error protecting %p", ptr);
        return false;
    }

    return true;
}
