#pragma once
#include <string>
#include <vector>

class MenuManager;

namespace MenuResourcePacks {
    void scan_packs();
    void update();
    void draw(MenuManager& manager);
    void save_selection();

    std::string get_active_pack_path();
}
