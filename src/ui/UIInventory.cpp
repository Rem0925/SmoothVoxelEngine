#include "ui/UI.hpp"
#include <algorithm>

using namespace Config;

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

void UI::consume_held_item() {
    if (selected_slot > 0 && selected_slot < (int)slots.size()) {
        slots[selected_slot].count--;
        if (slots[selected_slot].count <= 0) {
            slots[selected_slot].id = AIR;
            slots[selected_slot].name = "";
        }
    }
}
