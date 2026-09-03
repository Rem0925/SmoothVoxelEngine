#pragma once
#include <raylib.h>

class World;

struct PlayerPhysicsState {
    float player_vel_y = 0.0f;
    bool is_grounded = false;
    float smooth_step_offset = 0.0f;
    bool spectator_mode = false;
    float fall_distance = 0.0f;
    bool just_landed = false;
    float landed_fall_distance = 0.0f;
};

// pre_move_pos = position before WASD (for collision fallback)
// post_move_pos = position after WASD, before physics (for target compensation)
void UpdatePlayerPhysics(World& world, Camera3D& camera, PlayerPhysicsState& state, Vector3 pre_move_pos, Vector3 post_move_pos);
