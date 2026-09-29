#include <windows.h>
#include <stdio.h>
#include <math.h>

#include "../include/sdk.h"
#include "../include/globals.h"
#include "../include/cvars.h"
#include "../include/detour.h"
#include "features.h"

/* The standard GoldSrc/Half-Life shared-random seed table (256 entries).
 * Identical across HL/CS/DoD because it lives in the shared SDK code.
 * We embed it to (a) confirm DoD's client.dll uses it and (b) replicate
 * UTIL_SharedRandomFloat so we can predict the server's bullet spread. */
static const unsigned int hl_seed_table[256] = {
    28985, 27138, 26457, 9451, 17764, 10909, 28790, 8716, 6361, 4853, 17798, 21977, 19643, 20662, 10834, 20103,
    27067, 28634, 18623, 25849, 8576, 26234, 23887, 18228, 32587, 4836, 3306, 1811, 3035, 24559, 18399, 315,
    26766, 907, 24102, 12370, 9674, 2972, 10472, 16492, 22683, 11529, 27968, 30406, 13213, 2319, 23620, 16823,
    10013, 23772, 21567, 1251, 19579, 20313, 18241, 30130, 8402, 20807, 27354, 7169, 21211, 17293, 5410, 19223,
    10255, 22480, 27388, 9946, 15628, 24389, 17308, 2370, 9530, 31683, 25927, 23567, 11694, 26397, 32602, 15031,
    18255, 17582, 1422, 28835, 23607, 12597, 20602, 10138, 5212, 1252, 10074, 23166, 19823, 31667, 5902, 24630,
    18948, 14330, 14950, 8939, 23540, 21311, 22428, 22391, 3583, 29004, 30498, 18714, 4278, 2437, 22430, 3439,
    28313, 23161, 25396, 13471, 19324, 15287, 2563, 18901, 13103, 16867, 9714, 14322, 15197, 26889, 19372, 26241,
    31925, 14640, 11497, 8941, 10056, 6451, 28656, 10737, 13874, 17356, 8281, 25937, 1661, 4850, 7448, 12744,
    21826, 5477, 10167, 16705, 26897, 8839, 30947, 27978, 27283, 24685, 32298, 3525, 12398, 28726, 9475, 10208,
    617, 13467, 22287, 2376, 6097, 26312, 2974, 9114, 21787, 28010, 4725, 15387, 3274, 10762, 31695, 17320,
    18324, 12441, 16801, 27376, 22464, 7500, 5666, 18144, 15314, 31914, 31627, 6495, 5226, 31203, 2331, 4668,
    12650, 18275, 351, 7268, 31319, 30119, 7600, 2905, 13826, 11343, 13053, 15583, 30055, 31093, 5067, 761,
    9685, 11070, 21369, 27155, 3663, 26542, 20169, 12161, 15411, 30401, 7580, 31784, 8985, 29367, 20989, 14203,
    29694, 21167, 10337, 1706, 28578, 887, 3373, 19477, 14382, 675, 7033, 15111, 26138, 12252, 30996, 21409,
    25678, 18555, 13256, 23316, 22407, 16727, 991, 9236, 5373, 29402, 6117, 15241, 27715, 19291, 19888, 19847
};

/*----------------------------------------------------------------------------*/
/* Faithful re-implementation of the GoldSrc shared PRNG (from HL SDK util.cpp).
 * We keep our OWN seed state (ns_glSeed) instead of calling the game's
 * UTIL_SharedRandomFloat: the game's version writes the global glSeed via
 * U_Srand, so calling it out-of-band mid-frame would corrupt the engine's own
 * sequence. With a private state we can freely predict any (future) seed's
 * spread while leaving the game untouched. Verified identical in-range output
 * to the game via the [ns-rt] capture hook. */

static unsigned int ns_glSeed = 0;

static unsigned int ns_U_Random(void) {
    ns_glSeed *= 69069;
    ns_glSeed += hl_seed_table[ns_glSeed & 0xff];
    return (++ns_glSeed & 0x0fffffff);
}

static void ns_U_Srand(unsigned int seed) {
    ns_glSeed = hl_seed_table[seed & 0xff];
}

/* float UTIL_SharedRandomFloat(seed, low, high) — bit-exact port.
 * Note the SDK quirk: `range` is an unsigned int, so (high-low) is truncated
 * to an integer. Spread always calls this with low/high = -0.5/0.5 (range 1),
 * and the per-weapon cone is applied by the caller afterwards. */
static float ns_SharedRandomFloat(unsigned int seed, float low, float high) {
    ns_U_Srand((unsigned int)((int)seed + *(int*)&low + *(int*)&high));
    ns_U_Random();
    ns_U_Random();
    unsigned int range = (unsigned int)(high - low);
    if (range == 0) return low;
    unsigned int tensixrand = ns_U_Random() & 65535;
    float offset = (float)tensixrand / 65536.0f;
    return low + offset * range;
}

/* int UTIL_SharedRandomLong(seed, low, high) — bit-exact port. */
static int ns_SharedRandomLong(unsigned int seed, int low, int high) {
    ns_U_Srand((unsigned int)((int)seed + low + high));
    unsigned int range = (unsigned int)(high - low + 1);
    if ((range - 1) == 0) return low;
    unsigned int rnum = ns_U_Random();
    unsigned int offset = rnum % range;
    return low + (int)offset;
}

/*----------------------------------------------------------------------------*/
/* Reconnaissance: statically confirm DoD's client.dll uses the standard HL
 * shared PRNG and locate the machinery so we can later hook it to extract the
 * per-weapon spread cone and predict/compensate bullet spread. No shooting
 * required — everything below is derived by scanning the loaded image once.
 * Findings are printed to stdout (redirected to C:\lambdahook-log.txt). */

typedef unsigned char byte;

/* Section/range info for the loaded module. */
static byte* g_img_base = NULL;
static size_t g_img_size = 0;
static byte* g_text_beg = NULL;
static byte* g_text_end = NULL;

static void ns_resolve_ranges(void) {
    g_img_base = (byte*)client_dll;
    if (!g_img_base) return;

    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_img_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(g_img_base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    g_img_size = nt->OptionalHeader.SizeOfImage;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        byte* va = g_img_base + sec[i].VirtualAddress;
        size_t sz = sec[i].Misc.VirtualSize;
        if (sz < sec[i].SizeOfRawData) sz = sec[i].SizeOfRawData;
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            /* first (usually only) executable section = .text */
            if (!g_text_beg) { g_text_beg = va; g_text_end = va + sz; }
        }
    }
}

/* Find the start of the function containing `addr`. DoD's client.dll doesn't
 * always use double-int3 padding, so we combine heuristics walking backward:
 *   - stop just after 0xCC int3 padding, or after a ret (C3 / C2 imm16) that is
 *     followed by alignment padding (CC/90), returning the next real byte;
 *   - otherwise remember the nearest MSVC prologue (8B FF 55 8B EC hotpatch, or
 *     55 8B EC) and fall back to it. */
static byte* ns_func_start(byte* addr) {
    byte* limit = addr - 3072;
    if (limit < g_text_beg) limit = g_text_beg;
    byte* prologue = NULL;
    for (byte* p = addr; p > limit; p--) {
        /* int3 padding boundary */
        if (p[-1] == 0xCC) {
            /* skip any run of CC/90 alignment forward to the real start */
            byte* q = p;
            while (q < g_text_end && (*q == 0xCC || *q == 0x90)) q++;
            return q;
        }
        /* ret near (C3) followed by padding => next func starts after padding */
        if (p[-1] == 0xC3 && p < g_text_end && (*p == 0xCC || *p == 0x90)) {
            byte* q = p;
            while (q < g_text_end && (*q == 0xCC || *q == 0x90)) q++;
            return q;
        }
        /* remember the nearest (first encountered going backward) prologue */
        if (!prologue) {
            if (p[0] == 0x8B && p[1] == 0xFF && p[2] == 0x55 && p[3] == 0x8B && p[4] == 0xEC)
                prologue = p;
            else if (p[0] == 0x55 && p[1] == 0x8B && p[2] == 0xEC)
                prologue = p;
        }
    }
    return prologue;
}

/* Scan .text for CALL rel32 (E8) sites whose target == `callee`. Reports up to
 * `max` of them with the enclosing function start. */
static void ns_report_callers(const char* label, byte* callee, int max) __attribute__((unused));
static void ns_report_callers(const char* label, byte* callee, int max) {
    if (!callee) return;
    int found = 0;
    for (byte* p = g_text_beg; p + 5 <= g_text_end && found < max; p++) {
        if (*p != 0xE8) continue;
        int rel = *(int*)(p + 1);
        byte* tgt = p + 5 + rel;
        if (tgt != callee) continue;
        byte* fs = ns_func_start(p);
        printf("[ns]   %s call-site: client+0x%X  (in func client+0x%X)\n",
               label, (unsigned)(p - g_img_base),
               fs ? (unsigned)(fs - g_img_base) : 0);
        found++;
    }
    printf("[ns]   %s total call-sites: %d\n", label, found);
    fflush(stdout);
}

static void ns_hexdump(const char* label, byte* p, int n) {
    printf("[ns]   %s @client+0x%X:", label, (unsigned)(p - g_img_base));
    for (int i = 0; i < n; i++) printf(" %02X", p[i]);
    printf("\n");
    fflush(stdout);
}

/* Count E8 rel32 CALL sites targeting `target` inside the function body that
 * begins at func_start (stops at CC padding or after scan_len bytes). Used to
 * tell UTIL_SharedRandomFloat (calls U_Random ~3x) from UTIL_SharedRandomLong
 * (calls it once). */
static int ns_count_calls_to(byte* func_start, byte* target, int scan_len) {
    if (!func_start || !target) return -1;
    int n = 0;
    for (byte* p = func_start; p < func_start + scan_len && p + 5 <= g_text_end; p++) {
        /* stop at end-of-function: ret (C3) followed by int3/nop padding */
        if (p[0] == 0xC3 && (p[1] == 0xCC || p[1] == 0x90)) break;
        if (p[0] == 0xCC && (p[1] == 0xCC || p[1] == 0x90)) break;
        if (*p != 0xE8) continue;
        int rel = *(int*)(p + 1);
        if (p + 5 + rel == target) n++;
    }
    return n;
}

/*----------------------------------------------------------------------------*/
/* Runtime capture: once UTIL_SharedRandomFloat is located we detour it and log
 * (seed, low, high, return-value, caller) for the first N calls. Firing a
 * machine gun makes the fire code call it; this reveals DoD's exact seed
 * arithmetic (offset pattern) and whether the spread cone is baked into the
 * low/high args or applied separately in the caller — the last unknown before
 * we can predict and compensate spread. */

DECL_DETOUR_TYPE(float, ns_sharedfloat, unsigned int, float, float)
static detour_data_t g_sf_detour;
static bool g_sf_hooked = false;
static byte* g_sharedfloat = NULL;
static int g_sf_log_count = 0;
#define NS_SF_LOG_MAX 240

/* Set from h_HUD_PostRunCmd right before the game runs weapon prediction, so we
 * can compare the spread base-seed hitting UTIL_SharedRandomFloat against the
 * engine's shared random_seed (the value that is identical on client & server).
 * If base == random_seed (+ small offset), we can predict spread at cmd-build
 * time and counter-rotate viewangles. */
unsigned int g_ns_random_seed = 0;

static float hk_sharedfloat(unsigned int seed, float low, float high) {    void* ret = __builtin_return_address(0);
    float r = 0.0f;
    GET_ORIGINAL(g_sf_detour, r, ns_sharedfloat, seed, low, high);
    if (g_sf_log_count < NS_SF_LOG_MAX) {
        g_sf_log_count++;
        printf("[ns-rt] SF #%d seed=%u (rs=%u, seed-rs=%d) low=%.4f high=%.4f -> %.5f caller=client+0x%X\n",
               g_sf_log_count, seed, g_ns_random_seed,
               (int)(seed - g_ns_random_seed), low, high, r,
               (unsigned)((byte*)ret - g_img_base));
        fflush(stdout);
    }
    return r;
}

void nospread_prng_selftest(void);

/*----------------------------------------------------------------------------*/
/* Spread compensation.
 *
 * The spread generator ComputeSpread(out, ..., cone, ..., flag, seed) @0x12CF0
 * produces out=(x*cone, y*cone, 0) with
 *   x = SF(seed+0)+SF(seed+1),  y = SF(seed+2)+SF(seed+3)
 * and is run identically on client and (authoritatively) server from the shared
 * `seed` == the command's random_seed. We can't touch the server's copy, so we
 * counter-rotate cmd->viewangles in CL_CreateMove BEFORE the command is sent, so
 * that after the server re-adds the same spread the net direction is our true aim.
 *
 * Two live-measured facts make this possible:
 *   - base seed == HUD_PostRunCmd's random_seed exactly, and it increments by +1
 *     per command (rf==1). So the command being built in CL_CreateMove will use
 *     seed = (last rf==1 seed) + 1.
 *   - our PRNG port reproduces the game's SF outputs bit-exactly.
 *
 * `cone` is a per-weapon/stance parameter passed into ComputeSpread; we capture
 * the latest value live by hooking ComputeSpread. */

typedef void (__attribute__((stdcall)) * ns_cs_t)(
    void*, int, int, int, int, int, int, float, int, int, int, int, int, unsigned int);

static detour_data_t g_cs_detour;
static bool  g_cs_hooked = false;
static byte* g_computespread = NULL;

float        g_ns_last_cone     = 0.0f;  /* latest cone seen by ComputeSpread   */
unsigned int g_ns_last_cs_seed  = 0;     /* last seed that hit ComputeSpread     */
bool         g_ns_has_cs_seed   = false; /* true once we've seen at least 1 shot */
unsigned int g_ns_predicted_seed = 0;    /* what CL_CreateMove predicted for now */
static int   g_cs_log = 0;

/* The compensation offsets computed inside CL_CreateMove for this frame.
 * ComputeSpread will read these and zero them out after use. */
static float g_ns_comp_dyaw   = 0.0f;
static float g_ns_comp_dpitch = 0.0f;
static bool  g_ns_comp_pending = false;

static void __attribute__((stdcall)) hk_computespread(
    void* out, int a1, int a2, int a3, int a4, int a5, int a6,
    float cone, int a8, int a9, int a10, int a11, int flag, unsigned int seed) {
    g_ns_last_cone = cone;

    if (!g_ns_has_cs_seed || seed != g_ns_last_cs_seed) {
        g_ns_last_cs_seed = seed;
        g_ns_has_cs_seed  = true;
    }

    detour_del(&g_cs_detour);
    ((ns_cs_t)g_cs_detour.orig)(out, a1, a2, a3, a4, a5, a6,
                                cone, a8, a9, a10, a11, flag, seed);
    detour_add(&g_cs_detour);

    float* o = (float*)out;

    if (g_cs_log < 80) {
        g_cs_log++;
        printf("[ns-cs] #%d seed=%u cone=%.5f flag=%d out=(%.5f,%.5f,%.5f)\n",
               g_cs_log, seed, cone, flag, o[0], o[1], o[2]);
        fflush(stdout);
    }

    /* Nospread: zero out the spread vector so the bullet goes straight. */
    if (cv_nospread && cv_nospread->value != 0.0f && flag != 0) {
        static int zlog = 0;
        if (zlog < 40) {
            zlog++;
            printf("[ns-zero] #%d zeroed spread (was %.5f,%.5f)\n", zlog, o[0], o[1]);
            fflush(stdout);
        }
        o[0] = 0.0f;
        o[1] = 0.0f;
        o[2] = 0.0f;
    }
}

/* ComputeSpread is the (single) function that CALLs UTIL_SharedRandomFloat; it
 * does so 4x. Find it by the enclosing-function of the E8 call-sites to it. */
static byte* ns_find_computespread(void) {
    if (!g_sharedfloat) return NULL;
    byte* cand[16];
    int   cnt[16];
    int   nc = 0;
    for (byte* p = g_text_beg; p + 5 <= g_text_end; p++) {
        if (*p != 0xE8) continue;
        int rel = *(int*)(p + 1);
        if (p + 5 + rel != g_sharedfloat) continue;
        byte* fs = ns_func_start(p);
        if (!fs) continue;
        int i;
        for (i = 0; i < nc; i++)
            if (cand[i] == fs) { cnt[i]++; break; }
        if (i == nc && nc < 16) { cand[nc] = fs; cnt[nc] = 1; nc++; }
    }
    byte* best = NULL;
    int   bestcnt = 0;
    for (int i = 0; i < nc; i++)
        if (cnt[i] > bestcnt) { bestcnt = cnt[i]; best = cand[i]; }
    printf("[ns] ComputeSpread candidate client+0x%X (SF call-sites=%d)\n",
           best ? (unsigned)(best - g_img_base) : 0, bestcnt);
    return best;
}

static void ns_install_computespread_hook(void) {
    g_computespread = ns_find_computespread();
    if (!g_computespread) {
        printf("[ns] ComputeSpread not found; cone capture unavailable\n");
        return;
    }
    detour_init(&g_cs_detour, (void*)g_computespread, (void*)hk_computespread);
    if (detour_add(&g_cs_detour)) {
        g_cs_hooked = true;
        printf("[ns] hook INSTALLED on ComputeSpread @client+0x%X\n",
               (unsigned)(g_computespread - g_img_base));
    } else {
        printf("[ns] detour_add FAILED on ComputeSpread\n");
    }
    fflush(stdout);
}

/* Called from h_HUD_PostRunCmd(runfuncs==1) — kept for interface compat. */
void nospread_note_fire_seed(unsigned int random_seed) {
    (void)random_seed;
}

/* Called from h_CL_CreateMove AFTER the original ran (cmd->viewangles = true aim).
 * Predicts this command's spread and counter-rotates viewangles so the server's
 * re-added spread cancels out. Silent: only cmd->viewangles is touched, not the
 * local view (cl.viewangles), so the crosshair does not move.
 *
 * Seed prediction: the ComputeSpread hook records the actual firing seed.
 * Live data shows seeds +8 apart between shots. So next = last_cs_seed + 8. */
void nospread_on_createmove(usercmd_t* cmd) {
    unsigned int seed = g_ns_last_cs_seed + 8;
    g_ns_predicted_seed = seed;

    if (!cv_nospread || cv_nospread->value == 0.0f) return;
    if (!g_ns_has_cs_seed) return;

    /* The engine overwrites cmd->buttons AFTER CL_CreateMove, so IN_ATTACK is
     * never set here. Detect firing via mouse button + weapon ready. */
    bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (!lmb) return;
    if (g_flNextPrimaryAttack > 0.0f) return; /* weapon not ready yet */

    /* Only compensate ONCE per seed — avoid applying the same correction every
     * frame while LMB is held between shots. */
    static unsigned int last_compensated_seed = 0;
    if (seed == last_compensated_seed) return;
    last_compensated_seed = seed;

    float cone = g_ns_last_cone;
    if (cone <= 0.0f) return;

    float x = ns_SharedRandomFloat(seed + 0, -0.5f, 0.5f) +
              ns_SharedRandomFloat(seed + 1, -0.5f, 0.5f);
    float y = ns_SharedRandomFloat(seed + 2, -0.5f, 0.5f) +
              ns_SharedRandomFloat(seed + 3, -0.5f, 0.5f);

    /* Spread offsets (x*cone, y*cone) are direction-vector components, not angles.
     * Convert to degrees: atan2 of the offset over unit forward (1.0). */
    const float RAD2DEG = 57.2957795f;
    float dyaw   = atanf(x * cone) * RAD2DEG;
    float dpitch = atanf(y * cone) * RAD2DEG;

    cmd->viewangles.y -= dyaw;
    cmd->viewangles.x += dpitch;

    static int comp_log = 0;
    if (comp_log < 80) {
        comp_log++;
        printf("[ns-comp] #%d seed=%u cone=%.5f x=%.4f y=%.4f dyaw=%.4f dpitch=%.4f\n",
               comp_log, seed, cone, x, y, dyaw, dpitch);
        fflush(stdout);
    }
}

/*----------------------------------------------------------------------------*/

void nospread_recon(void) {
    ns_resolve_ranges();
    printf("\n[ns] ===== nospread recon =====\n");
    if (!g_img_base || !g_text_beg) {
        printf("[ns] FAILED to resolve client.dll ranges (base=%p)\n", g_img_base);
        fflush(stdout);
        return;
    }
    printf("[ns] client.dll base=%p size=0x%X  .text=0x%X..0x%X\n",
           g_img_base, (unsigned)g_img_size,
           (unsigned)(g_text_beg - g_img_base), (unsigned)(g_text_end - g_img_base));

    /* --- 1. Confirm & locate seed_table (match first 16 entries) --- */
    byte* stbl = NULL;
    {
        const byte* pat = (const byte*)hl_seed_table;
        const int patlen = 16 * sizeof(unsigned int); /* 64 bytes */
        for (byte* p = g_img_base; p + patlen <= g_img_base + g_img_size; p += 4) {
            if (memcmp(p, pat, patlen) == 0) { stbl = p; break; }
        }
    }
    if (stbl) {
        printf("[ns] seed_table FOUND at client+0x%X (DoD uses standard HL PRNG)\n",
               (unsigned)(stbl - g_img_base));
    } else {
        printf("[ns] seed_table NOT FOUND (unexpected; DoD may inline or relocate it)\n");
    }
    fflush(stdout);

    /* --- 2+3. Identify U_Random and U_Srand from the (usually two) functions
     *   that reference seed_table. DoD strength-reduces the *69069 multiply into
     *   lea chains, so the 69069 immediate is absent — instead we classify
     *   structurally: U_Srand starts by loading & masking its first stack arg
     *   (mov eax,[esp+4]; and eax,0xFF  =>  8B 44 24 04 25 FF 00 00 00), while
     *   U_Random (void) hashes the glSeed global. The other referencer is
     *   U_Random. --- */
    byte* urandom = NULL;
    byte* usrand = NULL;
    if (stbl) {
        byte* funcs[8];
        int nf = 0;
        for (byte* p = g_text_beg; p + 4 <= g_text_end; p++) {
            if (*(byte**)p != stbl) continue;
            byte* fs = ns_func_start(p);
            if (!fs) continue;
            bool dup = false;
            for (int i = 0; i < nf; i++) if (funcs[i] == fs) { dup = true; break; }
            if (dup) continue;
            if (nf < 8) funcs[nf++] = fs;
            printf("[ns] seed_table ref at client+0x%X (func client+0x%X)\n",
                   (unsigned)(p - g_img_base), (unsigned)(fs - g_img_base));
            ns_hexdump("  func start", fs, 20);
        }
        for (int i = 0; i < nf; i++) {
            byte* f = funcs[i];
            /* mov eax,[esp+4] ; and eax,0xFF  => U_Srand */
            if (!usrand && f[0] == 0x8B && f[1] == 0x44 && f[2] == 0x24 && f[3] == 0x04 &&
                f[4] == 0x25 && f[5] == 0xFF)
                usrand = f;
            else if (!urandom)
                urandom = f;
        }
        /* fallbacks if the signature shifted */
        if (!usrand && nf == 2) usrand = (funcs[0] == urandom) ? funcs[1] : funcs[0];
        if (!urandom && nf == 2) urandom = (funcs[0] == usrand) ? funcs[1] : funcs[0];
    }
    printf("[ns] U_Random=client+0x%X  U_Srand=client+0x%X\n",
           urandom ? (unsigned)(urandom - g_img_base) : 0,
           usrand ? (unsigned)(usrand - g_img_base) : 0);
    fflush(stdout);

    /* --- 4. Callers of U_Srand = UTIL_SharedRandomFloat + UTIL_SharedRandomLong.
     *        Classify each by how many times it calls U_Random (Float ~3, Long 1)
     *        and install a runtime logging detour on the Float variant. --- */
    if (usrand) {
        printf("[ns] U_Srand start=client+0x%X ; classifying callers (Shared* funcs):\n",
               (unsigned)(usrand - g_img_base));
        byte* callers[32];
        int urcalls[32];
        int ncall = 0;
        for (byte* p = g_text_beg; p + 5 <= g_text_end && ncall < 32; p++) {
            if (*p != 0xE8) continue;
            int rel = *(int*)(p + 1);
            if (p + 5 + rel != usrand) continue;
            byte* fs = ns_func_start(p);
            if (!fs) continue;
            bool dup = false;
            for (int i = 0; i < ncall; i++) if (callers[i] == fs) { dup = true; break; }
            if (dup) continue;
            int urc = ns_count_calls_to(fs, urandom, 512);
            const char* kind = (urc >= 2) ? "UTIL_SharedRandomFloat"
                             : (urc == 1) ? "UTIL_SharedRandomLong" : "?";
            printf("[ns]   Shared* func client+0x%X  U_Random-calls=%d  -> %s\n",
                   (unsigned)(fs - g_img_base), urc, kind);
            ns_hexdump("    body", fs, 24);
            callers[ncall] = fs;
            urcalls[ncall] = urc;
            ncall++;
        }
        /* pick Float: the caller with the most U_Random calls */
        int best = -1;
        for (int i = 0; i < ncall; i++)
            if (best < 0 || urcalls[i] > urcalls[best]) best = i;
        if (best >= 0 && urcalls[best] >= 2) g_sharedfloat = callers[best];
        fflush(stdout);
    } else {
        printf("[ns] U_Srand not isolated; cannot enumerate Shared* funcs this pass\n");
        fflush(stdout);
    }

    /* --- 5. Install runtime capture hook on UTIL_SharedRandomFloat --- */
    if (g_sharedfloat) {
        detour_init(&g_sf_detour, (void*)g_sharedfloat, (void*)hk_sharedfloat);
        if (detour_add(&g_sf_detour)) {
            g_sf_hooked = true;
            printf("[ns] runtime hook INSTALLED on UTIL_SharedRandomFloat @client+0x%X\n",
                   (unsigned)(g_sharedfloat - g_img_base));
            printf("[ns] >>> now FIRE a machine gun a few bursts, then send me the log <<<\n");
        } else {
            printf("[ns] detour_add FAILED on UTIL_SharedRandomFloat\n");
        }
    } else {
        printf("[ns] UTIL_SharedRandomFloat not identified; no runtime hook installed\n");
    }
    fflush(stdout);

    printf("[ns] ===== recon done =====\n\n");
    fflush(stdout);
    ns_install_computespread_hook();
    nospread_prng_selftest();
}

/* Emit reference PRNG outputs so that, once the [ns-rt] capture shows the
 * game's own (seed, low, high) -> result, we can confirm our private port is
 * bit-identical (and thus safe to use for prediction). */
void nospread_prng_selftest(void) {
    printf("[ns] PRNG self-test (our private port):\n");
    for (unsigned s = 0; s < 4; s++) {
        float a = ns_SharedRandomFloat(s, -0.5f, 0.5f);
        float b = ns_SharedRandomFloat(s + 1, -0.5f, 0.5f);
        printf("[ns]   SharedFloat(seed=%u,-0.5,0.5)=%.6f  seed=%u -> %.6f\n",
               s, a, s + 1, b);
    }
    printf("[ns]   SharedLong(seed=0,0,100)=%d  (seed=1)=%d\n",
           ns_SharedRandomLong(0, 0, 100), ns_SharedRandomLong(1, 0, 100));
    fflush(stdout);
}
