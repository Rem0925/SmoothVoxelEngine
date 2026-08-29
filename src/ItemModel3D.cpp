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
        int iy = ti.item_tex_y;
        Mesh m = generate_pixel_extruded_mesh(img, ix, iy, cols, rows, thickness, false);
        Model mdl = LoadModelFromMesh(m);
        mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture_items;
        tool_models[{ (int)ti.type, (int)ti.tier }] = mdl;
    }

    // 2. Generate 3D models for all Items
    for (const auto& [id, itype] : Config::ITEMS) {
        int ix = itype.item_tex_x;
        int iy = itype.item_tex_y;
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

Mesh ItemModel3D::generate_pixel_extruded_mesh(const Image& img, int cell_x, int cell_y, int cols, int rows, float thickness, bool center_pivot) {
    int cell_w = img.width / cols;
    int cell_h = img.height / rows;
    int px_start = cell_x * cell_w;
    int py_start = cell_y * cell_h;

    int voxel_res = cell_w;
    float step_x = (float)cell_w / voxel_res;
    float step_y = (float)cell_h / voxel_res;

    // Solid mask for the voxel grid
    std::vector<std::vector<bool>> solid(voxel_res, std::vector<bool>(voxel_res, false));
    for (int y = 0; y < voxel_res; y++) {
        for (int x = 0; x < voxel_res; x++) {
            int px = px_start + (int)((x + 0.5f) * step_x);
            int py = py_start + (int)((y + 0.5f) * step_y);
            Color c = GetImageColor(img, px, py);
            solid[y][x] = (c.a > 200);
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

    Color c_front = { 245, 245, 245, 255 }; // Front (+Z)
    Color c_back  = { 170, 170, 170, 255 }; // Back (-Z)
    Color c_top   = { 255, 255, 255, 255 }; // Top (+Y)
    Color c_bot   = { 130, 130, 130, 255 }; // Bottom (-Y)
    Color c_right = { 210, 210, 210, 255 }; // Right (+X)
    Color c_left  = { 180, 180, 180, 255 }; // Left (-X)

    // 1. FRONT & BACK FACES (Greedily merged horizontally)
    for (int y = 0; y < voxel_res; y++) {
        int x = 0;
        while (x < voxel_res) {
            if (solid[y][x]) {
                int x_end = x + 1;
                while (x_end < voxel_res && solid[y][x_end]) {
                    x_end++;
                }

                float x0 = (float)x / (float)voxel_res + off_x;
                float x1 = (float)x_end / (float)voxel_res + off_x;
                float y0 = 1.0f - (float)(y + 1) / (float)voxel_res + off_y;
                float y1 = 1.0f - (float)y / (float)voxel_res + off_y;

                // Inset UVs slightly to avoid atlas bleeding on pixel boundaries
                float u0 = (px_start + x * step_x + 0.1f) / (float)img.width;
                float u1 = (px_start + x_end * step_x - 0.1f) / (float)img.width;
                float v0 = (py_start + y * step_y + 0.1f) / (float)img.height;
                float v1 = (py_start + (y + 1) * step_y - 0.1f) / (float)img.height;

                // Front Face (+Z)
                add_quad({
                    {x0, y0,  half_z}, {x1, y0,  half_z}, {x1, y1,  half_z}, {x0, y1,  half_z},
                    {u0, v1},         {u1, v1},         {u1, v0},         {u0, v0},
                    {0.0f, 0.0f, 1.0f},
                    c_front
                });

                // Back Face (-Z)
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

    // 2. TOP EDGES (+Y)
    for (int y = 0; y < voxel_res; y++) {
        int x = 0;
        while (x < voxel_res) {
            bool is_top_edge = solid[y][x] && (y == 0 || !solid[y - 1][x]);
            if (is_top_edge) {
                int x_end = x + 1;
                while (x_end < voxel_res && solid[y][x_end] && (y == 0 || !solid[y - 1][x_end])) {
                    x_end++;
                }

                float x0 = (float)x / (float)voxel_res + off_x;
                float x1 = (float)x_end / (float)voxel_res + off_x;
                float y1 = 1.0f - (float)y / (float)voxel_res + off_y;

                float u0 = (px_start + x * step_x) / (float)img.width;
                float u1 = (px_start + x_end * step_x) / (float)img.width;
                float v0 = (py_start + (y + 0.5f) * step_y) / (float)img.height;

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

    // 3. BOTTOM EDGES (-Y)
    for (int y = 0; y < voxel_res; y++) {
        int x = 0;
        while (x < voxel_res) {
            bool is_bot_edge = solid[y][x] && (y == voxel_res - 1 || !solid[y + 1][x]);
            if (is_bot_edge) {
                int x_end = x + 1;
                while (x_end < voxel_res && solid[y][x_end] && (y == voxel_res - 1 || !solid[y + 1][x_end])) {
                    x_end++;
                }

                float x0 = (float)x / (float)voxel_res + off_x;
                float x1 = (float)x_end / (float)voxel_res + off_x;
                float y0 = 1.0f - (float)(y + 1) / (float)voxel_res + off_y;

                float u0 = (px_start + x * step_x) / (float)img.width;
                float u1 = (px_start + x_end * step_x) / (float)img.width;
                float v1 = (py_start + (y + 0.5f) * step_y) / (float)img.height;

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

    // 4. LEFT EDGES (-X)
    for (int x = 0; x < voxel_res; x++) {
        int y = 0;
        while (y < voxel_res) {
            bool is_left_edge = solid[y][x] && (x == 0 || !solid[y][x - 1]);
            if (is_left_edge) {
                int y_end = y + 1;
                while (y_end < voxel_res && solid[y_end][x] && (x == 0 || !solid[y_end][x - 1])) {
                    y_end++;
                }

                float x0 = (float)x / (float)voxel_res + off_x;
                float y0 = 1.0f - (float)y_end / (float)voxel_res + off_y;
                float y1 = 1.0f - (float)y / (float)voxel_res + off_y;

                float u0 = (px_start + (x + 0.5f) * step_x) / (float)img.width;
                float v0 = (py_start + y * step_y) / (float)img.height;
                float v1 = (py_start + y_end * step_y) / (float)img.height;

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

    // 5. RIGHT EDGES (+X)
    for (int x = 0; x < voxel_res; x++) {
        int y = 0;
        while (y < voxel_res) {
            bool is_right_edge = solid[y][x] && (x == voxel_res - 1 || !solid[y][x + 1]);
            if (is_right_edge) {
                int y_end = y + 1;
                while (y_end < voxel_res && solid[y_end][x] && (x == voxel_res - 1 || !solid[y_end][x + 1])) {
                    y_end++;
                }

                float x1 = (float)(x + 1) / (float)voxel_res + off_x;
                float y0 = 1.0f - (float)y_end / (float)voxel_res + off_y;
                float y1 = 1.0f - (float)y / (float)voxel_res + off_y;

                float u1 = (px_start + (x + 0.5f) * step_x) / (float)img.width;
                float v0 = (py_start + y * step_y) / (float)img.height;
                float v1 = (py_start + y_end * step_y) / (float)img.height;

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
