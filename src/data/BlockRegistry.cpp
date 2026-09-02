#include "data/BlockRegistry.hpp"
#include "core/json.hpp"
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
    if (str == "fluid") return Config::SHAPE_FLUID;
    return Config::SHAPE_CUBE;
}

bool load_atlas_config(const std::string& config_file) {
    if (!fs::exists(config_file)) return false;
    try {
        std::ifstream file(config_file);
        if (!file.is_open()) return false;
        json j;
        file >> j;

        if (j.contains("tiles") && j["tiles"].is_object()) {
            Config::TILES_ATLAS_COLS = j["tiles"].value("columns", 9);
            Config::TILES_ATLAS_ROWS = j["tiles"].value("rows", 10);
        }
        if (j.contains("items") && j["items"].is_object()) {
            Config::ITEMS_ATLAS_COLS = j["items"].value("columns", 7);
            Config::ITEMS_ATLAS_ROWS = j["items"].value("rows", 8);
        }

        std::cout << "[BlockRegistry] Atlas config: Tiles ("
                  << Config::TILES_ATLAS_COLS << "x" << Config::TILES_ATLAS_ROWS << "), Items ("
                  << Config::ITEMS_ATLAS_COLS << "x" << Config::ITEMS_ATLAS_ROWS << ")" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[BlockRegistry] Error reading atlas config: " << e.what() << std::endl;
        return false;
    }
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
            bt.is_foliage = j.value("is_foliage", (id == Config::LEAVES));
            bt.is_grass = j.value("is_grass", (id == Config::GRASS || id == Config::TALL_GRASS));
            if (id == Config::LEAVES) bt.is_foliage = true;
            if (id == Config::GRASS || id == Config::TALL_GRASS) bt.is_grass = true;
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

            // Textures dictionary map for this block
            std::unordered_map<std::string, std::pair<int, int>> block_textures;
            std::unordered_map<std::string, std::string> block_texture_names;

            // Textures
            if (j.contains("textures") && j["textures"].is_object()) {
                const auto& tj = j["textures"];
                for (auto& [key, val] : tj.items()) {
                    if (val.is_array() && val.size() >= 2) {
                        block_textures[key] = { val[0].get<int>(), val[1].get<int>() };
                    } else if (val.is_string()) {
                        block_texture_names[key] = val.get<std::string>();
                    }
                }

                if (block_textures.count("default")) {
                    bt.tex_x = block_textures["default"].first;
                    bt.tex_y = block_textures["default"].second;
                } else if (block_textures.count("all")) {
                    bt.tex_x = block_textures["all"].first;
                    bt.tex_y = block_textures["all"].second;
                } else {
                    bt.tex_x = 0;
                    bt.tex_y = 0;
                }
                
                if (block_texture_names.count("default")) {
                    bt.texture_mc = block_texture_names["default"];
                } else if (block_texture_names.count("all")) {
                    bt.texture_mc = block_texture_names["all"];
                }

                if (block_textures.count("top")) {
                    bt.tex_top_x = block_textures["top"].first;
                    bt.tex_top_y = block_textures["top"].second;
                }
                if (block_texture_names.count("top")) {
                    bt.texture_top_mc = block_texture_names["top"];
                }
                
                if (block_textures.count("bottom")) {
                    bt.tex_bottom_x = block_textures["bottom"].first;
                    bt.tex_bottom_y = block_textures["bottom"].second;
                }
                if (block_texture_names.count("bottom")) {
                    bt.texture_bottom_mc = block_texture_names["bottom"];
                }
                
                if (block_textures.count("front")) {
                    bt.tex_front_x = block_textures["front"].first;
                    bt.tex_front_y = block_textures["front"].second;
                }
                if (block_texture_names.count("front")) {
                    bt.texture_front_mc = block_texture_names["front"];
                }
                if (block_textures.count("latch")) {
                    bt.tex_latch_x = block_textures["latch"].first;
                    bt.tex_latch_y = block_textures["latch"].second;
                }
            }

            // Inventory Icon
            if (j.contains("icon")) {
                if (j["icon"].is_array() && j["icon"].size() >= 2) {
                    bt.tex_icon_x = j["icon"][0].get<int>();
                    bt.tex_icon_y = j["icon"][1].get<int>();
                } else if (j["icon"].is_string()) {
                    bt.texture_icon_mc = j["icon"].get<std::string>();
                }
            }

            // 3D Cuboid Elements (Solo para bloques tipo modelo)
            if (j.contains("elements") && j["elements"].is_array()) {
                for (const auto& ej : j["elements"]) {
                    Config::CuboidElement elem;
                    elem.name = ej.value("name", "box");

                    if (ej.contains("from") && ej["from"].is_array() && ej["from"].size() >= 3) {
                        float fx = ej["from"][0].get<float>();
                        float fy = ej["from"][1].get<float>();
                        float fz = ej["from"][2].get<float>();
                        // Convert -0.5..0.5 or 0..1 to 0..16 if needed
                        if (ej["to"].is_array() && ej["to"][0].get<float>() <= 1.0f) {
                            if (fx < 0.0f) { // -0.5..0.5
                                fx = (fx + 0.5f) * 16.0f;
                                fy = (fy + 0.5f) * 16.0f;
                                fz = (fz + 0.5f) * 16.0f;
                            } else { // 0..1
                                fx *= 16.0f;
                                fy *= 16.0f;
                                fz *= 16.0f;
                            }
                        }
                        elem.from = { fx, fy, fz };
                    }
                    if (ej.contains("to") && ej["to"].is_array() && ej["to"].size() >= 3) {
                        float tx = ej["to"][0].get<float>();
                        float ty = ej["to"][1].get<float>();
                        float tz = ej["to"][2].get<float>();
                        if (tx <= 1.0f) {
                            if (tx <= 0.5f && elem.from.x < 8.0f) {
                                tx = (tx + 0.5f) * 16.0f;
                                ty = (ty + 0.5f) * 16.0f;
                                tz = (tz + 0.5f) * 16.0f;
                            } else {
                                tx *= 16.0f;
                                ty *= 16.0f;
                                tz *= 16.0f;
                            }
                        }
                        elem.to = { tx, ty, tz };
                    }

                    // Parse element rotation if present
                    if (ej.contains("rotation") && ej["rotation"].is_object()) {
                        const auto& rj = ej["rotation"];
                        elem.rotation.enabled = true;
                        if (rj.contains("origin") && rj["origin"].is_array() && rj["origin"].size() >= 3) {
                            elem.rotation.origin = {
                                rj["origin"][0].get<float>(),
                                rj["origin"][1].get<float>(),
                                rj["origin"][2].get<float>()
                            };
                        }
                        if (rj.contains("axis")) {
                            std::string ax = rj["axis"].get<std::string>();
                            if (!ax.empty()) elem.rotation.axis = ax[0];
                        }
                        if (rj.contains("angle")) {
                            elem.rotation.angle = rj["angle"].get<float>();
                        }
                        if (rj.contains("rescale")) {
                            elem.rotation.rescale = rj["rescale"].get<bool>();
                        }
                    }

                    // Parse faces if present (Minecraft format)
                    if (ej.contains("faces") && ej["faces"].is_object()) {
                        const auto& fj = ej["faces"];
                        auto parse_face = [&](const std::string& face_name, Config::BlockFaceDirection dir) {
                            if (fj.contains(face_name)) {
                                const auto& face_obj = fj[face_name];
                                elem.faces[dir].enabled = true;
                                if (face_obj.contains("uv") && face_obj["uv"].is_array() && face_obj["uv"].size() >= 4) {
                                    elem.faces[dir].uv[0] = face_obj["uv"][0].get<float>();
                                    elem.faces[dir].uv[1] = face_obj["uv"][1].get<float>();
                                    elem.faces[dir].uv[2] = face_obj["uv"][2].get<float>();
                                    elem.faces[dir].uv[3] = face_obj["uv"][3].get<float>();
                                }
                                if (face_obj.contains("cullface")) {
                                    elem.faces[dir].cullface = face_obj["cullface"].get<std::string>();
                                }
                                if (face_obj.contains("tintindex")) {
                                    elem.faces[dir].tintindex = face_obj["tintindex"].get<int>();
                                }
                                if (face_obj.contains("rotation")) {
                                    elem.faces[dir].uv_rotation = face_obj["rotation"].get<int>();
                                }
                                // Texture
                                if (face_obj.contains("texture")) {
                                    if (face_obj["texture"].is_array() && face_obj["texture"].size() >= 2) {
                                        elem.faces[dir].tex_x = face_obj["texture"][0].get<int>();
                                        elem.faces[dir].tex_y = face_obj["texture"][1].get<int>();
                                    } else if (face_obj["texture"].is_string()) {
                                        std::string tname = face_obj["texture"].get<std::string>();
                                        if (!tname.empty() && tname[0] == '#') {
                                            std::string ref_name = tname.substr(1);
                                            if (block_textures.count(ref_name)) {
                                                elem.faces[dir].tex_x = block_textures[ref_name].first;
                                                elem.faces[dir].tex_y = block_textures[ref_name].second;
                                            }
                                            if (block_texture_names.count(ref_name)) {
                                                elem.faces[dir].texture_name = block_texture_names[ref_name];
                                            }
                                        } else {
                                            elem.faces[dir].texture_name = tname;
                                        }
                                    }
                                }
                                if (elem.faces[dir].tex_x < 0) {
                                    elem.faces[dir].tex_x = bt.tex_x;
                                    elem.faces[dir].tex_y = bt.tex_y;
                                }
                            }
                        };
                        parse_face("down", Config::FACE_DOWN);
                        parse_face("up", Config::FACE_UP);
                        parse_face("north", Config::FACE_NORTH);
                        parse_face("south", Config::FACE_SOUTH);
                        parse_face("west", Config::FACE_WEST);
                        parse_face("east", Config::FACE_EAST);
                    } else {
                        // Sin faces: habilitar 6 caras con textura default del bloque
                        for (int d = 0; d < 6; d++) {
                            elem.faces[d].enabled = true;
                            elem.faces[d].tex_x = bt.tex_x;
                            elem.faces[d].tex_y = bt.tex_y;
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
            if (j.contains("texture")) {
                if (j["texture"].is_array() && j["texture"].size() >= 2) {
                    it.item_tex_x = j["texture"][0].get<int>();
                    it.item_tex_y = j["texture"][1].get<int>();
                } else if (j["texture"].is_string()) {
                    it.texture_mc = j["texture"].get<std::string>();
                    it.item_tex_x = -1;
                    it.item_tex_y = -1;
                }
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

            if (j.contains("texture")) {
                if (j["texture"].is_array() && j["texture"].size() >= 2) {
                    t.item_tex_x = j["texture"][0].get<int>();
                    t.item_tex_y = j["texture"][1].get<int>();
                } else if (j["texture"].is_string()) {
                    t.texture_mc = j["texture"].get<std::string>();
                    t.item_tex_x = -1;
                    t.item_tex_y = -1;
                }
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

            bool req_table = j.value("requires_crafting_table", false);
            if (rec.result_is_tool) {
                req_table = true;
            } else if (rec.result_is_block) {
                if (rec.result_id == Config::CHEST || rec.result_id == Config::FURNACE || 
                    rec.result_id == Config::DOOR_WOOD || rec.result_id == Config::STAIRS_WOOD || 
                    rec.result_id == Config::STAIRS_STONE || rec.result_id == Config::FENCE_WOOD || 
                    rec.result_id == Config::STONE_BRICK) {
                    req_table = true;
                }
            }
            rec.requires_table = req_table;

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
    load_atlas_config(data_dir + "/atlas.json");
    bool b1 = load_blocks(data_dir + "/blocks");
    bool b2 = load_items(data_dir + "/items");
    bool b3 = load_tools(data_dir + "/tools");
    bool b4 = load_recipes(data_dir + "/recipes");
    return b1 && b2 && b3 && b4;
}

} // namespace BlockRegistry
