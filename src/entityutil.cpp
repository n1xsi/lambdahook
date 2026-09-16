#include <cmath>
#include <cstring>

#include "include/sdk.h"
#include "include/util.h"
#include "include/entityutil.h"
#include "include/globals.h"

cl_entity_t* get_player(int ent_idx) {
    if (ent_idx < 0 || ent_idx > 32)
        return NULL;

    cl_entity_t* ent = i_engine->GetEntityByIndex(ent_idx);

    if (!valid_player(ent))
        return NULL;

    return ent;
}

bool is_alive(cl_entity_t* ent) {
    return ent && ent->curstate.movetype != 6 && ent->curstate.movetype != 0;
}

bool valid_player(cl_entity_t* ent) {
    return ent && ent->player && ent->index != localplayer->index &&
           ent->curstate.messagenum >= localplayer->curstate.messagenum;
}

bool is_friend(cl_entity_t* ent) {
    if (!ent || !localplayer)
        return false;

    const char* ent_team = i_engine->PlayerInfo_ValueForKey(ent->index, "team");
    const char* local_team = i_engine->PlayerInfo_ValueForKey(localplayer->index, "team");

    if (!ent_team || !local_team)
        return false;

    return strcmp(ent_team, local_team) == 0;
}

bool can_shoot(void) {
    /* In DoD, g_iClip may be 0 for many weapon types (melee, grenades).
     * Only check timing — if nextAttack and nextPrimaryAttack have elapsed,
     * we can shoot. */
    return g_flNextAttack <= 0.0f && g_flNextPrimaryAttack <= 0.0f;
}

char* get_name(int ent_idx) {
    hud_player_info_t info;
    i_engine->pfnGetPlayerInfo(ent_idx, &info);

    return info.name;
}
