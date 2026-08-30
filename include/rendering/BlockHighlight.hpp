#pragma once
#include <raylib.h>

class World;
class UI;

struct BlockHighlightParams {
    World& world;
    UI& ui;
    Camera3D camera;
    bool ray_hit_valid;
    Vector3 target_solid;
    Vector3 target_empty;
    Vector3 hit;
    Texture2D spritesheet;
};

void DrawBlockHighlight(const BlockHighlightParams& params);
