#include "ui/UI.hpp"
#include <algorithm>

using namespace Config;

UI::UI(Texture2D sheet, Texture2D items_sheet) : spritesheet(sheet), spritesheet_items(items_sheet) {
    SetTextureFilter(spritesheet, TEXTURE_FILTER_POINT);
    SetTextureFilter(spritesheet_items, TEXTURE_FILTER_POINT);
    slots.resize(10);
    slots[0] = { AIR, "Mano", -1 };
    storage.resize(27); // 9x3 standard Minecraft storage
}

UI::~UI() {}

void UI::toggle_inventory() {
    if (is_open) {
        close_ui();
    } else {
        mode = UI_MODE_INVENTORY;
        is_open = true;
        EnableCursor();
    }
}

void UI::open_crafting_table(Vector3 pos) {
    if (is_open) cancel_drag();
    mode = UI_MODE_CRAFTING_TABLE;
    active_container_pos = pos;
    is_open = true;
    EnableCursor();
}

void UI::open_chest(Vector3 pos) {
    if (is_open) cancel_drag();
    mode = UI_MODE_CHEST;
    active_container_pos = pos;
    auto key = std::make_tuple((int)pos.x, (int)pos.y, (int)pos.z);
    if (world_chests.find(key) == world_chests.end()) {
        world_chests[key] = ChestData();
    }
    is_open = true;
    EnableCursor();
}

void UI::open_furnace(Vector3 pos) {
    if (is_open) cancel_drag();
    mode = UI_MODE_FURNACE;
    active_container_pos = pos;
    auto key = std::make_tuple((int)pos.x, (int)pos.y, (int)pos.z);
    if (world_furnaces.find(key) == world_furnaces.end()) {
        world_furnaces[key] = FurnaceData();
    }
    is_open = true;
    EnableCursor();
}

void UI::close_ui() {
    cancel_drag();
    mode = UI_MODE_CLOSED;
    is_open = false;
    DisableCursor();
}

void UI::select_slot(int index) {
    selected_slot = index % 10;
}

void UI::update() {
    if (IsKeyPressed(KEY_E)) {
        toggle_inventory();
    }
    if (IsKeyPressed(KEY_ESCAPE) && is_open) {
        close_ui();
    }

    if (!is_open) {
        for (int i = 0; i <= 9; i++) {
            int key = (i == 9) ? KEY_ZERO : (KEY_ONE + i);
            if (IsKeyPressed(key)) {
                select_slot(i);
            }
        }
        
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            int dir = (wheel > 0) ? -1 : 1; // Inverted scroll direction
            if (selected_slot == 0) {
                cycle_tool(dir);
            } else {
                cycle_block(dir);
            }
        }
    }
}
