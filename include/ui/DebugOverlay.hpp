#pragma once
#include <raylib.h>
#include <string>

class World;
class UI;
class Chat;

struct DebugOverlayParams {
    Camera3D camera;
    World& world;
    UI& ui;
    Chat& chat;
    float day_time;
    bool show_chunks;
    bool spectator_mode;
    bool ray_hit_valid;
    Vector3 target_solid;
    Vector3 target_empty;
    int cam_x;
    int cam_z;
};

void DrawDebugOverlay(const DebugOverlayParams& params);
