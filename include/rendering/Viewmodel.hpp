#pragma once
#include <raylib.h>

class UI;

struct ViewmodelState {
    float walk_bob_timer = 0.0f;
    float bob_x = 0.0f, bob_y = 0.0f;
    float swing_timer = 0.0f;
    int prev_slot = -1;
    int prev_tool_idx = -2;
    float equip_anim = 1.0f;
};

void DrawFirstPersonViewmodel(
    ViewmodelState& state,
    UI& ui,
    Texture2D spritesheet_tiles,
    Texture2D spritesheet_items,
    float light_intensity,
    bool is_mining,
    float mining_progress,
    bool is_grounded,
    float dt
);
