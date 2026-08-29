#include "UI.hpp"
#include <iostream>
#include <algorithm>
#include "rlgl.h"

using namespace Config;

UI::UI(Texture2D sheet, Texture2D items_sheet) : spritesheet(sheet), spritesheet_items(items_sheet) {
    slots.resize(10);
    slots[0] = { AIR, "Mano", -1 };
    storage.resize(27); // 9x3 standard Minecraft storage
}

UI::~UI() {}

void UI::cancel_drag() {
    if (!is_dragging) return;
    
    if (dragging_tool) {
        if (drag_source_type == 1 && drag_source_idx >= 0 && drag_source_idx <= (int)tool_inventory.size()) {
            tool_inventory.insert(tool_inventory.begin() + std::min(drag_source_idx, (int)tool_inventory.size()), drag_tool);
        } else if (drag_source_type == 3 && drag_source_idx >= 0) {
            auto key = std::make_tuple((int)active_container_pos.x, (int)active_container_pos.y, (int)active_container_pos.z);
            auto& c = world_chests[key];
            if (drag_source_idx < (int)c.slots.size() && !c.slots[drag_source_idx].is_tool && c.slots[drag_source_idx].item.count == 0) {
                c.slots[drag_source_idx].is_tool = true;
                c.slots[drag_source_idx].tool = drag_tool;
                c.slots[drag_source_idx].item = { AIR, "", 0 };
            } else {
                tool_inventory.push_back(drag_tool);
            }
        } else {
            tool_inventory.push_back(drag_tool);
        }
    } else {
        if (drag_item.count > 0 && (drag_item.id != Config::AIR || !drag_item.name.empty())) {
            if (drag_source_type == 0 && drag_source_idx >= 1 && drag_source_idx < (int)slots.size() && slots[drag_source_idx].count == 0) {
                slots[drag_source_idx] = drag_item;
            } else if (drag_source_type == 2 && drag_source_idx >= 0 && drag_source_idx < (int)storage.size() && storage[drag_source_idx].count == 0) {
                storage[drag_source_idx] = drag_item;
            } else if (drag_source_type == 3 && drag_source_idx >= 0) {
                auto key = std::make_tuple((int)active_container_pos.x, (int)active_container_pos.y, (int)active_container_pos.z);
                auto& c = world_chests[key];
                if (drag_source_idx < (int)c.slots.size() && !c.slots[drag_source_idx].is_tool && c.slots[drag_source_idx].item.count == 0) {
                    c.slots[drag_source_idx].is_tool = false;
                    c.slots[drag_source_idx].item = drag_item;
                } else {
                    add_resource(drag_item.id, drag_item.count);
                }
            } else {
                if (drag_item.id == 254) {
                    for (auto& [iid, itype] : Config::ITEMS) {
                        if (itype.name == drag_item.name) {
                            add_item(iid, drag_item.count);
                            break;
                        }
                    }
                } else if (drag_item.id != Config::AIR) {
                    add_resource(drag_item.id, drag_item.count);
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

void UI::tick_furnaces() {
    auto get_smelt_result = [](uint8_t in_id, const std::string& name) -> std::pair<uint8_t, bool> {
        if (in_id == Config::IRON_ORE || name == "Mineral de Hierro") return { Config::ITEM_IRON_INGOT, true };
        if (in_id == Config::GOLD_ORE || name == "Mineral de Oro") return { Config::ITEM_GOLD_INGOT, true };
        if (in_id == Config::SILVER_ORE || name == "Mineral de Plata") return { Config::ITEM_SILVER_INGOT, true };
        if (in_id == Config::DIAMOND_ORE || name == "Mineral de Diamante") return { Config::ITEM_DIAMOND_INGOT, true };
        if (in_id == Config::SAND || name == "Arena") return { Config::GLASS, false };
        if (in_id == Config::COBBLESTONE || name == "Adoquin" || name == "Cobblestone") return { Config::STONE, false };
        if (in_id == Config::WOOD || in_id == Config::BIRCH_WOOD || name == "Madera Roble" || name == "Madera Abedul") return { Config::ITEM_COAL, true };
        return { 0, false };
    };

    auto get_fuel_time = [](uint8_t f_id, const std::string& name) -> int {
        if (f_id == 254 && (name == "Carbon" || name == "Carbón")) return 1600;
        if (f_id == Config::ITEM_COAL) return 1600;
        if (f_id == Config::COAL_ORE || name == "Mineral de Carbon" || name == "Mineral de Carbón") return 1600;
        if (f_id == Config::WOOD || f_id == Config::BIRCH_WOOD || f_id == Config::PLANKS_CUBE || name == "Madera Roble" || name == "Madera Abedul" || name == "Tablas de Madera") return 300;
        if (f_id == 254 && (name == "Palo" || name == "Palos")) return 100;
        if (f_id == Config::ITEM_STICK) return 100;
        return 0;
    };

    for (auto& [pos, f] : world_furnaces) {
        if (f.burn_ticks > 0) {
            f.burn_ticks--;
        }

        auto [out_id, is_item] = get_smelt_result(f.input.id, f.input.name);
        bool can_smelt = (out_id != 0 && f.input.count > 0);
        if (can_smelt) {
            if (f.output.count > 0) {
                if (is_item && (f.output.id != 254 || f.output.name != Config::ITEMS.at(out_id).name)) can_smelt = false;
                else if (!is_item && f.output.id != out_id) can_smelt = false;
                else if (f.output.count >= 64) can_smelt = false;
            }
        }

        if (can_smelt) {
            if (f.burn_ticks == 0 && f.fuel.count > 0) {
                int ft = get_fuel_time(f.fuel.id, f.fuel.name);
                if (ft > 0) {
                    f.burn_ticks = ft;
                    f.max_burn_ticks = ft;
                    f.fuel.count--;
                    if (f.fuel.count <= 0) f.fuel = { AIR, "", 0 };
                }
            }

            if (f.burn_ticks > 0) {
                f.cook_ticks++;
                if (f.cook_ticks >= 200) {
                    f.cook_ticks = 0;
                    f.input.count--;
                    if (f.input.count <= 0) f.input = { AIR, "", 0 };

                    if (f.output.count == 0) {
                        if (is_item) {
                            f.output = { 254, Config::ITEMS.at(out_id).name, 1 };
                        } else {
                            f.output = { out_id, Config::BLOCKS.at(out_id).name, 1 };
                        }
                    } else {
                        f.output.count++;
                    }
                }
            } else {
                f.cook_ticks = std::max(0, f.cook_ticks - 2);
            }
        } else {
            f.cook_ticks = std::max(0, f.cook_ticks - 2);
        }
    }
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

void UI::draw_block_icon(uint8_t block_id, int x, int y, int size) {
    if (block_id == Config::AIR) return;
    if (Config::BLOCKS.find(block_id) == Config::BLOCKS.end()) return;
    
    const auto& b = Config::BLOCKS.at(block_id);
    int tx = b.tex_x;
    int ty = b.tex_y;
    if (b.tex_icon_x >= 0 && b.tex_icon_y >= 0) {
        tx = b.tex_icon_x;
        ty = b.tex_icon_y;
    } else if (b.tex_front_x >= 0 && b.tex_front_y >= 0) {
        tx = b.tex_front_x;
        ty = b.tex_front_y;
    } else if (b.shape == Config::SHAPE_CRAFTING_TABLE && b.tex_top_x >= 0 && b.tex_top_y >= 0) {
        tx = b.tex_top_x;
        ty = b.tex_top_y;
    }

    float tw = (float)spritesheet.width / (float)Config::TILES_ATLAS_COLS;
    float th = (float)spritesheet.height / (float)Config::TILES_ATLAS_ROWS;
    float correct_y = (float)Config::TILES_ATLAS_ROWS - 1.0f - (float)ty;
    
    Rectangle src = { (float)tx * tw, correct_y * th, tw, th };
    Rectangle dst = { (float)x, (float)y, (float)size, (float)size };
    DrawTexturePro(spritesheet, src, dst, {0, 0}, 0.0f, WHITE);
}

void UI::draw_tool_icon(Config::ToolType type, Config::ToolTier tier, int x, int y, int size) {
    for (const auto& t : Config::TOOLS) {
        if (t.type == type && t.tier == tier) {
            float tw = (float)spritesheet_items.width / (float)Config::ITEMS_ATLAS_COLS;
            float th = (float)spritesheet_items.height / (float)Config::ITEMS_ATLAS_ROWS;
            float correct_y = (float)Config::ITEMS_ATLAS_ROWS - 1.0f - (float)t.item_tex_y;
            
            Rectangle src = { (float)t.item_tex_x * tw, correct_y * th, tw, th };
            Rectangle dst = { (float)x, (float)y, (float)size, (float)size };
            DrawTexturePro(spritesheet_items, src, dst, {0, 0}, 0.0f, WHITE);
            return;
        }
    }
}

void UI::draw_item_icon(uint8_t item_id, int x, int y, int size) {
    if (Config::ITEMS.find(item_id) == Config::ITEMS.end()) return;
    const auto& it = Config::ITEMS.at(item_id);
    
    float tw = (float)spritesheet_items.width / (float)Config::ITEMS_ATLAS_COLS;
    float th = (float)spritesheet_items.height / (float)Config::ITEMS_ATLAS_ROWS;
    float correct_y = (float)Config::ITEMS_ATLAS_ROWS - 1.0f - (float)it.item_tex_y;
    
    Rectangle src = { (float)it.item_tex_x * tw, correct_y * th, tw, th };
    Rectangle dst = { (float)x, (float)y, (float)size, (float)size };
    DrawTexturePro(spritesheet_items, src, dst, {0, 0}, 0.0f, WHITE);
}

void UI::draw_hotbar() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    
    int slot_size = 50;
    int spacing = 6;
    int total_width = 10 * slot_size + 9 * spacing;
    int start_x = (sw - total_width) / 2;
    int start_y = sh - slot_size - 18;
    
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
                for (auto& [iid, itype] : Config::ITEMS) {
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

        int key_num = (i < 9) ? (i + 1) : 0;
        DrawText(TextFormat("%d", key_num), x + 5, y + 4, 10, BLACK);
        DrawText(TextFormat("%d", key_num), x + 4, y + 3, 10, is_sel ? Color{255, 230, 80, 255} : Color{220, 230, 245, 255});
    }
}

void UI::draw_tool_hud() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int slot_size = 50;
    int spacing = 6;
    int total_width = 10 * slot_size + 9 * spacing;
    int hotbar_x = (sw - total_width) / 2;
    int hotbar_y = sh - slot_size - 18;
    
    int hud_x = hotbar_x;
    int hud_y = hotbar_y - 56;
    int hud_w = 220;
    int hud_h = 44;

    if (selected_slot == 0) {
        ToolSlot* tool = get_active_tool();
        if (tool) {
            const ToolInfo* info = nullptr;
            for (auto& t : Config::TOOLS) {
                if (t.type == tool->type && t.tier == tool->tier) {
                    info = &t;
                    break;
                }
            }
            if (info) {
                DrawRectangleRounded({ (float)hud_x - 6, (float)hud_y - 4, (float)hud_w, (float)hud_h }, 0.25f, 4, Fade(Color{16, 20, 26, 255}, 0.85f));
                DrawRectangleRoundedLines({ (float)hud_x - 6, (float)hud_y - 4, (float)hud_w, (float)hud_h }, 0.25f, 4, Fade(Color{80, 100, 130, 255}, 0.5f));
                
                draw_tool_icon(tool->type, tool->tier, hud_x, hud_y + 1, 34);
                DrawText(info->name.c_str(), hud_x + 40, hud_y + 4, 13, WHITE);
                
                float pct = (float)tool->durability_current / (float)tool->durability_max;
                int bar_w = 95;
                int bar_h = 5;
                int bar_x = hud_x + 40;
                int bar_y = hud_y + 24;
                
                DrawRectangle(bar_x, bar_y, bar_w, bar_h, Fade(BLACK, 0.8f));
                Color dur_col = pct > 0.5f ? Color{50, 220, 80, 255} : (pct > 0.25f ? Color{240, 190, 40, 255} : Color{235, 50, 50, 255});
                DrawRectangle(bar_x, bar_y, (int)(bar_w * pct), bar_h, dur_col);
                DrawRectangleLines(bar_x, bar_y, bar_w, bar_h, Fade(BLACK, 0.6f));
                
                DrawText(TextFormat("%d/%d", tool->durability_current, tool->durability_max), bar_x + bar_w + 6, bar_y - 2, 10, Color{200, 210, 225, 255});
            }
        } else {
            DrawRectangleRounded({ (float)hud_x - 6, (float)hud_y - 4, (float)hud_w, (float)hud_h }, 0.25f, 4, Fade(Color{16, 20, 26, 255}, 0.85f));
            DrawRectangleRoundedLines({ (float)hud_x - 6, (float)hud_y - 4, (float)hud_w, (float)hud_h }, 0.25f, 4, Fade(Color{80, 100, 130, 255}, 0.4f));
            DrawText("Mano (Sin herramienta)", hud_x + 12, hud_y + 14, 12, Color{180, 195, 210, 255});
        }
    } else {
        const auto& slot = slots[selected_slot];
        if (slot.count > 0) {
            DrawRectangleRounded({ (float)hud_x - 6, (float)hud_y - 4, (float)hud_w, (float)hud_h }, 0.25f, 4, Fade(Color{16, 20, 26, 255}, 0.85f));
            DrawRectangleRoundedLines({ (float)hud_x - 6, (float)hud_y - 4, (float)hud_w, (float)hud_h }, 0.25f, 4, Fade(Color{80, 100, 130, 255}, 0.5f));
            
            if (slot.id == 254) {
                for (auto& [iid, itype] : Config::ITEMS) {
                    if (itype.name == slot.name) { draw_item_icon(iid, hud_x, hud_y + 2, 34); break; }
                }
            } else {
                draw_block_icon(slot.id, hud_x, hud_y + 2, 34);
            }
            DrawText(slot.name.c_str(), hud_x + 42, hud_y + 6, 13, WHITE);
            DrawText(TextFormat("Cantidad: %d", slot.count), hud_x + 42, hud_y + 24, 11, Color{255, 230, 100, 255});
        }
    }
}

void UI::draw_player_inventory_section(int px, int py, int mouse_x, int mouse_y, bool is_l_click, bool is_r_click, bool is_shift, bool show_tools) {
    int slot_size = 40;
    int slot_pad = 4;
    int slot_total = slot_size + slot_pad;
    
    int cur_y = py;
    
    // 1. Herramientas
    if (show_tools) {
        int hx = px + slot_total;
        int hy = cur_y;
        bool hover_mano = (mouse_x >= hx && mouse_x <= hx + slot_size && mouse_y >= hy && mouse_y <= hy + slot_size);
        DrawRectangle(hx, hy, slot_size, slot_size, hover_mano ? Color{60, 75, 95, 255} : Color{35, 42, 52, 255});
        DrawRectangleLines(hx, hy, slot_size, slot_size, (selected_slot == 0 && selected_tool_idx == -1) ? GOLD : Color{70, 80, 95, 255});
        DrawText("Mano", hx + 5, hy + 14, 10, WHITE);
        if (hover_mano) {
            tooltip_text = "Mano vacía / Golpe básico";
            if (is_l_click && !is_dragging) {
                selected_slot = 0;
                selected_tool_idx = -1;
            }
        }

        for (int i = 0; i < 6; i++) {
            int tx = hx + (i + 1) * slot_total;
            int ty = hy;
            bool hover = (mouse_x >= tx && mouse_x <= tx + slot_size && mouse_y >= ty && mouse_y <= ty + slot_size);
            DrawRectangle(tx, ty, slot_size, slot_size, hover ? Color{60, 75, 95, 255} : Color{35, 42, 52, 255});
            DrawRectangleLines(tx, ty, slot_size, slot_size, (selected_slot == 0 && selected_tool_idx == i) ? GOLD : Color{70, 80, 95, 255});
            
            if (i < (int)tool_inventory.size()) {
                const auto& t = tool_inventory[i];
                draw_tool_icon(t.type, t.tier, tx + 4, ty + 4, slot_size - 8);
                if (t.durability_max > 0) {
                    float pct = (float)t.durability_current / (float)t.durability_max;
                    Color dur_col = (pct > 0.5f) ? GREEN : ((pct > 0.2f) ? ORANGE : RED);
                    DrawRectangle(tx + 4, ty + slot_size - 6, (int)((slot_size - 8) * pct), 3, dur_col);
                }
            }
            
            if (hover) {
                if (i < (int)tool_inventory.size()) {
                    const auto& t = tool_inventory[i];
                    static const char* TIER_N[] = { "Madera", "Piedra", "Hierro", "Plata", "Oro", "Diamante" };
                    static const char* TYPE_N[] = { "Pico", "Hacha", "Pala", "Martillo", "Mangual", "Espada" };
                    tooltip_text = std::string(TYPE_N[t.type]) + " de " + TIER_N[t.tier] + "\nDurabilidad: " + std::to_string(t.durability_current) + "/" + std::to_string(t.durability_max);
                }

                if (is_l_click) {
                    if (is_shift && mode == UI_MODE_CHEST && i < (int)tool_inventory.size()) {
                        // Shift+Click tool into chest
                        auto key = std::make_tuple((int)active_container_pos.x, (int)active_container_pos.y, (int)active_container_pos.z);
                        auto& c = world_chests[key];
                        for (auto& cs : c.slots) {
                            if (!cs.is_tool && cs.item.count == 0) {
                                cs.is_tool = true;
                                cs.tool = tool_inventory[i];
                                cs.item = { AIR, "", 0 };
                                tool_inventory.erase(tool_inventory.begin() + i);
                                if (selected_tool_idx >= (int)tool_inventory.size()) {
                                    selected_tool_idx = tool_inventory.empty() ? -1 : (int)tool_inventory.size() - 1;
                                }
                                break;
                            }
                        }
                    } else if (is_dragging) {
                        if (dragging_tool) {
                            if (i < (int)tool_inventory.size()) {
                                ToolSlot temp = tool_inventory[i];
                                tool_inventory[i] = drag_tool;
                                drag_tool = temp;
                            } else {
                                tool_inventory.push_back(drag_tool);
                                is_dragging = false;
                                dragging_tool = false;
                            }
                        }
                    } else {
                        if (i < (int)tool_inventory.size()) {
                            selected_slot = 0;
                            selected_tool_idx = i;
                            drag_tool = tool_inventory[i];
                            tool_inventory.erase(tool_inventory.begin() + i);
                            if (selected_tool_idx >= (int)tool_inventory.size()) {
                                selected_tool_idx = tool_inventory.empty() ? -1 : (int)tool_inventory.size() - 1;
                            }
                            is_dragging = true;
                            dragging_tool = true;
                            drag_source_type = 1;
                            drag_source_idx = i;
                        }
                    }
                }
            }
        }
        cur_y += slot_total + 8;
    }

    // 2. Almacenamiento Principal (27 slots: 9x3)
    int st_y = cur_y;
    for (int i = 0; i < 27; i++) {
        int col = i % 9;
        int row = i / 9;
        int sx = px + col * slot_total;
        int sy = st_y + row * slot_total;
        
        bool hover = (mouse_x >= sx && mouse_x <= sx + slot_size && mouse_y >= sy && mouse_y <= sy + slot_size);
        DrawRectangle(sx, sy, slot_size, slot_size, hover ? Color{60, 75, 95, 255} : Color{35, 42, 52, 255});
        DrawRectangleLines(sx, sy, slot_size, slot_size, Color{70, 80, 95, 255});
        
        if (i < (int)storage.size() && storage[i].count > 0) {
            const auto& s = storage[i];
            if (s.id == 254) {
                for (auto& [iid, itype] : Config::ITEMS) {
                    if (itype.name == s.name) { draw_item_icon(iid, sx + 4, sy + 4, slot_size - 8); break; }
                }
            } else if (s.id != Config::AIR) {
                draw_block_icon(s.id, sx + 4, sy + 4, slot_size - 8);
            }
            DrawText(TextFormat("%d", s.count), sx + slot_size - 18, sy + slot_size - 13, 11, WHITE);
        }
        
        if (hover) {
            if (i < (int)storage.size() && storage[i].count > 0) {
                tooltip_text = storage[i].name + " (x" + std::to_string(storage[i].count) + ")";
            }

            for (int k = 1; k <= 9; k++) {
                if (IsKeyPressed(KEY_ONE + k - 1) && !is_dragging) {
                    std::swap(storage[i], slots[k]);
                }
            }

            if (is_l_click) {
                if (is_shift && storage[i].count > 0) {
                    if (mode == UI_MODE_CHEST) {
                        auto key = std::make_tuple((int)active_container_pos.x, (int)active_container_pos.y, (int)active_container_pos.z);
                        auto& c = world_chests[key];
                        for (auto& cs : c.slots) {
                            if (!cs.is_tool && cs.item.id == storage[i].id && cs.item.name == storage[i].name && cs.item.count < 64) {
                                int space = 64 - cs.item.count;
                                int add = std::min(space, storage[i].count);
                                cs.item.count += add;
                                storage[i].count -= add;
                                if (storage[i].count <= 0) break;
                            }
                        }
                        if (storage[i].count > 0) {
                            for (auto& cs : c.slots) {
                                if (!cs.is_tool && cs.item.count == 0) {
                                    cs.is_tool = false;
                                    cs.item = storage[i];
                                    storage[i] = { AIR, "", 0 };
                                    break;
                                }
                            }
                        } else {
                            storage[i] = { AIR, "", 0 };
                        }
                    } else {
                        // Move to hotbar
                        for (int k = 1; k <= 9; k++) {
                            if (slots[k].id == storage[i].id && slots[k].name == storage[i].name && slots[k].count < 64) {
                                int space = 64 - slots[k].count;
                                int add = std::min(space, storage[i].count);
                                slots[k].count += add;
                                storage[i].count -= add;
                                if (storage[i].count <= 0) break;
                            }
                        }
                        if (storage[i].count > 0) {
                            for (int k = 1; k <= 9; k++) {
                                if (slots[k].count == 0) {
                                    slots[k] = storage[i];
                                    storage[i] = { AIR, "", 0 };
                                    break;
                                }
                            }
                        } else {
                            storage[i] = { AIR, "", 0 };
                        }
                    }
                } else if (is_dragging) {
                    if (!dragging_tool) {
                        if (storage[i].count == 0) {
                            storage[i] = drag_item;
                            is_dragging = false;
                        } else if (storage[i].id == drag_item.id && storage[i].name == drag_item.name) {
                            int space = 64 - storage[i].count;
                            int add = std::min(space, drag_item.count);
                            storage[i].count += add;
                            drag_item.count -= add;
                            if (drag_item.count <= 0) is_dragging = false;
                        } else {
                            InventorySlot temp = storage[i];
                            storage[i] = drag_item;
                            drag_item = temp;
                        }
                    }
                } else {
                    if (i < (int)storage.size() && storage[i].count > 0) {
                        drag_item = storage[i];
                        storage[i] = { AIR, "", 0 };
                        is_dragging = true;
                        dragging_tool = false;
                        drag_source_type = 2;
                        drag_source_idx = i;
                    }
                }
            } else if (is_r_click && is_dragging && !dragging_tool && drag_item.count > 0) {
                if (storage[i].count == 0) {
                    storage[i] = { drag_item.id, drag_item.name, 1 };
                    drag_item.count--;
                    if (drag_item.count <= 0) is_dragging = false;
                } else if (storage[i].id == drag_item.id && storage[i].name == drag_item.name && storage[i].count < 64) {
                    storage[i].count++;
                    drag_item.count--;
                    if (drag_item.count <= 0) is_dragging = false;
                }
            }
        }
    }

    // 3. Barra Rápida (Hotbar: slots 1..9)
    int hb_y = st_y + 3 * slot_total + 8;
    DrawLine(px, hb_y - 4, px + 9 * slot_total - slot_pad, hb_y - 4, Color{55, 68, 85, 255});
    for (int i = 1; i < 10; i++) {
        int sx = px + (i - 1) * slot_total;
        int sy = hb_y;
        
        bool hover = (mouse_x >= sx && mouse_x <= sx + slot_size && mouse_y >= sy && mouse_y <= sy + slot_size);
        DrawRectangle(sx, sy, slot_size, slot_size, hover ? Color{60, 75, 95, 255} : Color{35, 42, 52, 255});
        DrawRectangleLines(sx, sy, slot_size, slot_size, (selected_slot == i) ? GOLD : Color{70, 80, 95, 255});
        
        if (slots[i].count > 0) {
            const auto& s = slots[i];
            if (s.id == 254) {
                for (auto& [iid, itype] : Config::ITEMS) {
                    if (itype.name == s.name) { draw_item_icon(iid, sx + 4, sy + 4, slot_size - 8); break; }
                }
            } else if (s.id != Config::AIR) {
                draw_block_icon(s.id, sx + 4, sy + 4, slot_size - 8);
            }
            DrawText(TextFormat("%d", s.count), sx + slot_size - 18, sy + slot_size - 13, 11, WHITE);
        }
        
        if (hover) {
            if (slots[i].count > 0) {
                tooltip_text = slots[i].name + " (x" + std::to_string(slots[i].count) + ")";
            }

            if (is_l_click) {
                if (is_shift && slots[i].count > 0) {
                    if (mode == UI_MODE_CHEST) {
                        auto key = std::make_tuple((int)active_container_pos.x, (int)active_container_pos.y, (int)active_container_pos.z);
                        auto& c = world_chests[key];
                        for (auto& cs : c.slots) {
                            if (!cs.is_tool && cs.item.id == slots[i].id && cs.item.name == slots[i].name && cs.item.count < 64) {
                                int space = 64 - cs.item.count;
                                int add = std::min(space, slots[i].count);
                                cs.item.count += add;
                                slots[i].count -= add;
                                if (slots[i].count <= 0) break;
                            }
                        }
                        if (slots[i].count > 0) {
                            for (auto& cs : c.slots) {
                                if (!cs.is_tool && cs.item.count == 0) {
                                    cs.is_tool = false;
                                    cs.item = slots[i];
                                    slots[i] = { AIR, "", 0 };
                                    break;
                                }
                            }
                        } else {
                            slots[i] = { AIR, "", 0 };
                        }
                    } else {
                        // Move to storage
                        for (size_t k = 0; k < storage.size(); k++) {
                            if (storage[k].id == slots[i].id && storage[k].name == slots[i].name && storage[k].count < 64) {
                                int space = 64 - storage[k].count;
                                int add = std::min(space, slots[i].count);
                                storage[k].count += add;
                                slots[i].count -= add;
                                if (slots[i].count <= 0) break;
                            }
                        }
                        if (slots[i].count > 0) {
                            for (size_t k = 0; k < storage.size(); k++) {
                                if (storage[k].count == 0) {
                                    storage[k] = slots[i];
                                    slots[i] = { AIR, "", 0 };
                                    break;
                                }
                            }
                        } else {
                            slots[i] = { AIR, "", 0 };
                        }
                    }
                } else if (is_dragging) {
                    if (!dragging_tool) {
                        if (slots[i].count == 0) {
                            slots[i] = drag_item;
                            is_dragging = false;
                        } else if (slots[i].id == drag_item.id && slots[i].name == drag_item.name) {
                            int space = 64 - slots[i].count;
                            int add = std::min(space, drag_item.count);
                            slots[i].count += add;
                            drag_item.count -= add;
                            if (drag_item.count <= 0) is_dragging = false;
                        } else {
                            InventorySlot temp = slots[i];
                            slots[i] = drag_item;
                            drag_item = temp;
                        }
                    }
                } else {
                    if (slots[i].count > 0) {
                        drag_item = slots[i];
                        slots[i] = { AIR, "", 0 };
                        is_dragging = true;
                        dragging_tool = false;
                        drag_source_type = 0;
                        drag_source_idx = i;
                    }
                }
            } else if (is_r_click && is_dragging && !dragging_tool && drag_item.count > 0) {
                if (slots[i].count == 0) {
                    slots[i] = { drag_item.id, drag_item.name, 1 };
                    drag_item.count--;
                    if (drag_item.count <= 0) is_dragging = false;
                } else if (slots[i].id == drag_item.id && slots[i].name == drag_item.name && slots[i].count < 64) {
                    slots[i].count++;
                    drag_item.count--;
                    if (drag_item.count <= 0) is_dragging = false;
                }
            }
        }
    }
}

void UI::draw_chest_panel() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int panel_w = 436;
    int panel_h = 440;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;
    
    DrawRectangle(px, py, panel_w, panel_h, Color{22, 26, 33, 250});
    DrawRectangleLines(px, py, panel_w, panel_h, Color{70, 85, 110, 255});
    
    DrawText("Cofre", px + 20, py + 15, 16, Color{255, 215, 0, 255});
    
    Vector2 m = GetMousePosition();
    bool is_l_click = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    bool is_r_click = IsMouseButtonPressed(MOUSE_RIGHT_BUTTON);
    bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    
    if (m.x >= px + panel_w - 35 && m.x <= px + panel_w - 10 && m.y >= py + 10 && m.y <= py + 35) {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, RED);
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
        if (is_l_click) { close_ui(); return; }
    } else {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, Color{50, 60, 75, 255});
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
    }
    
    auto key = std::make_tuple((int)active_container_pos.x, (int)active_container_pos.y, (int)active_container_pos.z);
    auto& chest = world_chests[key];
    
    int slot_size = 40;
    int slot_pad = 4;
    int slot_total = slot_size + slot_pad;
    int chest_x = px + 20;
    int chest_y = py + 42;
    
    for (int i = 0; i < 27; i++) {
        int col = i % 9;
        int row = i / 9;
        int sx = chest_x + col * slot_total;
        int sy = chest_y + row * slot_total;
        
        bool hover = (m.x >= sx && m.x <= sx + slot_size && m.y >= sy && m.y <= sy + slot_size);
        DrawRectangle(sx, sy, slot_size, slot_size, hover ? Color{65, 80, 105, 255} : Color{28, 34, 44, 255});
        DrawRectangleLines(sx, sy, slot_size, slot_size, Color{80, 95, 120, 255});
        
        if (i < (int)chest.slots.size()) {
            auto& cs = chest.slots[i];
            if (cs.is_tool) {
                draw_tool_icon(cs.tool.type, cs.tool.tier, sx + 4, sy + 4, slot_size - 8);
                if (cs.tool.durability_max > 0) {
                    float pct = (float)cs.tool.durability_current / (float)cs.tool.durability_max;
                    Color dur_col = (pct > 0.5f) ? GREEN : ((pct > 0.2f) ? ORANGE : RED);
                    DrawRectangle(sx + 4, sy + slot_size - 6, (int)((slot_size - 8) * pct), 3, dur_col);
                }
            } else if (cs.item.count > 0 && cs.item.id != Config::AIR) {
                if (cs.item.id == 254) {
                    for (auto& [iid, itype] : Config::ITEMS) {
                        if (itype.name == cs.item.name) { draw_item_icon(iid, sx + 4, sy + 4, slot_size - 8); break; }
                    }
                } else {
                    draw_block_icon(cs.item.id, sx + 4, sy + 4, slot_size - 8);
                }
                DrawText(TextFormat("%d", cs.item.count), sx + slot_size - 18, sy + slot_size - 13, 11, WHITE);
            }
        }
        
        if (hover) {
            if (i < (int)chest.slots.size()) {
                auto& cs = chest.slots[i];
                if (cs.is_tool) {
                    static const char* TIER_N[] = { "Madera", "Piedra", "Hierro", "Plata", "Oro", "Diamante" };
                    static const char* TYPE_N[] = { "Pico", "Hacha", "Pala", "Martillo", "Mangual", "Espada" };
                    tooltip_text = std::string(TYPE_N[cs.tool.type]) + " de " + TIER_N[cs.tool.tier] + "\nDurabilidad: " + std::to_string(cs.tool.durability_current) + "/" + std::to_string(cs.tool.durability_max);
                } else if (cs.item.count > 0 && cs.item.id != Config::AIR) {
                    tooltip_text = cs.item.name + " (x" + std::to_string(cs.item.count) + ")";
                }
            }

            if (is_l_click) {
                if (is_shift) {
                    // Shift+Click chest item/tool into player inventory
                    if (i < (int)chest.slots.size()) {
                        if (chest.slots[i].is_tool) {
                            tool_inventory.push_back(chest.slots[i].tool);
                            chest.slots[i].is_tool = false;
                            chest.slots[i].tool = ToolSlot();
                        } else if (chest.slots[i].item.count > 0) {
                            if (chest.slots[i].item.id == 254) {
                                for (auto& [iid, itype] : Config::ITEMS) {
                                    if (itype.name == chest.slots[i].item.name) {
                                        add_item(iid, chest.slots[i].item.count);
                                        break;
                                    }
                                }
                            } else {
                                add_resource(chest.slots[i].item.id, chest.slots[i].item.count);
                            }
                            chest.slots[i].is_tool = false;
                            chest.slots[i].item = { AIR, "", 0 };
                        }
                    }
                } else if (is_dragging) {
                    if (dragging_tool) {
                        if (chest.slots[i].is_tool) {
                            ToolSlot temp = chest.slots[i].tool;
                            chest.slots[i].tool = drag_tool;
                            drag_tool = temp;
                        } else if (chest.slots[i].item.count == 0) {
                            chest.slots[i].is_tool = true;
                            chest.slots[i].tool = drag_tool;
                            chest.slots[i].item = { AIR, "", 0 };
                            is_dragging = false;
                            dragging_tool = false;
                        }
                    } else {
                        if (!chest.slots[i].is_tool) {
                            if (chest.slots[i].item.count == 0) {
                                chest.slots[i].is_tool = false;
                                chest.slots[i].item = drag_item;
                                is_dragging = false;
                            } else if (chest.slots[i].item.id == drag_item.id && chest.slots[i].item.name == drag_item.name) {
                                int space = 64 - chest.slots[i].item.count;
                                int add = std::min(space, drag_item.count);
                                chest.slots[i].item.count += add;
                                drag_item.count -= add;
                                if (drag_item.count <= 0) is_dragging = false;
                            } else {
                                InventorySlot temp = chest.slots[i].item;
                                chest.slots[i].item = drag_item;
                                drag_item = temp;
                            }
                        }
                    }
                } else {
                    if (i < (int)chest.slots.size()) {
                        if (chest.slots[i].is_tool) {
                            drag_tool = chest.slots[i].tool;
                            chest.slots[i].is_tool = false;
                            chest.slots[i].tool = ToolSlot();
                            is_dragging = true;
                            dragging_tool = true;
                            drag_source_type = 3;
                            drag_source_idx = i;
                        } else if (chest.slots[i].item.count > 0) {
                            drag_item = chest.slots[i].item;
                            chest.slots[i].is_tool = false;
                            chest.slots[i].item = { AIR, "", 0 };
                            is_dragging = true;
                            dragging_tool = false;
                            drag_source_type = 3;
                            drag_source_idx = i;
                        }
                    }
                }
            } else if (is_r_click && is_dragging && !dragging_tool && drag_item.count > 0) {
                if (!chest.slots[i].is_tool) {
                    if (chest.slots[i].item.count == 0) {
                        chest.slots[i].is_tool = false;
                        chest.slots[i].item = { drag_item.id, drag_item.name, 1 };
                        drag_item.count--;
                        if (drag_item.count <= 0) is_dragging = false;
                    } else if (chest.slots[i].item.id == drag_item.id && chest.slots[i].item.name == drag_item.name && chest.slots[i].item.count < 64) {
                        chest.slots[i].item.count++;
                        drag_item.count--;
                        if (drag_item.count <= 0) is_dragging = false;
                    }
                }
            }
        }
    }
    
    DrawLine(px + 20, py + 180, px + panel_w - 20, py + 180, Color{60, 75, 95, 255});
    draw_player_inventory_section(px + 20, py + 190, (int)m.x, (int)m.y, is_l_click, is_r_click, is_shift, true /* show_tools */);
}

void UI::draw_furnace_panel() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int panel_w = 436;
    int panel_h = 380;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;
    
    DrawRectangle(px, py, panel_w, panel_h, Color{22, 26, 33, 250});
    DrawRectangleLines(px, py, panel_w, panel_h, Color{70, 85, 110, 255});
    
    DrawText("Horno", px + 20, py + 15, 16, Color{255, 160, 40, 255});
    
    Vector2 m = GetMousePosition();
    bool is_l_click = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    bool is_r_click = IsMouseButtonPressed(MOUSE_RIGHT_BUTTON);
    bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    
    if (m.x >= px + panel_w - 35 && m.x <= px + panel_w - 10 && m.y >= py + 10 && m.y <= py + 35) {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, RED);
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
        if (is_l_click) { close_ui(); return; }
    } else {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, Color{50, 60, 75, 255});
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
    }
    
    auto key = std::make_tuple((int)active_container_pos.x, (int)active_container_pos.y, (int)active_container_pos.z);
    auto& furnace = world_furnaces[key];
    
    int slot_size = 40;
    int in_x = px + 120;
    int in_y = py + 42;
    int fuel_x = in_x;
    int fuel_y = py + 106;
    int out_x = px + 260;
    int out_y = py + 70;
    
    bool hover_in = (m.x >= in_x && m.x <= in_x + slot_size && m.y >= in_y && m.y <= in_y + slot_size);
    DrawRectangle(in_x, in_y, slot_size, slot_size, hover_in ? Color{65, 80, 105, 255} : Color{30, 36, 46, 255});
    DrawRectangleLines(in_x, in_y, slot_size, slot_size, Color{85, 100, 125, 255});
    if (furnace.input.count > 0) {
        if (furnace.input.id == 254) {
            for (auto& [iid, itype] : Config::ITEMS) {
                if (itype.name == furnace.input.name) { draw_item_icon(iid, in_x + 4, in_y + 4, slot_size - 8); break; }
            }
        } else {
            draw_block_icon(furnace.input.id, in_x + 4, in_y + 4, slot_size - 8);
        }
        DrawText(TextFormat("%d", furnace.input.count), in_x + slot_size - 18, in_y + slot_size - 13, 11, WHITE);
    }
    if (hover_in) {
        if (furnace.input.count > 0) tooltip_text = furnace.input.name + " (x" + std::to_string(furnace.input.count) + ")";
        if (is_l_click) {
            if (is_dragging && !dragging_tool) {
                if (furnace.input.count == 0) {
                    furnace.input = drag_item;
                    is_dragging = false;
                } else if (furnace.input.id == drag_item.id && furnace.input.name == drag_item.name) {
                    int space = 64 - furnace.input.count;
                    int add = std::min(space, drag_item.count);
                    furnace.input.count += add;
                    drag_item.count -= add;
                    if (drag_item.count <= 0) is_dragging = false;
                } else {
                    InventorySlot temp = furnace.input;
                    furnace.input = drag_item;
                    drag_item = temp;
                }
            } else if (!is_dragging && furnace.input.count > 0) {
                drag_item = furnace.input;
                furnace.input = { AIR, "", 0 };
                is_dragging = true;
                dragging_tool = false;
            }
        } else if (is_r_click && is_dragging && !dragging_tool && drag_item.count > 0) {
            if (furnace.input.count == 0) {
                furnace.input = { drag_item.id, drag_item.name, 1 };
                drag_item.count--;
                if (drag_item.count <= 0) is_dragging = false;
            } else if (furnace.input.id == drag_item.id && furnace.input.name == drag_item.name && furnace.input.count < 64) {
                furnace.input.count++;
                drag_item.count--;
                if (drag_item.count <= 0) is_dragging = false;
            }
        }
    }
    
    int fire_x = in_x + 12;
    int fire_y = in_y + 43;
    float fire_pct = (furnace.max_burn_ticks > 0) ? ((float)furnace.burn_ticks / (float)furnace.max_burn_ticks) : 0.0f;
    DrawRectangle(fire_x, fire_y, 16, 18, Color{40, 40, 40, 255});
    if (fire_pct > 0.0f) {
        int fh = (int)(18.0f * fire_pct);
        DrawRectangle(fire_x, fire_y + (18 - fh), 16, fh, ORANGE);
        DrawRectangle(fire_x + 3, fire_y + (18 - fh) + 3, 10, fh - 3, YELLOW);
    }
    
    bool hover_fuel = (m.x >= fuel_x && m.x <= fuel_x + slot_size && m.y >= fuel_y && m.y <= fuel_y + slot_size);
    DrawRectangle(fuel_x, fuel_y, slot_size, slot_size, hover_fuel ? Color{65, 80, 105, 255} : Color{30, 36, 46, 255});
    DrawRectangleLines(fuel_x, fuel_y, slot_size, slot_size, Color{85, 100, 125, 255});
    if (furnace.fuel.count > 0) {
        if (furnace.fuel.id == 254) {
            for (auto& [iid, itype] : Config::ITEMS) {
                if (itype.name == furnace.fuel.name) { draw_item_icon(iid, fuel_x + 4, fuel_y + 4, slot_size - 8); break; }
            }
        } else {
            draw_block_icon(furnace.fuel.id, fuel_x + 4, fuel_y + 4, slot_size - 8);
        }
        DrawText(TextFormat("%d", furnace.fuel.count), fuel_x + slot_size - 18, fuel_y + slot_size - 13, 11, WHITE);
    }
    if (hover_fuel) {
        if (furnace.fuel.count > 0) tooltip_text = furnace.fuel.name + " (x" + std::to_string(furnace.fuel.count) + ")";
        if (is_l_click) {
            if (is_dragging && !dragging_tool) {
                if (furnace.fuel.count == 0) {
                    furnace.fuel = drag_item;
                    is_dragging = false;
                } else if (furnace.fuel.id == drag_item.id && furnace.fuel.name == drag_item.name) {
                    int space = 64 - furnace.fuel.count;
                    int add = std::min(space, drag_item.count);
                    furnace.fuel.count += add;
                    drag_item.count -= add;
                    if (drag_item.count <= 0) is_dragging = false;
                } else {
                    InventorySlot temp = furnace.fuel;
                    furnace.fuel = drag_item;
                    drag_item = temp;
                }
            } else if (!is_dragging && furnace.fuel.count > 0) {
                drag_item = furnace.fuel;
                furnace.fuel = { AIR, "", 0 };
                is_dragging = true;
                dragging_tool = false;
            }
        } else if (is_r_click && is_dragging && !dragging_tool && drag_item.count > 0) {
            if (furnace.fuel.count == 0) {
                furnace.fuel = { drag_item.id, drag_item.name, 1 };
                drag_item.count--;
                if (drag_item.count <= 0) is_dragging = false;
            } else if (furnace.fuel.id == drag_item.id && furnace.fuel.name == drag_item.name && furnace.fuel.count < 64) {
                furnace.fuel.count++;
                drag_item.count--;
                if (drag_item.count <= 0) is_dragging = false;
            }
        }
    }
    
    int arrow_x = in_x + 55;
    int arrow_y = py + 78;
    float cook_pct = (float)furnace.cook_ticks / 200.0f;
    DrawRectangle(arrow_x, arrow_y, 45, 14, Color{45, 52, 65, 255});
    DrawRectangle(arrow_x, arrow_y, (int)(45.0f * cook_pct), 14, Color{255, 215, 0, 255});
    DrawRectangleLines(arrow_x, arrow_y, 45, 14, Color{80, 95, 120, 255});
    DrawText(">>>", arrow_x + 12, arrow_y + 1, 12, WHITE);
    
    bool hover_out = (m.x >= out_x && m.x <= out_x + slot_size + 6 && m.y >= out_y && m.y <= out_y + slot_size + 6);
    DrawRectangle(out_x, out_y, slot_size + 6, slot_size + 6, hover_out ? Color{70, 90, 115, 255} : Color{35, 42, 55, 255});
    DrawRectangleLines(out_x, out_y, slot_size + 6, slot_size + 6, GOLD);
    if (furnace.output.count > 0) {
        if (furnace.output.id == 254) {
            for (auto& [iid, itype] : Config::ITEMS) {
                if (itype.name == furnace.output.name) { draw_item_icon(iid, out_x + 5, out_y + 5, slot_size - 4); break; }
            }
        } else {
            draw_block_icon(furnace.output.id, out_x + 5, out_y + 5, slot_size - 4);
        }
        DrawText(TextFormat("%d", furnace.output.count), out_x + slot_size - 12, out_y + slot_size - 9, 12, WHITE);
    }
    if (hover_out) {
        if (furnace.output.count > 0) tooltip_text = furnace.output.name + " (x" + std::to_string(furnace.output.count) + ")";
        if (is_l_click && furnace.output.count > 0) {
            if (is_shift) {
                // Shift+Click furnace output into player inventory
                if (furnace.output.id == 254) {
                    for (auto& [iid, itype] : Config::ITEMS) {
                        if (itype.name == furnace.output.name) {
                            add_item(iid, furnace.output.count);
                            break;
                        }
                    }
                } else {
                    add_resource(furnace.output.id, furnace.output.count);
                }
                furnace.output = { AIR, "", 0 };
            } else if (!is_dragging) {
                drag_item = furnace.output;
                furnace.output = { AIR, "", 0 };
                is_dragging = true;
                dragging_tool = false;
            } else if (!dragging_tool && drag_item.id == furnace.output.id && drag_item.name == furnace.output.name) {
                drag_item.count += furnace.output.count;
                furnace.output = { AIR, "", 0 };
            }
        }
    }
    
    DrawLine(px + 20, py + 155, px + panel_w - 20, py + 155, Color{60, 75, 95, 255});
    draw_player_inventory_section(px + 20, py + 165, (int)m.x, (int)m.y, is_l_click, is_r_click, is_shift, false /* NO tools in furnace */);
}

void UI::draw_inventory_panel() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int panel_w = 436;
    int panel_h = 350;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;
    
    DrawRectangle(px, py, panel_w, panel_h, Color{22, 26, 33, 250});
    DrawRectangleLines(px, py, panel_w, panel_h, Color{70, 85, 110, 255});
    
    DrawText("Inventario", px + 20, py + 15, 16, Color{200, 220, 255, 255});
    
    Vector2 m = GetMousePosition();
    bool is_l_click = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    bool is_r_click = IsMouseButtonPressed(MOUSE_RIGHT_BUTTON);
    bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    
    if (m.x >= px + panel_w - 35 && m.x <= px + panel_w - 10 && m.y >= py + 10 && m.y <= py + 35) {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, RED);
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
        if (is_l_click) { close_ui(); return; }
    } else {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, Color{50, 60, 75, 255});
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
    }
    
    // Crafteo Básico (Centrado y limpio)
    int slot_size = 40;
    int slot_pad = 4;
    int slot_total = slot_size + slot_pad;
    int craft_y = py + 42;
    
    std::vector<int> basic_recipes;
    for (size_t i = 0; i < Config::RECIPES.size(); i++) {
        if (!Config::RECIPES[i].requires_table) {
            basic_recipes.push_back(i);
        }
    }
    
    int total_craft_w = (int)basic_recipes.size() * slot_total - slot_pad;
    int c_start_x = px + (panel_w - total_craft_w) / 2;
    
    for (size_t k = 0; k < basic_recipes.size(); k++) {
        int r_idx = basic_recipes[k];
        const auto& rec = Config::RECIPES[r_idx];
        int sx = c_start_x + k * slot_total;
        int sy = craft_y;
        
        bool craftable = can_craft(r_idx);
        bool hover = (m.x >= sx && m.x <= sx + slot_size && m.y >= sy && m.y <= sy + slot_size);
        
        DrawRectangle(sx, sy, slot_size, slot_size, craftable ? (hover ? Color{50, 80, 65, 255} : Color{30, 48, 40, 255}) : (hover ? Color{45, 55, 70, 255} : Color{28, 34, 44, 255}));
        DrawRectangleLines(sx, sy, slot_size, slot_size, craftable ? (hover ? GREEN : Color{80, 160, 90, 255}) : Color{60, 70, 85, 255});
        
        if (rec.result_is_tool) {
            draw_tool_icon(rec.result_tool_type, rec.result_tool_tier, sx + 4, sy + 4, slot_size - 8);
        } else if (rec.result_is_block) {
            draw_block_icon(rec.result_id, sx + 4, sy + 4, slot_size - 8);
        } else {
            draw_item_icon(rec.result_id, sx + 4, sy + 4, slot_size - 8);
        }
        
        if (rec.result_count > 1) {
            DrawText(TextFormat("%d", rec.result_count), sx + slot_size - 16, sy + slot_size - 13, 11, WHITE);
        }
        
        if (hover) {
            std::string ttip = rec.result_name;
            if (rec.result_count > 1) ttip += " (x" + std::to_string(rec.result_count) + ")";
            ttip += "\nMateriales: ";
            for (size_t j = 0; j < rec.ingredients.size(); j++) {
                if (j > 0) ttip += ", ";
                std::string iname = rec.ingredients[j].is_item ? Config::ITEMS.at(rec.ingredients[j].id).name : Config::BLOCKS.at(rec.ingredients[j].id).name;
                int player_has = rec.ingredients[j].is_item ? (has_item(rec.ingredients[j].id, 1) ? 1 : 0) : count_block(rec.ingredients[j].id);
                ttip += std::to_string(rec.ingredients[j].count) + " " + iname + " (" + std::to_string(player_has) + "/" + std::to_string(rec.ingredients[j].count) + ")";
            }
            if (craftable) ttip += "\n[Clic: Craftear x1 | Shift+Clic: Craftear x5]";
            tooltip_text = ttip;
            
            if (is_l_click && craftable && !is_dragging) {
                craft(r_idx, is_shift ? 5 : 1);
            }
        }
    }
    
    DrawLine(px + 20, py + 90, px + panel_w - 20, py + 90, Color{60, 75, 95, 255});
    draw_player_inventory_section(px + 20, py + 98, (int)m.x, (int)m.y, is_l_click, is_r_click, is_shift, true /* show_tools */);
}

void UI::draw_crafting_table_panel() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int panel_w = 920;
    int panel_h = 360;
    int px = (sw - panel_w) / 2;
    int py = (sh - panel_h) / 2;
    
    DrawRectangle(px, py, panel_w, panel_h, Color{22, 26, 33, 250});
    DrawRectangleLines(px, py, panel_w, panel_h, Color{70, 85, 110, 255});
    
    DrawText("Mesa de Crafteo", px + 20, py + 15, 16, GOLD);
    
    Vector2 m = GetMousePosition();
    bool is_l_click = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    bool is_r_click = IsMouseButtonPressed(MOUSE_RIGHT_BUTTON);
    bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    
    if (m.x >= px + panel_w - 35 && m.x <= px + panel_w - 10 && m.y >= py + 10 && m.y <= py + 35) {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, RED);
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
        if (is_l_click) { close_ui(); return; }
    } else {
        DrawRectangle(px + panel_w - 35, py + 10, 25, 25, Color{50, 60, 75, 255});
        DrawText("X", px + panel_w - 28, py + 14, 14, WHITE);
    }
    
    // Left: Player Inventory with tools
    draw_player_inventory_section(px + 20, py + 45, (int)m.x, (int)m.y, is_l_click, is_r_click, is_shift, true /* show_tools */);
    
    // Vertical separator
    DrawLine(px + 430, py + 40, px + 430, py + panel_h - 20, Color{60, 75, 95, 255});
    
    // Right: Recipe Catalog
    int craft_x = px + 445;
    int craft_y = py + 80;
    int craft_w = panel_w - 465;
    int craft_h = panel_h - 100;
    
    static const char* CAT_NAMES[] = { "Todos", "Herramientas", "Bloques", "Listos" };
    for (int t = 0; t < 4; t++) {
        int tx = craft_x + t * 110;
        int ty = py + 45;
        bool active = (craft_category == t);
        bool hover = (m.x >= tx && m.x <= tx + 105 && m.y >= ty && m.y <= ty + 28);
        DrawRectangle(tx, ty, 105, 28, active ? Color{70, 95, 130, 255} : (hover ? Color{50, 60, 75, 255} : Color{32, 38, 48, 255}));
        DrawRectangleLines(tx, ty, 105, 28, active ? GOLD : Color{70, 80, 95, 255});
        DrawText(CAT_NAMES[t], tx + 14, ty + 7, 12, active ? WHITE : Color{200, 205, 215, 255});
        if (hover && is_l_click) {
            craft_category = t;
            craft_scroll_offset = 0;
        }
    }
    
    DrawRectangle(craft_x, craft_y, craft_w, craft_h, Color{18, 21, 27, 255});
    DrawRectangleLines(craft_x, craft_y, craft_w, craft_h, Color{60, 70, 85, 255});
    
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && m.x >= craft_x && m.x <= craft_x + craft_w && m.y >= craft_y && m.y <= craft_y + craft_h) {
        craft_scroll_offset -= (int)(wheel * 2);
        if (craft_scroll_offset < 0) craft_scroll_offset = 0;
    }
    
    std::vector<int> visible_recipes;
    for (size_t i = 0; i < Config::RECIPES.size(); i++) {
        const auto& rec = Config::RECIPES[i];
        if (craft_category == 1 && !rec.result_is_tool) continue;
        if (craft_category == 2 && rec.result_is_tool) continue;
        if (craft_category == 3 && !can_craft(i)) continue;
        visible_recipes.push_back(i);
    }
    
    int card_h = 58;
    int card_pad = 6;
    int start_idx = craft_scroll_offset;
    int max_visible = craft_h / (card_h + card_pad);
    
    for (int vi = 0; vi < max_visible && (start_idx + vi) < (int)visible_recipes.size(); vi++) {
        int r_idx = visible_recipes[start_idx + vi];
        const auto& rec = Config::RECIPES[r_idx];
        int ry = craft_y + 8 + vi * (card_h + card_pad);
        int rx = craft_x + 8;
        int rw = craft_w - 16;
        
        bool craftable = can_craft(r_idx);
        bool hover = (m.x >= rx && m.x <= rx + rw && m.y >= ry && m.y <= ry + card_h);
        
        DrawRectangle(rx, ry, rw, card_h, craftable ? (hover ? Color{45, 60, 75, 255} : Color{30, 38, 48, 255}) : Color{24, 28, 34, 200});
        DrawRectangleLines(rx, ry, rw, card_h, craftable ? Color{80, 110, 140, 255} : Color{50, 55, 65, 255});
        
        if (rec.result_is_tool) {
            draw_tool_icon(rec.result_tool_type, rec.result_tool_tier, rx + 6, ry + 10, 38);
        } else if (rec.result_is_block) {
            draw_block_icon(rec.result_id, rx + 6, ry + 10, 38);
        } else {
            draw_item_icon(rec.result_id, rx + 6, ry + 10, 38);
        }
        
        DrawText(rec.result_name.c_str(), rx + 52, ry + 8, 12, craftable ? WHITE : Color{140, 140, 140, 255});
        
        int ing_x = rx + 52;
        int ing_y = ry + 26;
        for (const auto& ing : rec.ingredients) {
            std::string ing_name = ing.is_item ? Config::ITEMS.at(ing.id).name : Config::BLOCKS.at(ing.id).name;
            int player_has = ing.is_item ? (has_item(ing.id, 1) ? 1 : 0) : count_block(ing.id);
            Color ing_col = (player_has >= ing.count) ? Color{150, 230, 150, 255} : Color{230, 120, 120, 255};
            DrawText(TextFormat("%s x%d", ing_name.c_str(), ing.count), ing_x, ing_y, 10, ing_col);
            ing_x += (int)ing_name.length() * 6 + 32;
        }
        
        int btn_w = 70;
        int btn_h = 32;
        int btn_x = rx + rw - btn_w - 8;
        int btn_y = ry + 13;
        bool hover_btn = (m.x >= btn_x && m.x <= btn_x + btn_w && m.y >= btn_y && m.y <= btn_y + btn_h);
        
        DrawRectangle(btn_x, btn_y, btn_w, btn_h, craftable ? (hover_btn ? Color{40, 160, 60, 255} : Color{30, 120, 45, 255}) : Color{50, 50, 50, 255});
        DrawRectangleLines(btn_x, btn_y, btn_w, btn_h, craftable ? GREEN : Color{70, 70, 70, 255});
        DrawText("Crear", btn_x + 16, btn_y + 10, 11, craftable ? WHITE : Color{150, 150, 150, 255});
        
        if (hover) {
            std::string ttip = rec.result_name;
            if (rec.result_count > 1) ttip += " (x" + std::to_string(rec.result_count) + ")";
            ttip += "\nMateriales: ";
            for (size_t j = 0; j < rec.ingredients.size(); j++) {
                if (j > 0) ttip += ", ";
                std::string iname = rec.ingredients[j].is_item ? Config::ITEMS.at(rec.ingredients[j].id).name : Config::BLOCKS.at(rec.ingredients[j].id).name;
                ttip += std::to_string(rec.ingredients[j].count) + " " + iname;
            }
            if (craftable) ttip += "\n[Clic: Craftear x1 | Shift+Clic: Craftear x5]";
            tooltip_text = ttip;
        }

        if (hover_btn && is_l_click && craftable) {
            craft(r_idx, is_shift ? 5 : 1);
        }
    }
}

void UI::draw() {
    tooltip_text = "";

    // 1. Mira en el centro de la pantalla
    if (!is_open) {
        int cx = GetScreenWidth() / 2;
        int cy = GetScreenHeight() / 2;
        DrawRectangle(cx - 1, cy - 6, 2, 12, Fade(WHITE, 0.8f));
        DrawRectangle(cx - 6, cy - 1, 12, 2, Fade(WHITE, 0.8f));
        DrawRectangle(cx - 1, cy - 1, 2, 2, WHITE);
    }
    
    draw_hotbar();
    
    if (!is_open) {
        draw_tool_hud();
    }
    
    if (is_open) {
        if (mode == UI_MODE_CHEST) {
            draw_chest_panel();
        } else if (mode == UI_MODE_FURNACE) {
            draw_furnace_panel();
        } else if (mode == UI_MODE_CRAFTING_TABLE) {
            draw_crafting_table_panel();
        } else {
            draw_inventory_panel();
        }
    }
    
    // Cursor con objeto arrastrado
    if (is_dragging) {
        Vector2 m = GetMousePosition();
        if (dragging_tool) {
            draw_tool_icon(drag_tool.type, drag_tool.tier, (int)m.x - 16, (int)m.y - 16, 32);
        } else if (drag_item.count > 0) {
            if (drag_item.id == 254) {
                for (auto& [iid, itype] : Config::ITEMS) {
                    if (itype.name == drag_item.name) { draw_item_icon(iid, (int)m.x - 16, (int)m.y - 16, 32); break; }
                }
            } else if (drag_item.id != Config::AIR) {
                draw_block_icon(drag_item.id, (int)m.x - 16, (int)m.y - 16, 32);
            }
            DrawText(TextFormat("%d", drag_item.count), (int)m.x + 8, (int)m.y + 4, 12, YELLOW);
        }
    }

    // Tooltip flotante
    if (!tooltip_text.empty() && !is_dragging) {
        Vector2 mpos = GetMousePosition();
        int font_size = 12;
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
        int ttip_h = line_count * 16 + pad * 2;
        
        int tx = (int)mpos.x + 14;
        int ty = (int)mpos.y + 14;
        if (tx + ttip_w > GetScreenWidth() - 10) tx = GetScreenWidth() - ttip_w - 10;
        if (ty + ttip_h > GetScreenHeight() - 10) ty = GetScreenHeight() - ttip_h - 10;
        
        DrawRectangleRounded({ (float)tx, (float)ty, (float)ttip_w, (float)ttip_h }, 0.15f, 4, Color{ 14, 18, 24, 250 });
        DrawRectangleRoundedLines({ (float)tx, (float)ty, (float)ttip_w, (float)ttip_h }, 0.15f, 4, Color{ 80, 105, 140, 255 });
        
        int curr_y = ty + pad;
        cur_line.clear();
        for (char c : tooltip_text) {
            if (c == '\n') {
                DrawText(cur_line.c_str(), tx + pad, curr_y, font_size, Color{ 240, 245, 255, 255 });
                cur_line.clear();
                curr_y += 16;
            } else {
                cur_line += c;
            }
        }
        if (!cur_line.empty()) {
            DrawText(cur_line.c_str(), tx + pad, curr_y, font_size, Color{ 240, 245, 255, 255 });
        }
    }
}

void UI::add_tool(Config::ToolType type, Config::ToolTier tier, int durability) {
    const ToolInfo* info = nullptr;
    for (auto& t : Config::TOOLS) {
        if (t.type == type && t.tier == tier) {
            info = &t;
            break;
        }
    }
    int max_dur = info ? info->durability : 100;
    int cur_dur = (durability > 0) ? std::min(durability, max_dur) : max_dur;

    ToolSlot ts = { type, tier, cur_dur, max_dur, false };
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
    if (tool_inventory.empty()) {
        selected_tool_idx = -1;
        return;
    }
    selected_tool_idx += direction;
    if (selected_tool_idx < -1) selected_tool_idx = (int)tool_inventory.size() - 1;
    if (selected_tool_idx >= (int)tool_inventory.size()) selected_tool_idx = -1;
}

void UI::cycle_block(int direction) {
    // Cycle ONLY within block slots 1..9 (never switch to slot 0)
    int cur = selected_slot;
    if (cur < 1) cur = 1;
    if (cur > 9) cur = 9;
    cur -= 1; // 0..8
    cur = (cur + direction + 9) % 9;
    selected_slot = cur + 1; // 1..9
}

void UI::add_resource(uint8_t block_type, int count) {
    if (block_type == AIR || count <= 0) return;
    std::string bname = Config::BLOCKS.count(block_type) ? Config::BLOCKS.at(block_type).name : "Bloque";
    
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == block_type && slots[i].count < 64) {
            int space = 64 - slots[i].count;
            int add = std::min(space, count);
            slots[i].count += add;
            count -= add;
            if (count <= 0) return;
        }
    }
    for (size_t i = 0; i < storage.size(); i++) {
        if (storage[i].id == block_type && storage[i].count < 64) {
            int space = 64 - storage[i].count;
            int add = std::min(space, count);
            storage[i].count += add;
            count -= add;
            if (count <= 0) return;
        }
    }
    for (int i = 1; i < 10; i++) {
        if (slots[i].count == 0) {
            int add = std::min(64, count);
            slots[i] = { block_type, bname, add };
            count -= add;
            if (count <= 0) return;
        }
    }
    for (size_t i = 0; i < storage.size(); i++) {
        if (storage[i].count == 0) {
            int add = std::min(64, count);
            storage[i] = { block_type, bname, add };
            count -= add;
            if (count <= 0) return;
        }
    }
}

void UI::add_item(uint8_t item_id, int count) {
    if (count <= 0 || !Config::ITEMS.count(item_id)) return;
    std::string iname = Config::ITEMS.at(item_id).name;
    
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == 254 && slots[i].name == iname && slots[i].count < 64) {
            int space = 64 - slots[i].count;
            int add = std::min(space, count);
            slots[i].count += add;
            count -= add;
            if (count <= 0) return;
        }
    }
    for (size_t i = 0; i < storage.size(); i++) {
        if (storage[i].id == 254 && storage[i].name == iname && storage[i].count < 64) {
            int space = 64 - storage[i].count;
            int add = std::min(space, count);
            storage[i].count += add;
            count -= add;
            if (count <= 0) return;
        }
    }
    for (int i = 1; i < 10; i++) {
        if (slots[i].count == 0) {
            int add = std::min(64, count);
            slots[i] = { 254, iname, add };
            count -= add;
            if (count <= 0) return;
        }
    }
    for (size_t i = 0; i < storage.size(); i++) {
        if (storage[i].count == 0) {
            int add = std::min(64, count);
            storage[i] = { 254, iname, add };
            count -= add;
            if (count <= 0) return;
        }
    }
}

bool UI::has_item(uint8_t item_id, int count) const {
    if (!Config::ITEMS.count(item_id)) return false;
    std::string iname = Config::ITEMS.at(item_id).name;
    int total = 0;
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == 254 && slots[i].name == iname) total += slots[i].count;
    }
    for (const auto& s : storage) {
        if (s.id == 254 && s.name == iname) total += s.count;
    }
    return total >= count;
}

bool UI::remove_item(uint8_t item_id, int count) {
    if (!has_item(item_id, count)) return false;
    std::string iname = Config::ITEMS.at(item_id).name;
    int rem = count;
    for (int i = 1; i < 10 && rem > 0; i++) {
        if (slots[i].id == 254 && slots[i].name == iname) {
            int take = std::min(slots[i].count, rem);
            slots[i].count -= take;
            rem -= take;
            if (slots[i].count <= 0) slots[i] = { AIR, "", 0 };
        }
    }
    for (size_t i = 0; i < storage.size() && rem > 0; i++) {
        if (storage[i].id == 254 && storage[i].name == iname) {
            int take = std::min(storage[i].count, rem);
            storage[i].count -= take;
            rem -= take;
            if (storage[i].count <= 0) storage[i] = { AIR, "", 0 };
        }
    }
    return true;
}

int UI::count_block(uint8_t block_id) const {
    int total = 0;
    for (int i = 1; i < 10; i++) {
        if (slots[i].id == block_id) total += slots[i].count;
    }
    for (const auto& s : storage) {
        if (s.id == block_id) total += s.count;
    }
    return total;
}

bool UI::remove_block(uint8_t block_id, int count) {
    if (count_block(block_id) < count) return false;
    int rem = count;
    for (int i = 1; i < 10 && rem > 0; i++) {
        if (slots[i].id == block_id) {
            int take = std::min(slots[i].count, rem);
            slots[i].count -= take;
            rem -= take;
            if (slots[i].count <= 0) slots[i] = { AIR, "", 0 };
        }
    }
    for (size_t i = 0; i < storage.size() && rem > 0; i++) {
        if (storage[i].id == block_id) {
            int take = std::min(storage[i].count, rem);
            storage[i].count -= take;
            rem -= take;
            if (storage[i].count <= 0) storage[i] = { AIR, "", 0 };
        }
    }
    return true;
}

bool UI::can_craft(int recipe_index) const {
    if (recipe_index < 0 || recipe_index >= (int)Config::RECIPES.size()) return false;
    const auto& rec = Config::RECIPES[recipe_index];
    for (const auto& ing : rec.ingredients) {
        if (ing.is_item) {
            if (!has_item(ing.id, ing.count)) return false;
        } else {
            if (count_block(ing.id) < ing.count) return false;
        }
    }
    return true;
}

void UI::craft(int recipe_index, int times) {
    if (!can_craft(recipe_index)) return;
    const auto& rec = Config::RECIPES[recipe_index];
    
    for (int t = 0; t < times; t++) {
        if (!can_craft(recipe_index)) break;
        for (const auto& ing : rec.ingredients) {
            if (ing.is_item) {
                remove_item(ing.id, ing.count);
            } else {
                remove_block(ing.id, ing.count);
            }
        }
        if (rec.result_is_tool) {
            add_tool(rec.result_tool_type, rec.result_tool_tier);
        } else if (rec.result_is_block) {
            add_resource(rec.result_id, rec.result_count);
        } else {
            add_item(rec.result_id, rec.result_count);
        }
    }
}

void UI::save_inventory() {}
void UI::load_inventory() {}
void UI::add_log() { add_resource(Config::WOOD, 16); }