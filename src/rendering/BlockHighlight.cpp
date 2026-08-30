#include "rendering/BlockHighlight.hpp"
#include "world/World.hpp"
#include "core/Config.hpp"
#include "world/MarchingCubes.hpp"
#include "ui/UI.hpp"
#include <rlgl.h>
#include <cmath>
#include <vector>

using namespace Config;

void DrawBlockHighlight(const BlockHighlightParams& p) {
    if (p.ui.is_open) return;

    bool is_valid_tool = false;
    if (p.ui.selected_slot == 0) {
        is_valid_tool = true;
    } else {
        uint8_t held_id = p.ui.slots[p.ui.selected_slot].id;
        if (held_id != AIR && held_id != 254 && p.ui.slots[p.ui.selected_slot].count > 0 && BLOCKS.find(held_id) != BLOCKS.end()) {
            is_valid_tool = true;
        }
    }
    if (!is_valid_tool || !p.ray_hit_valid) return;

    BeginMode3D(p.camera);
    rlDisableDepthMask();
    rlDisableDepthTest();

    Vector3 target_node = (p.ui.selected_slot == 0) ? p.target_solid : p.target_empty;
    rlDisableBackfaceCulling();

    int bx = std::floor(target_node.x);
    int by = std::floor(target_node.y);
    int bz = std::floor(target_node.z);

    ToolSlot* active_tool = (p.ui.selected_slot == 0) ? p.ui.get_active_tool() : nullptr;
    bool is_hammer = (active_tool && active_tool->type == TOOL_HAMMER);

    HammerArea h_area;
    if (is_hammer) {
        h_area = get_hammer_area((int)active_tool->tier, p.hit, bx, bz);
    } else {
        h_area = {0, 0, 0, 0, 0};
    }

    int off_min_x = h_area.min_dx - 1;
    int off_max_x = h_area.max_dx + 1;
    int off_min_z = h_area.min_dz - 1;
    int off_max_z = h_area.max_dz + 1;
    int size_x = off_max_x - off_min_x + 1;
    int size_z = off_max_z - off_min_z + 1;
    int size_y = is_hammer ? (h_area.max_h + 3) : 3;

    uint8_t held_b = (p.ui.selected_slot != 0) ? p.ui.slots[p.ui.selected_slot].id : AIR;
    bool is_construction_held = (p.ui.selected_slot != 0 && held_b != AIR && held_b != 254 && BLOCKS.find(held_b) != BLOCKS.end() && BLOCKS.at(held_b).shape != SHAPE_TERRAIN);

    uint8_t target_b = (p.ui.selected_slot == 0) ? p.world.get_block(bx, by, bz) : AIR;
    bool is_construction_targeted = (p.ui.selected_slot == 0 && target_b != AIR && target_b != WATER && BLOCKS.count(target_b) && BLOCKS.at(target_b).shape != SHAPE_TERRAIN);

    uint8_t target_rot = 0;
    if (is_construction_targeted) {
        int cx = std::floor((float)bx / CHUNK_SIZE);
        int cz = std::floor((float)bz / CHUNK_SIZE);
        Chunk* chk = p.world.get_chunk(cx, cz);
        if (chk) {
            int lx = bx - cx * CHUNK_SIZE;
            int lz = bz - cz * CHUNK_SIZE;
            if (lx >= 0 && lx <= CHUNK_SIZE && lz >= 0 && lz <= CHUNK_SIZE && by >= 0 && by < GRID_Y) {
                std::lock_guard<std::mutex> lock(chk->chunk_mutex);
                target_rot = chk->voxels[chk->get_idx(lx, by, lz)].rotation;
            }
        }
    }

    if (is_construction_held) {
        float pulse = 0.6f + std::sin(GetTime() * 6.0f) * 0.4f;
        unsigned char alpha = (unsigned char)(pulse * 255);
        Color wire_col = Color{25, 255, 75, alpha};
        Color solid_col = Color{0, 255, 0, 40};

        if (held_b == DOOR_WOOD) {
            Vector3 c_pos = { (float)bx, (float)by + 0.5f, (float)bz };
            DrawCubeWires(c_pos, 1.002f, 2.002f, 1.002f, wire_col);
            DrawCube(c_pos, 1.0f, 2.0f, 1.0f, solid_col);
        } else {
            Vector3 c_pos = { (float)bx, (float)by, (float)bz };
            DrawCubeWires(c_pos, 1.0f, 1.0f, 1.0f, wire_col);
            DrawCube(c_pos, 1.0f, 1.0f, 1.0f, solid_col);
        }
    } else if (is_construction_targeted) {
        float pulse = 0.6f + std::sin(GetTime() * 6.0f) * 0.4f;
        unsigned char alpha = (unsigned char)(pulse * 255);
        Color wire_col = Color{255, 45, 45, alpha};
        Color solid_col = Color{255, 25, 25, 40};

        auto shape = BLOCKS.at(target_b).shape;
        if (shape == SHAPE_CHEST) {
            DrawCubeWires({(float)bx, (float)by - 0.06f, (float)bz}, 0.89f, 0.89f, 0.89f, wire_col);
            DrawCube({(float)bx, (float)by - 0.06f, (float)bz}, 0.88f, 0.88f, 0.88f, solid_col);
        } else if (shape == SHAPE_TORCH) {
            DrawCubeWires({(float)bx, (float)by - 0.15f, (float)bz}, 0.16f, 0.70f, 0.16f, wire_col);
            DrawCube({(float)bx, (float)by - 0.15f, (float)bz}, 0.14f, 0.68f, 0.14f, solid_col);
        } else if (shape == SHAPE_FENCE) {
            DrawCubeWires({(float)bx, (float)by, (float)bz}, 0.26f, 1.002f, 0.26f, wire_col);
            DrawCube({(float)bx, (float)by, (float)bz}, 0.25f, 1.0f, 0.25f, solid_col);
        } else if (shape == SHAPE_DOOR) {
            DrawCubeWires({(float)bx, (float)by, (float)bz}, 1.002f, 1.002f, 1.002f, wire_col);
            DrawCube({(float)bx, (float)by, (float)bz}, 1.0f, 1.0f, 1.0f, solid_col);
        } else if (shape == SHAPE_STAIRS) {
            DrawCubeWires({(float)bx, (float)by - 0.25f, (float)bz}, 1.002f, 0.502f, 1.002f, wire_col);
            DrawCube({(float)bx, (float)by - 0.25f, (float)bz}, 1.0f, 0.5f, 1.0f, solid_col);
            if (target_rot == 0) {
                DrawCubeWires({(float)bx, (float)by + 0.25f, (float)bz - 0.25f}, 1.002f, 0.502f, 0.502f, wire_col);
                DrawCube({(float)bx, (float)by + 0.25f, (float)bz - 0.25f}, 1.0f, 0.5f, 0.5f, solid_col);
            } else if (target_rot == 1) {
                DrawCubeWires({(float)bx + 0.25f, (float)by + 0.25f, (float)bz}, 0.502f, 0.502f, 1.002f, wire_col);
                DrawCube({(float)bx + 0.25f, (float)by + 0.25f, (float)bz}, 0.5f, 0.5f, 1.0f, solid_col);
            } else if (target_rot == 2) {
                DrawCubeWires({(float)bx, (float)by + 0.25f, (float)bz + 0.25f}, 1.002f, 0.502f, 0.502f, wire_col);
                DrawCube({(float)bx, (float)by + 0.25f, (float)bz + 0.25f}, 1.0f, 0.5f, 0.5f, solid_col);
            } else {
                DrawCubeWires({(float)bx - 0.25f, (float)by + 0.25f, (float)bz}, 0.502f, 0.502f, 1.002f, wire_col);
                DrawCube({(float)bx - 0.25f, (float)by + 0.25f, (float)bz}, 0.5f, 0.5f, 1.0f, solid_col);
            }
        } else {
            Vector3 c_pos = { (float)bx, (float)by, (float)bz };
            DrawCubeWires(c_pos, 1.002f, 1.002f, 1.002f, wire_col);
            DrawCube(c_pos, 1.0f, 1.0f, 1.0f, solid_col);
        }
    } else {
        std::vector<float> d_slice(size_x * size_y * size_z, -1.0f);
        for (int dx = off_min_x; dx <= off_max_x; dx++) {
            for (int dz = off_min_z; dz <= off_max_z; dz++) {
                for (int dy = -1; dy <= size_y - 2; dy++) {
                    int idx = (dy + 1) * (size_x * size_z) + (dz - off_min_z) * size_x + (dx - off_min_x);
                    d_slice[idx] = p.world.get_density(bx + dx, by + dy, bz + dz);
                }
            }
        }

        if (is_hammer) {
            for (int dx = h_area.min_dx; dx <= h_area.max_dx; dx++) {
                for (int dz = h_area.min_dz; dz <= h_area.max_dz; dz++) {
                    for (int dy = 1; dy <= h_area.max_h; dy++) {
                        if (dy + 1 < size_y) {
                            uint8_t b = p.world.get_block(bx + dx, by + dy, bz + dz);
                            if (b != AIR && b != WATER && BLOCKS.count(b)) {
                                const auto& bt = BLOCKS.at(b);
                                if (bt.require_tier == 255 || bt.require_tier <= active_tool->tier) {
                                    int idx = (dy + 1) * (size_x * size_z) + (dz - off_min_z) * size_x + (dx - off_min_x);
                                    d_slice[idx] = -1.0f;
                                }
                            } else {
                                int idx = (dy + 1) * (size_x * size_z) + (dz - off_min_z) * size_x + (dx - off_min_x);
                                d_slice[idx] = -1.0f;
                            }
                        }
                    }
                    uint8_t b_base = p.world.get_block(bx + dx, by, bz + dz);
                    if (b_base != AIR && b_base != WATER) {
                        int idx_base = (0 + 1) * (size_x * size_z) + (dz - off_min_z) * size_x + (dx - off_min_x);
                        d_slice[idx_base] = 1.0f;
                    }
                }
            }
        } else if (p.ui.selected_slot == 0) {
            d_slice[1 * (size_x * size_z) + 1 * size_x + 1] = -1.0f;
        } else {
            d_slice[1 * (size_x * size_z) + 1 * size_x + 1] = 1.0f;
        }

        std::vector<Vector3> g_verts, g_norms;
        std::vector<Vector2> g_uvs, g_uvs2;
        std::vector<Color> g_cols;
        mc::generate(nullptr, d_slice.data(), size_x, size_y, size_z, 0.0f, AIR, g_verts, g_norms, g_uvs, g_uvs2, g_cols);

        rlPushMatrix();
        rlTranslatef(bx + (float)off_min_x, by - 1.0f, bz + (float)off_min_z);

        float pulse = 0.6f + std::sin(GetTime() * 6.0f) * 0.4f;
        unsigned char alpha = (unsigned char)(pulse * 255);
        Color wire_col = is_hammer ? Color{255, 180, 40, alpha} : ((p.ui.selected_slot == 0) ? Color{255, 25, 25, alpha} : Color{25, 255, 75, alpha});
        Color solid_col = is_hammer ? Color{255, 160, 0, 45} : ((p.ui.selected_slot == 0) ? Color{255, 0, 0, 40} : Color{0, 255, 0, 40});

        rlBegin(RL_LINES);
        rlColor4ub(wire_col.r, wire_col.g, wire_col.b, wire_col.a);
        for (size_t i = 0; i < g_verts.size(); i += 3) {
            rlVertex3f(g_verts[i].x, g_verts[i].y, g_verts[i].z);
            rlVertex3f(g_verts[i+1].x, g_verts[i+1].y, g_verts[i+1].z);
            rlVertex3f(g_verts[i+1].x, g_verts[i+1].y, g_verts[i+1].z);
            rlVertex3f(g_verts[i+2].x, g_verts[i+2].y, g_verts[i+2].z);
            rlVertex3f(g_verts[i+2].x, g_verts[i+2].y, g_verts[i+2].z);
            rlVertex3f(g_verts[i].x, g_verts[i].y, g_verts[i].z);
        }
        rlEnd();

        rlSetTexture(p.spritesheet.id);
        rlBegin(RL_TRIANGLES);
        rlColor4ub(solid_col.r, solid_col.g, solid_col.b, solid_col.a);
        for (size_t i = 0; i < g_verts.size(); ++i) {
            rlTexCoord2f(g_uvs[i].x, g_uvs[i].y);
            rlVertex3f(g_verts[i].x, g_verts[i].y, g_verts[i].z);
        }
        rlEnd();
        rlSetTexture(0);

        rlPopMatrix();
    }

    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndMode3D();
}
