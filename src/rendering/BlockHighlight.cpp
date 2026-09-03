#include "rendering/BlockHighlight.hpp"
#include "world/World.hpp"
#include "core/Config.hpp"
#include "world/MarchingCubes.hpp"
#include "ui/UI.hpp"
#include <rlgl.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <map>
#include <tuple>

using namespace Config;

static void DrawCubeWiresCullBack(Vector3 pos, float width, float height, float length, Color color, Vector3 cam) {
    float x = pos.x;
    float y = pos.y;
    float z = pos.z;

    float w2 = width * 0.5f;
    float h2 = height * 0.5f;
    float l2 = length * 0.5f;

    bool fx_p = (cam.x > x + w2);
    bool fx_n = (cam.x < x - w2);
    bool fy_p = (cam.y > y + h2);
    bool fy_n = (cam.y < y - h2);
    bool fz_p = (cam.z > z + l2);
    bool fz_n = (cam.z < z - l2);

    rlBegin(RL_LINES);
    rlColor4ub(color.r, color.g, color.b, color.a);

    // 4 aristas en X
    if (fy_p || fz_p) {
        rlVertex3f(x - w2, y + h2, z + l2);
        rlVertex3f(x + w2, y + h2, z + l2);
    }
    if (fy_p || fz_n) {
        rlVertex3f(x - w2, y + h2, z - l2);
        rlVertex3f(x + w2, y + h2, z - l2);
    }
    if (fy_n || fz_p) {
        rlVertex3f(x - w2, y - h2, z + l2);
        rlVertex3f(x + w2, y - h2, z + l2);
    }
    if (fy_n || fz_n) {
        rlVertex3f(x - w2, y - h2, z - l2);
        rlVertex3f(x + w2, y - h2, z - l2);
    }

    // 4 aristas en Y
    if (fx_p || fz_p) {
        rlVertex3f(x + w2, y - h2, z + l2);
        rlVertex3f(x + w2, y + h2, z + l2);
    }
    if (fx_p || fz_n) {
        rlVertex3f(x + w2, y - h2, z - l2);
        rlVertex3f(x + w2, y + h2, z - l2);
    }
    if (fx_n || fz_p) {
        rlVertex3f(x - w2, y - h2, z + l2);
        rlVertex3f(x - w2, y + h2, z + l2);
    }
    if (fx_n || fz_n) {
        rlVertex3f(x - w2, y - h2, z - l2);
        rlVertex3f(x - w2, y + h2, z - l2);
    }

    // 4 aristas en Z
    if (fy_p || fx_p) {
        rlVertex3f(x + w2, y + h2, z - l2);
        rlVertex3f(x + w2, y + h2, z + l2);
    }
    if (fy_p || fx_n) {
        rlVertex3f(x - w2, y + h2, z - l2);
        rlVertex3f(x - w2, y + h2, z + l2);
    }
    if (fy_n || fx_p) {
        rlVertex3f(x + w2, y - h2, z - l2);
        rlVertex3f(x + w2, y - h2, z + l2);
    }
    if (fy_n || fx_n) {
        rlVertex3f(x - w2, y - h2, z - l2);
        rlVertex3f(x - w2, y - h2, z + l2);
    }

    rlEnd();
}

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
    rlSetLineWidth(2.5f);

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
        Color wire_col = Color{70, 215, 125, 240};
        Color solid_col = Color{85, 225, 135, 50}; // 80% transparente

        if (held_b == DOOR_WOOD) {
            Vector3 c_pos = { (float)bx, (float)by + 0.5f, (float)bz };
            DrawCubeWiresCullBack(c_pos, 1.002f, 2.002f, 1.002f, wire_col, p.camera.position);
            DrawCube(c_pos, 1.0f, 2.0f, 1.0f, solid_col);
        } else {
            Vector3 c_pos = { (float)bx, (float)by, (float)bz };
            DrawCubeWiresCullBack(c_pos, 1.0f, 1.0f, 1.0f, wire_col, p.camera.position);
            DrawCube(c_pos, 1.0f, 1.0f, 1.0f, solid_col);
        }
    } else if (is_construction_targeted) {
        Color wire_col = Color{235, 75, 75, 240};
        Color solid_col = Color{240, 95, 80, 50}; // 80% transparente

        auto shape = BLOCKS.at(target_b).shape;
        if (shape == SHAPE_CHEST) {
            DrawCubeWiresCullBack({(float)bx, (float)by - 0.06f, (float)bz}, 0.89f, 0.89f, 0.89f, wire_col, p.camera.position);
            DrawCube({(float)bx, (float)by - 0.06f, (float)bz}, 0.88f, 0.88f, 0.88f, solid_col);
        } else if (shape == SHAPE_TORCH) {
            DrawCubeWiresCullBack({(float)bx, (float)by - 0.15f, (float)bz}, 0.16f, 0.70f, 0.16f, wire_col, p.camera.position);
            DrawCube({(float)bx, (float)by - 0.15f, (float)bz}, 0.14f, 0.68f, 0.14f, solid_col);
        } else if (shape == SHAPE_FENCE) {
            DrawCubeWiresCullBack({(float)bx, (float)by, (float)bz}, 0.26f, 1.002f, 0.26f, wire_col, p.camera.position);
            DrawCube({(float)bx, (float)by, (float)bz}, 0.25f, 1.0f, 0.25f, solid_col);
        } else if (shape == SHAPE_DOOR) {
            DrawCubeWiresCullBack({(float)bx, (float)by, (float)bz}, 1.002f, 1.002f, 1.002f, wire_col, p.camera.position);
            DrawCube({(float)bx, (float)by, (float)bz}, 1.0f, 1.0f, 1.0f, solid_col);
        } else if (shape == SHAPE_STAIRS) {
            DrawCubeWiresCullBack({(float)bx, (float)by - 0.25f, (float)bz}, 1.002f, 0.502f, 1.002f, wire_col, p.camera.position);
            DrawCube({(float)bx, (float)by - 0.25f, (float)bz}, 1.0f, 0.5f, 1.0f, solid_col);
            if (target_rot == 0) {
                DrawCubeWiresCullBack({(float)bx, (float)by + 0.25f, (float)bz - 0.25f}, 1.002f, 0.502f, 0.502f, wire_col, p.camera.position);
                DrawCube({(float)bx, (float)by + 0.25f, (float)bz - 0.25f}, 1.0f, 0.5f, 0.5f, solid_col);
            } else if (target_rot == 1) {
                DrawCubeWiresCullBack({(float)bx + 0.25f, (float)by + 0.25f, (float)bz}, 0.502f, 0.502f, 1.002f, wire_col, p.camera.position);
                DrawCube({(float)bx + 0.25f, (float)by + 0.25f, (float)bz}, 0.5f, 0.5f, 1.0f, solid_col);
            } else if (target_rot == 2) {
                DrawCubeWiresCullBack({(float)bx, (float)by + 0.25f, (float)bz + 0.25f}, 1.002f, 0.502f, 0.502f, wire_col, p.camera.position);
                DrawCube({(float)bx, (float)by + 0.25f, (float)bz + 0.25f}, 1.0f, 0.5f, 0.5f, solid_col);
            } else {
                DrawCubeWiresCullBack({(float)bx - 0.25f, (float)by + 0.25f, (float)bz}, 0.502f, 0.502f, 1.002f, wire_col, p.camera.position);
                DrawCube({(float)bx - 0.25f, (float)by + 0.25f, (float)bz}, 0.5f, 0.5f, 1.0f, solid_col);
            }
        } else {
            Vector3 c_pos = { (float)bx, (float)by, (float)bz };
            DrawCubeWiresCullBack(c_pos, 1.002f, 1.002f, 1.002f, wire_col, p.camera.position);
            DrawCube(c_pos, 1.0f, 1.0f, 1.0f, solid_col);
        }
    } else {
        std::vector<float> d_before(size_x * size_y * size_z, -1.0f);
        for (int dx = off_min_x; dx <= off_max_x; dx++) {
            for (int dz = off_min_z; dz <= off_max_z; dz++) {
                for (int dy = -1; dy <= size_y - 2; dy++) {
                    int idx = (dy + 1) * (size_x * size_z) + (dz - off_min_z) * size_x + (dx - off_min_x);
                    d_before[idx] = p.world.get_density(bx + dx, by + dy, bz + dz);
                }
            }
        }

        std::vector<float> d_after = d_before;

        bool is_mining = (!is_hammer && p.ui.selected_slot == 0);

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
                                    d_after[idx] = -1.0f;
                                }
                            } else {
                                int idx = (dy + 1) * (size_x * size_z) + (dz - off_min_z) * size_x + (dx - off_min_x);
                                d_after[idx] = -1.0f;
                            }
                        }
                    }
                }
            }
        } else if (is_mining) {
            // Quitar: el bloque pasa a ser aire (-1.0f) en d_after para calcular la cavidad / apertura interior
            d_after[1 * (size_x * size_z) + 1 * size_x + 1] = -1.0f;
        } else {
            // Colocar: el bloque pasa a ser sólido (1.0f)
            d_after[1 * (size_x * size_z) + 1 * size_x + 1] = 1.0f;
        }

        std::vector<Vector3> g_verts, g_norms;
        std::vector<Vector2> g_uvs, g_uvs2;
        std::vector<Color> g_cols;
        std::vector<Vector3> dummy_t_verts, dummy_t_norms;
        std::vector<Vector2> dummy_t_uvs, dummy_t_uvs2;
        std::vector<Color> dummy_t_cols;

        // Al minar bloque individual generamos primero desde d_before para extraer las caras exteriores existentes
        const float* mc_data = is_mining ? d_before.data() : d_after.data();
        mc::generate(nullptr, mc_data, size_x, size_y, size_z, 0.0f, AIR, 
                     g_verts, g_norms, g_uvs, g_uvs2, g_cols,
                     dummy_t_verts, dummy_t_norms, dummy_t_uvs, dummy_t_uvs2, dummy_t_cols);

        // Muestreo trilineal en la cuadrícula previa para comparar densidades matemáticas
        auto sample_density = [&](const std::vector<float>& grid, float sx, float sy, float sz) -> float {
            int ix = std::clamp((int)std::floor(sx), 0, size_x - 2);
            int iy = std::clamp((int)std::floor(sy), 0, size_y - 2);
            int iz = std::clamp((int)std::floor(sz), 0, size_z - 2);
            float fx = sx - (float)ix;
            float fy = sy - (float)iy;
            float fz = sz - (float)iz;

            auto get = [&](int x, int y, int z) -> float {
                return grid[y * (size_x * size_z) + z * size_x + x];
            };

            float d000 = get(ix,   iy,   iz);
            float d100 = get(ix+1, iy,   iz);
            float d010 = get(ix,   iy+1, iz);
            float d110 = get(ix+1, iy+1, iz);
            float d001 = get(ix,   iy,   iz+1);
            float d101 = get(ix+1, iy,   iz+1);
            float d011 = get(ix,   iy+1, iz+1);
            float d111 = get(ix+1, iy+1, iz+1);

            float c00 = d000 * (1.0f - fx) + d100 * fx;
            float c10 = d010 * (1.0f - fx) + d110 * fx;
            float c01 = d001 * (1.0f - fx) + d101 * fx;
            float c11 = d011 * (1.0f - fx) + d111 * fx;
            float c0  = c00  * (1.0f - fz) + c01  * fz;
            float c1  = c10  * (1.0f - fz) + c11  * fz;
            return c0 * (1.0f - fy) + c1 * fy;
        };

        std::vector<Vector3> f_verts;
        std::vector<Vector2> f_uvs;
        std::vector<Vector3> f_outer_verts;
        std::vector<Vector2> f_outer_uvs;
        std::vector<Vector3> f_inner_verts;
        std::vector<Vector2> f_inner_uvs;
        size_t f_outer_count = 0;
        f_verts.reserve(g_verts.size());
        f_uvs.reserve(g_uvs.size());

        if (is_hammer) {
            float x_min = (float)(h_area.min_dx - off_min_x) - 0.2f;
            float x_max = (float)(h_area.max_dx - off_min_x) + 1.2f;
            float z_min = (float)(h_area.min_dz - off_min_z) - 0.2f;
            float z_max = (float)(h_area.max_dz - off_min_z) + 1.2f;

            for (size_t i = 0; i < g_verts.size(); i += 3) {
                Vector3 vc = {
                    (g_verts[i].x + g_verts[i+1].x + g_verts[i+2].x) / 3.0f,
                    (g_verts[i].y + g_verts[i+1].y + g_verts[i+2].y) / 3.0f,
                    (g_verts[i].z + g_verts[i+1].z + g_verts[i+2].z) / 3.0f
                };
                if (vc.x >= x_min && vc.x <= x_max && vc.z >= z_min && vc.z <= z_max) {
                    float old_d = sample_density(d_before, vc.x, vc.y, vc.z);
                    // Solo el hueco / espacio vacío que deja el minado (antes era sólido > 0.03)
                    if (old_d > 0.03f) {
                        f_verts.push_back(g_verts[i]);
                        f_verts.push_back(g_verts[i+1]);
                        f_verts.push_back(g_verts[i+2]);
                        f_uvs.push_back(g_uvs[i]);
                        f_uvs.push_back(g_uvs[i+1]);
                        f_uvs.push_back(g_uvs[i+2]);

                        f_inner_verts.push_back(g_verts[i]);
                        f_inner_verts.push_back(g_verts[i+1]);
                        f_inner_verts.push_back(g_verts[i+2]);
                        f_inner_uvs.push_back(g_uvs[i]);
                        f_inner_uvs.push_back(g_uvs[i+1]);
                        f_inner_uvs.push_back(g_uvs[i+2]);
                    }
                }
            }
        } else if (is_mining) {
            // Minar bloque individual: combinar caras exteriores existentes (d_before)
            // con las caras interiores de la cavidad / quads que se formarán (d_after)
            // para formar la figura 3D completa del bloque que se retira
            auto touches_center = [](const Vector3& v) -> bool {
                int c = 0;
                if (std::abs(v.x - 1.0f) < 0.02f) c++;
                if (std::abs(v.y - 1.0f) < 0.02f) c++;
                if (std::abs(v.z - 1.0f) < 0.02f) c++;
                return c >= 2;
            };

            // 1. Caras exteriores existentes (corteza del árbol, superficie del suelo, domo del montículo)
            for (size_t i = 0; i < g_verts.size(); i += 3) {
                if (touches_center(g_verts[i]) || touches_center(g_verts[i+1]) || touches_center(g_verts[i+2])) {
                    f_verts.push_back(g_verts[i]);
                    f_verts.push_back(g_verts[i+1]);
                    f_verts.push_back(g_verts[i+2]);
                    f_uvs.push_back(g_uvs[i]);
                    f_uvs.push_back(g_uvs[i+1]);
                    f_uvs.push_back(g_uvs[i+2]);

                    f_outer_verts.push_back(g_verts[i]);
                    f_outer_verts.push_back(g_verts[i+1]);
                    f_outer_verts.push_back(g_verts[i+2]);
                    f_outer_uvs.push_back(g_uvs[i]);
                    f_outer_uvs.push_back(g_uvs[i+1]);
                    f_outer_uvs.push_back(g_uvs[i+2]);
                }
            }
            f_outer_count = f_verts.size();

            // 2. Caras interiores de la abertura (quads superiores e inferiores del tronco, hueco del piso, base del montículo)
            std::vector<Vector3> a_verts, a_norms, a_d1, a_d2;
            std::vector<Vector2> a_uvs, a_uvs2, a_d3, a_d4;
            std::vector<Color> a_cols, a_d5;
            mc::generate(nullptr, d_after.data(), size_x, size_y, size_z, 0.0f, AIR,
                         a_verts, a_norms, a_uvs, a_uvs2, a_cols,
                         a_d1, a_d2, a_d3, a_d4, a_d5);

            for (size_t i = 0; i < a_verts.size(); i += 3) {
                if (touches_center(a_verts[i]) || touches_center(a_verts[i+1]) || touches_center(a_verts[i+2])) {
                    Vector3 vc = {
                        (a_verts[i].x + a_verts[i+1].x + a_verts[i+2].x) / 3.0f,
                        (a_verts[i].y + a_verts[i+1].y + a_verts[i+2].y) / 3.0f,
                        (a_verts[i].z + a_verts[i+1].z + a_verts[i+2].z) / 3.0f
                    };
                    float old_d = sample_density(d_before, vc.x, vc.y, vc.z);
                    if (old_d > 0.03f) {
                        f_verts.push_back(a_verts[i]);
                        f_verts.push_back(a_verts[i+1]);
                        f_verts.push_back(a_verts[i+2]);
                        f_uvs.push_back(a_uvs[i]);
                        f_uvs.push_back(a_uvs[i+1]);
                        f_uvs.push_back(a_uvs[i+2]);

                        // Caras internas de corte / abertura
                        f_inner_verts.push_back(a_verts[i]);
                        f_inner_verts.push_back(a_verts[i+1]);
                        f_inner_verts.push_back(a_verts[i+2]);
                        f_inner_uvs.push_back(a_uvs[i]);
                        f_inner_uvs.push_back(a_uvs[i+1]);
                        f_inner_uvs.push_back(a_uvs[i+2]);
                    }
                }
            }
        } else {
            // Construcción (colocar bloque individual): solo nuevos vértices creados en el aire (old_d < -0.03)
            for (size_t i = 0; i < g_verts.size(); i += 3) {
                Vector3 vc = {
                    (g_verts[i].x + g_verts[i+1].x + g_verts[i+2].x) / 3.0f,
                    (g_verts[i].y + g_verts[i+1].y + g_verts[i+2].y) / 3.0f,
                    (g_verts[i].z + g_verts[i+1].z + g_verts[i+2].z) / 3.0f
                };
                float dist_sq = (vc.x - 1.0f)*(vc.x - 1.0f) + (vc.y - 1.0f)*(vc.y - 1.0f) + (vc.z - 1.0f)*(vc.z - 1.0f);
                if (dist_sq <= 1.25f * 1.25f) {
                    float old_d = sample_density(d_before, vc.x, vc.y, vc.z);
                    if (old_d < -0.03f) {
                        f_verts.push_back(g_verts[i]);
                        f_verts.push_back(g_verts[i+1]);
                        f_verts.push_back(g_verts[i+2]);
                        f_uvs.push_back(g_uvs[i]);
                        f_uvs.push_back(g_uvs[i+1]);
                        f_uvs.push_back(g_uvs[i+2]);

                        f_inner_verts.push_back(g_verts[i]);
                        f_inner_verts.push_back(g_verts[i+1]);
                        f_inner_verts.push_back(g_verts[i+2]);
                        f_inner_uvs.push_back(g_uvs[i]);
                        f_inner_uvs.push_back(g_uvs[i+1]);
                        f_inner_uvs.push_back(g_uvs[i+2]);
                    }
                }
            }
        }

        // === Líneas unicolor no tan intensas + caras internas 80% transparentes (alpha ~ 50) con tonalidad mínimamente diferente ===
        Color wire_col = is_hammer ? Color{245, 175, 50, 240} : ((p.ui.selected_slot == 0) ? Color{235, 75, 75, 240} : Color{70, 215, 125, 240});
        Color inner_face_col = is_hammer ? Color{250, 185, 60, 50} : ((p.ui.selected_slot == 0) ? Color{240, 95, 80, 50} : Color{85, 225, 135, 50});

        // Borde perimetral adaptado a la topografía del terreno para el martillo
        auto get_terrain_height_local = [&](float lx, float lz) -> float {
            float best_y = 1.5f;
            float prev_d = sample_density(d_before, lx, 0.5f, lz);
            for (float sy = 0.7f; sy <= (float)size_y - 1.2f; sy += 0.2f) {
                float cur_d = sample_density(d_before, lx, sy, lz);
                if ((prev_d >= 0.0f && cur_d < 0.0f) || (prev_d < 0.0f && cur_d >= 0.0f)) {
                    float denom = prev_d - cur_d;
                    float t = (std::abs(denom) > 1e-4f) ? (prev_d / denom) : 0.5f;
                    return (sy - 0.2f) + t * 0.2f;
                }
                prev_d = cur_d;
            }
            return best_y;
        };

        std::vector<Vector3> hammer_border_pts;
        if (is_hammer) {
            float x_min = (float)(h_area.min_dx - off_min_x);
            float x_max = (float)(h_area.max_dx - off_min_x + 1);
            float z_min = (float)(h_area.min_dz - off_min_z);
            float z_max = (float)(h_area.max_dz - off_min_z + 1);

            float step = 0.25f;
            // Borde 1: z = z_min, x de x_min a x_max
            for (float x = x_min; x < x_max; x += step) {
                hammer_border_pts.push_back({ x, get_terrain_height_local(x, z_min) + 0.03f, z_min });
            }
            hammer_border_pts.push_back({ x_max, get_terrain_height_local(x_max, z_min) + 0.03f, z_min });

            // Borde 2: x = x_max, z de z_min a z_max
            for (float z = z_min; z < z_max; z += step) {
                hammer_border_pts.push_back({ x_max, get_terrain_height_local(x_max, z) + 0.03f, z });
            }
            hammer_border_pts.push_back({ x_max, get_terrain_height_local(x_max, z_max) + 0.03f, z_max });

            // Borde 3: z = z_max, x de x_max a x_min
            for (float x = x_max; x > x_min; x -= step) {
                hammer_border_pts.push_back({ x, get_terrain_height_local(x, z_max) + 0.03f, z_max });
            }
            hammer_border_pts.push_back({ x_min, get_terrain_height_local(x_min, z_max) + 0.03f, z_max });

            // Borde 4: x = x_min, z de z_max a z_min
            for (float z = z_max; z > z_min; z -= step) {
                hammer_border_pts.push_back({ x_min, get_terrain_height_local(x_min, z) + 0.03f, z });
            }
            hammer_border_pts.push_back({ x_min, get_terrain_height_local(x_min, z_min) + 0.03f, z_min });
        }

        // === OPCIÓN B: Extracción de aristas limpias (Bordes de contorno y pliegues, sin diagonales coplanares) ===
        // === Extracción de aristas limpias (Bordes de contorno y pliegues, sin diagonales coplanares) ===
        struct EdgeInfo {
            Vector3 v0;
            Vector3 v1;
            Vector3 n1;
            Vector3 n2;
            int count = 0;
            bool is_inner = false;
        };

        auto point_to_key = [](const Vector3& pt) -> std::tuple<int, int, int> {
            return {
                (int)std::floor(pt.x * 200.0f + 0.5f),
                (int)std::floor(pt.y * 200.0f + 0.5f),
                (int)std::floor(pt.z * 200.0f + 0.5f)
            };
        };

        std::map<std::pair<std::tuple<int, int, int>, std::tuple<int, int, int>>, EdgeInfo> edge_map;

        for (size_t i = 0; i + 2 < f_verts.size(); i += 3) {
            bool is_inner_tri = (i >= f_outer_count);
            Vector3 v0 = f_verts[i];
            Vector3 v1 = f_verts[i+1];
            Vector3 v2 = f_verts[i+2];

            Vector3 u = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
            Vector3 v = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
            Vector3 n = {
                u.y * v.z - u.z * v.y,
                u.z * v.x - u.x * v.z,
                u.x * v.y - u.y * v.x
            };
            float nlen = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
            if (nlen > 1e-6f) { n.x /= nlen; n.y /= nlen; n.z /= nlen; }

            Vector3 tri_edges[3][2] = { {v0, v1}, {v1, v2}, {v2, v0} };
            for (int e = 0; e < 3; ++e) {
                auto k1 = point_to_key(tri_edges[e][0]);
                auto k2 = point_to_key(tri_edges[e][1]);
                if (k1 == k2) continue;
                auto edge_key = (k1 < k2) ? std::make_pair(k1, k2) : std::make_pair(k2, k1);

                auto& info = edge_map[edge_key];
                if (info.count == 0) {
                    info.v0 = tri_edges[e][0];
                    info.v1 = tri_edges[e][1];
                    info.n1 = n;
                } else if (info.count == 1) {
                    info.n2 = n;
                }
                if (is_inner_tri) {
                    info.is_inner = true;
                }
                info.count++;
            }
        }

        std::vector<std::pair<Vector3, Vector3>> clean_edges;
        clean_edges.reserve(edge_map.size());

        for (const auto& kv : edge_map) {
            const auto& info = kv.second;

            // Arista compartida entre 2 triángulos: solo dibujarla si hay quiebre angular (> 25°)
            if (info.count == 2) {
                float dot = info.n1.x * info.n2.x + info.n1.y * info.n2.y + info.n1.z * info.n2.z;
                if (dot >= 0.88f) {
                    continue; // Diagonal coplanar -> omitir
                }
            }

            if (!info.is_inner) {
                // Arista exterior: solo si mira a la cámara o silueta
                Vector3 mid_local = {
                    (info.v0.x + info.v1.x) * 0.5f,
                    (info.v0.y + info.v1.y) * 0.5f,
                    (info.v0.z + info.v1.z) * 0.5f
                };
                Vector3 mid_world = {
                    (float)bx + (float)off_min_x + mid_local.x,
                    (float)by - 1.0f + mid_local.y,
                    (float)bz + (float)off_min_z + mid_local.z
                };
                Vector3 to_cam = {
                    p.camera.position.x - mid_world.x,
                    p.camera.position.y - mid_world.y,
                    p.camera.position.z - mid_world.z
                };

                float cam_dot1 = info.n1.x * to_cam.x + info.n1.y * to_cam.y + info.n1.z * to_cam.z;
                float cam_dot2 = (info.count >= 2) ? (info.n2.x * to_cam.x + info.n2.y * to_cam.y + info.n2.z * to_cam.z) : cam_dot1;
                bool is_front_facing = (cam_dot1 > 0.0f) || (cam_dot2 > 0.0f);

                if (!is_front_facing) continue;
            }

            clean_edges.push_back({ info.v0, info.v1 });
        }

        rlPushMatrix();
        rlTranslatef(bx + (float)off_min_x, by - 1.0f, bz + (float)off_min_z);

        // 1. CARAS INTERNAS: 80% transparentes (alpha 50) para que medio se note, con color armónico
        if (!f_inner_verts.empty()) {
            rlDisableBackfaceCulling();
            rlSetTexture(p.spritesheet.id);
            rlBegin(RL_TRIANGLES);
            rlColor4ub(inner_face_col.r, inner_face_col.g, inner_face_col.b, inner_face_col.a);
            for (size_t i = 0; i < f_inner_verts.size(); ++i) {
                rlTexCoord2f(f_inner_uvs[i].x, f_inner_uvs[i].y);
                rlVertex3f(f_inner_verts[i].x, f_inner_verts[i].y, f_inner_verts[i].z);
            }
            rlEnd();
            rlSetTexture(0);
        }

        // 2. LÍNEAS UNICOLOR: tono no tan intenso con grosor limpio
        if (!clean_edges.empty()) {
            rlBegin(RL_LINES);
            rlColor4ub(wire_col.r, wire_col.g, wire_col.b, wire_col.a);
            for (const auto& edge : clean_edges) {
                rlVertex3f(edge.first.x, edge.first.y, edge.first.z);
                rlVertex3f(edge.second.x, edge.second.y, edge.second.z);
            }
            rlEnd();
        }

        if (is_hammer && !hammer_border_pts.empty()) {
            rlBegin(RL_LINES);
            rlColor4ub(wire_col.r, wire_col.g, wire_col.b, wire_col.a);
            for (size_t i = 0; i + 1 < hammer_border_pts.size(); ++i) {
                rlVertex3f(hammer_border_pts[i].x, hammer_border_pts[i].y, hammer_border_pts[i].z);
                rlVertex3f(hammer_border_pts[i+1].x, hammer_border_pts[i+1].y, hammer_border_pts[i+1].z);
            }
            rlEnd();
        }

        rlPopMatrix();
    }

    rlDrawRenderBatchActive();
    rlSetLineWidth(1.0f);
    rlEnableDepthTest();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndMode3D();
}
