#pragma once
#include <raylib.h>
#include <string>
#include <unordered_map>
#include <vector>

class ResourcePackManager {
public:
    bool apply_pack(const std::string& pack_path);
    void build_defaults(const std::string& default_path);
    void clear_pack();
    void cleanup();

    std::pair<int, int> get_tile_coord(const std::string& mc_name) const;
    std::pair<int, int> get_item_coord(const std::string& mc_name) const;

    Texture2D get_tiles_atlas() const { return is_active ? tiles_atlas : default_tiles_atlas; }
    Texture2D get_items_atlas() const { return is_active ? items_atlas : default_items_atlas; }

    Texture2D get_sun() const { return is_active && tex_sun.id != 0 ? tex_sun : default_sun; }
    Texture2D get_moon() const { return is_active && tex_moon.id != 0 ? tex_moon : default_moon; }
    Texture2D get_clouds() const { return is_active && tex_clouds.id != 0 ? tex_clouds : default_clouds; }
    Texture2D get_sky_side() const { return is_active && tex_sky_side.id != 0 ? tex_sky_side : default_sky_side; }
    Texture2D get_sky_top() const { return is_active && tex_sky_top.id != 0 ? tex_sky_top : default_sky_top; }
    Texture2D get_sky_bottom() const { return is_active && tex_sky_bottom.id != 0 ? tex_sky_bottom : default_sky_bottom; }

    static void set_defaults(Texture2D tiles, Texture2D items);
    static void set_env_defaults(Texture2D sun, Texture2D moon, Texture2D clouds, Texture2D sky_side, Texture2D sky_top, Texture2D sky_bottom);
    
    inline bool get_is_active() const { return is_active; }
    inline std::string get_active_pack_path() const { return active_pack_path; }

private:
    Image build_block_atlas(const std::string& pack_path, int& out_cols, int& out_rows);
    Image build_item_atlas(const std::string& pack_path, int& out_cols, int& out_rows);
    void apply_block_texture_mapping();
    void apply_item_texture_mapping();

    std::unordered_map<std::string, std::pair<int, int>> tile_map;
    std::unordered_map<std::string, std::pair<int, int>> item_map;

    bool is_active = false;
    std::string active_pack_path;
    Texture2D tiles_atlas = { 0 };
    Texture2D items_atlas = { 0 };
    
    Texture2D tex_sun = { 0 };
    Texture2D tex_moon = { 0 };
    Texture2D tex_clouds = { 0 };
    Texture2D tex_sky_side = { 0 };
    Texture2D tex_sky_top = { 0 };
    Texture2D tex_sky_bottom = { 0 };

    static inline int default_tiles_cols = 0;
    static inline int default_tiles_rows = 0;
    static inline int default_items_cols = 0;
    static inline int default_items_rows = 0;

    static inline Texture2D default_tiles_atlas = { 0 };
    static inline Texture2D default_items_atlas = { 0 };
    
    static inline Texture2D default_sun = { 0 };
    static inline Texture2D default_moon = { 0 };
    static inline Texture2D default_clouds = { 0 };
    static inline Texture2D default_sky_side = { 0 };
    static inline Texture2D default_sky_top = { 0 };
    static inline Texture2D default_sky_bottom = { 0 };
};
