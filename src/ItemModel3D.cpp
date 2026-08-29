#include "ItemModel3D.hpp"
#include "rlgl.h"
#include <GL/gl.h>
#include <iostream>
#include <vector>
#include <cstdlib>

struct ExtrudeQuad {
    Vector3 v0, v1, v2, v3;
    Vector2 uv0, uv1, uv2, uv3;
    Vector3 normal;
    Color color;
};

void ItemModel3D::rebuild(const Image& img, Texture2D items_texture) {
    cleanup();
    texture_items = items_texture;
    
    if (img.data == nullptr) {
        std::cerr << "[ItemModel3D] Invalid image for 3D extrusion!" << std::endl;
        return;
    }

    int cols = Config::ITEMS_ATLAS_COLS;
    int rows = Config::ITEMS_ATLAS_ROWS;
    float thickness = 0.0625f;

    // 1. Generate 3D models for all Tools
    for (const auto& ti : Config::TOOLS) {
        int ix = ti.item_tex_x;
        int iy = Config::ITEMS_ATLAS_ROWS - 1 - ti.item_tex_y;
        Mesh m = generate_pixel_extruded_mesh(img, ix, iy, cols, rows, thickness, false);
        Model mdl = LoadModelFromMesh(m);
        mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture_items;
        tool_models[{ (int)ti.type, (int)ti.tier }] = mdl;
    }

    // 2. Generate 3D models for all Items
    for (const auto& [id, itype] : Config::ITEMS) {
        int ix = itype.item_tex_x;
        int iy = Config::ITEMS_ATLAS_ROWS - 1 - itype.item_tex_y;
        bool center = (id != Config::ITEM_STICK);
        Mesh m = generate_pixel_extruded_mesh(img, ix, iy, cols, rows, thickness, center);
        Model mdl = LoadModelFromMesh(m);
        mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture_items;
        item_models[id] = mdl;
    }

    is_initialized = true;
    std::cout << "[ItemModel3D] Generated " << (tool_models.size() + item_models.size())
              << " 3D models." << std::endl;
}

void ItemModel3D::init(Texture2D items_texture) {
    if (is_initialized) return;
    Image img = LoadImage("assets/textures/spritesheet_items.png");
    rebuild(img, items_texture);
    UnloadImage(img);
}

void ItemModel3D::reset_to_default(Texture2D items_texture) {
    Image img = LoadImage("assets/textures/spritesheet_items.png");
    rebuild(img, items_texture);
    UnloadImage(img);
}

Mesh ItemModel3D::generate_pixel_extruded_mesh(const Image& img, int cell_x, int cell_y, int cols, int rows, float thickness, bool center_pivot) {
    int cell_w = img.width / cols;
    int cell_h = img.height / rows;
    int px_start = cell_x * cell_w;
    int py_start = cell_y * cell_h;

    // Solid mask for the cell
    std::vector<std::vector<bool>> solid(cell_h, std::vector<bool>(cell_w, false));
    for (int y = 0; y < cell_h; y++) {
        for (int x = 0; x < cell_w; x++) {
            Color c = GetImageColor(img, px_start + x, py_start + y);
            solid[y][x] = (c.a > 0);
        }
    }

    float off_x = center_pivot ? -0.5f : 0.0f;
    float off_y = center_pivot ? -0.5f : 0.0f;
    float half_z = thickness * 0.5f;

    std::vector<ExtrudeQuad> quads;
    quads.reserve(1000);

    auto add_quad = [&](const ExtrudeQuad& q) {
        quads.push_back(q);
    };

    // Shading brightness constants for high-contrast 3D pop
    Color c_front = { 245, 245, 245, 255 }; // Front (+Z)
    Color c_back  = { 170, 170, 170, 255 }; // Back (-Z)
    Color c_top   = { 255, 255, 255, 255 }; // Top (+Y)
    Color c_bot   = { 130, 130, 130, 255 }; // Bottom (-Y)
    Color c_right = { 210, 210, 210, 255 }; // Right (+X)
    Color c_left  = { 180, 180, 180, 255 }; // Left (-X)

    // 1. FRONT & BACK FACES (Greedily merged horizontally per pixel row)
    for (int y = 0; y < cell_h; y++) {
        int x = 0;
        while (x < cell_w) {
            if (solid[y][x]) {
                int x_end = x + 1;
                while (x_end < cell_w && solid[y][x_end]) {
                    x_end++;
                }

                float x0 = (float)x / (float)cell_w + off_x;
                float x1 = (float)x_end / (float)cell_w + off_x;
                float y0 = 1.0f - (float)(y + 1) / (float)cell_h + off_y;
                float y1 = 1.0f - (float)y / (float)cell_h + off_y;

                float u0 = (float)(px_start + x) / (float)img.width;
                float u1 = (float)(px_start + x_end) / (float)img.width;
                float v0 = (float)(py_start + y) / (float)img.height;
                float v1 = (float)(py_start + y + 1) / (float)img.height;

                // Front Face (+Z) (CCW)
                add_quad({
                    {x0, y0,  half_z}, {x1, y0,  half_z}, {x1, y1,  half_z}, {x0, y1,  half_z},
                    {u0, v1},         {u1, v1},         {u1, v0},         {u0, v0},
                    {0.0f, 0.0f, 1.0f},
                    c_front
                });

                // Back Face (-Z) (CCW viewed from back)
                add_quad({
                    {x1, y0, -half_z}, {x0, y0, -half_z}, {x0, y1, -half_z}, {x1, y1, -half_z},
                    {u1, v1},         {u0, v1},         {u0, v0},         {u1, v0},
                    {0.0f, 0.0f, -1.0f},
                    c_back
                });

                x = x_end;
            } else {
                x++;
            }
        }
    }

    // 2. TOP EDGES (+Y) (Greedily merged per horizontal row)
    for (int y = 0; y < cell_h; y++) {
        int x = 0;
        while (x < cell_w) {
            bool is_top_edge = solid[y][x] && (y == 0 || !solid[y - 1][x]);
            if (is_top_edge) {
                int x_end = x + 1;
                while (x_end < cell_w && solid[y][x_end] && (y == 0 || !solid[y - 1][x_end])) {
                    x_end++;
                }

                float x0 = (float)x / (float)cell_w + off_x;
                float x1 = (float)x_end / (float)cell_w + off_x;
                float y1 = 1.0f - (float)y / (float)cell_h + off_y;

                float u0 = (float)(px_start + x) / (float)img.width;
                float u1 = (float)(px_start + x_end) / (float)img.width;
                float v0 = ((float)(py_start + y) + 0.5f) / (float)img.height;

                add_quad({
                    {x0, y1, -half_z}, {x0, y1,  half_z}, {x1, y1,  half_z}, {x1, y1, -half_z},
                    {u0, v0},          {u0, v0},          {u1, v0},          {u1, v0},
                    {0.0f, 1.0f, 0.0f},
                    c_top
                });

                x = x_end;
            } else {
                x++;
            }
        }
    }

    // 3. BOTTOM EDGES (-Y) (Greedily merged per horizontal row)
    for (int y = 0; y < cell_h; y++) {
        int x = 0;
        while (x < cell_w) {
            bool is_bot_edge = solid[y][x] && (y == cell_h - 1 || !solid[y + 1][x]);
            if (is_bot_edge) {
                int x_end = x + 1;
                while (x_end < cell_w && solid[y][x_end] && (y == cell_h - 1 || !solid[y + 1][x_end])) {
                    x_end++;
                }

                float x0 = (float)x / (float)cell_w + off_x;
                float x1 = (float)x_end / (float)cell_w + off_x;
                float y0 = 1.0f - (float)(y + 1) / (float)cell_h + off_y;

                float u0 = (float)(px_start + x) / (float)img.width;
                float u1 = (float)(px_start + x_end) / (float)img.width;
                float v1 = ((float)(py_start + y) + 0.5f) / (float)img.height;

                add_quad({
                    {x0, y0,  half_z}, {x0, y0, -half_z}, {x1, y0, -half_z}, {x1, y0,  half_z},
                    {u0, v1},          {u0, v1},          {u1, v1},          {u1, v1},
                    {0.0f, -1.0f, 0.0f},
                    c_bot
                });

                x = x_end;
            } else {
                x++;
            }
        }
    }

    // 4. LEFT EDGES (-X) (Greedily merged per vertical column)
    for (int x = 0; x < cell_w; x++) {
        int y = 0;
        while (y < cell_h) {
            bool is_left_edge = solid[y][x] && (x == 0 || !solid[y][x - 1]);
            if (is_left_edge) {
                int y_end = y + 1;
                while (y_end < cell_h && solid[y_end][x] && (x == 0 || !solid[y_end][x - 1])) {
                    y_end++;
                }

                float x0 = (float)x / (float)cell_w + off_x;
                float y0 = 1.0f - (float)y_end / (float)cell_h + off_y;
                float y1 = 1.0f - (float)y / (float)cell_h + off_y;

                float u0 = ((float)(px_start + x) + 0.5f) / (float)img.width;
                float v0 = (float)(py_start + y) / (float)img.height;
                float v1 = (float)(py_start + y_end) / (float)img.height;

                add_quad({
                    {x0, y0, -half_z}, {x0, y0,  half_z}, {x0, y1,  half_z}, {x0, y1, -half_z},
                    {u0, v1},          {u0, v1},          {u0, v0},          {u0, v0},
                    {-1.0f, 0.0f, 0.0f},
                    c_left
                });

                y = y_end;
            } else {
                y++;
            }
        }
    }

    // 5. RIGHT EDGES (+X) (Greedily merged per vertical column)
    for (int x = 0; x < cell_w; x++) {
        int y = 0;
        while (y < cell_h) {
            bool is_right_edge = solid[y][x] && (x == cell_w - 1 || !solid[y][x + 1]);
            if (is_right_edge) {
                int y_end = y + 1;
                while (y_end < cell_h && solid[y_end][x] && (x == cell_w - 1 || !solid[y_end][x + 1])) {
                    y_end++;
                }

                float x1 = (float)(x + 1) / (float)cell_w + off_x;
                float y0 = 1.0f - (float)y_end / (float)cell_h + off_y;
                float y1 = 1.0f - (float)y / (float)cell_h + off_y;

                float u1 = ((float)(px_start + x) + 0.5f) / (float)img.width;
                float v0 = (float)(py_start + y) / (float)img.height;
                float v1 = (float)(py_start + y_end) / (float)img.height;

                add_quad({
                    {x1, y0,  half_z}, {x1, y0, -half_z}, {x1, y1, -half_z}, {x1, y1,  half_z},
                    {u1, v1},          {u1, v1},          {u1, v0},          {u1, v0},
                    {1.0f, 0.0f, 0.0f},
                    c_right
                });

                y = y_end;
            } else {
                y++;
            }
        }
    }

    // Build Raylib Mesh
    Mesh mesh = { 0 };
    mesh.triangleCount = (int)quads.size() * 2;
    mesh.vertexCount = mesh.triangleCount * 3;

    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)RL_MALLOC(mesh.vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = (unsigned char*)RL_MALLOC(mesh.vertexCount * 4 * sizeof(unsigned char));

    int vi = 0, ti = 0, ni = 0, ci = 0;
    auto push_vert = [&](const Vector3& v, const Vector2& uv, const Vector3& n, const Color& col) {
        mesh.vertices[vi++] = v.x;
        mesh.vertices[vi++] = v.y;
        mesh.vertices[vi++] = v.z;

        mesh.texcoords[ti++] = uv.x;
        mesh.texcoords[ti++] = uv.y;

        mesh.normals[ni++] = n.x;
        mesh.normals[ni++] = n.y;
        mesh.normals[ni++] = n.z;

        mesh.colors[ci++] = col.r;
        mesh.colors[ci++] = col.g;
        mesh.colors[ci++] = col.b;
        mesh.colors[ci++] = col.a;
    };

    for (const auto& q : quads) {
        push_vert(q.v0, q.uv0, q.normal, q.color);
        push_vert(q.v1, q.uv1, q.normal, q.color);
        push_vert(q.v2, q.uv2, q.normal, q.color);

        push_vert(q.v0, q.uv0, q.normal, q.color);
        push_vert(q.v2, q.uv2, q.normal, q.color);
        push_vert(q.v3, q.uv3, q.normal, q.color);
    }

    UploadMesh(&mesh, false);
    return mesh;
}

void ItemModel3D::draw_tool(Config::ToolType type, Config::ToolTier tier, Vector3 pos, Vector3 rot, Vector3 scale, Color tint) {
    auto it = tool_models.find({ (int)type, (int)tier });
    if (it == tool_models.end() || it->second.meshCount == 0) return;
    Model mdl = it->second;

    mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = tint;

    glDisable(GL_CULL_FACE);
    rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        if (rot.x != 0.0f) rlRotatef(rot.x, 1.0f, 0.0f, 0.0f);
        if (rot.y != 0.0f) rlRotatef(rot.y, 0.0f, 1.0f, 0.0f);
        if (rot.z != 0.0f) rlRotatef(rot.z, 0.0f, 0.0f, 1.0f);
        rlScalef(scale.x, scale.y, scale.z);
        DrawModel(mdl, {0, 0, 0}, 1.0f, tint);
    rlPopMatrix();
    glEnable(GL_CULL_FACE);
}

void ItemModel3D::draw_item(uint8_t item_id, Vector3 pos, Vector3 rot, Vector3 scale, Color tint) {
    auto it = item_models.find(item_id);
    if (it == item_models.end() || it->second.meshCount == 0) return;
    Model mdl = it->second;

    mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = tint;

    glDisable(GL_CULL_FACE);
    rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        if (rot.x != 0.0f) rlRotatef(rot.x, 1.0f, 0.0f, 0.0f);
        if (rot.y != 0.0f) rlRotatef(rot.y, 0.0f, 1.0f, 0.0f);
        if (rot.z != 0.0f) rlRotatef(rot.z, 0.0f, 0.0f, 1.0f);
        rlScalef(scale.x, scale.y, scale.z);
        DrawModel(mdl, {0, 0, 0}, 1.0f, tint);
    rlPopMatrix();
    glEnable(GL_CULL_FACE);
}

void ItemModel3D::cleanup() {
    for (auto& [k, mdl] : tool_models) {
        UnloadModel(mdl);
    }
    tool_models.clear();

    for (auto& [k, mdl] : item_models) {
        UnloadModel(mdl);
    }
    item_models.clear();

    is_initialized = false;
}
