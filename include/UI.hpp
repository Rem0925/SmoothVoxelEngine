#pragma once
#include <raylib.h>
#include <vector>
#include <string>
#include "Config.hpp"

struct InventorySlot {
    uint8_t id = Config::AIR;
    std::string name = "";
    int count = 0;
};

class UI {
public:
    std::vector<InventorySlot> slots;
    int selected_slot = 0;
    bool is_open = false;
    Texture2D spritesheet;

    UI(Texture2D sheet);
    ~UI();

    void update();
    void draw();
    
    void toggle_inventory();
    void select_slot(int index);
    void add_resource(uint8_t block_type);

private:
    void draw_hotbar();
    void draw_inventory_panel();
    void draw_icon(uint8_t block_id, int x, int y, int size);
};
