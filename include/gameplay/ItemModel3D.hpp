#pragma once
#include "raylib.h"
#include "core/Config.hpp"
#include <map>
#include <utility>

class ItemModel3D {
public:
    static ItemModel3D& get() {
        static ItemModel3D instance;
        return instance;
    }

    void rebuild(const Image& img, Texture2D items_texture, const std::string& pack_path = "");
    void cleanup();

    // Direct draw helpers
    void draw_tool(Config::ToolType type, Config::ToolTier tier, Vector3 pos, Vector3 rot, Vector3 scale, Color tint);
    void draw_item(uint8_t item_id, Vector3 pos, Vector3 rot, Vector3 scale, Color tint);

private:
    ItemModel3D() = default;
    ~ItemModel3D() = default;

    Texture2D texture_items = { 0 };
    bool is_initialized = false;

    std::map<std::pair<int, int>, Model> tool_models;
    std::map<uint8_t, Model> item_models;

    Mesh generate_pixel_extruded_mesh(const Image& img, int cell_x, int cell_y, int cols, int rows, float thickness, bool center_pivot);
    Mesh generate_elements_mesh(const std::vector<Config::CuboidElement>& elements, int cell_x, int cell_y, int cols, int rows, bool center_pivot);
};
