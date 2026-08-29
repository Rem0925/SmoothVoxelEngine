#include "ResourcePackManager.hpp"
#include "Config.hpp"
#include "ItemModel3D.hpp"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

static Texture2D image_to_texture(Image img) {
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

static int next_pow2(int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

void ResourcePackManager::set_defaults(Texture2D tiles, Texture2D items) {
    default_tiles_atlas = tiles;
    default_items_atlas = items;
}

std::pair<int, int> ResourcePackManager::get_tile_coord(const std::string& mc_name) const {
    auto it = tile_map.find(mc_name);
    if (it != tile_map.end()) return it->second;
    return { -1, -1 };
}

std::pair<int, int> ResourcePackManager::get_item_coord(const std::string& mc_name) const {
    auto it = item_map.find(mc_name);
    if (it != item_map.end()) return it->second;
    return { -1, -1 };
}

Image ResourcePackManager::build_block_atlas(const std::string& pack_path, int& out_cols, int& out_rows) {
    std::string block_dir = pack_path + "/assets/minecraft/textures/block";
    std::vector<std::pair<std::string, Image>> textures;

    if (!fs::exists(block_dir)) {
        out_cols = 0; out_rows = 0;
        return { 0 };
    }

    for (auto& entry : fs::directory_iterator(block_dir)) {
        if (entry.path().extension() != ".png") continue;
        std::string name = "block/" + entry.path().stem().string();
        Image img = LoadImage(entry.path().string().c_str());
        if (img.data == nullptr) continue;
        if (img.width != 16 || img.height != 16) {
            ImageResize(&img, 16, 16);
        }
        textures.push_back({ name, img });
    }

    std::sort(textures.begin(), textures.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    int count = (int)textures.size();
    if (count == 0) { out_cols = 0; out_rows = 0; return { 0 }; }

    out_cols = (int)std::ceil(std::sqrt((float)count));
    out_rows = (int)std::ceil((float)count / out_cols);

    tile_map.clear();
    Image atlas = GenImageColor(out_cols * 16, out_rows * 16, BLANK);

    for (int i = 0; i < count; ++i) {
        int col = i % out_cols;
        int row = i / out_cols;
        tile_map[textures[i].first] = { col, row };
        ImageDraw(&atlas, textures[i].second, { 0, 0, 16, 16 },
            { (float)(col * 16), (float)((out_rows - 1 - row) * 16), 16, 16 }, WHITE);
        UnloadImage(textures[i].second);
    }

    std::cout << "[ResourcePack] Block atlas: " << count << " textures -> "
              << out_cols << "x" << out_rows << " grid" << std::endl;
    return atlas;
}

Image ResourcePackManager::build_item_atlas(const std::string& pack_path, int& out_cols, int& out_rows) {
    std::string item_dir = pack_path + "/assets/minecraft/textures/item";
    std::vector<std::pair<std::string, Image>> textures;

    if (!fs::exists(item_dir)) {
        out_cols = 0; out_rows = 0;
        return { 0 };
    }

    for (auto& entry : fs::directory_iterator(item_dir)) {
        if (entry.path().extension() != ".png") continue;
        std::string name = "item/" + entry.path().stem().string();
        Image img = LoadImage(entry.path().string().c_str());
        if (img.data == nullptr) continue;
        if (img.width != 16 || img.height != 16) {
            ImageResize(&img, 16, 16);
        }
        textures.push_back({ name, img });
    }

    std::sort(textures.begin(), textures.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    int count = (int)textures.size();
    if (count == 0) { out_cols = 0; out_rows = 0; return { 0 }; }

    out_cols = (int)std::ceil(std::sqrt((float)count));
    out_rows = (int)std::ceil((float)count / out_cols);

    item_map.clear();
    Image atlas = GenImageColor(out_cols * 16, out_rows * 16, BLANK);

    for (int i = 0; i < count; ++i) {
        int col = i % out_cols;
        int row = i / out_cols;
        item_map[textures[i].first] = { col, row };
        ImageDraw(&atlas, textures[i].second, { 0, 0, 16, 16 },
            { (float)(col * 16), (float)((out_rows - 1 - row) * 16), 16, 16 }, WHITE);
        UnloadImage(textures[i].second);
    }

    std::cout << "[ResourcePack] Item atlas: " << count << " textures -> "
              << out_cols << "x" << out_rows << " grid" << std::endl;
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
    struct ItemTexMapping {
        uint8_t id;
        std::string mc_name;
        std::vector<std::string> fallbacks;
    };

    std::vector<ItemTexMapping> item_mappings = {
        { 0, "item/stick",              {} },
        { 1, "item/coal",               {} },
        { 2, "item/oak_planks",         { "item/planks" } },
        { 3, "item/raw_copper",         {} },
        { 4, "item/raw_iron",           {} },
        { 5, "item/raw_gold",           {} },
        { 6, "item/gold_ingot",         {} },
        { 7, "item/diamond",            {} },
    };

    for (auto& m : item_mappings) {
        if (!Config::ITEMS.count(m.id)) continue;
        auto c = resolve_tex_item(*this, m.mc_name, m.fallbacks);
        if (c.first >= 0) {
            auto& it = Config::ITEMS.at(m.id);
            it.item_tex_x = c.first;
            it.item_tex_y = c.second;
        }
    }

    const char* tier_prefix[] = { "wooden", "stone", "iron", "copper", "golden", "diamond" };
    const char* tool_suffix[] = { "pickaxe", "axe", "shovel", "hammer", "sword" };

    for (auto& t : Config::TOOLS) {
        int tool_idx = (int)t.type;
        int tier_idx = (int)t.tier;

        if (tool_idx >= 5 || tier_idx >= 6) continue;

        std::string mc_name = "item/" + std::string(tier_prefix[tier_idx]) + "_" + tool_suffix[tool_idx];
        auto c = get_item_coord(mc_name);
        if (c.first >= 0) {
            t.item_tex_x = c.first;
            t.item_tex_y = c.second;
        }
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

    if (item_cols > 0 && item_img.data != nullptr) {
        items_atlas = LoadTextureFromImage(item_img);
        SetTextureFilter(items_atlas, TEXTURE_FILTER_POINT);
        Config::ITEMS_ATLAS_COLS = item_cols;
        Config::ITEMS_ATLAS_ROWS = item_rows;
        
        apply_block_texture_mapping();
        apply_item_texture_mapping(); // UPDATE COORDS FIRST!
        ItemModel3D::get().rebuild(item_img, items_atlas); // THEN REBUILD!
        UnloadImage(item_img);
    } else {
        items_atlas = default_items_atlas;
        Config::ITEMS_ATLAS_COLS = 7;
        Config::ITEMS_ATLAS_ROWS = 8;
        apply_block_texture_mapping();
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

void ResourcePackManager::set_env_defaults(Texture2D sun, Texture2D moon, Texture2D clouds, Texture2D sky_side, Texture2D sky_top, Texture2D sky_bottom) {
    default_sun = sun;
    default_moon = moon;
    default_clouds = clouds;
    default_sky_side = sky_side;
    default_sky_top = sky_top;
    default_sky_bottom = sky_bottom;
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
        ItemModel3D::get().rebuild(item_img, default_items_atlas);
        UnloadImage(item_img);
    }
    
    apply_block_texture_mapping();
    apply_item_texture_mapping();
    
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
        if (items_atlas.id != default_items_atlas.id) {
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
    tile_map.clear();
    item_map.clear();
    Config::TILES_ATLAS_COLS = default_tiles_cols;
    Config::TILES_ATLAS_ROWS = default_tiles_rows;
    Config::ITEMS_ATLAS_COLS = default_items_cols;
    Config::ITEMS_ATLAS_ROWS = default_items_rows;
    Config::USING_RESOURCE_PACK = false;
}

void ResourcePackManager::cleanup() {
    clear_pack();
}
