#include "ui/UI.hpp"
#include <algorithm>

using namespace Config;

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
