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

void UI::cancel_drag() {
    if (!is_dragging) return;
    
    if (dragging_tool) {
        if (drag_source_type == 1 && drag_source_idx >= 0 && drag_source_idx <= (int)tool_inventory.size()) {
            tool_inventory.insert(tool_inventory.begin() + std::min(drag_source_idx, (int)tool_inventory.size()), drag_tool);
        } else {
            tool_inventory.push_back(drag_tool);
        }
    } else {
        if (drag_item.count > 0 && (drag_item.id != Config::AIR || !drag_item.name.empty())) {
            if (drag_source_type == 0 && drag_source_idx >= 1 && drag_source_idx < (int)slots.size() && slots[drag_source_idx].count == 0) {
                slots[drag_source_idx] = drag_item;
            } else if (drag_source_type == 2 && drag_source_idx >= 0 && drag_source_idx < (int)storage.size() && storage[drag_source_idx].count == 0) {
                storage[drag_source_idx] = drag_item;
            } else {
                if (drag_item.id == 254) {
                    for (auto& [iid, itype] : Config::ITEMS) {
                        if (itype.name == drag_item.name) {
                            add_item(iid, drag_item.count);
                            break;
                        }
                    }
                } else if (drag_item.id != Config::AIR) {
                    for (int c = 0; c < drag_item.count; ++c) {
                        add_resource(drag_item.id);
                    }
                }
            }
        }
    }
    is_dragging = false;
    dragging_tool = false;
    drag_source_type = -1;
    drag_source_idx = -1;
}

void UI::toggle_inventory() {
    if (is_open) {
        cancel_drag();
    }
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

void UI::update() {
    if (IsKeyPressed(KEY_E)) {
        toggle_inventory();
    }
    
    if (is_open) {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        
        int panel_w = 950;
        int panel_h = 570;
        int px = (sw - panel_w) / 2;
        int py = (sh - panel_h) / 2;
        
        int slot_size = 42;
        int slot_pad = 5;
        int slot_total = slot_size + slot_pad;
        
        int left_x = px + 20;
        int right_x = px + 480;
        
        int tools_y = py + 65;
        int storage_y = py + 155;
        int hotbar_y = py + 395;
        
        int craft_y = py + 95;
        int craft_w = panel_w - (right_x - px) - 20;
        int craft_h = panel_h - 115;
        
        Vector2 m = GetMousePosition();
        
        // Clic derecho cancela drag inmediatamente
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
            cancel_drag();
        }
        
        // Scroll con rueda del mouse en crafteo
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            bool mouse_in_craft = (m.x >= right_x && m.x <= right_x + craft_w && m.y >= craft_y && m.y <= craft_y + craft_h);
            if (mouse_in_craft) {
                craft_scroll_offset -= (int)(wheel * 2);
                if (craft_scroll_offset < 0) craft_scroll_offset = 0;
            }
        }
        
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            bool click_handled = false;
            bool shift_down = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            
            // 0. Botón de cerrar [ X ]
            if (m.x >= px + panel_w - 35 && m.x <= px + panel_w - 10 && m.y >= py + 10 && m.y <= py + 35) {
                toggle_inventory();
                return;
            }
            
            // 1. Pestañas de categorías de crafteo
            int tab_y = py + 60;
            int tab_w = 98;
            int tab_h = 26;
            for (int t = 0; t < 4; ++t) {
                int tx = right_x + t * (tab_w + 6);
                if (m.x >= tx && m.x <= tx + tab_w && m.y >= tab_y && m.y <= tab_y + tab_h) {
                    craft_category = t;
                    craft_scroll_offset = 0;
                    click_handled = true;
                    break;
                }
            }
            
            // 2. Click en Mano (Slot 0 de herramientas)
            if (!click_handled) {
                int hx = left_x;
                int hy = tools_y;
                if (m.x >= hx && m.x <= hx + slot_size && m.y >= hy && m.y <= hy + slot_size) {
                    click_handled = true;
                    if (is_dragging) {
                        cancel_drag();
                    } else {
                        selected_slot = 0;
                        selected_tool_idx = -1; // Equipar Mano
                    }
                }
            }
            
            // 3. Click en slots de herramientas (1 fila: 6 herramientas)
            if (!click_handled) {
                for (int i = 0; i < 6; i++) {
                    int tx = left_x + (i + 1) * slot_total;
                    int ty = tools_y;
                    
                    if (m.x >= tx && m.x <= tx + slot_size && m.y >= ty && m.y <= ty + slot_size) {
                        click_handled = true;
                        if (is_dragging) {
                            if (dragging_tool) {
                                if (i < (int)tool_inventory.size()) {
                                    ToolSlot temp = tool_inventory[i];
                                    tool_inventory[i] = drag_tool;
                                    drag_tool = temp;
                                    drag_source_type = 1;
                                    drag_source_idx = i;
                                } else {
                                    tool_inventory.push_back(drag_tool);
                                    is_dragging = false;
                                    dragging_tool = false;
                                    drag_source_type = -1;
                                    drag_source_idx = -1;
                                }
                            } else {
                                cancel_drag();
                            }
                        } else {
                            if (i < (int)tool_inventory.size()) {
                                selected_slot = 0;
                                selected_tool_idx = i;
                                
                                drag_tool = tool_inventory[i];
                                tool_inventory.erase(tool_inventory.begin() + i);
                                is_dragging = true;
                                dragging_tool = true;
                                drag_source_type = 1;
                                drag_source_idx = i;
                            }
                        }
                        break;
                    }
                }
            }
            
            // 4. Click en Storage (Mochila 6x3)
            if (!click_handled) {
                int storage_cols = 6;
                for (int i = 0; i < (int)storage.size(); i++) {
                    int col = i % storage_cols;
                    int row = i / storage_cols;
                    int sx = left_x + col * slot_total;
                    int sy = storage_y + row * slot_total;
                    
                    if (m.x >= sx && m.x <= sx + slot_size && m.y >= sy && m.y <= sy + slot_size) {
                        click_handled = true;
                        if (is_dragging) {
                            if (!dragging_tool) {
                                if (storage[i].count == 0) {
                                    storage[i] = drag_item;
                                    is_dragging = false;
                                } else if (storage[i].id == drag_item.id && storage[i].name == drag_item.name) {
                                    storage[i].count += drag_item.count;
                                    is_dragging = false;
                                } else {
                                    InventorySlot temp = storage[i];
                                    storage[i] = drag_item;
                                    drag_item = temp;
                                    drag_source_type = 2;
                                    drag_source_idx = i;
                                }
                            } else {
                                cancel_drag();
                            }
                        } else {
                            if (storage[i].count > 0) {
                                if (shift_down) {
                                    bool moved = false;
                                    for (int h = 1; h < 10; h++) {
                                        if (slots[h].id == storage[i].id && slots[h].name == storage[i].name) {
                                            slots[h].count += storage[i].count;
                                            storage[i] = { AIR, "", 0 };
                                            moved = true; break;
                                        }
                                    }
                                    if (!moved) {
                                        for (int h = 1; h < 10; h++) {
                                            if (slots[h].count == 0) {
                                                slots[h] = storage[i];
                                                storage[i] = { AIR, "", 0 };
                                                moved = true; break;
                                            }
                                        }
                                    }
                                } else {
                                    drag_item = storage[i];
                                    storage[i] = { AIR, "", 0 };
                                    is_dragging = true;
                                    dragging_tool = false;
                                    drag_source_type = 2;
                                    drag_source_idx = i;
                                }
                            }
                        }
                        break;
                    }
                }
            }
            
            // 5. Click en Hotbar (Slots 1-9)
            if (!click_handled) {
                for (int i = 0; i < 9; i++) {
                    int hx = left_x + i * slot_total;
                    int hy = hotbar_y;
                    
                    if (m.x >= hx && m.x <= hx + slot_size && m.y >= hy && m.y <= hy + slot_size) {
                        click_handled = true;
                        int si = i + 1;
                        selected_slot = si;
                        
                        if (is_dragging) {
                            if (!dragging_tool) {
                                if (slots[si].count == 0) {
                                    slots[si] = drag_item;
                                    is_dragging = false;
                                } else if (slots[si].id == drag_item.id && slots[si].name == drag_item.name) {
                                    slots[si].count += drag_item.count;
                                    is_dragging = false;
                                } else {
                                    InventorySlot temp = slots[si];
                                    slots[si] = drag_item;
                                    drag_item = temp;
                                    drag_source_type = 0;
                                    drag_source_idx = si;
                                }
                            } else {
                                cancel_drag();
                            }
                        } else {
                            if (slots[si].count > 0) {
                                if (shift_down) {
                                    bool moved = false;
                                    for (size_t s = 0; s < storage.size(); s++) {
                                        if (storage[s].id == slots[si].id && storage[s].name == slots[si].name) {
                                            storage[s].count += slots[si].count;
                                            slots[si] = { AIR, "", 0 };
                                            moved = true; break;
                                        }
                                    }
                                    if (!moved) {
                                        for (size_t s = 0; s < storage.size(); s++) {
                                            if (storage[s].count == 0) {
                                                storage[s] = slots[si];
                                                slots[si] = { AIR, "", 0 };
                                                moved = true; break;
                                            }
                                        }
                                    }
                                } else {
                                    drag_item = slots[si];
                                    slots[si] = { AIR, "", 0 };
                                    is_dragging = true;
                                    dragging_tool = false;
                                    drag_source_type = 0;
                                    drag_source_idx = si;
                                }
                            }
                        }
                        break;
                    }
                }
            }
            
            // 6. Click en recetas de Crafteo
            if (!click_handled && !is_dragging) {
                std::vector<int> filtered;
                for (size_t r = 0; r < RECIPES.size(); ++r) {
                    if (craft_category == 1 && !RECIPES[r].result_is_tool) continue;
                    if (craft_category == 2 && RECIPES[r].result_is_tool) continue;
                    if (craft_category == 3 && !can_craft((int)r)) continue;
                    filtered.push_back((int)r);
                }
                
                int card_w = (craft_w - 18) / 2;
                int card_h = 72;
                int visible_cards = 10;
                
                for (int vi = 0; vi < visible_cards; ++vi) {
                    int f_idx = vi + craft_scroll_offset * 2;
                    if (f_idx >= (int)filtered.size()) break;
                    
                    int recipe_idx = filtered[f_idx];
                    int col = vi % 2;
                    int row = vi / 2;
                    
                    int rx = right_x + col * (card_w + 10);
                    int ry = craft_y + row * (card_h + 8);
                    
                    if (m.x >= rx && m.x <= rx + card_w && m.y >= ry && m.y <= ry + card_h) {
                        click_handled = true;
                        if (can_craft(recipe_idx)) {
                            craft(recipe_idx, shift_down ? 5 : 1);
                        }
                        break;
                    }
                }
            }
            
            // 7. Click fuera de cualquier slot -> recuperar item seguro
            if (!click_handled && is_dragging) {
                cancel_drag();
            }
        }
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
            if (selected_slot == 0) {
                cycle_tool((int)wheel);
            } else {
                cycle_block((int)wheel);
            }
        }
    }
}

// ===================== HERRAMIENTAS =====================

void UI::add_tool(ToolType type, ToolTier tier) {
    const ToolInfo* info = nullptr;
    for (auto& t : TOOLS) {
        if (t.type == type && t.tier == tier) {
            info = &t;
            break;
        }
    }
    if (!info) return;

    // Permitir múltiples herramientas del mismo tipo (sin sobreescribir ni bloquear)
    ToolSlot ts;
    ts.type = type;
    ts.tier = tier;
    ts.durability_current = info->durability;
    ts.durability_max = info->durability;
    ts.active = false;
    tool_inventory.push_back(ts);

    if (selected_tool_idx < 0) {
        selected_tool_idx = 0;
    }
}

void UI::remove_active_tool() {
    if (tool_inventory.empty()) return;
    if (selected_tool_idx >= 0 && selected_tool_idx < (int)tool_inventory.size()) {
        tool_inventory.erase(tool_inventory.begin() + selected_tool_idx);
        if (selected_tool_idx >= (int)tool_inventory.size()) {
            selected_tool_idx = tool_inventory.empty() ? -1 : (int)tool_inventory.size() - 1;
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
    int total = (int)tool_inventory.size() + 1; // +1 por Mano (-1)
    if (total <= 1) {
        selected_tool_idx = -1;
        return;
    }
    
    int val = selected_tool_idx + 1; // mapear -1..size-1 a 0..size
    val = (val + direction) % total;
    if (val < 0) val += total;
    selected_tool_idx = val - 1;
}

void UI::cycle_block(int direction) {
    std::vector<int> block_slots;
    for (int i = 1; i < 10; i++) {
        if (slots[i].count > 0) block_slots.push_back(i);
    }
    if (block_slots.empty()) return;
    
    int current_idx = 0;
    for (int j = 0; j < (int)block_slots.size(); j++) {
        if (block_slots[j] == selected_slot) {
            current_idx = j;
            break;
        }
    }
    
    current_idx = (current_idx + direction) % (int)block_slots.size();
    if (current_idx < 0) current_idx += (int)block_slots.size();
    
    selected_slot = block_slots[current_idx];
}

// ===================== BLOQUES / ITEMS =====================

void UI::add_resource(uint8_t block_type) {
    if (BLOCKS.find(block_type) == BLOCKS.end()) return;
    std::string bname = BLOCKS.at(block_type).name;
    
    // 1. Slot existente en hotbar
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == block_type && slots[i].count > 0) {
            slots[i].count++;
            return;
        }
    }
    // 2. Slot vacío en hotbar
    for (int i = 1; i < 10; i++) {
        if (slots[i].count <= 0) {
            slots[i].id = block_type;
            slots[i].name = bname;
            slots[i].count = 1;
            return;
        }
    }
    // 3. Slot existente en storage
    for (size_t i = 0; i < storage.size(); i++) {
        if (storage[i].id == block_type && storage[i].count > 0) {
            storage[i].count++;
            return;
        }
    }
    // 4. Slot vacío en storage
    for (size_t i = 0; i < storage.size(); i++) {
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
    
    // 1. Slot existente en hotbar
    for (int i = 1; i < 10; i++) {
        if (slots[i].count > 0 && slots[i].name == iname) {
            slots[i].count += count;
            return;
        }
    }
    // 2. Slot vacío en hotbar
    for (int i = 1; i < 10; i++) {
        if (slots[i].count <= 0) {
            slots[i].id = 254;
            slots[i].name = iname;
            slots[i].count = count;
            return;
        }
    }
    // 3. Slot existente en storage
    for (size_t i = 0; i < storage.size(); i++) {
        if (storage[i].count > 0 && storage[i].name == iname) {
            storage[i].count += count;
            return;
        }
    }
    // 4. Slot vacío en storage
    for (size_t i = 0; i < storage.size(); i++) {
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
    for (size_t i = 0; i < storage.size(); i++) {
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
    for (size_t i = 0; i < storage.size() && remaining > 0; i++) {
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

int UI::count_block(uint8_t block_id) const {
    int total = 0;
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == block_id && slots[i].count > 0) total += slots[i].count;
    }
    for (size_t i = 0; i < storage.size(); i++) {
        if (storage[i].id == block_id && storage[i].count > 0) total += storage[i].count;
    }
    return total;
}

bool UI::remove_block(uint8_t block_id, int count) {
    if (count_block(block_id) < count) return false;
    int remaining = count;
    for (int i = 1; i < 10 && remaining > 0; i++) {
        if (slots[i].id == block_id && slots[i].count > 0) {
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
    for (size_t i = 0; i < storage.size() && remaining > 0; i++) {
        if (storage[i].id == block_id && storage[i].count > 0) {
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
    add_item(ITEM_PLANKS, 4);
}

// ===================== CRAFTEO =====================

bool UI::can_craft(int recipe_index) const {
    if (recipe_index < 0 || recipe_index >= (int)RECIPES.size()) return false;
    const CraftingRecipe& r = RECIPES[recipe_index];
    for (auto& ing : r.ingredients) {
        if (ing.is_item) {
            if (!has_item(ing.id, ing.count)) return false;
        } else {
            if (count_block(ing.id) < ing.count) return false;
        }
    }
    return true;
}

void UI::craft(int recipe_index, int times) {
    for (int t = 0; t < times; ++t) {
        if (!can_craft(recipe_index)) break;
        const CraftingRecipe& r = RECIPES[recipe_index];
        
        // Remover ingredientes
        for (auto& ing : r.ingredients) {
            if (ing.is_item) {
                remove_item(ing.id, ing.count);
            } else {
                remove_block(ing.id, ing.count);
            }
        }
        
        // Agregar resultado
        if (r.result_is_tool) {
            add_tool(r.result_tool_type, r.result_tool_tier);
        } else {
            add_item(r.result_item_id, r.result_count);
        }
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
    int spacing = 6;
    int total_width = 10 * slot_size + 9 * spacing;
    int start_x = (sw - total_width) / 2;
    int start_y = sh - slot_size - 18;
    
    // Marco exterior con borde elegante
    DrawRectangleRounded({ (float)start_x - 8, (float)start_y - 8, (float)total_width + 16, (float)slot_size + 16 }, 0.2f, 4, Fade(Color{16, 20, 26, 255}, 0.85f));
    DrawRectangleRoundedLines({ (float)start_x - 8, (float)start_y - 8, (float)total_width + 16, (float)slot_size + 16 }, 0.2f, 4, Fade(Color{80, 100, 130, 255}, 0.6f));
    
    for (int i = 0; i < 10; i++) {
        int x = start_x + i * (slot_size + spacing);
        int y = start_y;
        
        bool is_sel = (i == selected_slot);
        Color slot_bg = is_sel ? Fade(Color{255, 215, 0, 255}, 0.25f) : Fade(Color{30, 36, 46, 255}, 0.9f);
        DrawRectangle(x, y, slot_size, slot_size, slot_bg);
        
        Color border_col = is_sel ? Color{255, 215, 0, 255} : Fade(Color{100, 120, 150, 255}, 0.5f);
        DrawRectangleLines(x, y, slot_size, slot_size, border_col);
        if (is_sel) {
            DrawRectangleLines(x - 1, y - 1, slot_size + 2, slot_size + 2, Color{255, 230, 80, 180});
        }
        
        int key_num = (i < 9) ? (i + 1) : 0;
        DrawText(TextFormat("%d", key_num), x + 4, y + 3, 10, is_sel ? Color{255, 230, 80, 255} : Color{160, 175, 190, 255});
        
        if (i == 0) {
            ToolSlot* tool = get_active_tool();
            if (tool) {
                draw_tool_icon(tool->type, tool->tier, x + 5, y + 5, 40);
                float pct = (float)tool->durability_current / (float)tool->durability_max;
                Color dur_col = pct > 0.5f ? Color{50, 220, 80, 255} : (pct > 0.25f ? Color{240, 190, 40, 255} : Color{235, 50, 50, 255});
                DrawRectangle(x + 5, y + 42, (int)(40 * pct), 4, dur_col);
                DrawRectangleLines(x + 5, y + 42, 40, 4, Fade(BLACK, 0.6f));
            } else {
                DrawText("Mano", x + 8, y + 20, 12, Fade(WHITE, 0.7f));
            }
        } else if (slots[i].count > 0) {
            if (slots[i].id == 254) {
                for (auto& [iid, itype] : ITEMS) {
                    if (itype.name == slots[i].name) {
                        draw_item_icon(iid, x + 5, y + 5, 40);
                        break;
                    }
                }
            } else {
                draw_block_icon(slots[i].id, x + 5, y + 5, 40);
            }
            DrawText(TextFormat("%d", slots[i].count), x + 29, y + 34, 14, BLACK);
            DrawText(TextFormat("%d", slots[i].count), x + 28, y + 33, 14, WHITE);
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
    
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int slot_size = 50;
    int spacing = 6;
    int total_width = 10 * slot_size + 9 * spacing;
    int hotbar_x = (sw - total_width) / 2;
    int hotbar_y = sh - slot_size - 18;
    
    int hud_x = hotbar_x;
    int hud_y = hotbar_y - 48;
    
    DrawRectangleRounded({ (float)hud_x - 6, (float)hud_y - 4, 195.0f, 42.0f }, 0.25f, 4, Fade(Color{16, 20, 26, 255}, 0.85f));
    DrawRectangleRoundedLines({ (float)hud_x - 6, (float)hud_y - 4, 195.0f, 42.0f }, 0.25f, 4, Fade(Color{80, 100, 130, 255}, 0.5f));
    
    draw_tool_icon(tool->type, tool->tier, hud_x, hud_y + 1, 32);
    DrawText(info->name.c_str(), hud_x + 38, hud_y + 3, 13, WHITE);
    
    float pct = (float)tool->durability_current / (float)tool->durability_max;
    int bar_w = 90;
    int bar_h = 5;
    int bar_x = hud_x + 38;
    int bar_y = hud_y + 22;
    
    DrawRectangle(bar_x, bar_y, bar_w, bar_h, Fade(BLACK, 0.8f));
    Color dur_col = pct > 0.5f ? Color{50, 220, 80, 255} : (pct > 0.25f ? Color{240, 190, 40, 255} : Color{235, 50, 50, 255});
    DrawRectangle(bar_x, bar_y, (int)(bar_w * pct), bar_h, dur_col);
    DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, Fade(BLACK, 0.6f));
    
    DrawText(TextFormat("%d/%d", tool->durability_current, tool->durability_max), bar_x + bar_w + 6, bar_y - 2, 10, Color{200, 210, 225, 255});
}

void UI::draw_inventory_panel() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    
    int panel_w = 950;
    int panel_h = 570;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;
    
    int slot_size = 42;
    int slot_pad = 5;
    int slot_total = slot_size + slot_pad;
    
    int left_x = px + 20;
    int right_x = px + 480;
    
    int tools_y = py + 65;
    int storage_y = py + 155;
    int hotbar_y = py + 395;
    
    int craft_y = py + 95;
    int craft_w = panel_w - (right_x - px) - 20;
    int craft_h = panel_h - 115;
    
    std::string tooltip_text;
    Vector2 mpos = GetMousePosition();
    
    // Fondo oscuro con sombra
    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.45f));
    
    // Ventana principal
    DrawRectangleRounded({ (float)px, (float)py, (float)panel_w, (float)panel_h }, 0.03f, 6, Color{ 20, 24, 32, 245 });
    DrawRectangleRoundedLines({ (float)px, (float)py, (float)panel_w, (float)panel_h }, 0.03f, 6, Fade(Color{ 80, 105, 140, 255 }, 0.7f));
    
    // Cabecera
    DrawRectangleRounded({ (float)px + 2, (float)py + 2, (float)panel_w - 4, 42.0f }, 0.08f, 4, Color{ 28, 34, 46, 255 });
    DrawText("INVENTARIO & TALLER DE CRAFTEO", px + 24, py + 11, 20, Color{ 240, 245, 255, 255 });
    
    // Botón de cierre [ X ]
    bool hover_close = (mpos.x >= px + panel_w - 35 && mpos.x <= px + panel_w - 10 && mpos.y >= py + 10 && mpos.y <= py + 35);
    DrawRectangleRounded({ (float)px + panel_w - 35, (float)py + 10, 25.0f, 25.0f }, 0.2f, 4, hover_close ? Color{ 220, 45, 45, 255 } : Color{ 50, 60, 75, 255 });
    DrawText("X", px + panel_w - 28, py + 14, 16, WHITE);
    
    // Divisor vertical central
    DrawLine(px + 465, py + 52, px + 465, py + panel_h - 18, Fade(Color{ 80, 100, 130, 255 }, 0.4f));
    
    // =================================================================
    // PANEL IZQUIERDO: HERRAMIENTAS, MOCHILA, HOTBAR
    // =================================================================
    
    // --- 1. HERRAMIENTAS (1 sola fila: Mano + 6 herramientas) ---
    DrawText("HERRAMIENTAS", left_x, tools_y - 20, 13, Color{ 255, 215, 80, 255 });
    DrawText("(Haz clic para equipar o arrastrar)", left_x + 115, tools_y - 18, 10, Color{ 150, 165, 185, 255 });
    
    // Slot Mano (fijo)
    {
        int hx = left_x;
        int hy = tools_y;
        bool is_active = (selected_slot == 0 && selected_tool_idx == -1);
        
        DrawRectangle(hx, hy, slot_size, slot_size, is_active ? Fade(Color{255, 215, 0, 255}, 0.25f) : Color{28, 34, 44, 255});
        DrawRectangleLines(hx, hy, slot_size, slot_size, is_active ? Color{255, 215, 0, 255} : Fade(Color{90, 110, 135, 255}, 0.5f));
        if (is_active) {
            DrawRectangleLines(hx - 1, hy - 1, slot_size + 2, slot_size + 2, Color{255, 230, 80, 180});
        }
        
        DrawText("MANO", hx + 5, hy + 15, 10, is_active ? Color{255, 230, 100, 255} : Color{180, 195, 210, 255});
        
        if (mpos.x >= hx && mpos.x <= hx + slot_size && mpos.y >= hy && mpos.y <= hy + slot_size) {
            tooltip_text = "Mano Desarmada (Golpe b\xc3\xa1sico)";
        }
    }
    
    // 6 Slots de herramientas (en una sola fila horizontal)
    for (int i = 0; i < 6; i++) {
        int tx = left_x + (i + 1) * slot_total;
        int ty = tools_y;
        
        bool is_active = (selected_slot == 0 && i == selected_tool_idx && i < (int)tool_inventory.size());
        
        DrawRectangle(tx, ty, slot_size, slot_size, is_active ? Fade(Color{255, 215, 0, 255}, 0.25f) : Color{28, 34, 44, 255});
        DrawRectangleLines(tx, ty, slot_size, slot_size, is_active ? Color{255, 215, 0, 255} : Fade(Color{80, 100, 125, 255}, 0.4f));
        if (is_active) {
            DrawRectangleLines(tx - 1, ty - 1, slot_size + 2, slot_size + 2, Color{255, 230, 80, 180});
        }
        
        if (i < (int)tool_inventory.size()) {
            ToolSlot& t = tool_inventory[i];
            const ToolInfo* info = nullptr;
            for (auto& ti : TOOLS) {
                if (ti.type == t.type && ti.tier == t.tier) { info = &ti; break; }
            }
            
            if (info) {
                draw_tool_icon(t.type, t.tier, tx + 4, ty + 3, slot_size - 8);
                
                float pct = (float)t.durability_current / (float)t.durability_max;
                Color dur_col = pct > 0.5f ? Color{50, 220, 80, 255} : (pct > 0.25f ? Color{240, 190, 40, 255} : Color{235, 50, 50, 255});
                DrawRectangle(tx + 4, ty + slot_size - 6, (int)((slot_size - 8) * pct), 3, dur_col);
                DrawRectangleLines(tx + 4, ty + slot_size - 6, slot_size - 8, 3, Fade(BLACK, 0.6f));
                
                if (mpos.x >= tx && mpos.x <= tx + slot_size && mpos.y >= ty && mpos.y <= ty + slot_size) {
                    tooltip_text = info->name + "  [Durabilidad: " + std::to_string(t.durability_current) + "/" + std::to_string(t.durability_max) + "]\nVelocidad de minado: x" + TextFormat("%.1f", info->mining_speed);
                }
            }
        }
    }
    
    // --- 2. MOCHILA / ALMACENAMIENTO (6x3) ---
    DrawText("MOCHILA (Almacenamiento)", left_x, storage_y - 20, 13, Color{ 255, 215, 80, 255 });
    DrawText("(Shift + Clic para mover a Barra R\xc3\xa1pida)", left_x + 185, storage_y - 18, 10, Color{ 150, 165, 185, 255 });
    
    int storage_cols = 6;
    for (int i = 0; i < (int)storage.size(); i++) {
        int col = i % storage_cols;
        int row = i / storage_cols;
        int sx = left_x + col * slot_total;
        int sy = storage_y + row * slot_total;
        
        bool hover = (mpos.x >= sx && mpos.x <= sx + slot_size && mpos.y >= sy && mpos.y <= sy + slot_size);
        DrawRectangle(sx, sy, slot_size, slot_size, hover ? Color{38, 46, 60, 255} : Color{28, 34, 44, 255});
        DrawRectangleLines(sx, sy, slot_size, slot_size, hover ? Color{140, 170, 210, 255} : Fade(Color{80, 100, 125, 255}, 0.4f));
        
        if (storage[i].count > 0) {
            if (storage[i].id == 254) {
                for (auto& [iid, itype] : ITEMS) {
                    if (itype.name == storage[i].name) {
                        draw_item_icon(iid, sx + 4, sy + 4, slot_size - 8);
                        break;
                    }
                }
            } else {
                draw_block_icon(storage[i].id, sx + 4, sy + 4, slot_size - 8);
            }
            
            DrawText(TextFormat("%d", storage[i].count), sx + slot_size - 17, sy + slot_size - 13, 11, BLACK);
            DrawText(TextFormat("%d", storage[i].count), sx + slot_size - 18, sy + slot_size - 14, 11, WHITE);
            
            if (hover) {
                tooltip_text = storage[i].name + " x" + std::to_string(storage[i].count);
            }
        }
    }
    
    // --- 3. BARRA RÁPIDA (Slots 1-9) ---
    DrawText("BARRA R\xc3\x81PIDA (Hotbar Teclas 1-9)", left_x, hotbar_y - 20, 13, Color{ 255, 215, 80, 255 });
    
    for (int i = 0; i < 9; i++) {
        int hx = left_x + i * slot_total;
        int hy = hotbar_y;
        int si = i + 1;
        
        bool is_sel = (selected_slot == si);
        bool hover = (mpos.x >= hx && mpos.x <= hx + slot_size && mpos.y >= hy && mpos.y <= hy + slot_size);
        
        DrawRectangle(hx, hy, slot_size, slot_size, is_sel ? Fade(Color{255, 215, 0, 255}, 0.25f) : (hover ? Color{38, 46, 60, 255} : Color{28, 34, 44, 255}));
        DrawRectangleLines(hx, hy, slot_size, slot_size, is_sel ? Color{255, 215, 0, 255} : (hover ? Color{140, 170, 210, 255} : Fade(Color{80, 100, 125, 255}, 0.4f)));
        if (is_sel) {
            DrawRectangleLines(hx - 1, hy - 1, slot_size + 2, slot_size + 2, Color{255, 230, 80, 180});
        }
        
        DrawText(TextFormat("%d", si), hx + 3, hy + 2, 9, is_sel ? Color{255, 230, 80, 255} : Color{140, 155, 175, 255});
        
        if (slots[si].count > 0) {
            if (slots[si].id == 254) {
                for (auto& [iid, itype] : ITEMS) {
                    if (itype.name == slots[si].name) {
                        draw_item_icon(iid, hx + 4, hy + 4, slot_size - 8);
                        break;
                    }
                }
            } else {
                draw_block_icon(slots[si].id, hx + 4, hy + 4, slot_size - 8);
            }
            
            DrawText(TextFormat("%d", slots[si].count), hx + slot_size - 17, hy + slot_size - 13, 11, BLACK);
            DrawText(TextFormat("%d", slots[si].count), hx + slot_size - 18, hy + slot_size - 14, 11, WHITE);
            
            if (hover) {
                tooltip_text = slots[si].name + " x" + std::to_string(slots[si].count);
            }
        }
    }
    
    // =================================================================
    // PANEL DERECHO: TALLER DE CRAFTEO
    // =================================================================
    
    // Pestañas de categorías
    int tab_y = py + 58;
    int tab_w = 98;
    int tab_h = 24;
    const char* tab_labels[] = { "TODOS", "HERRAM.", "MATER.", "LISTOS" };
    for (int t = 0; t < 4; ++t) {
        int tx = right_x + t * (tab_w + 6);
        bool is_active_tab = (craft_category == t);
        bool hover_tab = (mpos.x >= tx && mpos.x <= tx + tab_w && mpos.y >= tab_y && mpos.y <= tab_y + tab_h);
        
        Color tab_bg = is_active_tab ? Color{ 45, 110, 200, 255 } : (hover_tab ? Color{ 40, 50, 68, 255 } : Color{ 26, 32, 42, 255 });
        DrawRectangleRounded({ (float)tx, (float)tab_y, (float)tab_w, (float)tab_h }, 0.25f, 4, tab_bg);
        DrawRectangleRoundedLines({ (float)tx, (float)tab_y, (float)tab_w, (float)tab_h }, 0.25f, 4, is_active_tab ? Color{ 90, 160, 255, 255 } : Fade(Color{ 80, 100, 130, 255 }, 0.5f));
        
        int text_w = MeasureText(tab_labels[t], 11);
        DrawText(tab_labels[t], tx + (tab_w - text_w) / 2, tab_y + 6, 11, is_active_tab ? WHITE : Color{ 180, 195, 215, 255 });
    }
    
    // Filtrar recetas
    std::vector<int> filtered_recipes;
    for (size_t r = 0; r < RECIPES.size(); ++r) {
        if (craft_category == 1 && !RECIPES[r].result_is_tool) continue;
        if (craft_category == 2 && RECIPES[r].result_is_tool) continue;
        if (craft_category == 3 && !can_craft((int)r)) continue;
        filtered_recipes.push_back((int)r);
    }
    
    // Scroll clamp
    int card_w = (craft_w - 18) / 2;
    int card_h = 72;
    int visible_rows = 5;
    int total_filtered = (int)filtered_recipes.size();
    int total_rows = (total_filtered + 1) / 2;
    int max_scroll = std::max(0, total_rows - visible_rows);
    if (craft_scroll_offset > max_scroll) craft_scroll_offset = max_scroll;
    if (craft_scroll_offset < 0) craft_scroll_offset = 0;
    
    // Contenedor con Scissor
    rlDrawRenderBatchActive();
    BeginScissorMode(right_x - 4, craft_y - 2, craft_w + 8, visible_rows * (card_h + 8) + 4);
    
    for (int vi = 0; vi < visible_rows * 2; vi++) {
        int f_idx = vi + craft_scroll_offset * 2;
        if (f_idx >= total_filtered) break;
        
        int recipe_idx = filtered_recipes[f_idx];
        const CraftingRecipe& rec = RECIPES[recipe_idx];
        bool available = can_craft(recipe_idx);
        
        int col = vi % 2;
        int row = vi / 2;
        int rx = right_x + col * (card_w + 10);
        int ry = craft_y + row * (card_h + 8);
        
        bool hover_card = (mpos.x >= rx && mpos.x <= rx + card_w && mpos.y >= ry && mpos.y <= ry + card_h);
        
        // Fondo de la tarjeta
        Color card_bg = available ? (hover_card ? Color{ 36, 52, 45, 255 } : Color{ 24, 38, 32, 240 }) 
                                  : (hover_card ? Color{ 36, 42, 54, 255 } : Color{ 22, 28, 36, 230 });
        Color card_border = available ? (hover_card ? Color{ 60, 230, 110, 255 } : Color{ 40, 170, 80, 200 })
                                      : (hover_card ? Color{ 110, 130, 160, 255 } : Fade(Color{ 70, 85, 110, 255 }, 0.5f));
        
        DrawRectangleRounded({ (float)rx, (float)ry, (float)card_w, (float)card_h }, 0.15f, 4, card_bg);
        DrawRectangleRoundedLines({ (float)rx, (float)ry, (float)card_w, (float)card_h }, 0.15f, 4, card_border);
        
        // Icono del resultado
        int icon_box_x = rx + 6;
        int icon_box_y = ry + 8;
        DrawRectangle(icon_box_x, icon_box_y, 34, 34, Color{ 16, 20, 28, 200 });
        DrawRectangleLines(icon_box_x, icon_box_y, 34, 34, Fade(Color{ 90, 110, 140, 255 }, 0.4f));
        
        if (rec.result_is_tool) {
            const ToolInfo* tinfo = nullptr;
            for (auto& t : TOOLS) {
                if (t.type == rec.result_tool_type && t.tier == rec.result_tool_tier) { tinfo = &t; break; }
            }
            if (tinfo) draw_tool_icon(tinfo->type, tinfo->tier, icon_box_x + 1, icon_box_y + 1, 32);
        } else {
            draw_item_icon(rec.result_item_id, icon_box_x + 1, icon_box_y + 1, 32);
            if (rec.result_count > 1) {
                DrawText(TextFormat("x%d", rec.result_count), icon_box_x + 18, icon_box_y + 22, 10, Color{ 255, 230, 100, 255 });
            }
        }
        
        // Nombre del resultado
        DrawText(rec.result_name.c_str(), rx + 46, ry + 6, 12, available ? Color{ 240, 255, 240, 255 } : Color{ 175, 185, 200, 255 });
        
        // Ingredientes en mini iconos
        int ing_x = rx + 46;
        for (size_t ig = 0; ig < rec.ingredients.size() && ig < 3; ig++) {
            auto& ing = rec.ingredients[ig];
            int total_owned = 0;
            if (ing.is_item) {
                std::string iname = ITEMS.at(ing.id).name;
                for (int h = 1; h < 10; h++) if (slots[h].count > 0 && slots[h].name == iname) total_owned += slots[h].count;
                for (size_t s = 0; s < storage.size(); s++) if (storage[s].count > 0 && storage[s].name == iname) total_owned += storage[s].count;
            } else {
                total_owned = count_block(ing.id);
            }
            
            bool has_enough = (total_owned >= ing.count);
            
            if (ing.is_item) {
                draw_item_icon(ing.id, ing_x, ry + 24, 16);
            } else {
                draw_block_icon(ing.id, ing_x, ry + 24, 16);
            }
            
            DrawText(TextFormat("%d/%d", total_owned, ing.count), ing_x + 18, ry + 27, 9, has_enough ? Color{ 60, 230, 100, 255 } : Color{ 240, 80, 80, 255 });
            ing_x += 50;
        }
        
        // Botón Craftear o estado
        if (available) {
            DrawRectangleRounded({ (float)rx + 46, (float)ry + 46, 80.0f, 18.0f }, 0.3f, 3, Color{ 35, 150, 65, 255 });
            DrawText("CRAFTEAR", rx + 55, ry + 50, 10, WHITE);
        } else {
            DrawText("Faltan materiales", rx + 46, ry + 50, 10, Color{ 140, 150, 165, 255 });
        }
        
        // Tooltip al pasar el mouse
        if (hover_card) {
            std::string ttip = rec.result_name;
            if (rec.result_count > 1) ttip += " (x" + std::to_string(rec.result_count) + ")";
            ttip += "\nMateriales: ";
            for (size_t j = 0; j < rec.ingredients.size(); j++) {
                if (j > 0) ttip += ", ";
                std::string iname = rec.ingredients[j].is_item ? ITEMS.at(rec.ingredients[j].id).name : BLOCKS.at(rec.ingredients[j].id).name;
                ttip += std::to_string(rec.ingredients[j].count) + " " + iname;
            }
            if (available) ttip += "\n[Clic: Craftear x1 | Shift+Clic: Craftear x5]";
            tooltip_text = ttip;
        }
    }
    
    EndScissorMode();
    
    // Barra de scroll si hay más recetas
    if (total_rows > visible_rows) {
        int sb_x = right_x + craft_w - 6;
        int sb_y = craft_y;
        int sb_h = visible_rows * (card_h + 8);
        DrawRectangle(sb_x, sb_y, 5, sb_h, Color{ 26, 32, 42, 255 });
        
        float ratio = (float)craft_scroll_offset / (float)max_scroll;
        int thumb_h = std::max(24, sb_h * visible_rows / total_rows);
        int thumb_y = sb_y + (int)(ratio * (sb_h - thumb_h));
        DrawRectangleRounded({ (float)sb_x, (float)thumb_y, 5.0f, (float)thumb_h }, 0.5f, 2, Color{ 90, 120, 160, 255 });
    }
    
    // =================================================================
    // ITEM ARRASTRADO (Siguiendo al mouse)
    // =================================================================
    if (is_dragging) {
        int dx = (int)mpos.x - slot_size / 2;
        int dy = (int)mpos.y - slot_size / 2;
        
        DrawRectangleRounded({ (float)dx, (float)dy, (float)slot_size, (float)slot_size }, 0.2f, 4, Fade(Color{ 255, 215, 0, 255 }, 0.35f));
        DrawRectangleRoundedLines({ (float)dx, (float)dy, (float)slot_size, (float)slot_size }, 0.2f, 4, Color{ 255, 230, 80, 255 });
        
        if (dragging_tool) {
            draw_tool_icon(drag_tool.type, drag_tool.tier, dx + 4, dy + 3, slot_size - 8);
            float pct = (float)drag_tool.durability_current / (float)drag_tool.durability_max;
            Color dur_col = pct > 0.5f ? Color{50, 220, 80, 255} : (pct > 0.25f ? Color{240, 190, 40, 255} : Color{235, 50, 50, 255});
            DrawRectangle(dx + 4, dy + slot_size - 6, (int)((slot_size - 8) * pct), 3, dur_col);
        } else if (drag_item.id == 254) {
            for (auto& [iid, itype] : ITEMS) {
                if (itype.name == drag_item.name) {
                    draw_item_icon(iid, dx + 4, dy + 4, slot_size - 8);
                    break;
                }
            }
            DrawText(TextFormat("%d", drag_item.count), dx + slot_size - 17, dy + slot_size - 13, 11, BLACK);
            DrawText(TextFormat("%d", drag_item.count), dx + slot_size - 18, dy + slot_size - 14, 11, WHITE);
        } else if (drag_item.id != AIR && drag_item.count > 0) {
            draw_block_icon(drag_item.id, dx + 4, dy + 4, slot_size - 8);
            DrawText(TextFormat("%d", drag_item.count), dx + slot_size - 17, dy + slot_size - 13, 11, BLACK);
            DrawText(TextFormat("%d", drag_item.count), dx + slot_size - 18, dy + slot_size - 14, 11, WHITE);
        }
    }
    
    // =================================================================
    // TOOLTIP FLOTANTE (Por encima de todo)
    // =================================================================
    if (!tooltip_text.empty() && !is_dragging) {
        int font_size = 13;
        int line_count = 1;
        int max_line_w = 0;
        std::string cur_line;
        for (char c : tooltip_text) {
            if (c == '\n') {
                max_line_w = std::max(max_line_w, MeasureText(cur_line.c_str(), font_size));
                cur_line.clear();
                line_count++;
            } else {
                cur_line += c;
            }
        }
        max_line_w = std::max(max_line_w, MeasureText(cur_line.c_str(), font_size));
        
        int pad = 8;
        int ttip_w = max_line_w + pad * 2;
        int ttip_h = line_count * 18 + pad * 2;
        
        int tx = (int)mpos.x + 14;
        int ty = (int)mpos.y + 14;
        if (tx + ttip_w > sw - 10) tx = sw - ttip_w - 10;
        if (ty + ttip_h > sh - 10) ty = sh - ttip_h - 10;
        
        DrawRectangleRounded({ (float)tx, (float)ty, (float)ttip_w, (float)ttip_h }, 0.15f, 4, Color{ 14, 18, 24, 250 });
        DrawRectangleRoundedLines({ (float)tx, (float)ty, (float)ttip_w, (float)ttip_h }, 0.15f, 4, Color{ 80, 105, 140, 255 });
        
        int curr_y = ty + pad;
        cur_line.clear();
        for (char c : tooltip_text) {
            if (c == '\n') {
                DrawText(cur_line.c_str(), tx + pad, curr_y, font_size, Color{ 240, 245, 255, 255 });
                cur_line.clear();
                curr_y += 18;
            } else {
                cur_line += c;
            }
        }
        if (!cur_line.empty()) {
            DrawText(cur_line.c_str(), tx + pad, curr_y, font_size, Color{ 240, 245, 255, 255 });
        }
    }
}
