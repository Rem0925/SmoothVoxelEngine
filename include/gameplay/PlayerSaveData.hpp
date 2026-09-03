#pragma once
#include <string>

class Camera3D;
class UI;

void load_player_data(const std::string& save_dir, Camera3D& camera, UI& ui, float& day_time, int& health);
void save_player_data(const std::string& save_dir, Camera3D& camera, UI& ui, float day_time, int health);
