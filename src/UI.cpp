#include "UI.hpp"
#include <iostream>
#include <algorithm>
#include "rlgl.h"

using namespace Config;

UI::UI(Texture2D sheet, Texture2D items_sheet) : spritesheet(sheet), spritesheet_items(items_sheet) {
    slots.resize(10);
    slots[0] = { AIR, "Mano", -1 };
    storage.resize(18); // 6x3 storage grid
}

UI::~UI() {}

void UI::update() {
    if (IsKeyPressed(KEY_E)) {
        toggle_inventory();
    }
    
    if (is_open) {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        int icon_size = 40, icon_pad = 5;
        int slot_total = icon_size + icon_pad;
        int margin = 15;
        int tool_slots = 7;
        int left_w = tool_slots * slot_total + margin;
        int storage_cols = 6;
        int right_w = storage_cols * slot_total + margin;
        int panel_content_w = left_w + 10 + right_w;
        int panel_w = panel_content_w + margin * 2;
        
        int tools_h = slot_total + 25;
        int blocks_h = 3 * slot_total + 25;
        int storage_h = 3 * slot_total + 25;
        int craft_h = 2 * (60 + 5) + 30;
        int right_content_h = 25 + blocks_h + storage_h;
        int panel_content_h = 25 + std::max(tools_h, right_content_h) + 10 + craft_h + 10;
        int panel_h = panel_content_h + margin;
        
        int px = (sw - panel_w) / 2;
        int py = (sh - panel_h) / 2;
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            Vector2 m = GetMousePosition();
            
            // Click en herramientas (1 row: Mano + 6 tools)
            int cur_y_tools = py + 40 + 18;
            
            // Click en Mano (slot visual 0, siempre fijo) — solo seleccionar
            {
                int x = px + margin;
                int y = cur_y_tools;
                if (m.x >= x && m.x <= x + icon_size && m.y >= y && m.y <= y + icon_size) {
                    if (is_dragging) {
                        // No permitir soltar items en el slot de mano
                    } else {
                        selected_slot = 0;
                        selected_tool_idx = -1;
                    }
                }
            }
            
            // Click en slots de herramientas (visual 1..6 -> tool_inventory[0..5])
            for (int i = 0; i < tool_slots - 1; i++) {
                int x = px + margin + (i + 1) * slot_total;
                int y = cur_y_tools;
                if (m.x >= x && m.x <= x + icon_size && m.y >= y && m.y <= y + icon_size) {
                    if (is_dragging && dragging_tool) {
                        // Soltar herramienta arrastrada en este slot
                        if (i < (int)tool_inventory.size()) {
                            // Slot ocupado: swap
                            ToolSlot temp = tool_inventory[i];
                            tool_inventory[i] = drag_tool;
                            drag_tool = temp;
                        } else if (i == (int)tool_inventory.size()) {
                            // Slot justo despás del último: agregar al final
                            tool_inventory.push_back(drag_tool);
                            is_dragging = false;
                            dragging_tool = false;
                        }
                        // Si i > size, ignorar (slot vacío lejano)
                        if (is_dragging) {
                            // Todavía arrastrando (swap devolvió algo)
                        } else {
                            dragging_tool = false;
                        }
                    } else if (!is_dragging && i < (int)tool_inventory.size()) {
                        // Pick up tool
                        is_dragging = true;
                        dragging_tool = true;
                        drag_tool = tool_inventory[i];
                        drag_source_type = 1;
                        // Shift tools down to fill gap
                        for (int j = i; j < (int)tool_inventory.size() - 1; j++) {
                            tool_inventory[j] = tool_inventory[j + 1];
                        }
                        tool_inventory.pop_back();
                        selected_slot = 0;
                        selected_tool_idx = i < (int)tool_inventory.size() ? i : std::max(0, (int)tool_inventory.size() - 1);
                    }
                    break;
                }
            }
            
            // Click en bloques (hotbar 3x3)
            int right_x_click = px + left_w + 10;
            int right_y_click = py + 40 + 18;
            bool clicked_hotbar = false;
            for (int i = 0; i < 9; i++) {
                int col = i % 3;
                int row = i / 3;
                int bx = right_x_click + col * slot_total;
                int by = right_y_click + row * slot_total;
                if (m.x >= bx && m.x <= bx + icon_size && m.y >= by && m.y <= by + icon_size) {
                    clicked_hotbar = true;
                    int si = i + 1;
                    if (is_dragging && !dragging_tool) {
                        // Soltar item/bloque en hotbar
                        InventorySlot temp = slots[si];
                        slots[si] = drag_item;
                        drag_item = temp;
                        if (drag_item.count <= 0) is_dragging = false;
                    } else if (!is_dragging && slots[si].count > 0) {
                        // Pick up from hotbar
                        drag_item = slots[si];
                        slots[si] = {AIR, "", 0};
                        is_dragging = true;
                        dragging_tool = false;
                        drag_source_type = 0;
                    }
                    selected_slot = si;
                    break;
                }
            }
            
            // Click en almacenamiento (6x3)
            int storage_x = right_x_click;
            int storage_y = right_y_click + 3 * slot_total + 18 + 18;
            for (int i = 0; i < storage_cols * 3; i++) {
                int col = i % storage_cols;
                int row = i / storage_cols;
                int sx = storage_x + col * slot_total;
                int sy = storage_y + row * slot_total;
                if (m.x >= sx && m.x <= sx + icon_size && m.y >= sy && m.y <= sy + icon_size) {
                    if (is_dragging && !dragging_tool) {
                        // Soltar item/bloque en storage
                        InventorySlot temp = storage[i];
                        storage[i] = drag_item;
                        drag_item = temp;
                        if (drag_item.count <= 0) is_dragging = false;
                    } else if (!is_dragging && storage[i].count > 0) {
                        // Pick up from storage
                        drag_item = storage[i];
                        storage[i] = {AIR, "", 0};
                        is_dragging = true;
                        dragging_tool = false;
                        drag_source_type = 2;
                    }
                    break;
                }
            }
            
            // Click fuera del panel = soltar item
            if (!clicked_hotbar) {
                bool clicked_anywhere = false;
                // Check tools
                for (int i = 0; i < tool_slots; i++) {
                    int x = px + margin + i * slot_total;
                    int y = cur_y_tools;
                    if (m.x >= x && m.x <= x + icon_size && m.y >= y && m.y <= y + icon_size) {
                        clicked_anywhere = true; break;
                    }
                }
                // Check storage
                for (int i = 0; i < storage_cols * 3; i++) {
                    int col = i % storage_cols;
                    int row = i / storage_cols;
                    int sx = storage_x + col * slot_total;
                    int sy = storage_y + row * slot_total;
                    if (m.x >= sx && m.x <= sx + icon_size && m.y >= sy && m.y <= sy + icon_size) {
                        clicked_anywhere = true; break;
                    }
                }
                if (!clicked_anywhere && is_dragging) {
                    // Drop item — just lose it (or we could place in world)
                    is_dragging = false;
                }
            }
            
            // Click en recetas de crafteo (solo si no estamos arrastrando)
            if (!is_dragging) {
                int craft_y_click = std::max(cur_y_tools + slot_total + 10, right_y_click + 3 * slot_total + 18 + 3 * slot_total + 10) + 18;
                int recipes_per_row_c = 8;
                int recipe_card_w_c = (panel_w - 40) / recipes_per_row_c;
                int recipe_card_h_c = 60;
                for (int vi = 0; vi < recipes_per_row_c * 2; vi++) {
                    int r = vi + craft_scroll_offset;
                    if (r >= (int)RECIPES.size()) break;
                    int col = vi % recipes_per_row_c;
                    int row = vi / recipes_per_row_c;
                    int rx = px + 15 + col * recipe_card_w_c;
                    int ry = craft_y_click + row * (recipe_card_h_c + 5);
                    if (m.x >= rx && m.x <= rx + recipe_card_w_c - 4 && m.y >= ry && m.y <= ry + recipe_card_h_c) {
                        if (can_craft(r)) craft(r);
                        break;
                    }
                }
            }
        }
    }
    
    if (!is_open) {
        // Teclas 1-9, 0 para seleccionar slot
        for (int i = 0; i <= 9; i++) {
            int key = (i == 9) ? KEY_ZERO : (KEY_ONE + i);
            if (IsKeyPressed(key)) {
                select_slot(i);
            }
        }
        
        // Rueda del mouse: contexto depende del slot seleccionado
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            if (selected_slot == 0) {
                cycle_tool((int)wheel);
            } else {
                cycle_block((int)wheel);
            }
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

// ===================== HERRAMIENTAS =====================

void UI::add_tool(ToolType type, ToolTier tier) {
    // Buscar info de la herramienta
    const ToolInfo* info = nullptr;
    for (auto& t : TOOLS) {
        if (t.type == type && t.tier == tier) {
            info = &t;
            break;
        }
    }
    if (!info) return;

    // Verificar si ya tiene esta herramienta exacta
    for (auto& tool : tool_inventory) {
        if (tool.type == type && tool.tier == tier) {
            return; // ya tiene una igual
        }
    }

    ToolSlot ts;
    ts.type = type;
    ts.tier = tier;
    ts.durability_current = info->durability;
    ts.durability_max = info->durability;
    ts.active = (tool_inventory.empty());
    tool_inventory.push_back(ts);
}

void UI::remove_active_tool() {
    if (tool_inventory.empty()) return;
    if (selected_tool_idx >= 0 && selected_tool_idx < (int)tool_inventory.size()) {
        tool_inventory.erase(tool_inventory.begin() + selected_tool_idx);
        if (selected_tool_idx >= (int)tool_inventory.size()) {
            selected_tool_idx = std::max(0, (int)tool_inventory.size() - 1);
        }
    }
}

ToolSlot* UI::get_active_tool() {
    if (tool_inventory.empty()) return nullptr;
    if (selected_tool_idx >= 0 && selected_tool_idx < (int)tool_inventory.size()) {
        return &tool_inventory[selected_tool_idx];
    }
    return nullptr;
}

void UI::cycle_tool(int direction) {
    if (tool_inventory.empty()) return;
    selected_tool_idx += direction;
    if (selected_tool_idx < 0) selected_tool_idx = tool_inventory.size() - 1;
    if (selected_tool_idx >= (int)tool_inventory.size()) selected_tool_idx = 0;
}

void UI::cycle_block(int direction) {
    // Encontrar slots con bloques (1-9) y ciclar entre ellos
    std::vector<int> block_slots;
    for (int i = 1; i < 10; i++) {
        if (slots[i].count > 0) block_slots.push_back(i);
    }
    if (block_slots.empty()) return;
    
    // Encontrar el indice actual
    int current_idx = 0;
    for (int j = 0; j < (int)block_slots.size(); j++) {
        if (block_slots[j] == selected_slot) {
            current_idx = j;
            break;
        }
    }
    
    current_idx += direction;
    if (current_idx < 0) current_idx = block_slots.size() - 1;
    if (current_idx >= (int)block_slots.size()) current_idx = 0;
    
    selected_slot = block_slots[current_idx];
}

// ===================== BLOQUES / ITEMS =====================

void UI::add_resource(uint8_t block_type) {
    if (BLOCKS.find(block_type) == BLOCKS.end()) return;
    
    std::string bname = BLOCKS.at(block_type).name;
    
    // Primero: buscar slot existente en hotbar con el mismo bloque
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == block_type && slots[i].count > 0) {
            slots[i].count++;
            return;
        }
    }
    
    // Segundo: slot vacio en hotbar
    for (int i = 1; i < 10; i++) {
        if (slots[i].count <= 0) {
            slots[i].id = block_type;
            slots[i].name = bname;
            slots[i].count = 1;
            return;
        }
    }
    
    // Tercero: buscar en almacenamiento
    for (int i = 0; i < (int)storage.size(); i++) {
        if (storage[i].id == block_type && storage[i].count > 0) {
            storage[i].count++;
            return;
        }
    }
    for (int i = 0; i < (int)storage.size(); i++) {
        if (storage[i].count <= 0) {
            storage[i].id = block_type;
            storage[i].name = bname;
            storage[i].count = 1;
            return;
        }
    }
}

void UI::add_item(uint8_t item_id, int count) {
    if (ITEMS.find(item_id) == ITEMS.end()) return;
    
    std::string iname = ITEMS.at(item_id).name;
    
    // Buscar slot existente en hotbar con el mismo item
    for (int i = 1; i < 10; i++) {
        if (slots[i].count > 0 && slots[i].name == iname) {
            slots[i].count += count;
            return;
        }
    }
    
    // Buscar slot vacio en hotbar
    for (int i = 1; i < 10; i++) {
        if (slots[i].count <= 0) {
            slots[i].id = 254;
            slots[i].name = iname;
            slots[i].count = count;
            return;
        }
    }
    
    // Buscar en almacenamiento
    for (int i = 0; i < (int)storage.size(); i++) {
        if (storage[i].count > 0 && storage[i].name == iname) {
            storage[i].count += count;
            return;
        }
    }
    for (int i = 0; i < (int)storage.size(); i++) {
        if (storage[i].count <= 0) {
            storage[i].id = 254;
            storage[i].name = iname;
            storage[i].count = count;
            return;
        }
    }
}

bool UI::has_item(uint8_t item_id, int count) const {
    if (ITEMS.find(item_id) == ITEMS.end()) return false;
    std::string iname = ITEMS.at(item_id).name;
    int total = 0;
    for (int i = 1; i < 10; i++) {
        if (slots[i].count > 0 && slots[i].name == iname) {
            total += slots[i].count;
        }
    }
    for (int i = 0; i < (int)storage.size(); i++) {
        if (storage[i].count > 0 && storage[i].name == iname) {
            total += storage[i].count;
        }
    }
    return total >= count;
}

bool UI::remove_item(uint8_t item_id, int count) {
    if (!has_item(item_id, count)) return false;
    std::string iname = ITEMS.at(item_id).name;
    int remaining = count;
    // Remover de hotbar primero
    for (int i = 1; i < 10 && remaining > 0; i++) {
        if (slots[i].count > 0 && slots[i].name == iname) {
            int take = std::min(remaining, slots[i].count);
            slots[i].count -= take;
            remaining -= take;
            if (slots[i].count <= 0) {
                slots[i].id = AIR;
                slots[i].name = "";
                slots[i].count = 0;
            }
        }
    }
    // Luego de almacenamiento
    for (int i = 0; i < (int)storage.size() && remaining > 0; i++) {
        if (storage[i].count > 0 && storage[i].name == iname) {
            int take = std::min(remaining, storage[i].count);
            storage[i].count -= take;
            remaining -= take;
            if (storage[i].count <= 0) {
                storage[i].id = AIR;
                storage[i].name = "";
                storage[i].count = 0;
            }
        }
    }
    return true;
}

void UI::add_log() {
    add_item(ITEM_PLANKS, 4); // Simplificado: tronco -> 4 tablas directamente
}

// ===================== CRAFTEO =====================

bool UI::can_craft(int recipe_index) const {
    if (recipe_index < 0 || recipe_index >= (int)RECIPES.size()) return false;
    const CraftingRecipe& r = RECIPES[recipe_index];
    for (auto& ing : r.ingredients) {
        if (ing.is_item) {
            if (!has_item(ing.id, ing.count)) return false;
        } else {
            // Es un bloque - buscar en slots
            int total = 0;
            for (int i = 1; i < 10; i++) {
                if (slots[i].id == ing.id && slots[i].count > 0) {
                    total += slots[i].count;
                }
            }
            if (total < ing.count) return false;
        }
    }
    return true;
}

void UI::craft(int recipe_index) {
    if (!can_craft(recipe_index)) return;
    const CraftingRecipe& r = RECIPES[recipe_index];
    
    // Remover ingredientes
    for (auto& ing : r.ingredients) {
        if (ing.is_item) {
            remove_item(ing.id, ing.count);
        } else {
            // Remover bloques
            int remaining = ing.count;
            for (int i = 1; i < 10 && remaining > 0; i++) {
                if (slots[i].id == ing.id && slots[i].count > 0) {
                    int take = std::min(remaining, slots[i].count);
                    slots[i].count -= take;
                    remaining -= take;
                    if (slots[i].count <= 0) {
                        slots[i].id = AIR;
                        slots[i].name = "";
                        slots[i].count = 0;
                    }
                }
            }
        }
    }
    
    // Agregar resultado
    if (r.result_is_tool) {
        add_tool(r.result_tool_type, r.result_tool_tier);
    } else {
        add_item(r.result_item_id, r.result_count);
    }
}

// ===================== DIBUJAR =====================

void UI::draw_block_icon(uint8_t block_id, int x, int y, int size) {
    if (BLOCKS.find(block_id) == BLOCKS.end()) return;
    auto b = BLOCKS.at(block_id);
    float tex_w = spritesheet.width / 9.0f;
    float tex_h = spritesheet.height / 10.0f;
    float correct_y = 10.0f - 1.0f - b.tex_y;
    Rectangle src = { b.tex_x * tex_w, correct_y * tex_h, tex_w, tex_h };
    Rectangle dest = { (float)x, (float)y, (float)size, (float)size };
    DrawTexturePro(spritesheet, src, dest, {0, 0}, 0.0f, WHITE);
}

void UI::draw_tool_icon(ToolType type, ToolTier tier, int x, int y, int size) {
    for (auto& t : TOOLS) {
        if (t.type == type && t.tier == tier) {
            float tex_w = spritesheet_items.width / 7.0f;
            float tex_h = spritesheet_items.height / 8.0f;
            float correct_y = 8.0f - 1.0f - t.item_tex_y;
            Rectangle src = { t.item_tex_x * tex_w, correct_y * tex_h, tex_w, tex_h };
            Rectangle dest = { (float)x, (float)y, (float)size, (float)size };
            DrawTexturePro(spritesheet_items, src, dest, {0, 0}, 0.0f, WHITE);
            return;
        }
    }
}

void UI::draw_item_icon(uint8_t item_id, int x, int y, int size) {
    if (ITEMS.find(item_id) == ITEMS.end()) return;
    auto& item = ITEMS.at(item_id);
    float tex_w = spritesheet_items.width / 7.0f;
    float tex_h = spritesheet_items.height / 8.0f;
    float correct_y = 8.0f - 1.0f - item.item_tex_y;
    Rectangle src = { item.item_tex_x * tex_w, correct_y * tex_h, tex_w, tex_h };
    Rectangle dest = { (float)x, (float)y, (float)size, (float)size };
    DrawTexturePro(spritesheet_items, src, dest, {0, 0}, 0.0f, WHITE);
}

void UI::draw() {
    if (!is_open) {
        DrawRectangle(GetScreenWidth()/2 - 2, GetScreenHeight()/2 - 2, 4, 4, WHITE);
    }
    
    draw_hotbar();
    
    if (!is_open) {
        draw_tool_hud();
    }
    
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
            // Slot de herramienta
            ToolSlot* tool = get_active_tool();
            if (tool) {
                draw_tool_icon(tool->type, tool->tier, x + 5, y + 5, 40);
                // Barra de durabilidad mini
                float pct = (float)tool->durability_current / (float)tool->durability_max;
                Color dur_col = pct > 0.5f ? GREEN : (pct > 0.25f ? YELLOW : RED);
                DrawRectangle(x + 5, y + 42, (int)(40 * pct), 3, dur_col);
            } else {
                DrawText("Mano", x + 10, y + 20, 12, GRAY);
            }
        } else if (slots[i].count > 0) {
            if (slots[i].id == 254) {
                // Item (no bloque) - dibujar desde items spritesheet
                // Buscar item_id por nombre
                for (auto& [iid, itype] : ITEMS) {
                    if (itype.name == slots[i].name) {
                        draw_item_icon(iid, x + 5, y + 5, 40);
                        break;
                    }
                }
            } else {
                draw_block_icon(slots[i].id, x + 5, y + 5, 40);
            }
            DrawText(TextFormat("%d", slots[i].count), x + 30, y + 35, 15, WHITE);
        }
    }
}

void UI::draw_tool_hud() {
    ToolSlot* tool = get_active_tool();
    if (!tool) return;
    
    const ToolInfo* info = nullptr;
    for (auto& t : TOOLS) {
        if (t.type == tool->type && t.tier == tool->tier) {
            info = &t;
            break;
        }
    }
    if (!info) return;
    
    // Posicion: abajo a la izquierda del hotbar
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int slot_size = 50;
    int spacing = 5;
    int total_width = 10 * slot_size + 9 * spacing;
    int hotbar_x = (sw - total_width) / 2;
    int hotbar_y = sh - slot_size - 20;
    
    int hud_x = hotbar_x;
    int hud_y = hotbar_y - 45;
    
    // Icono de la herramienta
    draw_tool_icon(tool->type, tool->tier, hud_x, hud_y, 32);
    
    // Nombre al lado del icono
    DrawText(info->name.c_str(), hud_x + 38, hud_y + 2, 14, WHITE);
    
    // Barra de durabilidad debajo del nombre
    float pct = (float)tool->durability_current / (float)tool->durability_max;
    int bar_w = 100;
    int bar_h = 5;
    int bar_x = hud_x + 38;
    int bar_y = hud_y + 20;
    
    DrawRectangle(bar_x, bar_y, bar_w, bar_h, Fade(BLACK, 0.6f));
    Color dur_col = pct > 0.5f ? GREEN : (pct > 0.25f ? YELLOW : RED);
    DrawRectangle(bar_x, bar_y, (int)(bar_w * pct), bar_h, dur_col);
    
    // Texto durabilidad
    DrawText(TextFormat("%d/%d", tool->durability_current, tool->durability_max), bar_x + bar_w + 5, bar_y - 1, 10, LIGHTGRAY);
}

void UI::draw_inventory_panel() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    
    int icon_size = 40;
    int icon_pad = 5;
    int slot_total = icon_size + icon_pad;
    std::string tooltip_text;
    
    // Layout constants
    int tool_slots = 7;       // Mano + 6 tools (1 row)
    int hotbar_slots = 9;     // 3x3
    int storage_cols = 6;
    int storage_rows = 3;     // 6x3 = 18 storage slots
    int margin = 15;
    
    // Calculate panel size from content
    int tools_w = tool_slots * slot_total + margin;
    int blocks_w = storage_cols * slot_total + margin;
    int left_w = tools_w;
    int right_w = blocks_w;
    int panel_content_w = left_w + 10 + right_w;
    
    int tools_h = slot_total + 25;
    int blocks_h = 3 * slot_total + 25;
    int storage_h = storage_rows * slot_total + 25;
    int craft_h = 2 * (60 + 5) + 30;
    int right_content_h = 25 + blocks_h + storage_h;
    int panel_content_h = 25 + std::max(tools_h, right_content_h) + 10 + craft_h + 10;
    
    int panel_w = panel_content_w + margin * 2;
    int panel_h = panel_content_h + margin;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;
    
    DrawRectangle(px, py, panel_w, panel_h, Fade(BLACK, 0.95f));
    DrawText("INVENTARIO", px + panel_w/2 - MeasureText("INVENTARIO", 24)/2, py + 8, 24, WHITE);
    
    int cur_y = py + 40;
    
    // === HERRAMIENTAS (fila unica: Mano + 6 tools) ===
    DrawText("HERRAMIENTAS", px + margin, cur_y, 14, YELLOW);
    cur_y += 18;
    
    // Mano slot (fijo, siempre primero)
    {
        int x = px + margin;
        int y = cur_y;
        bool is_selected = (selected_slot == 0 && selected_tool_idx == -1);
        Color bg = is_selected ? Fade(YELLOW, 0.4f) : Fade(WHITE, 0.1f);
        DrawRectangle(x, y, icon_size, icon_size, bg);
        DrawRectangleLines(x, y, icon_size, icon_size, Fade(WHITE, 0.3f));
        
        int hand_cx = x + icon_size / 2;
        int hand_cy = y + icon_size / 2;
        Color skin = {210, 170, 130, 255};
        DrawCircle(hand_cx - 3, hand_cy - 5, 3, Fade(skin, 0.8f));
        DrawLine(hand_cx - 3, hand_cy - 2, hand_cx - 3, hand_cy + 4, Fade(skin, 0.8f));
        DrawLine(hand_cx - 3, hand_cy + 4, hand_cx - 7, hand_cy + 8, Fade(skin, 0.8f));
        DrawLine(hand_cx - 3, hand_cy + 4, hand_cx + 1, hand_cy + 8, Fade(skin, 0.8f));
        DrawLine(hand_cx - 3, hand_cy, hand_cx + 3, hand_cy - 4, Fade(skin, 0.8f));
        
        Vector2 m = GetMousePosition();
        if (m.x >= x && m.x <= x + icon_size && m.y >= y && m.y <= y + icon_size) {
            tooltip_text = "Mano (sin herramienta)";
        }
    }
    
    // Tool slots (1..6)
    for (int i = 0; i < tool_slots - 1; i++) {
        int x = px + margin + (i + 1) * slot_total;
        int y = cur_y;
        
        DrawRectangleLines(x, y, icon_size, icon_size, Fade(WHITE, 0.2f));
        
        if (i < (int)tool_inventory.size()) {
            ToolSlot& t = tool_inventory[i];
            const ToolInfo* info = nullptr;
            for (auto& ti : TOOLS) {
                if (ti.type == t.type && ti.tier == t.tier) { info = &ti; break; }
            }
            
            bool is_selected = (selected_slot == 0 && i == selected_tool_idx);
            Color bg = is_selected ? Fade(YELLOW, 0.4f) : Fade(WHITE, 0.1f);
            DrawRectangle(x + 1, y + 1, icon_size - 2, icon_size - 2, bg);
            
            if (info) {
                draw_tool_icon(t.type, t.tier, x + 4, y + 4, icon_size - 8);
                float pct = (float)t.durability_current / (float)t.durability_max;
                Color dur_col = pct > 0.5f ? GREEN : (pct > 0.25f ? YELLOW : RED);
                DrawRectangle(x + 4, y + icon_size - 7, icon_size - 8, 3, dur_col);
            }
            
            Vector2 m = GetMousePosition();
            if (m.x >= x && m.x <= x + icon_size && m.y >= y && m.y <= y + icon_size && info) {
                tooltip_text = info->name + " (" + std::to_string(t.durability_current) + "/" + std::to_string(t.durability_max) + ")";
            }
        }
    }
    
    cur_y += slot_total + 10;
    
    // === BLOQUES: Hotbar 3x3 + Almacenamiento 6x3 ===
    int right_x = px + left_w + 10;
    int right_y = py + 40;
    
    DrawText("BLOQUES", right_x, right_y, 14, YELLOW);
    right_y += 18;
    
    // Hotbar 3x3
    for (int i = 0; i < hotbar_slots; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = right_x + col * slot_total;
        int y = right_y + row * slot_total;
        
        bool is_selected = (selected_slot == (i + 1));
        Color bg = is_selected ? Fade(YELLOW, 0.3f) : Fade(WHITE, 0.05f);
        DrawRectangle(x, y, icon_size, icon_size, bg);
        DrawRectangleLines(x, y, icon_size, icon_size, Fade(WHITE, 0.2f));
        
        int slot_idx = i + 1;
        if (slots[slot_idx].count > 0) {
            if (slots[slot_idx].id == 254) {
                for (auto& [iid, itype] : ITEMS) {
                    if (itype.name == slots[slot_idx].name) {
                        draw_item_icon(iid, x + 4, y + 4, icon_size - 8);
                        break;
                    }
                }
            } else {
                draw_block_icon(slots[slot_idx].id, x + 4, y + 4, icon_size - 8);
            }
            DrawText(TextFormat("x%d", slots[slot_idx].count), x + 3, y + icon_size - 11, 9, WHITE);
        }
        
        DrawText(TextFormat("%d", (i == 9) ? 0 : (i + 1)), x + 2, y + 1, 8, Fade(WHITE, 0.3f));
        
        Vector2 m = GetMousePosition();
        if (m.x >= x && m.x <= x + icon_size && m.y >= y && m.y <= y + icon_size && slots[slot_idx].count > 0) {
            tooltip_text = std::string(slots[slot_idx].name) + " x" + std::to_string(slots[slot_idx].count);
        }
    }
    
    right_y += 3 * slot_total + 8;
    DrawText("ALMACENAMIENTO", right_x, right_y, 14, YELLOW);
    right_y += 18;
    
    // Storage 6x3
    for (int i = 0; i < storage_cols * storage_rows; i++) {
        int col = i % storage_cols;
        int row = i / storage_cols;
        int x = right_x + col * slot_total;
        int y = right_y + row * slot_total;
        
        DrawRectangleLines(x, y, icon_size, icon_size, Fade(WHITE, 0.15f));
        
        if (i < (int)storage.size() && storage[i].count > 0) {
            if (storage[i].id == 254) {
                for (auto& [iid, itype] : ITEMS) {
                    if (itype.name == storage[i].name) {
                        draw_item_icon(iid, x + 4, y + 4, icon_size - 8);
                        break;
                    }
                }
            } else {
                draw_block_icon(storage[i].id, x + 4, y + 4, icon_size - 8);
            }
            DrawText(TextFormat("x%d", storage[i].count), x + 3, y + icon_size - 11, 9, WHITE);
            
            Vector2 m = GetMousePosition();
            if (m.x >= x && m.x <= x + icon_size && m.y >= y && m.y <= y + icon_size) {
                tooltip_text = std::string(storage[i].name) + " x" + std::to_string(storage[i].count);
            }
        }
    }
    
    // === CRAFTEO (parte inferior, scrollable) ===
    int craft_y = std::max(cur_y, right_y + 3 * slot_total + 10);
    DrawText("CRAFTING", px + margin, craft_y, 14, YELLOW);
    craft_y += 18;
    
    // Scroll con rueda del mouse
    Vector2 m = GetMousePosition();
    bool mouse_in_craft = (m.x >= px && m.x <= px + panel_w && m.y >= craft_y + 20 && m.y <= py + panel_h - 10);
    if (mouse_in_craft) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            craft_scroll_offset -= (int)(wheel * 3);
            int max_scroll = (int)RECIPES.size() - 8;
            if (max_scroll < 0) max_scroll = 0;
            if (craft_scroll_offset < 0) craft_scroll_offset = 0;
            if (craft_scroll_offset > max_scroll) craft_scroll_offset = max_scroll;
        }
    }
    
    int recipes_per_row = 8;
    int visible_rows = 2;
    int visible_count = recipes_per_row * visible_rows;
    int total_recipes = (int)RECIPES.size();
    
    // Clamp scroll
    int max_scroll = total_recipes - visible_count;
    if (max_scroll < 0) max_scroll = 0;
    if (craft_scroll_offset > max_scroll) craft_scroll_offset = max_scroll;
    if (craft_scroll_offset < 0) craft_scroll_offset = 0;
    
    int recipe_card_w = (panel_w - 40) / recipes_per_row;
    int recipe_card_h = 60;
    
    // Clip area for recipes
    rlDrawRenderBatchActive();
    BeginScissorMode(px + 5, craft_y + 20, panel_w - 10, visible_rows * (recipe_card_h + 5) + 5);
    
    for (int vi = 0; vi < visible_count; vi++) {
        int r = vi + craft_scroll_offset;
        if (r >= total_recipes) break;
        
        const CraftingRecipe& rec = RECIPES[r];
        bool available = can_craft(r);
        
        int col = vi % recipes_per_row;
        int row = vi / recipes_per_row;
        int rx = px + 15 + col * recipe_card_w;
        int ry = craft_y + 22 + row * (recipe_card_h + 5);
        
        Color bg = available ? Fade(WHITE, 0.15f) : Fade(WHITE, 0.05f);
        DrawRectangle(rx, ry, recipe_card_w - 4, recipe_card_h, bg);
        
        // Icono del resultado
        if (rec.result_is_tool) {
            const ToolInfo* tinfo = nullptr;
            for (auto& t : TOOLS) {
                if (t.type == rec.result_tool_type && t.tier == rec.result_tool_tier) { tinfo = &t; break; }
            }
            if (tinfo) draw_tool_icon(tinfo->type, tinfo->tier, rx + 3, ry + 2, 28);
        } else {
            draw_item_icon(rec.result_item_id, rx + 3, ry + 2, 28);
        }
        
        // Ingredientes como iconos pequeños
        int ing_x = rx + 34;
        for (size_t ig = 0; ig < rec.ingredients.size() && ig < 4; ig++) {
            auto& ing = rec.ingredients[ig];
            if (ing.is_item) {
                draw_item_icon(ing.id, ing_x, ry + 4, 14);
            } else {
                draw_block_icon(ing.id, ing_x, ry + 4, 14);
            }
            DrawText(TextFormat("x%d", ing.count), ing_x, ry + 20, 8, available ? GREEN : DARKGRAY);
            ing_x += 18;
        }
        
        // Cantidad resultado
        if (rec.result_count > 1) {
            DrawText(TextFormat("x%d", rec.result_count), rx + 3, ry + 35, 10, available ? GREEN : DARKGRAY);
        }
        
        if (available) {
            DrawText("[Craft]", rx + 3, ry + 48, 10, GREEN);
        }
        
        // Tooltip on hover
        Vector2 mpos = GetMousePosition();
        if (mpos.x >= rx && mpos.x <= rx + recipe_card_w - 4 && mpos.y >= ry && mpos.y <= ry + recipe_card_h) {
            tooltip_text = rec.result_name + " (";
            for (size_t j = 0; j < rec.ingredients.size(); j++) {
                if (j > 0) tooltip_text += " + ";
                std::string iname = rec.ingredients[j].is_item ? ITEMS.at(rec.ingredients[j].id).name : BLOCKS.at(rec.ingredients[j].id).name;
                tooltip_text += std::to_string(rec.ingredients[j].count) + " " + iname;
            }
            tooltip_text += ")";
        }
    }
    
    EndScissorMode();
    
    // Scrollbar
    if (total_recipes > visible_count) {
        int sb_x = px + panel_w - 12;
        int sb_y = craft_y + 20;
        int sb_h = visible_rows * (recipe_card_h + 5) + 5;
        DrawRectangle(sb_x, sb_y, 8, sb_h, Fade(WHITE, 0.1f));
        
        float scroll_ratio = (max_scroll > 0) ? (float)craft_scroll_offset / (float)max_scroll : 0.0f;
        int thumb_h = std::max(20, sb_h * visible_count / total_recipes);
        int thumb_y = sb_y + (int)(scroll_ratio * (sb_h - thumb_h));
        DrawRectangle(sb_x, thumb_y, 8, thumb_h, Fade(WHITE, 0.4f));
        
        // Page info
        int page_start = craft_scroll_offset + 1;
        int page_end = std::min(craft_scroll_offset + visible_count, total_recipes);
        DrawText(TextFormat("%d-%d / %d", page_start, page_end, total_recipes), px + panel_w - 90, craft_y + 4, 10, DARKGRAY);
    }
    
    // Dibujar tooltip al final (encima de todo)
    if (!tooltip_text.empty()) {
        Vector2 mpos = GetMousePosition();
        int tw = MeasureText(tooltip_text.c_str(), 14);
        int tx = (int)mpos.x + 15;
        int ty = (int)mpos.y - 10;
        if (tx + tw + 10 > sw) tx = sw - tw - 15;
        if (ty < 0) ty = 5;
        DrawRectangle(tx - 5, ty - 3, tw + 10, 22, Fade(BLACK, 0.9f));
        DrawText(tooltip_text.c_str(), tx, ty, 14, WHITE);
    }
    
    // Dibujar item arrastrado siguiendo al cursor
    if (is_dragging) {
        Vector2 mpos = GetMousePosition();
        int dx = (int)mpos.x - icon_size / 2;
        int dy = (int)mpos.y - icon_size / 2;
        DrawRectangle(dx, dy, icon_size, icon_size, Fade(YELLOW, 0.3f));
        DrawRectangleLines(dx, dy, icon_size, icon_size, YELLOW);
        
        if (dragging_tool) {
            draw_tool_icon(drag_tool.type, drag_tool.tier, dx + 4, dy + 4, icon_size - 8);
            float pct = (float)drag_tool.durability_current / (float)drag_tool.durability_max;
            Color dur_col = pct > 0.5f ? GREEN : (pct > 0.25f ? YELLOW : RED);
            DrawRectangle(dx + 4, dy + icon_size - 7, icon_size - 8, 3, dur_col);
        } else if (drag_item.id == 254) {
            for (auto& [iid, itype] : ITEMS) {
                if (itype.name == drag_item.name) {
                    draw_item_icon(iid, dx + 4, dy + 4, icon_size - 8);
                    break;
                }
            }
            DrawText(TextFormat("x%d", drag_item.count), dx + 3, dy + icon_size - 11, 9, WHITE);
        } else if (drag_item.id != AIR && drag_item.count > 0) {
            draw_block_icon(drag_item.id, dx + 4, dy + 4, icon_size - 8);
            DrawText(TextFormat("x%d", drag_item.count), dx + 3, dy + icon_size - 11, 9, WHITE);
        }
    }
}
