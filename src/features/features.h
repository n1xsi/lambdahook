#ifndef FEATURES_H_
#define FEATURES_H_

#include "../include/sdk.h"

/*----------------------------------------------------------------------------*/

/* src/features/movement.cpp */
void bhop(usercmd_t* cmd);

/* src/features/esp.cpp */
void esp(void);
void correct_movement(usercmd_t* cmd, vec3_t old_angles);

/* src/features/chams.cpp */
bool chams(void* this_ptr);
void chams_init(void);
void chams_restore(void);
void chams_unhook_hw(void);

/* src/features/aim.cpp */
void aimbot(usercmd_t* cmd);

/* src/features/misc.cpp */
void custom_crosshair(void);
void bullet_tracers(usercmd_t* cmd);
void draw_fov_circle(void);

#endif /* FEATURES_H_ */
