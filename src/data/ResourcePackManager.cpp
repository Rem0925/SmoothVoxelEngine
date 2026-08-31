#include "data/ResourcePackManager.hpp"
#include "data/BlockRegistry.hpp"
#include "core/Config.hpp"
#include "gameplay/ItemModel3D.hpp"
#include "generation/Biome.hpp"
#include "core/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;
using json = nlohmann::json;

static Texture2D image_to_texture(Image img) {
    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    return tex;
}

static std::string normalize_mc_path(std::string name) {
    if (name.rfind("minecraft:", 0) == 0) {
        name = name.substr(10);
    }
    return name;
}

std::pair<int, int> ResourcePackManager::get_tile_coord(const std::string& mc_name) const {
    std::string norm = normalize_mc_path(mc_name);
    auto it = tile_map.find(norm);
    if (it != tile_map.end()) return it->second;

    if (norm.rfind("block/", 0) != 0) {
        it = tile_map.find("block/" + norm);
        if (it != tile_map.end()) return it->second;
    } else {
        it = tile_map.find(norm.substr(6));
        if (it != tile_map.end()) return it->second;
    }

    // Aliases comunes de Minecraft
    if (norm == "block/water" || norm == "block/water_still" || norm == "block/water_flow" || norm == "block/water_overlay") {
        for (const auto& w_name : {"block/water_still", "block/water_flow", "block/water_overlay", "block/water"}) {
            it = tile_map.find(w_name);
            if (it != tile_map.end()) return it->second;
        }
    }
    if (norm == "block/short_grass" || norm == "block/grass") {
        for (const auto& g_name : {"block/short_grass", "block/grass"}) {
            it = tile_map.find(g_name);
            if (it != tile_map.end()) return it->second;
        }
    }
    if (norm == "block/oak_planks" || norm == "block/planks_oak") {
        for (const auto& p_name : {"block/oak_planks", "block/planks_oak"}) {
            it = tile_map.find(p_name);
            if (it != tile_map.end()) return it->second;
        }
    }

    return { -1, -1 };
}

std::pair<int, int> ResourcePackManager::get_item_coord(const std::string& mc_name) const {
    std::string norm = normalize_mc_path(mc_name);
    auto it = item_map.find(norm);
    if (it != item_map.end()) return it->second;

    if (norm.rfind("item/", 0) != 0) {
        it = item_map.find("item/" + norm);
        if (it != item_map.end()) return it->second;
    } else {
        it = item_map.find(norm.substr(5));
        if (it != item_map.end()) return it->second;
    }

    // Aliases comunes y fallbacks de ítems y herramientas de Minecraft
    static const std::unordered_map<std::string, std::vector<std::string>> item_fallbacks = {
        {"item/wooden_pickaxe", {"item/wood_pickaxe"}},
        {"item/wood_pickaxe", {"item/wooden_pickaxe"}},
        {"item/wooden_axe", {"item/wood_axe"}},
        {"item/wood_axe", {"item/wooden_axe"}},
        {"item/wooden_shovel", {"item/wood_shovel"}},
        {"item/wood_shovel", {"item/wooden_shovel"}},
        {"item/wooden_sword", {"item/wood_sword"}},
        {"item/wood_sword", {"item/wooden_sword"}},
        {"item/wooden_hoe", {"item/wood_hoe", "item/wooden_pickaxe", "item/wood_pickaxe"}},
        {"item/wood_hoe", {"item/wooden_hoe", "item/wooden_pickaxe"}},
        {"item/stone_hoe", {"item/stone_pickaxe"}},
        {"item/iron_hoe", {"item/iron_pickaxe"}},
        {"item/diamond_hoe", {"item/diamond_pickaxe"}},
        {"item/golden_pickaxe", {"item/gold_pickaxe"}},
        {"item/gold_pickaxe", {"item/golden_pickaxe"}},
        {"item/golden_axe", {"item/gold_axe"}},
        {"item/gold_axe", {"item/golden_axe"}},
        {"item/golden_shovel", {"item/gold_shovel"}},
        {"item/gold_shovel", {"item/golden_shovel"}},
        {"item/golden_sword", {"item/gold_sword"}},
        {"item/gold_sword", {"item/golden_sword"}},
        {"item/golden_hoe", {"item/gold_hoe", "item/golden_pickaxe", "item/gold_pickaxe"}},
        {"item/gold_hoe", {"item/golden_hoe", "item/golden_pickaxe"}},
        {"item/netherite_pickaxe", {"item/iron_pickaxe", "item/diamond_pickaxe"}},
        {"item/netherite_axe", {"item/iron_axe", "item/diamond_axe"}},
        {"item/netherite_shovel", {"item/iron_shovel", "item/diamond_shovel"}},
        {"item/netherite_sword", {"item/iron_sword", "item/diamond_sword"}},
        {"item/netherite_hoe", {"item/iron_hoe", "item/diamond_hoe", "item/iron_pickaxe"}},
        {"item/netherite_ingot", {"item/iron_ingot", "item/gold_ingot"}},
        {"item/stick", {"item/wood_stick"}}
    };

    auto fb_it = item_fallbacks.find(norm);
    if (fb_it != item_fallbacks.end()) {
        for (const auto& candidate : fb_it->second) {
            auto hit = item_map.find(candidate);
            if (hit != item_map.end()) return hit->second;
        }
    }

    return { -1, -1 };
}

Image ResourcePackManager::build_block_atlas(const std::string& pack_path, int& out_cols, int& out_rows) {
    std::string pack_block_dir = pack_path + "/assets/minecraft/textures/block";
    std::string default_block_dir = "assets/minecraft/textures/block";
    std::unordered_map<std::string, Image> tex_map;

    int tile_size = 16;

    auto scan_dir = [&](const std::string& dir, bool is_pack) {
        if (!fs::exists(dir)) return;
        for (auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() != ".png") continue;
            std::string stem = entry.path().stem().string();
            std::string name = "block/" + stem;

            if (!is_pack && tex_map.count(name)) continue;

            Image img = LoadImage(entry.path().string().c_str());
            if (img.data == nullptr) continue;

            // Si es una tira animada vertical (ej. 16x512 para agua/fuego), recortar el primer fotograma 1:1
            if (img.height > img.width && (img.height % img.width == 0)) {
                ImageCrop(&img, { 0, 0, (float)img.width, (float)img.width });
            }

            if (is_pack) {
                if (img.width > tile_size) tile_size = img.width;
                if (img.height > tile_size) tile_size = img.height;
            }

            if (tex_map.count(name)) {
                UnloadImage(tex_map[name]);
            }
            tex_map[name] = img;
        }
    };

    // 1. Cargar texturas del paquete de recursos
    scan_dir(pack_block_dir, true);
    // 2. Rellenar las texturas faltantes con las texturas base por defecto
    if (pack_path != "assets" && pack_path != "assets/minecraft" && pack_path != "assets/data") {
        scan_dir(default_block_dir, false);
    }

    if (tex_map.empty()) {
        out_cols = 0; out_rows = 0;
        return { 0 };
    }

    std::vector<std::pair<std::string, Image>> textures;
    textures.reserve(tex_map.size());
    for (auto& [name, img] : tex_map) {
        if (img.width != tile_size || img.height != tile_size) {
            ImageResizeNN(&img, tile_size, tile_size);
        }
        textures.push_back({ name, img });
    }

    std::sort(textures.begin(), textures.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    int count = (int)textures.size();
    out_cols = (int)std::ceil(std::sqrt((float)count));
    out_rows = (int)std::ceil((float)count / out_cols);

    tile_map.clear();
    Image atlas = GenImageColor(out_cols * tile_size, out_rows * tile_size, BLANK);

    for (int i = 0; i < count; ++i) {
        int col = i % out_cols;
        int row = i / out_cols;
        tile_map[textures[i].first] = { col, row };
        ImageDraw(&atlas, textures[i].second, { 0, 0, (float)tile_size, (float)tile_size },
            { (float)(col * tile_size), (float)((out_rows - 1 - row) * tile_size), (float)tile_size, (float)tile_size }, WHITE);
        UnloadImage(textures[i].second);
    }

    std::cout << "[ResourcePack] Block atlas: " << count << " textures (" << tile_size << "x" << tile_size << ") -> "
              << out_cols << "x" << out_rows << " grid (" << atlas.width << "x" << atlas.height << ")" << std::endl;
    return atlas;
}

Image ResourcePackManager::build_item_atlas(const std::string& pack_path, int& out_cols, int& out_rows) {
    std::string pack_item_dir = pack_path + "/assets/minecraft/textures/item";
    std::string default_item_dir = "assets/minecraft/textures/item";
    std::unordered_map<std::string, Image> tex_map;

    int tile_size = 16;

    auto scan_dir = [&](const std::string& dir, bool is_pack) {
        if (!fs::exists(dir)) return;
        for (auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() != ".png") continue;
            std::string stem = entry.path().stem().string();
            std::string name = "item/" + stem;

            if (!is_pack && tex_map.count(name)) continue;

            Image img = LoadImage(entry.path().string().c_str());
            if (img.data == nullptr) continue;

            // Si es una tira animada vertical (ej. brújula/reloj 16x512), recortar el primer fotograma 1:1
            if (img.height > img.width && (img.height % img.width == 0)) {
                ImageCrop(&img, { 0, 0, (float)img.width, (float)img.width });
            }

            if (is_pack) {
                if (img.width > tile_size) tile_size = img.width;
                if (img.height > tile_size) tile_size = img.height;
            }

            if (tex_map.count(name)) {
                UnloadImage(tex_map[name]);
            }
            tex_map[name] = img;
        }
    };

    scan_dir(pack_item_dir, true);
    if (pack_path != "assets" && pack_path != "assets/minecraft" && pack_path != "assets/data") {
        scan_dir(default_item_dir, false);
    }

    if (tex_map.empty()) {
        out_cols = 0; out_rows = 0;
        return { 0 };
    }

    std::vector<std::pair<std::string, Image>> textures;
    textures.reserve(tex_map.size());
    for (auto& [name, img] : tex_map) {
        if (img.width != tile_size || img.height != tile_size) {
            ImageResizeNN(&img, tile_size, tile_size);
        }
        textures.push_back({ name, img });
    }

    std::sort(textures.begin(), textures.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    int count = (int)textures.size();
    out_cols = (int)std::ceil(std::sqrt((float)count));
    out_rows = (int)std::ceil((float)count / out_cols);

    item_map.clear();
    Image atlas = GenImageColor(out_cols * tile_size, out_rows * tile_size, BLANK);

    for (int i = 0; i < count; ++i) {
        int col = i % out_cols;
        int row = i / out_cols;
        item_map[textures[i].first] = { col, row };
        ImageDraw(&atlas, textures[i].second, { 0, 0, (float)tile_size, (float)tile_size },
            { (float)(col * tile_size), (float)(row * tile_size), (float)tile_size, (float)tile_size }, WHITE);
        UnloadImage(textures[i].second);
    }

    std::cout << "[ResourcePack] Item atlas: " << count << " textures (" << tile_size << "x" << tile_size << ") -> "
              << out_cols << "x" << out_rows << " grid (" << atlas.width << "x" << atlas.height << ")" << std::endl;
    return atlas;
}

static std::pair<int, int> resolve_tex(const ResourcePackManager& rpm, const std::string& primary,
                                        const std::vector<std::string>& fallbacks = {}) {
    if (!primary.empty()) {
        auto c = rpm.get_tile_coord(primary);
        if (c.first >= 0) return c;
    }
    for (auto& fb : fallbacks) {
        auto c = rpm.get_tile_coord(fb);
        if (c.first >= 0) return c;
    }
    auto dirt = rpm.get_tile_coord("block/dirt");
    if (dirt.first >= 0) return dirt;
    return { 0, 0 };
}

void ResourcePackManager::apply_block_texture_mapping() {
    for (auto& [id, bt] : Config::BLOCKS) {
        if (!bt.texture_mc.empty()) {
            auto def = resolve_tex(*this, bt.texture_mc);
            bt.tex_x = def.first;
            bt.tex_y = def.second;
        }
        
        if (!bt.texture_top_mc.empty()) {
            auto top = resolve_tex(*this, bt.texture_top_mc);
            bt.tex_top_x = top.first;
            bt.tex_top_y = top.second;
        }

        if (!bt.texture_bottom_mc.empty()) {
            auto bot = resolve_tex(*this, bt.texture_bottom_mc);
            bt.tex_bottom_x = bot.first;
            bt.tex_bottom_y = bot.second;
        }

        if (!bt.texture_front_mc.empty()) {
            auto front = resolve_tex(*this, bt.texture_front_mc);
            bt.tex_front_x = front.first;
            bt.tex_front_y = front.second;
        }

        if (!bt.texture_icon_mc.empty()) {
            auto icon = resolve_tex(*this, bt.texture_icon_mc);
            bt.tex_icon_x = icon.first;
            bt.tex_icon_y = icon.second;
        }

        for (auto& elem : bt.elements) {
            for (int f = 0; f < 6; ++f) {
                auto& face = elem.faces[f];
                if (!face.texture_name.empty()) {
                    auto fc = resolve_tex(*this, face.texture_name);
                    face.tex_x = fc.first;
                    face.tex_y = fc.second;
                } else if (face.tex_x >= 0) {
                    // Fallback to top/bottom/front/default logic based on face direction if string isn't specified
                    std::string face_mc = bt.texture_mc;
                    if (f == Config::FACE_UP && !bt.texture_top_mc.empty()) face_mc = bt.texture_top_mc;
                    else if (f == Config::FACE_DOWN && !bt.texture_bottom_mc.empty()) face_mc = bt.texture_bottom_mc;
                    else if (f == Config::FACE_SOUTH && !bt.texture_front_mc.empty()) face_mc = bt.texture_front_mc;
                    
                    auto fc = resolve_tex(*this, face_mc);
                    face.tex_x = fc.first;
                    face.tex_y = fc.second;
                }
            }
        }
    }
}

static std::pair<int, int> resolve_tex_item(const ResourcePackManager& rpm, const std::string& primary,
                                             const std::vector<std::string>& fallbacks = {}) {
    if (!primary.empty()) {
        auto c = rpm.get_item_coord(primary);
        if (c.first >= 0) return c;
    }
    for (auto& fb : fallbacks) {
        auto c = rpm.get_item_coord(fb);
        if (c.first >= 0) return c;
    }
    return { -1, -1 };
}

void ResourcePackManager::apply_item_texture_mapping() {
    for (auto& [id, it] : Config::ITEMS) {
        if (!it.texture_mc.empty()) {
            auto c = resolve_tex_item(*this, it.texture_mc);
            if (c.first >= 0) {
                it.item_tex_x = c.first;
                it.item_tex_y = c.second;
            }
        }
    }

    for (auto& t : Config::TOOLS) {
        if (!t.texture_mc.empty()) {
            auto c = resolve_tex_item(*this, t.texture_mc);
            if (c.first >= 0) {
                t.item_tex_x = c.first;
                t.item_tex_y = c.second;
            }
        }
    }
}

void ResourcePackManager::load_block_models_from_pack(const std::string& pack_path) {
    std::string models_dir = pack_path + "/assets/minecraft/models/block";
    if (!fs::exists(models_dir)) return;

    // Solo bloques de construcción, decorativos o mobiliario (NUNCA bloques de terreno liso / marching cubes)
    static const std::unordered_map<std::string, uint8_t> mc_block_to_id = {
        {"torch", Config::TORCH},
        {"oak_planks", Config::PLANKS_CUBE},
        {"stone_bricks", Config::STONE_BRICK},
        {"oak_stairs", Config::STAIRS_WOOD},
        {"cobblestone_stairs", Config::STAIRS_STONE},
        {"stone_stairs", Config::STAIRS_STONE},
        {"oak_fence", Config::FENCE_WOOD},
        {"oak_door_bottom", Config::DOOR_WOOD},
        {"oak_door", Config::DOOR_WOOD},
        {"chest", Config::CHEST},
        {"furnace", Config::FURNACE},
        {"crafting_table", Config::CRAFTING_TABLE},
        {"glass", Config::GLASS}
    };

    for (const auto& entry : fs::directory_iterator(models_dir)) {
        if (entry.path().extension() != ".json") continue;
        std::string stem = entry.path().stem().string();
        auto it = mc_block_to_id.find(stem);
        if (it == mc_block_to_id.end()) continue;

        uint8_t bid = it->second;
        if (Config::BLOCKS.find(bid) == Config::BLOCKS.end()) continue;
        // Los bloques suaves de terreno nunca aceptan elementos de cubos
        if (Config::BLOCKS[bid].shape == Config::SHAPE_TERRAIN) continue;

        try {
            std::ifstream f(entry.path());
            if (!f.is_open()) continue;
            json j;
            f >> j;

            std::unordered_map<std::string, std::string> model_tex_names;
            if (j.contains("textures") && j["textures"].is_object()) {
                for (auto& [k, v] : j["textures"].items()) {
                    if (v.is_string()) model_tex_names[k] = v.get<std::string>();
                }
            }

            if (j.contains("elements") && j["elements"].is_array() && !j["elements"].empty()) {
                std::vector<Config::CuboidElement> new_elements;
                for (const auto& ej : j["elements"]) {
                    Config::CuboidElement elem;
                    elem.name = ej.value("name", "box");
                    if (ej.contains("from") && ej["from"].is_array() && ej["from"].size() >= 3) {
                        elem.from = { ej["from"][0].get<float>(), ej["from"][1].get<float>(), ej["from"][2].get<float>() };
                    }
                    if (ej.contains("to") && ej["to"].is_array() && ej["to"].size() >= 3) {
                        elem.to = { ej["to"][0].get<float>(), ej["to"][1].get<float>(), ej["to"][2].get<float>() };
                    }
                    if (ej.contains("rotation") && ej["rotation"].is_object()) {
                        const auto& rj = ej["rotation"];
                        elem.rotation.enabled = true;
                        if (rj.contains("origin") && rj["origin"].is_array() && rj["origin"].size() >= 3) {
                            elem.rotation.origin = { rj["origin"][0].get<float>(), rj["origin"][1].get<float>(), rj["origin"][2].get<float>() };
                        }
                        if (rj.contains("axis")) {
                            std::string ax = rj["axis"].get<std::string>();
                            if (!ax.empty()) elem.rotation.axis = ax[0];
                        }
                        if (rj.contains("angle")) elem.rotation.angle = rj["angle"].get<float>();
                        if (rj.contains("rescale")) elem.rotation.rescale = rj["rescale"].get<bool>();
                    }

                    if (ej.contains("faces") && ej["faces"].is_object()) {
                        const auto& fj = ej["faces"];
                        auto parse_f = [&](const std::string& fname, Config::BlockFaceDirection dir) {
                            if (fj.contains(fname)) {
                                const auto& fo = fj[fname];
                                elem.faces[dir].enabled = true;
                                if (fo.contains("uv") && fo["uv"].is_array() && fo["uv"].size() >= 4) {
                                    elem.faces[dir].uv[0] = fo["uv"][0].get<float>();
                                    elem.faces[dir].uv[1] = fo["uv"][1].get<float>();
                                    elem.faces[dir].uv[2] = fo["uv"][2].get<float>();
                                    elem.faces[dir].uv[3] = fo["uv"][3].get<float>();
                                }
                                if (fo.contains("cullface")) elem.faces[dir].cullface = fo["cullface"].get<std::string>();
                                if (fo.contains("tintindex")) elem.faces[dir].tintindex = fo["tintindex"].get<int>();
                                if (fo.contains("rotation")) elem.faces[dir].uv_rotation = fo["rotation"].get<int>();
                                if (fo.contains("texture") && fo["texture"].is_string()) {
                                    std::string tname = fo["texture"].get<std::string>();
                                    if (!tname.empty() && tname[0] == '#') {
                                        std::string ref = tname.substr(1);
                                        if (model_tex_names.count(ref)) tname = model_tex_names[ref];
                                    }
                                    elem.faces[dir].texture_name = tname;
                                    auto c = resolve_tex(*this, tname);
                                    elem.faces[dir].tex_x = c.first;
                                    elem.faces[dir].tex_y = c.second;
                                }
                            }
                        };
                        parse_f("down", Config::FACE_DOWN);
                        parse_f("up", Config::FACE_UP);
                        parse_f("north", Config::FACE_NORTH);
                        parse_f("south", Config::FACE_SOUTH);
                        parse_f("west", Config::FACE_WEST);
                        parse_f("east", Config::FACE_EAST);
                    }
                    new_elements.push_back(elem);
                }
                if (!new_elements.empty()) {
                    Config::BLOCKS[bid].elements = new_elements;
                }
            }
        } catch (...) {}
    }
}

bool ResourcePackManager::apply_pack(const std::string& pack_path) {
    clear_pack();

    int tile_cols, tile_rows;
    Image tile_img = build_block_atlas(pack_path, tile_cols, tile_rows);
    if (tile_cols == 0) {
        std::cerr << "[ResourcePack] No block textures found in " << pack_path << std::endl;
        return false;
    }

    int item_cols, item_rows;
    Image item_img = build_item_atlas(pack_path, item_cols, item_rows);

    tiles_atlas = image_to_texture(tile_img);
    SetTextureFilter(tiles_atlas, TEXTURE_FILTER_POINT);
    Config::TILES_ATLAS_COLS = tile_cols;
    Config::TILES_ATLAS_ROWS = tile_rows;

    // Cargar colormaps (grass.png / foliage.png) si existen en el pack
    Biome::load_colormaps(pack_path);

    // Cargar y mapear texturas y modelos 3D del resource pack
    apply_block_texture_mapping();
    load_block_models_from_pack(pack_path);

    if (item_cols > 0 && item_img.data != nullptr) {
        items_atlas = LoadTextureFromImage(item_img);
        SetTextureFilter(items_atlas, TEXTURE_FILTER_POINT);
        Config::ITEMS_ATLAS_COLS = item_cols;
        Config::ITEMS_ATLAS_ROWS = item_rows;
        
        apply_item_texture_mapping();
        ItemModel3D::get().rebuild(item_img, items_atlas, pack_path);
        UnloadImage(item_img);
    } else {
        items_atlas = default_items_atlas;
        Config::ITEMS_ATLAS_COLS = 7;
        Config::ITEMS_ATLAS_ROWS = 8;
        apply_item_texture_mapping();
    }

    auto try_load_env = [&](const std::string& name, Texture2D& tex, Texture2D def) {
        std::string path = pack_path + "/assets/minecraft/textures/environment/" + name;
        if (std::filesystem::exists(path)) {
            tex = LoadTexture(path.c_str());
            SetTextureFilter(tex, TEXTURE_FILTER_POINT);
            if (name == "clouds.png") SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
            else SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
        } else {
            tex = { 0 };
        }
    };
    
    try_load_env("sun.png", tex_sun, default_sun);
    try_load_env("moon.png", tex_moon, default_moon);
    try_load_env("clouds.png", tex_clouds, default_clouds);
    try_load_env("skybox_sideClouds.png", tex_sky_side, default_sky_side);
    try_load_env("skybox_top.png", tex_sky_top, default_sky_top);
    try_load_env("skybox_bottom.png", tex_sky_bottom, default_sky_bottom);

    active_pack_path = pack_path;
    is_active = true;
    Config::USING_RESOURCE_PACK = true;

    std::cout << "[ResourcePack] Applied: " << pack_path << std::endl;
    return true;
}

void ResourcePackManager::build_defaults(const std::string& default_path) {
    int tile_cols, tile_rows;
    Image tile_img = build_block_atlas(default_path, tile_cols, tile_rows);
    if (tile_cols > 0) {
        default_tiles_atlas = image_to_texture(tile_img);
        SetTextureFilter(default_tiles_atlas, TEXTURE_FILTER_POINT);
        Config::TILES_ATLAS_COLS = tile_cols;
        Config::TILES_ATLAS_ROWS = tile_rows;
        default_tiles_cols = tile_cols;
        default_tiles_rows = tile_rows;
    }
    
    int item_cols, item_rows;
    Image item_img = build_item_atlas(default_path, item_cols, item_rows);
    if (item_cols > 0 && item_img.data != nullptr) {
        default_items_atlas = LoadTextureFromImage(item_img);
        SetTextureFilter(default_items_atlas, TEXTURE_FILTER_POINT);
        Config::ITEMS_ATLAS_COLS = item_cols;
        Config::ITEMS_ATLAS_ROWS = item_rows;
        default_items_cols = item_cols;
        default_items_rows = item_rows;
    }
    
    default_tile_map = tile_map;
    default_item_map = item_map;

    Biome::load_colormaps(default_path);

    apply_block_texture_mapping();
    apply_item_texture_mapping();

    if (item_cols > 0 && item_img.data != nullptr) {
        default_item_image = item_img;
        ItemModel3D::get().rebuild(item_img, default_items_atlas, default_path);
    } else {
        default_item_image = { 0 };
    }
    
    auto try_load_env = [&](const std::string& name, Texture2D& tex) {
        std::string path = default_path + "/assets/minecraft/textures/environment/" + name;
        if (std::filesystem::exists(path)) {
            tex = LoadTexture(path.c_str());
            SetTextureFilter(tex, TEXTURE_FILTER_POINT);
            if (name == "clouds.png") SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
            else SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
        } else {
            tex = { 0 };
        }
    };
    
    try_load_env("sun.png", default_sun);
    try_load_env("moon.png", default_moon);
    try_load_env("clouds.png", default_clouds);
    try_load_env("skybox_sideClouds.png", default_sky_side);
    try_load_env("skybox_top.png", default_sky_top);
    try_load_env("skybox_bottom.png", default_sky_bottom);
}

void ResourcePackManager::clear_pack() {
    if (is_active) {
        UnloadTexture(tiles_atlas);
        if (items_atlas.id != default_items_atlas.id && items_atlas.id != 0) {
            UnloadTexture(items_atlas);
        }
        tiles_atlas = { 0 };
        items_atlas = { 0 };
        
        if (tex_sun.id != 0) UnloadTexture(tex_sun);
        if (tex_moon.id != 0) UnloadTexture(tex_moon);
        if (tex_clouds.id != 0) UnloadTexture(tex_clouds);
        if (tex_sky_side.id != 0) UnloadTexture(tex_sky_side);
        if (tex_sky_top.id != 0) UnloadTexture(tex_sky_top);
        if (tex_sky_bottom.id != 0) UnloadTexture(tex_sky_bottom);
        
        tex_sun = { 0 };
        tex_moon = { 0 };
        tex_clouds = { 0 };
        tex_sky_side = { 0 };
        tex_sky_top = { 0 };
        tex_sky_bottom = { 0 };
    }
    is_active = false;
    active_pack_path.clear();
    tile_map = default_tile_map;
    item_map = default_item_map;
    Config::TILES_ATLAS_COLS = default_tiles_cols;
    Config::TILES_ATLAS_ROWS = default_tiles_rows;
    Config::ITEMS_ATLAS_COLS = default_items_cols;
    Config::ITEMS_ATLAS_ROWS = default_items_rows;
    Config::USING_RESOURCE_PACK = false;

    Biome::unload_colormaps();
    BlockRegistry::load_all("assets/data");
    apply_block_texture_mapping();
    apply_item_texture_mapping();

    if (default_item_image.data != nullptr) {
        ItemModel3D::get().rebuild(default_item_image, default_items_atlas);
    }
}

void ResourcePackManager::cleanup() {
    clear_pack();
    if (default_item_image.data != nullptr) {
        UnloadImage(default_item_image);
        default_item_image = { 0 };
    }
}
