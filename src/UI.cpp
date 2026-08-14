#include "UI.hpp"
#include <iostream>

using namespace Config;

UI::UI(Texture2D sheet) : spritesheet(sheet) {
    slots.resize(10);
    slots[0] = { AIR, "Pico", -1 }; // Slot 0 es Pico
    slots[1] = { WATER, "Agua", 64 };
}

UI::~UI() {}

void UI::update() {
    if (IsKeyPressed(KEY_E)) {
        toggle_inventory();
    }
    
    // Check num keys 1-9 and 0
    for (int i = 0; i <= 9; i++) {
        int key = (i == 9) ? KEY_ZERO : (KEY_ONE + i);
        if (IsKeyPressed(key)) {
            select_slot(i == 9 ? 9 : i);
        }
    }
}

void UI::toggle_inventory() {
    is_open = !is_open;
    if (is_open) {
        EnableCursor();
    } else {
        DisableCursor();
    }
}

void UI::select_slot(int index) {
    selected_slot = index % 10;
}

void UI::add_resource(uint8_t block_type) {
    if (BLOCKS.find(block_type) == BLOCKS.end()) return;
    
    std::string bname = BLOCKS.at(block_type).name;
    
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == block_type && slots[i].count > 0) {
            slots[i].count++;
            return;
        }
    }
    
    for (int i = 1; i < 10; i++) {
        if (slots[i].count <= 0) {
            slots[i].id = block_type;
            slots[i].name = bname;
            slots[i].count = 1;
            return;
        }
    }
}

void UI::draw_icon(uint8_t block_id, int x, int y, int size) {
    if (BLOCKS.find(block_id) != BLOCKS.end()) {
        auto b = BLOCKS.at(block_id);
        float tex_w = spritesheet.width / 9.0f;
        float tex_h = spritesheet.height / 10.0f;
        
        // El spritesheet de Ursina usa Y invertido (0 = abajo).
        // Convertimos la coordenada 'tex_y' para que encaje con la lectura de Raylib (0 = arriba)
        float correct_y = 10.0f - 1.0f - b.tex_y;
        
        Rectangle src = { b.tex_x * tex_w, correct_y * tex_h, tex_w, tex_h };
        Rectangle dest = { (float)x, (float)y, (float)size, (float)size };
        DrawTexturePro(spritesheet, src, dest, {0, 0}, 0.0f, WHITE);
    }
}

void UI::draw() {
    // Crosshair
    if (!is_open) {
        DrawRectangle(GetScreenWidth()/2 - 2, GetScreenHeight()/2 - 2, 4, 4, WHITE);
    }
    
    draw_hotbar();
    
    if (is_open) {
        draw_inventory_panel();
    }
}

void UI::draw_hotbar() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    
    int slot_size = 50;
    int spacing = 5;
    int total_width = 10 * slot_size + 9 * spacing;
    int start_x = (sw - total_width) / 2;
    int start_y = sh - slot_size - 20;
    
    DrawRectangle(start_x - 10, start_y - 10, total_width + 20, slot_size + 20, Fade(BLACK, 0.8f));
    
    for (int i = 0; i < 10; i++) {
        int x = start_x + i * (slot_size + spacing);
        int y = start_y;
        
        Color bg = (i == selected_slot) ? Fade(YELLOW, 0.5f) : Fade(WHITE, 0.2f);
        DrawRectangle(x, y, slot_size, slot_size, bg);
        
        int key_num = (i < 9) ? (i + 1) : 0;
        DrawText(TextFormat("%d", key_num), x + 5, y + 5, 10, LIGHTGRAY);
        
        if (i == 0) {
            DrawText("Pico", x + 10, y + 20, 15, BLACK);
        } else if (slots[i].count > 0) {
            draw_icon(slots[i].id, x + 5, y + 5, 40);
            DrawText(TextFormat("%d", slots[i].count), x + 30, y + 35, 15, WHITE);
        }
    }
}

void UI::draw_inventory_panel() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    
    int panel_w = 400;
    int panel_h = 500;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;
    
    DrawRectangle(px, py, panel_w, panel_h, Fade(BLACK, 0.95f));
    DrawText("INVENTARIO", px + 100, py + 20, 30, WHITE);
    
    for (int i = 0; i < 10; i++) {
        int x = px + 20;
        int y = py + 70 + i * 40;
        
        DrawRectangle(x, y, panel_w - 40, 35, Fade(WHITE, 0.1f));
        
        int key_num = (i < 9) ? (i + 1) : 0;
        
        if (i == 0) {
            DrawText(TextFormat("Slot [%d]: Pico", key_num), x + 50, y + 10, 20, GRAY);
        } else if (slots[i].count > 0) {
            draw_icon(slots[i].id, x + 5, y, 35);
            DrawText(TextFormat("Slot [%d]: %s (x%d)", key_num, slots[i].name.c_str(), slots[i].count), x + 50, y + 10, 20, WHITE);
        } else {
            DrawText(TextFormat("Slot [%d]: Vacio", key_num), x + 50, y + 10, 20, DARKGRAY);
        }
    }
}
