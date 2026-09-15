#ifndef SDK_H_
#define SDK_H_

/*
 * Self-contained GoldSrc SDK types for Day of Defeat cheat.
 * Based on the official Half-Life 1 SDK (Valve LLC).
 * Defines only what's needed — no external SDK dependency.
 */

#include <cstdint>
#include <cstring>
#include <windows.h>
#include <gl/gl.h>

/*===========================================================================
 * Basic types
 *===========================================================================*/

typedef unsigned char byte;
typedef int qboolean;
typedef int HSPRITE_HL; /* avoid collision with windows HSPRITE */

typedef float vec_t;

/* vec3_t — supports both .x/.y/.z member access and [i] indexing.
 * Required because the engine passes float* and we use struct members. */
struct vec3_t {
    float x, y, z;
    float& operator[](int i)       { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    operator float*()              { return &x; }
    operator const float*() const  { return &x; }
};

typedef float vec4_t[4];
typedef float vec2_t[2];

typedef struct {
    byte r, g, b;
} color24;

typedef struct {
    byte r, g, b, a;
} colorVec;

typedef struct {
    int left, right, top, bottom;
} wrect_t;

/*===========================================================================
 * Constants
 *===========================================================================*/

/* Input buttons */
#define IN_ATTACK    (1 << 0)
#define IN_JUMP      (1 << 1)
#define IN_DUCK      (1 << 2)
#define IN_FORWARD   (1 << 3)
#define IN_BACK      (1 << 4)
#define IN_USE       (1 << 5)
#define IN_MOVELEFT  (1 << 9)
#define IN_MOVERIGHT (1 << 10)
#define IN_ATTACK2   (1 << 11)
#define IN_RELOAD    (1 << 13)

/* Player flags */
#define FL_ONGROUND  (1 << 9)
#define FL_DUCKING   (1 << 14)

/* Move types */
#define MOVETYPE_NONE      0
#define MOVETYPE_WALK      3
#define MOVETYPE_STEP      4
#define MOVETYPE_FLY       5
#define MOVETYPE_NOCLIP    8

/* PM_TraceLine flags */
#define PM_TRACELINE_PHYSENTSONLY 0
#define PM_TRACELINE_ANYVISIBLE  1

/* Studio model limits */
#define MAXSTUDIOBONES     128
#define MAXSTUDIOCONTROLLERS 8

/* Max entities / weapons */
#define MAX_WEAPONS    64
#define MAX_PHYSENTS   600
#define MAX_MOVEENTS   64
#define MAX_PHYSINFO_STRING 256
#define MAX_MAPNAME    64

/*===========================================================================
 * Console variable
 *===========================================================================*/

typedef struct cvar_s {
    char*  name;
    char*  string;
    int    flags;
    float  value;
    struct cvar_s* next;
} cvar_t;

/*===========================================================================
 * Screen info
 *===========================================================================*/

typedef struct SCREENINFO_s {
    int   iSize;
    int   iWidth;
    int   iHeight;
    int   iFlags;
    int   iCharHeight;
    short charWidths[256];
} SCREENINFO;

/*===========================================================================
 * User command
 *===========================================================================*/

typedef struct usercmd_s {
    short lerp_msec;
    byte  msec;
    vec3_t viewangles;
    float forwardmove;
    float sidemove;
    float upmove;
    int   lightlevel;
    unsigned short buttons;
    byte  impulse;
    byte  weaponselect;
    int   impact_index;
    vec3_t impact_position;
} usercmd_t;

/*===========================================================================
 * Entity state
 *===========================================================================*/

typedef struct entity_state_s {
    int    entityType;
    int    number;
    float  msg_time;
    int    messagenum;
    vec3_t origin;
    vec3_t angles;
    int    modelindex;
    int    sequence;
    float  frame;
    int    colormap;
    short  skin;
    short  solid;
    int    effects;
    float  scale;
    byte   eflags;
    int    rendermode;
    int    renderamt;
    color24 rendercolor;
    int    renderfx;
    int    movetype;
    float  animtime;
    float  framerate;
    int    body;
    byte   controller[4];
    byte   blending[4];
    vec3_t velocity;
    vec3_t mins;
    vec3_t maxs;
    int    aiment;
    int    owner;
    float  friction;
    float  gravity;
    int    team;
    int    playerclass;
    int    health;
    qboolean spectator;
    int    weaponmodel;
    int    gaitsequence;
    vec3_t basevelocity;
    int    usehull;
    int    oldbuttons;
    int    onground;
    int    iStepLeft;
    float  flFallVelocity;
    float  fov;
    int    weaponanim;
    vec3_t startpos;
    vec3_t endpos;
    float  impacttime;
    float  starttime;
    int    iuser1;
    int    iuser2;
    int    iuser3;
    int    iuser4;
    float  fuser1;
    float  fuser2;
    float  fuser3;
    float  fuser4;
    vec3_t vuser1;
    vec3_t vuser2;
    vec3_t vuser3;
    vec3_t vuser4;
} entity_state_t;

/*===========================================================================
 * Client entity
 *===========================================================================*/

typedef struct {
    float animtime;
    vec3_t origin;
    vec3_t angles;
} position_history_t;

typedef struct {
    byte mouthopen;
    byte sndcount;
    int  sndavg;
} mouth_t;

typedef struct {
    float prevanimtime;
    float sequencetime;
    byte  prevseqblending[2];
    vec3_t prevorigin;
    vec3_t prevangles;
    int   prevsequence;
    float prevframe;
    byte  prevcontroller[4];
    byte  prevblending[2];
} latchedvars_t;

/* Forward declarations */
struct model_s;
typedef struct model_s model_t;
struct efrag_s;
typedef struct efrag_s efrag_t;

typedef struct cl_entity_s {
    int           index;
    qboolean      player;
    entity_state_t baseline;
    entity_state_t prevstate;
    entity_state_t curstate;
    int           current_position;
    position_history_t ph[64]; /* HISTORY_MAX */
    mouth_t       mouth;
    latchedvars_t latched;
    float         lastmove;

    vec3_t        origin;
    vec3_t        angles;
    vec3_t        attachment[4];

    int           trivial_accept;
    model_t*      model;
    efrag_t*      efrag;
    struct cl_entity_s* topnode;
    float         visframe;
    colorVec      cvFloorColor;
} cl_entity_t;

/*===========================================================================
 * Player movement
 *===========================================================================*/

typedef struct {
    vec3_t normal;
    float  dist;
} pmplane_t;

typedef struct pmtrace_s {
    qboolean allsolid;
    qboolean startsolid;
    qboolean inopen, inwater;
    float    fraction;
    vec3_t   endpos;
    pmplane_t plane;
    int      ent;
    vec3_t   deltavelocity;
    int      hitgroup;
} pmtrace_t;

typedef struct physent_s {
    char   name[32];
    int    player;
    vec3_t origin;
    model_t* model;
    model_t* studiomodel;
    vec3_t mins, maxs;
    int    info;
    vec3_t angles;
    int    solid;
    int    skin;
    int    rendermode;
    float  frame;
    int    sequence;
    byte   controller[4];
    byte   blending[2];
    int    movetype;
    int    takedamage;
    int    blooddecal;
    int    team;
    int    classnumber;
    int    iuser1;
    int    iuser2;
    int    iuser3;
    int    iuser4;
    float  fuser1;
    float  fuser2;
    float  fuser3;
    float  fuser4;
    vec3_t vuser1;
    vec3_t vuser2;
    vec3_t vuser3;
    vec3_t vuser4;
} physent_t;

/* movevars_t */
typedef struct movevars_s {
    float gravity;
    float stopspeed;
    float maxspeed;
    float spectatormaxspeed;
    float accelerate;
    float airaccelerate;
    float wateraccelerate;
    float friction;
    float edgefriction;
    float waterfriction;
    float entgravity;
    float bounce;
    float stepsize;
    float maxvelocity;
    float zmax;
    float waveHeight;
    qboolean footsteps;
    char  skyName[32];
    float rollangle;
    float rollspeed;
    float skycolor_r;
    float skycolor_g;
    float skycolor_b;
    float skyvec_x;
    float skyvec_y;
    float skyvec_z;
} movevars_t;

typedef struct playermove_s {
    int    player_index;
    qboolean server;
    qboolean multiplayer;
    float  time;
    float  frametime;
    vec3_t forward;
    vec3_t right;
    vec3_t up;
    vec3_t origin;
    vec3_t angles;
    vec3_t oldangles;
    vec3_t velocity;
    vec3_t movedir;
    vec3_t basevelocity;
    vec3_t view_ofs;
    float  flDuckTime;
    qboolean bInDuck;
    int    flTimeStepSound;
    int    iStepLeft;
    float  flFallVelocity;
    vec3_t punchangle;
    float  flSwimTime;
    float  flNextPrimaryAttack;
    int    effects;
    int    flags;
    int    usehull;
    float  gravity;
    float  friction;
    int    oldbuttons;
    float  waterjumptime;
    qboolean dead;
    int    deadflag;
    int    spectator;
    int    movetype;
    int    onground;
    int    waterlevel;
    int    watertype;
    int    oldwaterlevel;
    char   sztexturename[256];
    char   chtexturetype;
    float  maxspeed;
    float  clientmaxspeed;
    int    iuser1;
    int    iuser2;
    int    iuser3;
    int    iuser4;
    float  fuser1;
    float  fuser2;
    float  fuser3;
    float  fuser4;
    vec3_t vuser1;
    vec3_t vuser2;
    vec3_t vuser3;
    vec3_t vuser4;
    int    numphysent;
    physent_t physents[MAX_PHYSENTS];
    int    nummoveent;
    physent_t moveents[MAX_MOVEENTS];
    int    numvisent;
    physent_t visents[MAX_PHYSENTS];
    usercmd_t cmd;
    int    numtouch;
    pmtrace_t touchindex[MAX_PHYSENTS];
    char   physinfo[MAX_PHYSINFO_STRING];
    movevars_t* movevars;
    vec3_t player_mins[4];
    vec3_t player_maxs[4];
    /* There are more members, but we don't need them */
} playermove_t;

/*===========================================================================
 * Ref params (CalcRefdef)
 *===========================================================================*/

typedef struct ref_params_s {
    vec3_t vieworg;
    vec3_t viewangles;
    vec3_t forward;
    vec3_t right;
    vec3_t up;
    float  frametime;
    float  time;
    int    intermission;
    int    paused;
    int    spectator;
    int    onground;
    int    waterlevel;
    vec3_t simvel;
    vec3_t simorg;
    vec3_t viewheight;
    float  idealpitch;
    vec3_t cl_viewangles;
    int    health;
    vec3_t crosshairangle;
    float  viewsize;
    vec3_t punchangle;
    int    maxclients;
    int    viewentity;
    int    playernum;
    int    max_entities;
    int    demoplayback;
    int    hardware;
    int    smoothing;
    usercmd_t* cmd;
    movevars_t* movevars;
    int    viewport[4];
    int    nextView;
    int    onlyClientDraw;
} ref_params_t;

/*===========================================================================
 * Client data / Weapon data / Local state (for HUD_PostRunCmd)
 *===========================================================================*/

typedef struct clientdata_s {
    vec3_t origin;
    vec3_t velocity;
    int    viewmodel;
    vec3_t punchangle;
    int    flags;
    int    waterlevel;
    int    watertype;
    vec3_t view_ofs;
    float  health;
    int    bInDuck;
    int    weapons;
    int    flTimeStepSound;
    int    flDuckTime;
    int    flSwimTime;
    int    iWaterJumpTime;
    float  maxspeed;
    float  fov;
    int    weaponanim;
    int    m_iId;
    int    ammo_shells;
    int    ammo_nails;
    int    ammo_cells;
    int    ammo_rockets;
    float  m_flNextAttack;
    int    tfstate;
    int    pushmsec;
    int    deadflag;
    char   physinfo[MAX_PHYSINFO_STRING];
    int    iuser1;
    int    iuser2;
    int    iuser3;
    int    iuser4;
    float  fuser1;
    float  fuser2;
    float  fuser3;
    float  fuser4;
    vec3_t vuser1;
    vec3_t vuser2;
    vec3_t vuser3;
    vec3_t vuser4;
} clientdata_t;

typedef struct weapon_data_s {
    int    m_iId;
    int    m_iClip;
    float  m_flNextPrimaryAttack;
    float  m_flNextSecondaryAttack;
    float  m_flTimeWeaponIdle;
    int    m_fInReload;
    int    m_fInSpecialReload;
    float  m_flNextReload;
    float  m_flPumpTime;
    float  m_fReloadTime;
    float  m_fAimedDamage;
    float  m_fNextAimBonus;
    int    m_fInZoom;
    int    m_iWeaponState;
    int    iuser1;
    int    iuser2;
    float  fuser1;
    float  fuser2;
    float  fuser3;
    float  fuser4;
} weapon_data_t;

typedef struct local_state_s {
    entity_state_t playerstate;
    clientdata_t   client;
    weapon_data_t  weapondata[MAX_WEAPONS];
} local_state_t;

/*===========================================================================
 * Player info
 *===========================================================================*/

typedef struct hud_player_info_s {
    char*    name;
    short    ping;
    byte     thisplayer;
    byte     spectator;
    byte     packetloss;
    char*    model;
    short    topcolor;
    short    bottomcolor;
    uint64_t m_nSteamID;
} hud_player_info_t;

/*===========================================================================
 * Sub-API structures (event_api, triangleapi, efx_api)
 *===========================================================================*/

typedef struct event_api_s {
    int   version;
    void  (*EV_PlaySound)(int ent, float* origin, int channel, const char* sample,
                          float volume, float attenuation, int fFlags, int pitch);
    void  (*EV_StopSound)(int ent, int channel, const char* sample);
    int   (*EV_FindModelIndex)(const char* pmodel);
    int   (*EV_IsLocal)(int playernum);
    int   (*EV_LocalPlayerDucking)(void);
    void  (*EV_LocalPlayerViewheight)(float*);
    void  (*EV_LocalPlayerBounds)(int hull, float* mins, float* maxs);
    int   (*EV_IndexFromTrace)(struct pmtrace_s* pTrace);
    physent_t* (*EV_GetPhysent)(int idx);
    void  (*EV_SetUpPlayerPrediction)(int dopred, int bIncludeLocalClient);
    void  (*EV_PushPMStates)(void);
    void  (*EV_PopPMStates)(void);
    void  (*EV_SetSolidPlayers)(int playernum);
    void  (*EV_SetTraceHull)(int hull);
    void  (*EV_PlayerTrace)(float* start, float* end, int traceFlags, int ignore_pe, pmtrace_t* tr);
    void  (*EV_WeaponAnimation)(int sequence, int body);
    unsigned short (*EV_PrecacheEvent)(int type, const char* psz);
    void  (*EV_PlaybackEvent)(int flags, const void* pInvoker, unsigned short eventindex,
                              float delay, float* origin, float* angles,
                              float fparam1, float fparam2,
                              int iparam1, int iparam2,
                              int bparam1, int bparam2);
    const char* (*EV_TraceTexture)(int ground, float* vstart, float* vend);
    void  (*EV_StopAllSounds)(int entnum, int entchannel);
    void  (*EV_KillEvents)(int entnum, const char* eventname);
} event_api_t;

typedef struct triangleapi_s {
    int   version;
    void  (*RenderMode)(int mode);
    void  (*Begin)(int primitiveCode);
    void  (*End)(void);
    void  (*Color4f)(float r, float g, float b, float a);
    void  (*Color4ub)(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
    void  (*TexCoord2f)(float u, float v);
    void  (*Vertex3fv)(float* worldPoint);
    void  (*Vertex3f)(float x, float y, float z);
    void  (*Brightness)(float brightness);
    void  (*CullFace)(int style);
    int   (*SpriteTexture)(model_t* pSpriteModel, int frame);
    int   (*WorldToScreen)(float* world, float* screen);
    void  (*Fog)(float flFogColor[3], float flStart, float flEnd, int bOn);
    void  (*ScreenToWorld)(float* screen, float* world);
    void  (*GetMatrix)(const int pname, float* matrix);
    int   (*BoxInPVS)(float* mins, float* maxs);
    void  (*LightAtPoint)(float* pos, float* value);
    void  (*Color4fRendermode)(float r, float g, float b, float a, int rendermode);
    void  (*FogParams)(float flDensity, int iFogSkybox);
} triangleapi_t;

/* Minimal BEAM struct for R_BeamPoints */
struct BEAM;

typedef struct efx_api_s {
    void*  (*AllocParticle)(void* callback);
    void   (*BlobExplosion)(float* org);
    void   (*Blood)(float* org, float* dir, int pcolor, int speed);
    void   (*BloodSprite)(float* org, int colorindex, int modelIndex, int modelIndex2, float size);
    void   (*BloodStream)(float* org, float* dir, int pcolor, int speed);
    void   (*BreakModel)(float* pos, float* size, float* dir, float random, float life, int count, int modelIndex, char flags);
    void   (*Bubbles)(float* mins, float* maxs, float height, int modelIndex, int count, float speed);
    void   (*BubbleTrail)(float* start, float* end, float height, int modelIndex, int count, float speed);
    void   (*DecalShoot)(int textureIndex, int entity, int modelIndex, float* position, int flags);
    void   (*DLight)(float* org, float radius, float life, float r, float g, float b);
    void   (*Draw_DecalIndex)(int id);
    int    (*Draw_DecalIndexFromName)(char* name);
    void   (*EntityParticles)(cl_entity_t* ent);
    void   (*Explosion)(float* pos, int model, float scale, float framerate, int flags);
    void   (*FizzEffect)(cl_entity_t* pent, int modelIndex, int density);
    void   (*FireField)(float* org, int radius, int modelIndex, int count, int flags, float life);
    void   (*FlickerParticles)(float* org);
    void   (*FunnelSprite)(float* org, int modelIndex, int reverse);
    void   (*Implosion)(float* end, float radius, int count, float life);
    void   (*LargeFunnel)(float* org, int reverse);
    void   (*LavaSplash)(float* org);
    void   (*MultiGunshot)(float* org, float* dir, float* noise, int count, int decalCount, int* decalIndices);
    void   (*MuzzleFlash)(float* pos1, int type);
    void*  (*TempSprite)(float* pos, float* dir, float scale, int modelIndex, int rendermode, int renderfx, float a, float life, int flags);
    void   (*ParticleBox)(float* mins, float* maxs, unsigned char r, unsigned char g, unsigned char b, float life);
    void   (*ParticleBurst)(float* pos, int size, int color, float life);
    void   (*ParticleExplosion)(float* org);
    void   (*ParticleExplosion2)(float* org, int colorStart, int colorLength);
    void   (*ParticleLine)(float* start, float* end, unsigned char r, unsigned char g, unsigned char b, float life);
    void   (*PlayerSprites)(int client, int modelIndex, int count, int size);
    void   (*Projectile)(float* origin, float* velocity, int modelIndex, int life, int owner, void (*hitcallback)(struct tempent_s*, struct pmtrace_s*));
    void   (*RicochetSound)(float* pos);
    void   (*RicochetSprite)(float* pos, model_t* pmodel, float duration, float scale);
    void   (*RocketFlare)(float* pos);
    void   (*RocketTrail)(float* start, float* end, int type);
    void   (*RunParticleEffect)(float* org, float* dir, int color, int count);
    void   (*ShowLine)(float* start, float* end);
    void   (*SparkEffect)(float* pos, int count, int velocityMin, int velocityMax);
    void   (*SparkShower)(float* pos);
    void   (*SparkStreaks)(float* pos, int count, int velocityMin, int velocityMax);
    void   (*Spray)(float* pos, float* dir, int modelIndex, int count, int speed, int spread, int rendermode);
    void   (*Sprite_Explode)(void* pTemp, float scale, int flags);
    void   (*Sprite_Smoke)(void* pTemp, float scale);
    void   (*Sprite_Spray)(float* pos, float* dir, int modelIndex, int count, int speed, int iRand);
    void   (*Sprite_Trail)(int type, float* start, float* end, int modelIndex, int count, float life, float size, float amplitude, int renderamt, float speed);
    void   (*Sprite_WallPuff)(void* pTemp, float scale);
    void   (*StreakSplash)(float* pos, float* dir, int color, int count, float speed, int velocityMin, int velocityMax);
    void   (*TracerEffect)(float* start, float* end);
    void   (*UserTracerParticle)(float* org, float* vel, float life, int colorIndex, float length, unsigned char deathcontext, void (*deathfunc)(void*));
    void*  (*TracerParticles)(float* org, float* vel, float life);
    void   (*TeleportSplash)(float* org);
    void   (*TempSphereModel)(float* pos, float speed, float life, int count, int modelIndex);
    void*  (*TempModel)(float* pos, float* dir, vec3_t angles, float life, int modelIndex, int soundtype);
    void*  (*DefaultSprite)(float* pos, int spriteIndex, float framerate);
    void*  (*TempSprite2)(float* pos, float* dir, float scale, int modelIndex, int rendermode, int renderfx, float a, float life, int flags);
    BEAM*  (*R_BeamPoints)(float* start, float* end, int modelIndex, float life, float width,
                           float amplitude, float brightness, float speed,
                           int startFrame, float framerate, float r, float g, float b);
    /* There are more efx functions, but R_BeamPoints is the last one we use.
     * Remaining entries are padded below to maintain correct vtable size. */
    void* _pad_efx[12]; /* remaining efx functions we don't use */
} efx_api_t;

/*===========================================================================
 * Engine function table (cl_enginefunc_t)
 *
 * This is the core interface the engine passes to the client DLL.
 * Order MUST match the GoldSrc SDK exactly.
 *===========================================================================*/

/* Forward decl for pfnHookUserMsg callback */
typedef int (*pfnUserMsgHook)(const char* pszName, int iSize, void* pbuf);

/* Forward decl for text message */
struct client_textmessage_s;
typedef struct client_textmessage_s client_textmessage_t;

/* Forward decl for con_nprint */
struct con_nprint_s;

/* Forward decl for screen fade */
struct screenfade_s;

/* Forward decl for edict */
struct edict_s;
typedef struct edict_s edict_t;

/* Forward decl for event args */
struct event_args_s;

/* Forward decl for tempent */
struct tempent_s;

typedef struct cl_enginefuncs_s {
    /*  0 */ HSPRITE_HL       (*pfnSPR_Load)(const char*);
    /*  1 */ int              (*pfnSPR_Frames)(HSPRITE_HL);
    /*  2 */ int              (*pfnSPR_Height)(HSPRITE_HL, int);
    /*  3 */ int              (*pfnSPR_Width)(HSPRITE_HL, int);
    /*  4 */ void             (*pfnSPR_Set)(HSPRITE_HL, int, int, int);
    /*  5 */ void             (*pfnSPR_Draw)(int, int, int, const wrect_t*);
    /*  6 */ void             (*pfnSPR_DrawHoles)(int, int, int, const wrect_t*);
    /*  7 */ void             (*pfnSPR_DrawAdditive)(int, int, int, const wrect_t*);
    /*  8 */ void             (*pfnSPR_EnableScissor)(int, int, int, int);
    /*  9 */ void             (*pfnSPR_DisableScissor)(void);
    /* 10 */ void*            (*pfnSPR_GetList)(char*, int*);
    /* 11 */ void             (*pfnFillRGBA)(int, int, int, int, int, int, int, int);
    /* 12 */ int              (*pfnGetScreenInfo)(SCREENINFO*);
    /* 13 */ void             (*pfnSetCrosshair)(HSPRITE_HL, wrect_t, int, int, int);
    /* 14 */ cvar_t*          (*pfnRegisterVariable)(char*, char*, int);
    /* 15 */ float            (*pfnGetCvarFloat)(char*);
    /* 16 */ char*            (*pfnGetCvarString)(char*);
    /* 17 */ int              (*pfnAddCommand)(char*, void (*)(void));
    /* 18 */ int              (*pfnHookUserMsg)(char*, pfnUserMsgHook);
    /* 19 */ int              (*pfnServerCmd)(char*);
    /* 20 */ int              (*pfnClientCmd)(char*);
    /* 21 */ void             (*pfnGetPlayerInfo)(int, hud_player_info_t*);
    /* 22 */ void             (*pfnPlaySoundByName)(char*, float);
    /* 23 */ void             (*pfnPlaySoundByIndex)(int, float);
    /* 24 */ void             (*pfnAngleVectors)(const float*, float*, float*, float*);
    /* 25 */ client_textmessage_t* (*pfnTextMessageGet)(const char*);
    /* 26 */ int              (*pfnDrawCharacter)(int, int, int, int, int, int);
    /* 27 */ int              (*pfnDrawConsoleString)(int, int, char*);
    /* 28 */ void             (*pfnDrawSetTextColor)(float, float, float);
    /* 29 */ void             (*pfnDrawConsoleStringLen)(const char*, int*, int*);
    /* 30 */ void             (*pfnConsolePrint)(const char*);
    /* 31 */ void             (*pfnCenterPrint)(const char*);
    /* 32 */ int              (*GetWindowCenterX)(void);
    /* 33 */ int              (*GetWindowCenterY)(void);
    /* 34 */ void             (*GetViewAngles)(float*);
    /* 35 */ void             (*SetViewAngles)(float*);
    /* 36 */ int              (*GetMaxClients)(void);
    /* 37 */ void             (*Cvar_SetValue)(char*, float);
    /* 38 */ int              (*Cmd_Argc)(void);
    /* 39 */ char*            (*Cmd_Argv)(int);
    /* 40 */ void             (*Con_Printf)(char*, ...);
    /* 41 */ void             (*Con_DPrintf)(char*, ...);
    /* 42 */ void             (*Con_NPrintf)(int, char*, ...);
    /* 43 */ void             (*Con_NXPrintf)(con_nprint_s*, char*, ...);
    /* 44 */ const char*      (*PhysInfo_ValueForKey)(const char*);
    /* 45 */ const char*      (*ServerInfo_ValueForKey)(const char*);
    /* 46 */ float            (*GetClientMaxspeed)(void);
    /* 47 */ int              (*CheckParm)(char*, char**);
    /* 48 */ void             (*Key_Event)(int, int);
    /* 49 */ void             (*GetMousePosition)(int*, int*);
    /* 50 */ int              (*IsNoClipping)(void);
    /* 51 */ cl_entity_t*     (*GetLocalPlayer)(void);
    /* 52 */ cl_entity_t*     (*GetViewModel)(void);
    /* 53 */ cl_entity_t*     (*GetEntityByIndex)(int);
    /* 54 */ float            (*GetClientTime)(void);
    /* 55 */ void             (*V_CalcShake)(void);
    /* 56 */ void             (*V_ApplyShake)(float*, float*, float);
    /* 57 */ int              (*PM_PointContents)(float*, int*);
    /* 58 */ int              (*PM_WaterEntity)(float*);
    /* 59 */ pmtrace_t*       (*PM_TraceLine)(float*, float*, int, int, int);
    /* 60 */ model_t*         (*CL_LoadModel)(const char*, int*);
    /* 61 */ int              (*CL_CreateVisibleEntity)(int, cl_entity_t*);
    /* 62 */ const model_t*   (*GetSpritePointer)(HSPRITE_HL);
    /* 63 */ void             (*pfnPlaySoundByNameAtLocation)(char*, float, float*);
    /* 64 */ unsigned short   (*pfnPrecacheEvent)(int, const char*);
    /* 65 */ void             (*pfnPlaybackEvent)(int, const edict_t*, unsigned short, float,
                                                  float*, float*, float, float, int, int, int, int);
    /* 66 */ void             (*pfnWeaponAnim)(int, int);
    /* 67 */ float            (*pfnRandomFloat)(float, float);
    /* 68 */ long             (*pfnRandomLong)(long, long);
    /* 69 */ void             (*pfnHookEvent)(char*, void (*)(event_args_s*));
    /* 70 */ int              (*Con_IsVisible)(void);
    /* 71 */ const char*      (*pfnGetGameDirectory)(void);
    /* 72 */ cvar_t*          (*pfnGetCvarPointer)(const char*);
    /* 73 */ const char*      (*Key_LookupBinding)(const char*);
    /* 74 */ const char*      (*pfnGetLevelName)(void);
    /* 75 */ void             (*pfnGetScreenFade)(screenfade_s*);
    /* 76 */ void             (*pfnSetScreenFade)(screenfade_s*);
    /* 77 */ void*            (*VGui_GetPanel)(void);
    /* 78 */ void             (*VGui_ViewportPaintBackground)(int[4]);
    /* 79 */ byte*            (*COM_LoadFile)(char*, int, int*);
    /* 80 */ char*            (*COM_ParseFile)(char*, char*);
    /* 81 */ void             (*COM_FreeFile)(void*);

    /* Sub-API pointers */
    /* 82 */ triangleapi_t*   pTriAPI;
    /* 83 */ efx_api_t*       pEfxAPI;
    /* 84 */ event_api_t*     pEventAPI;
    /* 85 */ void*            pDemoAPI;
    /* 86 */ void*            pNetAPI;
    /* 87 */ void*            pVoiceTweak;

    /* More functions after the APIs */
    /* 88 */ int              (*IsSpectateOnly)(void);
    /* 89 */ model_t*         (*LoadMapSprite)(const char*);
    /* 90 */ void             (*COM_AddAppDirectoryToSearchPath)(const char*, const char*);
    /* 91 */ int              (*COM_ExpandFilename)(const char*, char*, int);
    /* 92 */ const char*      (*PlayerInfo_ValueForKey)(int, const char*);
    /* 93 */ void             (*PlayerInfo_SetValueForKey)(const char*, const char*);
    /* 94 */ qboolean         (*GetPlayerUniqueID)(int, char[16]);
    /* 95 */ int              (*GetTrackerIDForPlayer)(int);
    /* 96 */ int              (*GetPlayerForTrackerID)(int);
    /* 97 */ int              (*pfnServerCmdUnreliable)(char*);
    /* 98 */ void             (*pfnVguiWrap2_GetMouseDelta)(int*, int*);
    /* 99 */ int              (*pfnFilteredClientCmd)(char*);
} cl_enginefunc_t;

/*===========================================================================
 * Client DLL function table (cl_clientfunc_t)
 *===========================================================================*/

typedef struct cl_clientfuncs_s {
    int   (*Initialize)(cl_enginefunc_t* pEnginefuncs, int iVersion);
    void  (*HUD_Init)(void);
    int   (*HUD_VidInit)(void);
    int   (*HUD_Redraw)(float time, int intermission);
    int   (*HUD_UpdateClientData)(clientdata_t* pcldata, float flTime);
    void  (*HUD_Reset)(void);
    void  (*HUD_PlayerMove)(playermove_t* ppmove, int server);
    void  (*HUD_PlayerMoveInit)(playermove_t* ppmove);
    char  (*HUD_PlayerMoveTexture)(char* name);
    void  (*IN_ActivateMouse)(void);
    void  (*IN_DeactivateMouse)(void);
    void  (*IN_MouseEvent)(int mstate);
    void  (*IN_ClearStates)(void);
    void  (*IN_Accumulate)(void);
    void  (*CL_CreateMove)(float frametime, usercmd_t* cmd, int active);
    int   (*CL_IsThirdPerson)(void);
    void  (*CL_CameraOffset)(float* ofs);
    void* (*KB_Find)(const char* name); /* kbutton_s* */
    void  (*CAM_Think)(void);
    void  (*CalcRefdef)(ref_params_t* pparams);
    int   (*HUD_AddEntity)(int type, cl_entity_t* ent, const char* modelname);
    void  (*HUD_CreateEntities)(void);
    void  (*HUD_DrawNormalTriangles)(void);
    void  (*HUD_DrawTransparentTriangles)(void);
    void  (*HUD_StudioEvent)(const void* event, const cl_entity_t* entity);
    void  (*HUD_PostRunCmd)(local_state_t* from, local_state_t* to,
                            usercmd_t* cmd, int runfuncs, double time,
                            unsigned int random_seed);
    void  (*HUD_Shutdown)(void);
    void  (*HUD_TxferLocalOverrides)(entity_state_t* state, const clientdata_t* client);
    void  (*HUD_ProcessPlayerState)(entity_state_t* dst, const entity_state_t* src);
    void  (*HUD_TxferPredictionData)(entity_state_t* ps, const entity_state_t* pps,
                                     clientdata_t* pcd, const clientdata_t* ppcd,
                                     weapon_data_t* wd, const weapon_data_t* pwd);
    void  (*Demo_ReadBuffer)(int size, unsigned char* buffer);
    int   (*HUD_ConnectionlessPacket)(void* net_from, const char* args,
                                     char* response_buffer, int* response_buffer_size);
    int   (*HUD_GetHullBounds)(int hullnumber, float* mins, float* maxs);
    void  (*HUD_Frame)(double time);
    int   (*HUD_Key_Event)(int down, int keynum, const char* pszCurrentBinding);
    void  (*HUD_TempEntUpdate)(double frametime, double client_time, double cl_gravity,
                               tempent_s** ppTempEntFree, tempent_s** ppTempEntActive,
                               int (*Callback_AddVisibleEntity)(cl_entity_t* pEntity),
                               void (*Callback_TempEntPlaySound)(tempent_s* pTemp, float damp));
    cl_entity_t* (*HUD_GetUserEntity)(int index);
    int   (*HUD_VoiceStatus)(int entindex, qboolean bTalking);
    int   (*HUD_DirectorMessage)(unsigned char command, unsigned int firstObject,
                                 unsigned int secondObject, unsigned int flags);
    int   (*HUD_GetStudioModelInterface)(int version, void** ppinterface, void* pstudio);
    void  (*HUD_CHATINPUTPOSITION_FUNCTION)(int* x, int* y);
    int   (*HUD_GETPLAYERTEAM_FUNCTION)(int iplayer);
    void  (*CLIENTFACTORY)(void);
} cl_clientfunc_t;

/*===========================================================================
 * Studio model types
 *===========================================================================*/

/* Minimal studio types — only what's needed for the cheat */
typedef struct {
    char  name[32];
    int   parent;
    int   flags;
    int   bonecontroller[6];
    float value[6];
    float scale[6];
} mstudiobone_t;

typedef struct {
    unsigned short offset[6];
} mstudioanim_t;

typedef struct {
    char  label[32];
    float fps;
    int   flags;
    int   activity;
    int   actweight;
    int   numevents;
    int   eventindex;
    int   numframes;
    int   numpivots;
    int   pivotindex;
    int   motiontype;
    int   motionbone;
    vec3_t linearmovement;
    int   automoveposindex;
    int   automoveangleindex;
    vec3_t bbmin;
    vec3_t bbmax;
    int   numblends;
    int   animindex;
    int   blendtype[2];
    float blendstart[2];
    float blendend[2];
    int   blendparent;
    int   seqgroup;
    int   entrynode;
    int   exitnode;
    int   nodeflags;
    int   nextseq;
} mstudioseqdesc_t;

typedef struct {
    int   id;
    int   version;
    char  name[64];
    int   length;
    vec3_t eyeposition;
    vec3_t min;
    vec3_t max;
    vec3_t bbmin;
    vec3_t bbmax;
    int   flags;
    int   numbones;
    int   boneindex;
    int   numbonecontrollers;
    int   bonecontrollerindex;
    int   numhitboxes;
    int   hitboxindex;
    int   numseq;
    int   seqindex;
    int   numseqgroups;
    int   seqgroupindex;
    int   numtextures;
    int   textureindex;
    int   texturedataindex;
    int   numskinref;
    int   numskinfamilies;
    int   skinindex;
    int   numbodyparts;
    int   bodypartindex;
    int   numattachments;
    int   attachmentindex;
    int   soundtable;
    int   soundindex;
    int   soundgroups;
    int   soundgroupindex;
    int   numtransitions;
    int   transitionindex;
} studiohdr_t;

typedef struct {
    char name[64];
    int  nummodels;
    int  base;
    int  modelindex;
} mstudiobodyparts_t;

typedef struct {
    char  name[64];
    int   type;
    float boundingradius;
    int   nummesh;
    int   meshindex;
    int   numverts;
    int   vertinfoindex;
    int   vertindex;
    int   numnorms;
    int   norminfoindex;
    int   normindex;
    int   numgroups;
    int   groupindex;
} mstudiomodel_t;

typedef struct {
    int type;
    int event;
    int frame;
    char options[64];
} mstudioevent_t;

/*===========================================================================
 * Engine studio API
 *===========================================================================*/

/* player_info_s forward decl */
struct player_info_s;

typedef struct engine_studio_api_s {
    void*           (*Mem_Calloc)(int number, size_t size);
    void*           (*Cache_Check)(void* c);
    void            (*LoadCacheFile)(char* path, void* cu);
    model_t*        (*Mod_ForName)(const char* name, int crash_if_missing);
    void*           (*Mod_Extradata)(model_t* mod);
    model_t*        (*GetModelByIndex)(int index);
    cl_entity_t*    (*GetCurrentEntity)(void);
    player_info_s*  (*PlayerInfo)(int index);
    entity_state_t* (*GetPlayerState)(int index);
    cl_entity_t*    (*GetViewEntity)(void);
    void            (*GetTimes)(int* framecount, double* current, double* old);
    cvar_t*         (*GetCvar)(const char* name);
    void            (*GetViewInfo)(float* origin, float* upv, float* rightv, float* vpnv);
    model_t*        (*GetChromeSprite)(void);
    void            (*GetModelCounters)(int** s, int** a);
    void            (*GetAliasScale)(float* x, float* y);
    float***        (*StudioGetBoneTransform)(void);
    float***        (*StudioGetLightTransform)(void);
    float**         (*StudioGetAliasTransform)(void);
    float**         (*StudioGetRotationMatrix)(void);
    void            (*StudioSetupModel)(int bodypart, void** ppbodypart, void** ppsubmodel);
    int             (*StudioCheckBBox)(void);
    void            (*StudioDynamicLight)(cl_entity_t* ent, void* plight);
    void            (*StudioEntityLight)(void* plight);
    void            (*StudioSetupLighting)(void* plighting);
    void            (*StudioDrawPoints)(void);
    void            (*StudioDrawHulls)(void);
    void            (*StudioDrawAbsBBox)(void);
    void            (*StudioDrawBones)(void);
    void            (*StudioSetupSkin)(void* ptexturehdr, int index);
    void            (*StudioSetRemapColors)(int top, int bottom);
    model_t*        (*SetupPlayerModel)(int index);
    void            (*StudioClientEvents)(void);
    int             (*GetForceFaceFlags)(void);
    void            (*SetForceFaceFlags)(int flags);
    void            (*StudioSetHeader)(void* header);
    void            (*SetRenderModel)(model_t* model);
    void            (*SetupRenderer)(int rendermode);
    void            (*RestoreRenderer)(void);
    void            (*SetChromeOrigin)(void);
    int             (*IsHardware)(void);
    void            (*GL_StudioDrawShadow)(void);
    void            (*GL_SetRenderMode)(int mode);
    void            (*StudioSetRenderamt)(int iRenderamt);
    void            (*StudioSetCullState)(int iCull);
    void            (*StudioRenderShadow)(int iSprite, float* p1, float* p2, float* p3, float* p4);
} engine_studio_api_t;

/*===========================================================================
 * Studio model renderer (vtable struct)
 *===========================================================================*/

typedef struct StudioModelRenderer_s {
    void             (*CStudioModelRenderer)(void* this_ptr);
    void             (*_CStudioModelRenderer)(void* this_ptr);
    void             (*Init)(void* this_ptr);
    int              (*StudioDrawModel)(void* this_ptr, int flags);
    int              (*StudioDrawPlayer)(void* this_ptr, int flags, entity_state_t* pplayer);
    mstudioanim_t*   (*StudioGetAnim)(void* this_ptr, model_t* m_pSubModel, mstudioseqdesc_t* pseqdesc);
    void             (*StudioSetUpTransform)(void* this_ptr, int trivial_accept);
    void             (*StudioSetupBones)(void* this_ptr);
    void             (*StudioCalcAttachments)(void* this_ptr);
    void             (*StudioSaveBones)(void* this_ptr);
    void             (*StudioMergeBones)(void* this_ptr, model_t* m_pSubModel);
    float            (*StudioEstimateInterpolant)(void* this_ptr);
    float            (*StudioEstimateFrame)(void* this_ptr, mstudioseqdesc_t* pseqdesc);
    void             (*StudioFxTransform)(void* this_ptr, cl_entity_t* ent, float transform[3][4]);
    void             (*StudioSlerpBones)(void* this_ptr, vec4_t q1[], float pos1[][3],
                                         vec4_t q2[], float pos2[][3], float s);
    void             (*StudioCalcBoneAdj)(void* this_ptr, float dadt, float* adj,
                                          const byte* pcontroller1, const byte* pcontroller2, byte mouthopen);
    void             (*StudioCalcBoneQuaterion)(void* this_ptr, int frame, float s,
                                                mstudiobone_t* pbone, mstudioanim_t* panim, float* adj, float* q);
    void             (*StudioCalcBonePosition)(void* this_ptr, int frame, float s,
                                               mstudiobone_t* pbone, mstudioanim_t* panim, float* adj, float* pos);
    void             (*StudioCalcRotations)(void* this_ptr, float pos[][3], vec4_t* q,
                                            mstudioseqdesc_t* pseqdesc, mstudioanim_t* panim, float f);
    void             (*StudioRenderModel)(void* this_ptr);
    void             (*StudioRenderFinal)(void* this_ptr);
    void             (*StudioRenderFinal_Software)(void* this_ptr);
    void             (*StudioRenderFinal_Hardware)(void* this_ptr);
    void             (*StudioPlayerBlend)(void* this_ptr, mstudioseqdesc_t* pseqdesc, int* pBlend, float* pPitch);
    void             (*StudioEstimateGait)(void* this_ptr, entity_state_t* pplayer);
    void             (*StudioProcessGait)(void* this_ptr, entity_state_t* pplayer);
} StudioModelRenderer_t;

/*===========================================================================
 * Game info struct (engine internal)
 * Credits: @oxiKKK
 *===========================================================================*/

typedef struct {
    void* vtbl;
    bool  m_bActiveApp;
    void* m_hSDLWindow;
    void* m_hSDLGLContext;
    bool  m_bExpectSyntheticMouseMotion;
    int   m_nMouseTargetX;
    int   m_nMouseTargetY;
    int   m_nWarpDelta;
    bool  m_bCursorVisible;
    int   m_x;
    int   m_y;
    int   m_width;
    int   m_height;
    bool  m_bMultiplayer;
} game_t;

/*===========================================================================
 * DoD-specific extra player info
 *===========================================================================*/

typedef struct {
    int16_t frags;
    int16_t objscore;
    int16_t deaths;
    int16_t playerclass;
    int16_t teamnumber;
    int32_t status;
    char    teamname[16];
    int32_t showhealth;
    int32_t health;
    int32_t teamId;
    bool    dead;
} extra_player_info_dod_t;

/*===========================================================================
 * Matrix types
 *===========================================================================*/

typedef float matrix_3x4[3][4];
typedef matrix_3x4 bone_matrix[MAXSTUDIOBONES];

#endif /* SDK_H_ */
