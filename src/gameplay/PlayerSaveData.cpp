#include "gameplay/PlayerSaveData.hpp"
#include "core/Config.hpp"
#include "ui/UI.hpp"
#include <raylib.h>
#include <fstream>
#include <iostream>
#include "core/json.hpp"

using json = nlohmann::json;

void load_player_data(const std::string& save_dir, Camera3D& camera, UI& ui, float& day_time, int& health) {
    std::ifstream file(save_dir + "/player.json");
    if (file.is_open()) {
        try {
            json pj = json::parse(file);
            camera.position.x = pj.value("pos_x", 0.0f);
            camera.position.y = pj.value("pos_y", 100.0f);
            camera.position.z = pj.value("pos_z", 0.0f);

            camera.target.x = pj.value("target_x", camera.position.x);
            camera.target.y = pj.value("target_y", camera.position.y);
            camera.target.z = pj.value("target_z", camera.position.z + 1.0f);

            day_time = pj.value("day_time", 1.5f);
            health = pj.value("health", 20);

            for (size_t i = 0; i < ui.slots.size(); i++) {
                std::string k_id = "slot_" + std::to_string(i) + "_id";
                std::string k_cnt = "slot_" + std::to_string(i) + "_count";
                std::string k_nm = "slot_" + std::to_string(i) + "_name";
                if (pj.contains(k_id)) {
                    ui.slots[i].id = (uint8_t)pj[k_id].get<int>();
                    ui.slots[i].count = pj.value(k_cnt, 0);
                    if (pj.contains(k_nm)) {
                        ui.slots[i].name = pj[k_nm].get<std::string>();
                    } else if (Config::BLOCKS.count(ui.slots[i].id)) {
                        ui.slots[i].name = Config::BLOCKS.at(ui.slots[i].id).name;
                    }
                }
            }

            ui.tool_inventory.clear();
            int num_tools = pj.value("tool_count", 0);
            for (int t = 0; t < num_tools && t < 18; t++) {
                std::string k_type = "tool_" + std::to_string(t) + "_type";
                std::string k_tier = "tool_" + std::to_string(t) + "_tier";
                std::string k_dur = "tool_" + std::to_string(t) + "_dur";
                if (pj.contains(k_type) && pj.contains(k_tier)) {
                    int ttype = pj[k_type].get<int>();
                    int ttier = pj[k_tier].get<int>();
                    int tdur = pj.value(k_dur, 100);
                    if (ttype >= 0 && ttype < (int)Config::TOOL_COUNT && ttier >= 0 && ttier < (int)Config::TIER_COUNT) {
                        Config::ToolType type = (Config::ToolType)ttype;
                        Config::ToolTier tier = (Config::ToolTier)ttier;
                        int max_dur = 100;
                        for (auto& ti : Config::TOOLS) {
                            if (ti.type == type && ti.tier == tier) { max_dur = ti.durability; break; }
                        }
                        ui.tool_inventory.push_back({type, tier, tdur, max_dur, false});
                    }
                }
            }
            ui.selected_tool_idx = pj.value("selected_tool", 0);

            for (size_t i = 0; i < ui.storage.size(); i++) {
                std::string k_id = "storage_" + std::to_string(i) + "_id";
                std::string k_cnt = "storage_" + std::to_string(i) + "_count";
                std::string k_nm = "storage_" + std::to_string(i) + "_name";
                if (pj.contains(k_id)) {
                    ui.storage[i].id = (uint8_t)pj[k_id].get<int>();
                    ui.storage[i].count = pj.value(k_cnt, 0);
                    if (pj.contains(k_nm)) {
                        ui.storage[i].name = pj[k_nm].get<std::string>();
                    } else if (Config::BLOCKS.count(ui.storage[i].id)) {
                        ui.storage[i].name = Config::BLOCKS.at(ui.storage[i].id).name;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[SaveSystem] Error parsing player.json: " << e.what() << std::endl;
        }
    }
}

void save_player_data(const std::string& save_dir, Camera3D& camera, UI& ui, float day_time, int health) {
    std::ofstream out(save_dir + "/player.json");
    if (out.is_open()) {
        json pj;
        pj["pos_x"] = camera.position.x;
        pj["pos_y"] = camera.position.y;
        pj["pos_z"] = camera.position.z;
        pj["target_x"] = camera.target.x;
        pj["target_y"] = camera.target.y;
        pj["target_z"] = camera.target.z;
        pj["day_time"] = day_time;
        pj["health"] = health;

        for (size_t i = 0; i < ui.slots.size(); i++) {
            pj["slot_" + std::to_string(i) + "_id"] = (int)ui.slots[i].id;
            pj["slot_" + std::to_string(i) + "_count"] = ui.slots[i].count;
            pj["slot_" + std::to_string(i) + "_name"] = ui.slots[i].name;
        }

        pj["tool_count"] = ui.tool_inventory.size();
        for (size_t t = 0; t < ui.tool_inventory.size(); t++) {
            pj["tool_" + std::to_string(t) + "_type"] = (int)ui.tool_inventory[t].type;
            pj["tool_" + std::to_string(t) + "_tier"] = (int)ui.tool_inventory[t].tier;
            pj["tool_" + std::to_string(t) + "_dur"] = ui.tool_inventory[t].durability_current;
        }
        pj["selected_tool"] = ui.selected_tool_idx;

        for (size_t i = 0; i < ui.storage.size(); i++) {
            pj["storage_" + std::to_string(i) + "_id"] = (int)ui.storage[i].id;
            pj["storage_" + std::to_string(i) + "_count"] = ui.storage[i].count;
            pj["storage_" + std::to_string(i) + "_name"] = ui.storage[i].name;
        }

        out << pj.dump(2) << std::endl;
        out.close();
    }
}
