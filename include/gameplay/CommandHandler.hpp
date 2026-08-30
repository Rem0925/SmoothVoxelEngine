#pragma once
#include <string>
#include <raylib.h>
#include "ui/Chat.hpp"
#include "ui/UI.hpp"

namespace CommandHandler {
    void process(Chat& chat, float& day_time, Camera3D& camera, bool& spectator_mode, float& player_vel_y, UI& ui, bool& show_chunks);
}
