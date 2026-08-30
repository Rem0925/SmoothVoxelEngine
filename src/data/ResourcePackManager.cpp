#include "data/ResourcePackManager.hpp"
#include "core/Config.hpp"
#include "gameplay/ItemModel3D.hpp"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

static Texture2D image_to_texture(Image img) {
    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    return tex;
}

static int next_pow2(int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
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

    int tile_size = 16;
    for (auto& entry : fs::directory_iterator(block_dir)) {
        if (entry.path().extension() != ".png") continue;
        std::string name = "block/" + entry.path().stem().string();
        Image img = LoadImage(entry.path().string().c_str());
        if (img.data == nullptr) continue;
        if (img.width > tile_size) tile_size = img.width;
        if (img.height > tile_size) tile_size = img.height;
        textures.push_back({ name, img });
    }

    int count = (int)textures.size();
    if (count == 0) { out_cols = 0; out_rows = 0; return { 0 }; }

    for (auto& t : textures) {
        if (t.second.width != tile_size || t.second.height != tile_size) {
            ImageResizeNN(&t.second, tile_size, tile_size);
        }
    }

    std::sort(textures.begin(), textures.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

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
    std::string item_dir = pack_path + "/assets/minecraft/textures/item";
    std::vector<std::pair<std::string, Image>> textures;

    if (!fs::exists(item_dir)) {
        out_cols = 0; out_rows = 0;
        return { 0 };
    }

    int tile_size = 16;
    for (auto& entry : fs::directory_iterator(item_dir)) {
        if (entry.path().extension() != ".png") continue;
        std::string name = "item/" + entry.path().stem().string();
        Image img = LoadImage(entry.path().string().c_str());
        if (img.data == nullptr) continue;
        if (img.width > tile_size) tile_size = img.width;
        if (img.height > tile_size) tile_size = img.height;
        textures.push_back({ name, img });
    }

    int count = (int)textures.size();
    if (count == 0) { out_cols = 0; out_rows = 0; return { 0 }; }

    for (auto& t : textures) {
        if (t.second.width != tile_size || t.second.height != tile_size) {
            ImageResizeNN(&t.second, tile_size, tile_size);
        }
    }

    std::sort(textures.begin(), textures.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

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

    apply_block_texture_mapping();
    apply_item_texture_mapping();

    if (item_cols > 0 && item_img.data != nullptr) {
        default_item_image = item_img;
        ItemModel3D::get().rebuild(item_img, default_items_atlas);
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
