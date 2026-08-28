#include "BlockRegistry.hpp"
#include "json.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace BlockRegistry {

static Config::ToolType parse_tool_type(const std::string& str) {
    if (str == "pickaxe" || str == "pico") return Config::TOOL_PICKAXE;
    if (str == "axe" || str == "hacha") return Config::TOOL_AXE;
    if (str == "shovel" || str == "pala") return Config::TOOL_SHOVEL;
    if (str == "hammer" || str == "martillo") return Config::TOOL_HAMMER;
    if (str == "flail" || str == "mangual") return Config::TOOL_FLAIL;
    if (str == "sword" || str == "espada") return Config::TOOL_SWORD;
    return Config::TOOL_COUNT;
}

static Config::ToolTier parse_tool_tier(const std::string& str) {
    if (str == "wood" || str == "madera") return Config::TIER_WOOD;
    if (str == "stone" || str == "piedra") return Config::TIER_STONE;
    if (str == "iron" || str == "hierro") return Config::TIER_IRON;
    if (str == "silver" || str == "plata") return Config::TIER_SILVER;
    if (str == "gold" || str == "oro") return Config::TIER_GOLD;
    if (str == "diamond" || str == "diamante") return Config::TIER_DIAMOND;
    return Config::TIER_WOOD;
}

static Config::BlockShape parse_block_shape(const std::string& str) {
    if (str == "terrain") return Config::SHAPE_TERRAIN;
    if (str == "cube") return Config::SHAPE_CUBE;
    if (str == "stairs") return Config::SHAPE_STAIRS;
    if (str == "fence") return Config::SHAPE_FENCE;
    if (str == "torch") return Config::SHAPE_TORCH;
    if (str == "door") return Config::SHAPE_DOOR;
    if (str == "chest") return Config::SHAPE_CHEST;
    if (str == "furnace") return Config::SHAPE_FURNACE;
    if (str == "crafting_table") return Config::SHAPE_CRAFTING_TABLE;
    if (str == "glass") return Config::SHAPE_GLASS;
    return Config::SHAPE_CUBE;
}

bool load_blocks(const std::string& blocks_dir) {
    if (!fs::exists(blocks_dir)) {
        std::cerr << "[BlockRegistry] Error: Blocks directory not found: " << blocks_dir << std::endl;
        return false;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(blocks_dir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            json j;
            file >> j;

            uint8_t id = j.value("id", (uint8_t)255);
            if (id == 255 && !j.contains("id")) continue;

            Config::BlockType bt;
            bt.name = j.value("name", "Bloque");
            bt.hardness = j.value("hardness", 1.0f);
            bt.transparent = j.value("transparent", false);
            bt.is_waving = j.value("is_waving", false);
            bt.light_emission = (uint8_t)j.value("light_emission", (int)j.value("light", 0));
            bt.light_filter = (uint8_t)j.value("light_filter", bt.transparent ? 0 : 15);
            
            std::string shape_str = j.value("shape", "terrain");
            bt.shape = parse_block_shape(shape_str);

            // Tool requirements
            if (j.contains("tool")) {
                const auto& tj = j["tool"];
                std::string ideal = tj.value("ideal", "none");
                bt.ideal_tool = (ideal == "none") ? 255 : (uint8_t)parse_tool_type(ideal);

                if (tj.contains("required_tool")) {
                    std::string req = tj["required_tool"].get<std::string>();
                    bt.require_tool = (req == "none") ? 255 : (uint8_t)parse_tool_type(req);
                } else {
                    bt.require_tool = 255;
                }

                if (tj.contains("required_tier")) {
                    if (tj["required_tier"].is_number()) {
                        bt.require_tier = tj["required_tier"].get<uint8_t>();
                    } else {
                        std::string tier_str = tj["required_tier"].get<std::string>();
                        bt.require_tier = (tier_str == "none") ? 0 : (uint8_t)parse_tool_tier(tier_str);
                    }
                } else {
                    bt.require_tier = 0;
                }
            } else {
                bt.ideal_tool = 255;
                bt.require_tool = 255;
                bt.require_tier = 0;
            }

            // Drop
            if (j.contains("drop")) {
                if (j["drop"].is_number()) {
                    bt.drop_id = j["drop"].get<uint8_t>();
                    bt.drop_is_item = false;
                } else if (j["drop"].is_object()) {
                    bt.drop_id = j["drop"].value("id", id);
                    bt.drop_is_item = j["drop"].value("is_item", false);
                }
            } else {
                bt.drop_id = id;
                bt.drop_is_item = false;
            }

            // Textures
            if (j.contains("textures")) {
                const auto& tj = j["textures"];
                if (tj.contains("default") && tj["default"].is_array() && tj["default"].size() >= 2) {
                    bt.tex_x = tj["default"][0].get<int>();
                    bt.tex_y = tj["default"][1].get<int>();
                } else if (tj.contains("all") && tj["all"].is_array() && tj["all"].size() >= 2) {
                    bt.tex_x = tj["all"][0].get<int>();
                    bt.tex_y = tj["all"][1].get<int>();
                } else {
                    bt.tex_x = 0;
                    bt.tex_y = 0;
                }

                if (tj.contains("top") && tj["top"].is_array() && tj["top"].size() >= 2) {
                    bt.tex_top_x = tj["top"][0].get<int>();
                    bt.tex_top_y = tj["top"][1].get<int>();
                }
                if (tj.contains("bottom") && tj["bottom"].is_array() && tj["bottom"].size() >= 2) {
                    bt.tex_bottom_x = tj["bottom"][0].get<int>();
                    bt.tex_bottom_y = tj["bottom"][1].get<int>();
                }
                if (tj.contains("front") && tj["front"].is_array() && tj["front"].size() >= 2) {
                    bt.tex_front_x = tj["front"][0].get<int>();
                    bt.tex_front_y = tj["front"][1].get<int>();
                }
                if (tj.contains("latch") && tj["latch"].is_array() && tj["latch"].size() >= 2) {
                    bt.tex_latch_x = tj["latch"][0].get<int>();
                    bt.tex_latch_y = tj["latch"][1].get<int>();
                }
            }

            // Inventory Icon
            if (j.contains("icon") && j["icon"].is_array() && j["icon"].size() >= 2) {
                bt.tex_icon_x = j["icon"][0].get<int>();
                bt.tex_icon_y = j["icon"][1].get<int>();
            }

            // 3D Cuboid Elements
            if (j.contains("elements") && j["elements"].is_array()) {
                for (const auto& ej : j["elements"]) {
                    Config::CuboidElement elem;
                    elem.name = ej.value("name", "box");
                    if (ej.contains("from") && ej["from"].is_array() && ej["from"].size() >= 3) {
                        elem.from = { ej["from"][0].get<float>(), ej["from"][1].get<float>(), ej["from"][2].get<float>() };
                    }
                    if (ej.contains("to") && ej["to"].is_array() && ej["to"].size() >= 3) {
                        elem.to = { ej["to"][0].get<float>(), ej["to"][1].get<float>(), ej["to"][2].get<float>() };
                    }

                    if (ej.contains("textures")) {
                        const auto& etj = ej["textures"];
                        if (etj.contains("all") && etj["all"].is_array() && etj["all"].size() >= 2) {
                            elem.tex_top_x = elem.tex_bottom_x = elem.tex_front_x = elem.tex_back_x = elem.tex_left_x = elem.tex_right_x = etj["all"][0].get<int>();
                            elem.tex_top_y = elem.tex_bottom_y = elem.tex_front_y = elem.tex_back_y = elem.tex_left_y = elem.tex_right_y = etj["all"][1].get<int>();
                        }
                        if (etj.contains("top") && etj["top"].is_array() && etj["top"].size() >= 2) {
                            elem.tex_top_x = etj["top"][0].get<int>();
                            elem.tex_top_y = etj["top"][1].get<int>();
                        }
                        if (etj.contains("bottom") && etj["bottom"].is_array() && etj["bottom"].size() >= 2) {
                            elem.tex_bottom_x = etj["bottom"][0].get<int>();
                            elem.tex_bottom_y = etj["bottom"][1].get<int>();
                        }
                        if (etj.contains("front") && etj["front"].is_array() && etj["front"].size() >= 2) {
                            elem.tex_front_x = etj["front"][0].get<int>();
                            elem.tex_front_y = etj["front"][1].get<int>();
                        }
                        if (etj.contains("sides") && etj["sides"].is_array() && etj["sides"].size() >= 2) {
                            elem.tex_front_x = elem.tex_back_x = elem.tex_left_x = elem.tex_right_x = etj["sides"][0].get<int>();
                            elem.tex_front_y = elem.tex_back_y = elem.tex_left_y = elem.tex_right_y = etj["sides"][1].get<int>();
                        }
                    }
                    bt.elements.push_back(elem);
                }
            }

            Config::BLOCKS[id] = bt;
            count++;
        } catch (const std::exception& e) {
            std::cerr << "[BlockRegistry] Error loading block JSON (" << entry.path() << "): " << e.what() << std::endl;
        }
    }

    std::cout << "[BlockRegistry] Loaded " << count << " block definitions from " << blocks_dir << std::endl;
    return true;
}

bool load_items(const std::string& items_dir) {
    if (!fs::exists(items_dir)) {
        std::cerr << "[BlockRegistry] Error: Items directory not found: " << items_dir << std::endl;
        return false;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(items_dir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            json j;
            file >> j;

            uint8_t id = j.value("id", (uint8_t)255);
            if (id == 255 && !j.contains("id")) continue;

            Config::ItemType it;
            it.name = j.value("name", "Item");
            if (j.contains("texture") && j["texture"].is_array() && j["texture"].size() >= 2) {
                it.item_tex_x = j["texture"][0].get<int>();
                it.item_tex_y = j["texture"][1].get<int>();
            } else {
                it.item_tex_x = 0;
                it.item_tex_y = 0;
            }

            Config::ITEMS[id] = it;
            count++;
        } catch (const std::exception& e) {
            std::cerr << "[BlockRegistry] Error loading item JSON (" << entry.path() << "): " << e.what() << std::endl;
        }
    }

    std::cout << "[BlockRegistry] Loaded " << count << " item definitions from " << items_dir << std::endl;
    return true;
}

bool load_tools(const std::string& tools_dir) {
    if (!fs::exists(tools_dir)) {
        std::cerr << "[BlockRegistry] Error: Tools directory not found: " << tools_dir << std::endl;
        return false;
    }

    std::vector<Config::ToolInfo> loaded_tools;
    for (const auto& entry : fs::directory_iterator(tools_dir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            json j;
            file >> j;

            Config::ToolInfo t;
            t.type = parse_tool_type(j.value("type", "pickaxe"));
            t.tier = parse_tool_tier(j.value("tier", "wood"));
            t.name = j.value("name", "Herramienta");
            t.durability = j.value("durability", 100);
            t.mining_speed = j.value("mining_speed", 1.0f);

            if (j.contains("texture") && j["texture"].is_array() && j["texture"].size() >= 2) {
                t.item_tex_x = j["texture"][0].get<int>();
                t.item_tex_y = j["texture"][1].get<int>();
            } else {
                t.item_tex_x = 0;
                t.item_tex_y = 0;
            }

            loaded_tools.push_back(t);
        } catch (const std::exception& e) {
            std::cerr << "[BlockRegistry] Error loading tool JSON (" << entry.path() << "): " << e.what() << std::endl;
        }
    }

    if (!loaded_tools.empty()) {
        Config::TOOLS = loaded_tools;
    }

    std::cout << "[BlockRegistry] Loaded " << loaded_tools.size() << " tool definitions from " << tools_dir << std::endl;
    return true;
}

bool load_recipes(const std::string& recipes_dir) {
    if (!fs::exists(recipes_dir)) {
        std::cerr << "[BlockRegistry] Error: Recipes directory not found: " << recipes_dir << std::endl;
        return false;
    }

    std::vector<Config::CraftingRecipe> loaded_recipes;
    for (const auto& entry : fs::directory_iterator(recipes_dir)) {
        if (entry.path().extension() != ".json") continue;

        try {
            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            json j;
            file >> j;

            Config::CraftingRecipe rec;
            rec.result_name = j.value("name", "Receta");

            if (j.contains("result")) {
                const auto& rj = j["result"];
                std::string type = rj.value("type", "item");
                rec.result_count = rj.value("count", 1);

                if (type == "tool") {
                    rec.result_is_tool = true;
                    rec.result_is_block = false;
                    rec.result_tool_type = parse_tool_type(rj.value("tool_type", "pickaxe"));
                    rec.result_tool_tier = parse_tool_tier(rj.value("tool_tier", "wood"));
                    rec.result_id = 0;
                } else if (type == "block") {
                    rec.result_is_tool = false;
                    rec.result_is_block = true;
                    rec.result_tool_type = Config::TOOL_COUNT;
                    rec.result_tool_tier = Config::TIER_COUNT;
                    rec.result_id = rj.value("id", (uint8_t)0);
                } else { // "item"
                    rec.result_is_tool = false;
                    rec.result_is_block = false;
                    rec.result_tool_type = Config::TOOL_COUNT;
                    rec.result_tool_tier = Config::TIER_COUNT;
                    rec.result_id = rj.value("id", (uint8_t)0);
                }
            }

            if (j.contains("ingredients") && j["ingredients"].is_array()) {
                for (const auto& ing_j : j["ingredients"]) {
                    Config::RecipeIngredient ing;
                    std::string type = ing_j.value("type", "item");
                    ing.is_item = (type == "item");
                    ing.id = ing_j.value("id", (uint8_t)0);
                    ing.count = ing_j.value("count", 1);
                    rec.ingredients.push_back(ing);
                }
            }

            loaded_recipes.push_back(rec);
        } catch (const std::exception& e) {
            std::cerr << "[BlockRegistry] Error loading recipe JSON (" << entry.path() << "): " << e.what() << std::endl;
        }
    }

    if (!loaded_recipes.empty()) {
        Config::RECIPES = loaded_recipes;
    }

    std::cout << "[BlockRegistry] Loaded " << loaded_recipes.size() << " crafting recipes from " << recipes_dir << std::endl;
    return true;
}

bool load_all(const std::string& data_dir) {
    std::cout << "[BlockRegistry] Initializing data-driven registries from " << data_dir << "..." << std::endl;
    bool b1 = load_blocks(data_dir + "/blocks");
    bool b2 = load_items(data_dir + "/items");
    bool b3 = load_tools(data_dir + "/tools");
    bool b4 = load_recipes(data_dir + "/recipes");
    return b1 && b2 && b3 && b4;
}

} // namespace BlockRegistry
