#include <raylib.h>
#include <raymath.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "Config.hpp"
#include "BlockRegistry.hpp"
#include "MarchingCubes.hpp"
#include "Biome.hpp"
#include "World.hpp"
#include "UI.hpp"
#include "Chat.hpp"
#include "CommandHandler.hpp"
#include <rlgl.h>
#include <GL/gl.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <sqlite3.h>
#include "DatabaseIO.hpp"
#include "ItemDrop.hpp"
#include "ItemModel3D.hpp"
#include "VoxelLighting.hpp"
#include "json.hpp"
#include "MenuManager.hpp"
#include "MenuResourcePacks.hpp"
#include "ResourcePackManager.hpp"

using json = nlohmann::json;
using namespace Config;

bool VoxelRaycastSmooth(World& world, Vector3 origin, Vector3 dir, float max_dist, Vector3& out_hit, Vector3& out_solid, Vector3& out_empty) {
    Ray ray = { origin, dir };
    
    RayCollision closest_hit = { 0 };
    closest_hit.distance = max_dist;
    closest_hit.hit = false;
    
    bool hit_is_build = false;
    for (auto& pair : world.chunks) {
        Chunk* c = pair.second.get();
        if (!c->is_ready) continue;
        
        float cx_center = c->cx * Config::CHUNK_SIZE + Config::CHUNK_SIZE / 2.0f;
        float cz_center = c->cz * Config::CHUNK_SIZE + Config::CHUNK_SIZE / 2.0f;
        float dist_sq = (origin.x - cx_center)*(origin.x - cx_center) + (origin.z - cz_center)*(origin.z - cz_center);
        
        // Fast radial discard (max_dist + chunk_diagonal)
        float max_r = max_dist + (Config::CHUNK_SIZE * 1.5f);
        if (dist_sq > max_r * max_r) continue;
        
        BoundingBox box = { 
            { (float)(c->cx * Config::CHUNK_SIZE) - 1.0f, 0.0f, (float)(c->cz * Config::CHUNK_SIZE) - 1.0f },
            { (float)((c->cx + 1) * Config::CHUNK_SIZE) + 1.0f, (float)Config::GRID_Y, (float)((c->cz + 1) * Config::CHUNK_SIZE) + 1.0f }
        };
        if (!GetRayCollisionBox(ray, box).hit) continue;

        for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
            auto& sc = c->subchunks[s];
            BoundingBox sbox = { 
                { (float)(c->cx * Config::CHUNK_SIZE) - 0.5f, (float)(s * Config::SUBCHUNK_SIZE) - 0.5f, (float)(c->cz * Config::CHUNK_SIZE) - 0.5f },
                { (float)((c->cx + 1) * Config::CHUNK_SIZE) + 0.5f, (float)((s + 1) * Config::SUBCHUNK_SIZE) + 0.5f, (float)((c->cz + 1) * Config::CHUNK_SIZE) + 0.5f }
            };
            if (!GetRayCollisionBox(ray, sbox).hit) continue;

            if (sc.solid_mesh.vertexCount > 0) {
                RayCollision mesh_hit = GetRayCollisionMesh(ray, sc.solid_mesh, MatrixIdentity());
                if (mesh_hit.hit && mesh_hit.distance < closest_hit.distance) {
                    closest_hit = mesh_hit;
                    hit_is_build = false;
                }
            }

            if (sc.build_mesh.vertexCount > 0) {
                RayCollision build_hit = GetRayCollisionMesh(ray, sc.build_mesh, MatrixIdentity());
                if (build_hit.hit && build_hit.distance < closest_hit.distance) {
                    closest_hit = build_hit;
                    hit_is_build = true;
                }
            }
        }
    }
    
    if (closest_hit.hit) {
        out_hit = closest_hit.point;
        
        if (hit_is_build) {
            // El rayo impactó DIRECTAMENTE en los polígonos del bloque de construcción
            int bx = std::round(out_hit.x - closest_hit.normal.x * 0.02f);
            int by = std::round(out_hit.y - closest_hit.normal.y * 0.02f);
            int bz = std::round(out_hit.z - closest_hit.normal.z * 0.02f);
            out_solid = { (float)bx, (float)by, (float)bz };
            
            // Determinar cara para colocar bloque adyacente
            Vector3 n = closest_hit.normal;
            Vector3 card = {0, 0, 0};
            if (std::abs(n.y) >= std::abs(n.x) && std::abs(n.y) >= std::abs(n.z)) {
                card.y = (n.y > 0) ? 1.0f : -1.0f;
            } else if (std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z)) {
                card.x = (n.x > 0) ? 1.0f : -1.0f;
            } else {
                card.z = (n.z > 0) ? 1.0f : -1.0f;
            }
            out_empty = Vector3Add(out_solid, card);
            return true;
        }

        // El rayo impactó en el terreno natural Marching Cubes
        int ix = std::floor(out_hit.x);
        int iy = std::floor(out_hit.y);
        int iz = std::floor(out_hit.z);
        
        float best_dist_solid = 9999.0f;
        Vector3 best_solid = { (float)ix, (float)iy, (float)iz };
        
        float best_dist_empty = 9999.0f;
        Vector3 best_empty = { (float)ix, (float)iy, (float)iz };
        
        for (int dx = 0; dx <= 1; dx++) {
            for (int dy = 0; dy <= 1; dy++) {
                for (int dz = 0; dz <= 1; dz++) {
                    int nx = ix + dx;
                    int ny = iy + dy;
                    int nz = iz + dz;
                    
                    if (ny >= 0 && ny < Config::GRID_Y) {
                        float den = world.get_density(nx, ny, nz);
                        Vector3 node_pos = { (float)nx, (float)ny, (float)nz };
                        float dist = Vector3Distance(out_hit, node_pos);
                        
                        if (den >= Config::ISO_SURFACE) {
                            if (dist < best_dist_solid) {
                                best_dist_solid = dist;
                                best_solid = node_pos;
                            }
                        } else {
                            if (dist < best_dist_empty) {
                                best_dist_empty = dist;
                                best_empty = node_pos;
                            }
                        }
                    }
                }
            }
        }
        
        out_solid = best_solid;
        out_empty = best_empty;
        return true;
    }
    
    return false;
}

void DrawSkybox(Camera3D camera, Texture2D side, Texture2D top, Texture2D bottom, float sky_intensity) {
    rlDisableDepthMask();
    rlDisableDepthTest();
    
    unsigned char c = (unsigned char)(255.0f * sky_intensity);
    rlPushMatrix();
        rlTranslatef(camera.position.x, camera.position.y, camera.position.z);
        float s = 500.0f * 1.01f;
        float u0 = 0.005f;
        float u1 = 0.995f;
        
        // Top
        rlSetTexture(top.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, s, -s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, s, -s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, s, s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, s, s);
        rlEnd();
        
        // Bottom
        rlSetTexture(bottom.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, -s, s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, -s, s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, -s, -s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, -s, -s);
        rlEnd();
        
        // Front
        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, s, -s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, -s, -s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, -s, -s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, s, -s);
        rlEnd();
        
        // Right
        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(s, s, -s);
            rlTexCoord2f(u0, u1); rlVertex3f(s, -s, -s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, -s, s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, s, s);
        rlEnd();
        
        // Back
        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(s, s, s);
            rlTexCoord2f(u0, u1); rlVertex3f(s, -s, s);
            rlTexCoord2f(u1, u1); rlVertex3f(-s, -s, s);
            rlTexCoord2f(u1, u0); rlVertex3f(-s, s, s);
        rlEnd();
        
        // Left
        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, s, s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, -s, s);
            rlTexCoord2f(u1, u1); rlVertex3f(-s, -s, -s);
            rlTexCoord2f(u1, u0); rlVertex3f(-s, s, -s);
        rlEnd();
        
        rlSetTexture(0);
    rlPopMatrix();
    
    rlEnableDepthTest();
    rlEnableDepthMask();
}

void DrawFirstPersonViewmodel(
    UI& ui,
    Texture2D spritesheet_tiles,
    Texture2D spritesheet_items,
    float light_intensity,
    bool is_mining,
    float mining_progress,
    bool is_grounded,
    float dt
) {
    static float walk_bob_timer = 0.0f;
    static float bob_x = 0.0f, bob_y = 0.0f;
    static float swing_timer = 0.0f;
    static int prev_slot = -1;
    static int prev_tool_idx = -2;
    static float equip_anim = 1.0f;

    bool is_moving = is_grounded && (IsKeyDown(KEY_W) || IsKeyDown(KEY_A) || IsKeyDown(KEY_S) || IsKeyDown(KEY_D));
    if (is_moving) {
        walk_bob_timer += dt * 9.0f;
        float target_bob_x = std::sin(walk_bob_timer) * 0.012f;
        float target_bob_y = std::abs(std::cos(walk_bob_timer)) * 0.010f;
        bob_x = Lerp(bob_x, target_bob_x, 15.0f * dt);
        bob_y = Lerp(bob_y, target_bob_y, 15.0f * dt);
    } else {
        bob_x = Lerp(bob_x, 0.0f, 10.0f * dt);
        bob_y = Lerp(bob_y, 0.0f, 10.0f * dt);
    }

    int current_tool_idx = (ui.selected_slot == 0) ? ui.selected_tool_idx : -2;
    if (ui.selected_slot != prev_slot || current_tool_idx != prev_tool_idx) {
        prev_slot = ui.selected_slot;
        prev_tool_idx = current_tool_idx;
        equip_anim = 0.0f;
    }
    if (equip_anim < 1.0f) {
        equip_anim = std::min(1.0f, equip_anim + dt * 6.0f);
    }
    float equip_y = (1.0f - equip_anim) * -0.32f;

    if (is_mining) {
        swing_timer += dt * 6.0f;
        if (swing_timer >= 1.0f) swing_timer -= 1.0f;
    } else {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && swing_timer <= 0.0f) {
            swing_timer = 0.01f;
        }
        if (swing_timer > 0.0f) {
            swing_timer += dt * 5.0f;
            if (swing_timer >= 1.0f) swing_timer = 0.0f;
        }
    }
    float swing_sin = (swing_timer > 0.0f) ? std::sin(swing_timer * PI) : 0.0f;

    Camera3D vm_cam = { 0 };
    vm_cam.position = { 0.0f, 0.0f, 0.0f };
    vm_cam.target = { 0.0f, 0.0f, 1.0f };
    vm_cam.up = { 0.0f, 1.0f, 0.0f };
    vm_cam.fovy = 54.0f;
    vm_cam.projection = CAMERA_PERSPECTIVE;

    BeginMode3D(vm_cam);
    rlDrawRenderBatchActive();
    glClear(GL_DEPTH_BUFFER_BIT);
    rlEnableDepthTest();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    rlEnableColorBlend();

    float light = std::clamp(light_intensity, 0.25f, 1.0f);

    rlPushMatrix();
        // Posición visible y prominente de la mano DERECHA
        rlTranslatef(-0.28f - bob_x, -0.20f + bob_y + equip_y, 0.50f);
        
        if (swing_sin > 0.0f) {
            rlTranslatef(swing_sin * 0.08f, swing_sin * 0.04f, -swing_sin * 0.06f);
            rlRotatef(swing_sin * 38.0f, 1.0f, 0.0f, 0.0f);
            rlRotatef(swing_sin * 20.0f, 0.0f, 1.0f, 0.0f);
            rlRotatef(-swing_sin * 14.0f, 0.0f, 0.0f, 1.0f);
        }

        // Rotación de reposo orientada naturalmente hacia el frente-centro
        rlRotatef(-14.0f, 1.0f, 0.0f, 0.0f);
        rlRotatef(14.0f, 0.0f, 1.0f, 0.0f);
        rlRotatef(-4.0f, 0.0f, 0.0f, 1.0f);

        // --- DIBUJAR BRAZO DEL JUGADOR (Winding CCW exacto) ---
        auto draw_shaded_box = [&](float x0, float y0, float z0, float x1, float y1, float z1, Color base_col) {
            Color c_top = ColorAlpha(ColorBrightness(base_col, 0.06f), 1.0f);
            Color c_front = ColorAlpha(ColorBrightness(base_col, -0.04f), 1.0f);
            Color c_right = ColorAlpha(ColorBrightness(base_col, -0.10f), 1.0f);
            Color c_left = ColorAlpha(ColorBrightness(base_col, -0.16f), 1.0f);
            Color c_bot = ColorAlpha(ColorBrightness(base_col, -0.28f), 1.0f);

            auto mul_light = [&](Color c) -> Color {
                return Color{ (unsigned char)(c.r * light), (unsigned char)(c.g * light), (unsigned char)(c.b * light), c.a };
            };
            c_top = mul_light(c_top);
            c_front = mul_light(c_front);
            c_right = mul_light(c_right);
            c_left = mul_light(c_left);
            c_bot = mul_light(c_bot);

            rlSetTexture(0);
            rlBegin(RL_QUADS);
                // Top (+Y)
                rlColor4ub(c_top.r, c_top.g, c_top.b, c_top.a);
                rlVertex3f(x0, y1, z0); rlVertex3f(x0, y1, z1); rlVertex3f(x1, y1, z1); rlVertex3f(x1, y1, z0);
                // Bottom (-Y)
                rlColor4ub(c_bot.r, c_bot.g, c_bot.b, c_bot.a);
                rlVertex3f(x0, y0, z1); rlVertex3f(x0, y0, z0); rlVertex3f(x1, y0, z0); rlVertex3f(x1, y0, z1);
                // Front (+Z)
                rlColor4ub(c_front.r, c_front.g, c_front.b, c_front.a);
                rlVertex3f(x0, y0, z1); rlVertex3f(x1, y0, z1); rlVertex3f(x1, y1, z1); rlVertex3f(x0, y1, z1);
                // Back (-Z)
                rlColor4ub(c_front.r, c_front.g, c_front.b, c_front.a);
                rlVertex3f(x1, y0, z0); rlVertex3f(x0, y0, z0); rlVertex3f(x0, y1, z0); rlVertex3f(x1, y1, z0);
                // Right (+X)
                rlColor4ub(c_right.r, c_right.g, c_right.b, c_right.a);
                rlVertex3f(x1, y0, z1); rlVertex3f(x1, y0, z0); rlVertex3f(x1, y1, z0); rlVertex3f(x1, y1, z1);
                // Left (-X)
                rlColor4ub(c_left.r, c_left.g, c_left.b, c_left.a);
                rlVertex3f(x0, y0, z0); rlVertex3f(x0, y0, z1); rlVertex3f(x0, y1, z1); rlVertex3f(x0, y1, z0);
            rlEnd();
        };

        // Manga (Sleeve): Cyan / Teal
        draw_shaded_box(-0.065f, -0.065f, -0.50f, 0.065f, 0.065f, -0.16f, Color{ 0, 155, 175, 255 });
        // Mano y antebrazo (Skin)
        draw_shaded_box(-0.060f, -0.060f, -0.16f, 0.060f, 0.060f, 0.06f, Color{ 215, 155, 125, 255 });

        // Identificar qué elemento sostiene
        uint8_t held_block = AIR;
        uint8_t held_item = 255;
        Config::ToolType held_tool_type = Config::TOOL_COUNT;
        Config::ToolTier held_tool_tier = Config::TIER_COUNT;
        bool holding_tool = false;
        bool holding_item = false;
        bool holding_block = false;

        if (ui.selected_slot == 0) {
            ToolSlot* t = ui.get_active_tool();
            if (t != nullptr) {
                holding_tool = true;
                held_tool_type = t->type;
                held_tool_tier = t->tier;
            }
        } else if (ui.selected_slot >= 1 && ui.selected_slot <= 9) {
            const InventorySlot& s = ui.slots[ui.selected_slot];
            if (s.count > 0) {
                if (s.id == 254) {
                    for (auto& [iid, itype] : Config::ITEMS) {
                        if (itype.name == s.name) {
                            held_item = iid;
                            holding_item = true;
                            break;
                        }
                    }
                } else if (s.id != AIR && Config::BLOCKS.count(s.id)) {
                    held_block = s.id;
                    holding_block = true;
                }
            }
        }

        float tw = 1.0f / (float)Config::TILES_ATLAS_COLS;
        float th = 1.0f / (float)Config::TILES_ATLAS_ROWS;
        auto get_tile_uv = [&](int tx, int ty) -> std::array<Vector2, 4> {
            float u0 = (float)tx * tw;
            float v0 = ((float)Config::TILES_ATLAS_ROWS - 1.0f - (float)ty) * th;
            float u1 = u0 + tw;
            float v1 = v0 + th;
            return { Vector2{u0, v1}, Vector2{u1, v1}, Vector2{u1, v0}, Vector2{u0, v0} };
        };

        float itw = 1.0f / (float)Config::ITEMS_ATLAS_COLS;
        float ith = 1.0f / (float)Config::ITEMS_ATLAS_ROWS;
        auto get_item_uv = [&](int ix, int iy) -> std::array<Vector2, 4> {
            float u0 = (float)ix * itw;
            float v0 = (float)iy * ith;
            float u1 = u0 + itw;
            float v1 = v0 + ith;
            return { Vector2{u0, v1}, Vector2{u1, v1}, Vector2{u1, v0}, Vector2{u0, v0} };
        };

        auto draw_tex_box = [&](float x0, float y0, float z0, float x1, float y1, float z1,
                                 int top_tx, int top_ty, int bot_tx, int bot_ty,
                                 int front_tx, int front_ty, int back_tx, int back_ty,
                                 int right_tx, int right_ty, int left_tx, int left_ty) {
            rlSetTexture(spritesheet_tiles.id);
            rlBegin(RL_QUADS);
                // Top (+Y)
                unsigned char c_t = (unsigned char)(255 * light);
                rlColor4ub(c_t, c_t, c_t, 255);
                auto uvs = get_tile_uv(top_tx, top_ty);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y1, z1);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y1, z1);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z0);

                // Bottom (-Y)
                unsigned char c_b = (unsigned char)(140 * light);
                rlColor4ub(c_b, c_b, c_b, 255);
                uvs = get_tile_uv(bot_tx, bot_ty);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z1);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y0, z0);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y0, z0);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);

                // Front (+Z)
                unsigned char c_f = (unsigned char)(215 * light);
                rlColor4ub(c_f, c_f, c_f, 255);
                uvs = get_tile_uv(front_tx, front_ty);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z1);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z1);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z1);

                // Back (-Z)
                unsigned char c_bk = (unsigned char)(190 * light);
                rlColor4ub(c_bk, c_bk, c_bk, 255);
                uvs = get_tile_uv(back_tx, back_ty);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z0);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z0);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z0);

                // Right (+X)
                unsigned char c_r = (unsigned char)(200 * light);
                rlColor4ub(c_r, c_r, c_r, 255);
                uvs = get_tile_uv(right_tx, right_ty);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x1, y0, z0);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x1, y1, z0);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z1);

                // Left (-X)
                unsigned char c_l = (unsigned char)(175 * light);
                rlColor4ub(c_l, c_l, c_l, 255);
                uvs = get_tile_uv(left_tx, left_ty);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z0);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x0, y0, z1);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x0, y1, z1);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
            rlEnd();
            rlSetTexture(0);
        };

        // === ITEM/HERRAMIENTA/BLOQUE 3D EN LA MANO (UNIVERSAL) ===
        // Editar estos valores para ajustar posición, rotación y tamaño
        Vector3 held_pos = { -0.015f, -0.01f, -0.01f };
        Vector3 held_rot = { -40.0f, -80.0f, 0.0f };
        Vector3 held_scale = { 0.30f, 0.30f, 0.30f };

        if (holding_tool) {
            unsigned char c_tool = (unsigned char)(255 * light);
            Color tint = { c_tool, c_tool, c_tool, 255 };
            ItemModel3D::get().draw_tool(held_tool_type, held_tool_tier, held_pos, held_rot, held_scale, tint);
        }
        else if (holding_item && held_item != 255 && Config::ITEMS.count(held_item)) {
            unsigned char c_item = (unsigned char)(255 * light);
            Color tint = { c_item, c_item, c_item, 255 };
            ItemModel3D::get().draw_item(held_item, held_pos, held_rot, held_scale, tint);
        }
        else if (holding_block && held_block != AIR && Config::BLOCKS.count(held_block)) {
            const auto& bt = Config::BLOCKS.at(held_block);
            bool is_plant = bt.transparent && (held_block == Config::TALL_GRASS || held_block == Config::RED_MUSHROOM || held_block == Config::BROWN_MUSHROOM || held_block == Config::DEAD_BUSH);

            if (is_plant) {
                // === PLANTA CROSS-QUAD 3D ===
                auto uvs = get_tile_uv(bt.tex_x, bt.tex_y);
                unsigned char c_plant = (unsigned char)(255 * light);

                rlPushMatrix();
                    rlTranslatef(0.01f, 0.06f, 0.04f);
                    float ps_w = 0.20f;
                    float ps_h = 0.28f;
                    rlSetTexture(spritesheet_tiles.id);
                    rlBegin(RL_QUADS);
                        rlColor4ub(c_plant, c_plant, c_plant, 255);
                        // Quad 1 (CCW)
                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h,  ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h, -ps_w*0.5f);

                        // Quad 1 back (CCW)
                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h, -ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h,  ps_w*0.5f);

                        // Quad 2 (CCW)
                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h, -ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h,  ps_w*0.5f);

                        // Quad 2 back (CCW)
                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h,  ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h, -ps_w*0.5f);
                    rlEnd();
                    rlSetTexture(0);
                rlPopMatrix();
            }
            else if (bt.shape != Config::SHAPE_TERRAIN) {
                // === BLOQUE DE CONSTRUCCIÓN: CUBO 3D / MODELO ISOMÉTRICO ===
                int def_tx = bt.tex_x, def_ty = bt.tex_y;
                int top_tx = bt.tex_top_x >= 0 ? bt.tex_top_x : def_tx, top_ty = bt.tex_top_y >= 0 ? bt.tex_top_y : def_ty;
                int bot_tx = bt.tex_bottom_x >= 0 ? bt.tex_bottom_x : def_tx, bot_ty = bt.tex_bottom_y >= 0 ? bt.tex_bottom_y : def_ty;
                int front_tx = bt.tex_front_x >= 0 ? bt.tex_front_x : def_tx, front_ty = bt.tex_front_y >= 0 ? bt.tex_front_y : def_ty;

                rlPushMatrix();
                    rlTranslatef(0.02f, 0.06f, 0.06f);
                    rlRotatef(45.0f, 0.0f, 1.0f, 0.0f);
                    rlRotatef(22.0f, 1.0f, 0.0f, 0.0f);
                    rlRotatef(-10.0f, 0.0f, 0.0f, 1.0f);

                    float scale = 0.72f; // Aumentado un 20% (0.60 * 1.20)
                    if (!bt.elements.empty()) {
                        float unit = 0.16f * scale / 16.0f;
                        for (const auto& elem : bt.elements) {
                            float x0 = (elem.from.x - 8.0f) * unit;
                            float y0 = (elem.from.y - 8.0f) * unit;
                            float z0 = (elem.from.z - 8.0f) * unit;
                            float x1 = (elem.to.x - 8.0f) * unit;
                            float y1 = (elem.to.y - 8.0f) * unit;
                            float z1 = (elem.to.z - 8.0f) * unit;

                            int top_x = elem.faces[Config::FACE_UP].tex_x >= 0 ? elem.faces[Config::FACE_UP].tex_x : def_tx;
                            int top_y = elem.faces[Config::FACE_UP].tex_y >= 0 ? elem.faces[Config::FACE_UP].tex_y : def_ty;
                            int bot_x = elem.faces[Config::FACE_DOWN].tex_x >= 0 ? elem.faces[Config::FACE_DOWN].tex_x : def_tx;
                            int bot_y = elem.faces[Config::FACE_DOWN].tex_y >= 0 ? elem.faces[Config::FACE_DOWN].tex_y : def_ty;
                            int front_x = elem.faces[Config::FACE_SOUTH].tex_x >= 0 ? elem.faces[Config::FACE_SOUTH].tex_x : def_tx;
                            int front_y = elem.faces[Config::FACE_SOUTH].tex_y >= 0 ? elem.faces[Config::FACE_SOUTH].tex_y : def_ty;
                            int back_x = elem.faces[Config::FACE_NORTH].tex_x >= 0 ? elem.faces[Config::FACE_NORTH].tex_x : def_tx;
                            int back_y = elem.faces[Config::FACE_NORTH].tex_y >= 0 ? elem.faces[Config::FACE_NORTH].tex_y : def_ty;
                            int left_x = elem.faces[Config::FACE_WEST].tex_x >= 0 ? elem.faces[Config::FACE_WEST].tex_x : def_tx;
                            int left_y = elem.faces[Config::FACE_WEST].tex_y >= 0 ? elem.faces[Config::FACE_WEST].tex_y : def_ty;
                            int right_x = elem.faces[Config::FACE_EAST].tex_x >= 0 ? elem.faces[Config::FACE_EAST].tex_x : def_tx;
                            int right_y = elem.faces[Config::FACE_EAST].tex_y >= 0 ? elem.faces[Config::FACE_EAST].tex_y : def_ty;

                            draw_tex_box(x0, y0, z0, x1, y1, z1, top_x, top_y, bot_x, bot_y, front_x, front_y, back_x, back_y, right_x, right_y, left_x, left_y);
                        }
                    } else if (bt.shape == Config::SHAPE_TORCH) {
                        float cs = 0.11f;
                        draw_tex_box(-0.02f, -0.14f, -0.02f, 0.02f, 0.14f, 0.02f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                    } else if (bt.shape == Config::SHAPE_STAIRS) {
                        float cs = 0.06f * scale;
                        draw_tex_box(-cs, -cs, -cs, cs, 0.0f, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                        draw_tex_box(-cs, 0.0f, 0.0f, cs, cs, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                    } else if (bt.shape == Config::SHAPE_CHEST) {
                        float cs = 0.07f * scale;
                        int latch_tx = bt.tex_latch_x >= 0 ? bt.tex_latch_x : 2;
                        int latch_ty = bt.tex_latch_y >= 0 ? bt.tex_latch_y : 3;
                        draw_tex_box(-0.02f*scale, -0.02f*scale, cs, 0.02f*scale, 0.04f*scale, cs + 0.02f*scale, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty);
                        draw_tex_box(-cs, -cs, -cs, cs, cs, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                    } else {
                        float cs = 0.08f * scale;
                        draw_tex_box(-cs, -cs, -cs, cs, cs, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                    }
                rlPopMatrix();
            }
            else {
                // === BLOQUE NATURAL: ROCA FACETADA 8 LADOS ===
                int def_tx = bt.tex_x, def_ty = bt.tex_y;
                int top_tx = (bt.tex_top_x >= 0) ? bt.tex_top_x : def_tx;
                int top_ty = (bt.tex_top_y >= 0) ? bt.tex_top_y : def_ty;
                int bot_tx = (bt.tex_bottom_x >= 0) ? bt.tex_bottom_x : def_tx;
                int bot_ty = (bt.tex_bottom_y >= 0) ? bt.tex_bottom_y : def_ty;

                if (held_block == Config::GRASS) {
                    top_tx = 6; top_ty = 8;
                    def_tx = 6; def_ty = 8;
                    bot_tx = 7; bot_ty = 4;
                }

                rlPushMatrix();
                    rlTranslatef(0.02f, 0.06f, 0.06f);
                    rlRotatef(32.0f, 0.0f, 1.0f, 0.0f);
                    rlRotatef(18.0f, 1.0f, 0.0f, 0.0f);

                    float n_scale = 1.20f; // Aumentado un 20%
                    float r_top = 0.04f * n_scale;
                    float r_mid = 0.06f * n_scale;
                    float r_bot = 0.045f * n_scale;
                    float y_peak = +0.06f * n_scale;
                    float y_top  = +0.035f * n_scale;
                    float y_mid  =  0.00f;
                    float y_bot  = -0.04f * n_scale;
                    
                    auto uvs_top = get_tile_uv(top_tx, top_ty);
                    auto uvs_mid = get_tile_uv(def_tx, def_ty);
                    auto uvs_bot = get_tile_uv(bot_tx, bot_ty);

                    unsigned char c_t = (unsigned char)(255 * light);
                    unsigned char c_s = (unsigned char)(200 * light);
                    unsigned char c_ls = (unsigned char)(160 * light);
                    unsigned char c_b = (unsigned char)(120 * light);

                    rlSetTexture(spritesheet_tiles.id);
                    rlBegin(RL_QUADS);

                    // 1. Tapa Superior (Apex -> Apex -> Top-1 -> Top-0)
                    for(int i=0; i<8; i++) {
                        int next = (i+1)%8;
                        float a0 = i * (PI / 4.0f);
                        float a1 = next * (PI / 4.0f);
                        
                        float u_cen_top = (uvs_top[0].x + uvs_top[1].x) * 0.5f;
                        float v_cen_top = (uvs_top[0].y + uvs_top[2].y) * 0.5f;
                        float uv_x0 = u_cen_top + std::cos(a0) * (tw * 0.45f);
                        float uv_y0 = v_cen_top - std::sin(a0) * (th * 0.45f);
                        float uv_x1 = u_cen_top + std::cos(a1) * (tw * 0.45f);
                        float uv_y1 = v_cen_top - std::sin(a1) * (th * 0.45f);

                        rlColor4ub(c_t, c_t, c_t, 255);
                        rlTexCoord2f(u_cen_top, v_cen_top); rlVertex3f(0.0f, y_peak, 0.0f);
                        rlTexCoord2f(u_cen_top, v_cen_top); rlVertex3f(0.0f, y_peak, 0.0f);
                        rlTexCoord2f(uv_x1, uv_y1); rlVertex3f(r_top * std::cos(a1), y_top, r_top * std::sin(a1));
                        rlTexCoord2f(uv_x0, uv_y0); rlVertex3f(r_top * std::cos(a0), y_top, r_top * std::sin(a0));
                    }

                    // 2. Caras Laterales Superiores (Top-0 -> Top-1 -> Mid-1 -> Mid-0)
                    for(int i=0; i<8; i++) {
                        int next = (i+1)%8;
                        float a0 = i * (PI / 4.0f);
                        float a1 = next * (PI / 4.0f);
                        
                        rlColor4ub(c_s, c_s, c_s, 255);
                        rlTexCoord2f(uvs_mid[3].x, uvs_mid[3].y); rlVertex3f(r_top * std::cos(a0), y_top, r_top * std::sin(a0));
                        rlTexCoord2f(uvs_mid[2].x, uvs_mid[2].y); rlVertex3f(r_top * std::cos(a1), y_top, r_top * std::sin(a1));
                        rlTexCoord2f(uvs_mid[1].x, uvs_mid[1].y); rlVertex3f(r_mid * std::cos(a1), y_mid, r_mid * std::sin(a1));
                        rlTexCoord2f(uvs_mid[0].x, uvs_mid[0].y); rlVertex3f(r_mid * std::cos(a0), y_mid, r_mid * std::sin(a0));
                    }

                    // 3. Caras Laterales Inferiores (Mid-0 -> Mid-1 -> Bot-1 -> Bot-0)
                    for(int i=0; i<8; i++) {
                        int next = (i+1)%8;
                        float a0 = i * (PI / 4.0f);
                        float a1 = next * (PI / 4.0f);
                        
                        rlColor4ub(c_ls, c_ls, c_ls, 255);
                        rlTexCoord2f(uvs_mid[3].x, uvs_mid[3].y); rlVertex3f(r_mid * std::cos(a0), y_mid, r_mid * std::sin(a0));
                        rlTexCoord2f(uvs_mid[2].x, uvs_mid[2].y); rlVertex3f(r_mid * std::cos(a1), y_mid, r_mid * std::sin(a1));
                        rlTexCoord2f(uvs_mid[1].x, uvs_mid[1].y); rlVertex3f(r_bot * std::cos(a1), y_bot, r_bot * std::sin(a1));
                        rlTexCoord2f(uvs_mid[0].x, uvs_mid[0].y); rlVertex3f(r_bot * std::cos(a0), y_bot, r_bot * std::sin(a0));
                    }

                    // 4. Base Inferior (Center -> Center -> Bot-0 -> Bot-1)
                    for(int i=0; i<8; i++) {
                        int next = (i+1)%8;
                        float a0 = i * (PI / 4.0f);
                        float a1 = next * (PI / 4.0f);
                        
                        float u_cen_bot = (uvs_bot[0].x + uvs_bot[1].x) * 0.5f;
                        float v_cen_bot = (uvs_bot[0].y + uvs_bot[2].y) * 0.5f;
                        float uv_x0 = u_cen_bot + std::cos(a0) * (tw * 0.45f);
                        float uv_y0 = v_cen_bot - std::sin(a0) * (th * 0.45f);
                        float uv_x1 = u_cen_bot + std::cos(a1) * (tw * 0.45f);
                        float uv_y1 = v_cen_bot - std::sin(a1) * (th * 0.45f);

                        rlColor4ub(c_b, c_b, c_b, 255);
                        rlTexCoord2f(u_cen_bot, v_cen_bot); rlVertex3f(0.0f, y_bot, 0.0f);
                        rlTexCoord2f(u_cen_bot, v_cen_bot); rlVertex3f(0.0f, y_bot, 0.0f);
                        rlTexCoord2f(uv_x0, uv_y0); rlVertex3f(r_bot * std::cos(a0), y_bot, r_bot * std::sin(a0));
                        rlTexCoord2f(uv_x1, uv_y1); rlVertex3f(r_bot * std::cos(a1), y_bot, r_bot * std::sin(a1));
                    }

                    rlEnd();
                    rlSetTexture(0);
                rlPopMatrix();
            }
        }

    rlPopMatrix();

    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    EndMode3D();
}

World* g_world = nullptr;

int main() {
    InitWindow(1280, 720, "Smooth Voxel Engine C++");
    SetExitKey(0);
    SetTargetFPS(MAX_FPS); 
    DisableCursor();

    BlockRegistry::load_all("assets/data");
    
    ResourcePackManager res_pack;
    res_pack.build_defaults(".");
    
    Texture2D spritesheet = res_pack.get_tiles_atlas();
    Texture2D spritesheet_items = res_pack.get_items_atlas();
    
    Texture2D tex_sun = res_pack.get_sun();
    Texture2D tex_moon = res_pack.get_moon();
    Texture2D tex_clouds = res_pack.get_clouds();
    Texture2D sky_side = res_pack.get_sky_side();
    Texture2D sky_top = res_pack.get_sky_top();
    Texture2D sky_bottom = res_pack.get_sky_bottom();
    

    Shader terrainSolidShader = LoadShader("assets/shaders/terrain.vs", "assets/shaders/terrain_solid.fs");
    Shader terrainPlantsShader = LoadShader("assets/shaders/terrain.vs", "assets/shaders/terrain.fs");
    Shader waterShader = LoadShader("assets/shaders/water.vs", "assets/shaders/water.fs");
    Shader skyboxShader = LoadShader("assets/shaders/skybox.vs", "assets/shaders/skybox.fs");
    
    int skyboxSunDirLoc = GetShaderLocation(skyboxShader, "sunDir");
    int terrainSolidSunDirLoc = GetShaderLocation(terrainSolidShader, "sunDir");
    int terrainPlantsSunDirLoc = GetShaderLocation(terrainPlantsShader, "sunDir");
    int waterSunDirLoc = GetShaderLocation(waterShader, "sunDir");
    
    int timeLocSolid = GetShaderLocation(terrainSolidShader, "time");
    int camPosLocSolid = GetShaderLocation(terrainSolidShader, "cameraPos");
    int timeLocPlants = GetShaderLocation(terrainPlantsShader, "time");
    int camPosLocPlants = GetShaderLocation(terrainPlantsShader, "cameraPos");
    int timeLocWater = GetShaderLocation(waterShader, "time");
    int camPosLocWater = GetShaderLocation(waterShader, "cameraPos");
    
    int fogLocSolid = GetShaderLocation(terrainSolidShader, "fogColor");
    int fogLocPlants = GetShaderLocation(terrainPlantsShader, "fogColor");
    int fogLocWater = GetShaderLocation(waterShader, "fogColor");
    int fogStartLocSolid = GetShaderLocation(terrainSolidShader, "fogStart");
    int fogEndLocSolid = GetShaderLocation(terrainSolidShader, "fogEnd");
    int fogStartLocPlants = GetShaderLocation(terrainPlantsShader, "fogStart");
    int fogEndLocPlants = GetShaderLocation(terrainPlantsShader, "fogEnd");
    int fogStartLocWater = GetShaderLocation(waterShader, "fogStart");
    int fogEndLocWater = GetShaderLocation(waterShader, "fogEnd");
    
    // Generate Perlin Noise Texture for Water
    Image noiseImg = GenImagePerlinNoise(256, 256, 0, 0, 4.0f);
    Texture2D noiseTex = LoadTextureFromImage(noiseImg);
    UnloadImage(noiseImg);
    SetTextureFilter(noiseTex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(noiseTex, TEXTURE_WRAP_REPEAT);
    
    Material mat_solid = LoadMaterialDefault();
    mat_solid.shader = terrainSolidShader;
    mat_solid.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
    
    Material mat_plants = LoadMaterialDefault();
    mat_plants.shader = terrainPlantsShader;
    mat_plants.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
    
    Material mat_water = LoadMaterialDefault();
    mat_water.shader = waterShader;
    mat_water.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
    mat_water.maps[MATERIAL_MAP_DIFFUSE].color = { 255, 255, 255, 200 };
    
    // Bind noiseTex to the shader's "noiseTex" uniform using a spare material map
    waterShader.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(waterShader, "noiseTex");
    mat_water.maps[MATERIAL_MAP_EMISSION].texture = noiseTex;

    World world(mat_solid, mat_plants, mat_water);
    g_world = &world;
    UI ui(spritesheet, spritesheet_items);
    Chat chat;
    ItemDropManager item_drops;
    MenuManager menu;
    
    std::string world_name = "world1";
    std::string save_dir = "worlds/" + world_name;
    std::filesystem::create_directories(save_dir);
    
    if (sqlite3_open_v2((save_dir + "/chunks.db").c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) == SQLITE_OK) {
        sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS chunks (cx INTEGER, cz INTEGER, chunk_data BLOB, PRIMARY KEY(cx, cz))", 0, 0, 0);
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS chests (x INTEGER, y INTEGER, z INTEGER, data TEXT, PRIMARY KEY(x, y, z))", 0, 0, 0);
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS furnaces (x INTEGER, y INTEGER, z INTEGER, data TEXT, PRIMARY KEY(x, y, z))", 0, 0, 0);

        // Cargar cofres guardados
        {
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, "SELECT x, y, z, data FROM chests", -1, &stmt, 0) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    int cx = sqlite3_column_int(stmt, 0);
                    int cy = sqlite3_column_int(stmt, 1);
                    int cz = sqlite3_column_int(stmt, 2);
                    const char* data_str = (const char*)sqlite3_column_text(stmt, 3);
                    if (data_str) {
                        try {
                            auto j = json::parse(data_str);
                            ChestData cd;
                            if (j.contains("slots") && j["slots"].is_array()) {
                                int idx = 0;
                                for (const auto& sj : j["slots"]) {
                                    if (idx >= 27) break;
                                    bool is_tool = sj.value("is_tool", false);
                                    cd.slots[idx].is_tool = is_tool;
                                    if (is_tool) {
                                        cd.slots[idx].tool.type = (Config::ToolType)sj.value("tool_type", 0);
                                        cd.slots[idx].tool.tier = (Config::ToolTier)sj.value("tool_tier", 0);
                                        cd.slots[idx].tool.durability_current = sj.value("dur_cur", 100);
                                        cd.slots[idx].tool.durability_max = sj.value("dur_max", 100);
                                        cd.slots[idx].tool.active = sj.value("active", true);
                                    } else {
                                        cd.slots[idx].item.id = sj.value("id", (uint8_t)Config::AIR);
                                        cd.slots[idx].item.count = sj.value("count", 0);
                                        cd.slots[idx].item.name = sj.value("name", "");
                                    }
                                    idx++;
                                }
                            }
                            ui.world_chests[std::make_tuple(cx, cy, cz)] = cd;
                        } catch (...) {}
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
    }

    Camera3D camera = { 0 };
    camera.position = { 0.0f, 100.0f, 0.0f };
    camera.target = { 0.0f, 100.0f, 1.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 90.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float player_vel_y = 0.0f;
    bool is_grounded = false;
    float day_time = 1.5f; // Start at noon
    bool spectator_mode = false;
    bool show_chunks = false;
    bool show_debug_info = false;
    float smooth_step_offset = 0.0f;
    float mining_progress = 0.0f;
    Vector3 mining_target = {0, 0, 0};
    Vector3 mining_block = {0, 0, 0}; // coordenadas enteras del bloque siendo minado
    bool is_mining = false;

    std::ifstream file(save_dir + "/player.json");
    if (file.is_open()) {
        try {
            json pj = json::parse(file);
            camera.position.x = pj.value("pos_x", 0.0f);
            camera.position.y = pj.value("pos_y", 100.0f);
            camera.position.z = pj.value("pos_z", 0.0f);
            
            camera.target.x = pj.value("target_x", camera.position.x);
            camera.target.y = pj.value("target_y", camera.position.y);
            camera.target.z = pj.value("target_z", camera.position.z + 1.0f);
            
            day_time = pj.value("day_time", 1.5f);
            
            for (size_t i = 0; i < ui.slots.size(); i++) {
                std::string k_id = "slot_" + std::to_string(i) + "_id";
                std::string k_cnt = "slot_" + std::to_string(i) + "_count";
                std::string k_nm = "slot_" + std::to_string(i) + "_name";
                if (pj.contains(k_id)) {
                    ui.slots[i].id = (uint8_t)pj[k_id].get<int>();
                    ui.slots[i].count = pj.value(k_cnt, 0);
                    if (pj.contains(k_nm)) {
                        ui.slots[i].name = pj[k_nm].get<std::string>();
                    } else if (Config::BLOCKS.count(ui.slots[i].id)) {
                        ui.slots[i].name = Config::BLOCKS.at(ui.slots[i].id).name;
                    }
                }
            }
            
            // Load tools
            ui.tool_inventory.clear();
            int num_tools = pj.value("tool_count", 0);
            for (int t = 0; t < num_tools && t < 18; t++) {
                std::string k_type = "tool_" + std::to_string(t) + "_type";
                std::string k_tier = "tool_" + std::to_string(t) + "_tier";
                std::string k_dur = "tool_" + std::to_string(t) + "_dur";
                if (pj.contains(k_type) && pj.contains(k_tier)) {
                    int ttype = pj[k_type].get<int>();
                    int ttier = pj[k_tier].get<int>();
                    int tdur = pj.value(k_dur, 100);
                    if (ttype >= 0 && ttype < (int)Config::TOOL_COUNT && ttier >= 0 && ttier < (int)Config::TIER_COUNT) {
                        Config::ToolType type = (Config::ToolType)ttype;
                        Config::ToolTier tier = (Config::ToolTier)ttier;
                        int max_dur = 100;
                        for (auto& ti : Config::TOOLS) {
                            if (ti.type == type && ti.tier == tier) { max_dur = ti.durability; break; }
                        }
                        ui.tool_inventory.push_back({type, tier, tdur, max_dur, false});
                    }
                }
            }
            ui.selected_tool_idx = pj.value("selected_tool", 0);
            
            // Load storage (mochila)
            for (size_t i = 0; i < ui.storage.size(); i++) {
                std::string k_id = "storage_" + std::to_string(i) + "_id";
                std::string k_cnt = "storage_" + std::to_string(i) + "_count";
                std::string k_nm = "storage_" + std::to_string(i) + "_name";
                if (pj.contains(k_id)) {
                    ui.storage[i].id = (uint8_t)pj[k_id].get<int>();
                    ui.storage[i].count = pj.value(k_cnt, 0);
                    if (pj.contains(k_nm)) {
                        ui.storage[i].name = pj[k_nm].get<std::string>();
                    } else if (Config::BLOCKS.count(ui.storage[i].id)) {
                        ui.storage[i].name = Config::BLOCKS.at(ui.storage[i].id).name;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[SaveSystem] Error parsing player.json: " << e.what() << std::endl;
        }
    }

    auto save_player_data = [&]() {
        std::ofstream out(save_dir + "/player.json");
        if (out.is_open()) {
            json pj;
            pj["pos_x"] = camera.position.x;
            pj["pos_y"] = camera.position.y;
            pj["pos_z"] = camera.position.z;
            pj["target_x"] = camera.target.x;
            pj["target_y"] = camera.target.y;
            pj["target_z"] = camera.target.z;
            pj["day_time"] = day_time;

            for (size_t i = 0; i < ui.slots.size(); i++) {
                pj["slot_" + std::to_string(i) + "_id"] = (int)ui.slots[i].id;
                pj["slot_" + std::to_string(i) + "_count"] = ui.slots[i].count;
                pj["slot_" + std::to_string(i) + "_name"] = ui.slots[i].name;
            }

            pj["tool_count"] = ui.tool_inventory.size();
            for (size_t t = 0; t < ui.tool_inventory.size(); t++) {
                pj["tool_" + std::to_string(t) + "_type"] = (int)ui.tool_inventory[t].type;
                pj["tool_" + std::to_string(t) + "_tier"] = (int)ui.tool_inventory[t].tier;
                pj["tool_" + std::to_string(t) + "_dur"] = ui.tool_inventory[t].durability_current;
            }
            pj["selected_tool"] = ui.selected_tool_idx;

            for (size_t i = 0; i < ui.storage.size(); i++) {
                pj["storage_" + std::to_string(i) + "_id"] = (int)ui.storage[i].id;
                pj["storage_" + std::to_string(i) + "_count"] = ui.storage[i].count;
                pj["storage_" + std::to_string(i) + "_name"] = ui.storage[i].name;
            }

            out << pj.dump(2) << std::endl;
            out.close();
        }
    };

    while (!WindowShouldClose()) {
        bool ray_hit_valid = false;
        Vector3 hit = {0}, target_solid = {0}, target_empty = {0};

        if (IsKeyPressed(KEY_ESCAPE)) {
            if (menu.is_paused()) {
                menu.go_back();
            } else if (chat.is_open) {
                chat.toggle();
            } else if (ui.is_open) {
                ui.close_ui();
            } else {
                menu.open_pause();
                EnableCursor();
            }
        }
        
        static bool was_paused = false;
        if (menu.is_paused()) {
            menu.update();
            if (menu.wants_quit) break;
            was_paused = true;
        } else if (was_paused) {
            was_paused = false;
        }
        
        static std::string last_pack_path = MenuResourcePacks::get_active_pack_path();
        std::string current_pack = MenuResourcePacks::get_active_pack_path();
        if (current_pack != last_pack_path || (current_pack.empty() && res_pack.get_is_active()) || (!current_pack.empty() && !res_pack.get_is_active() && current_pack != res_pack.get_active_pack_path())) {
            last_pack_path = current_pack;
            if (!current_pack.empty()) {
                if (res_pack.apply_pack(current_pack)) {
                    spritesheet = res_pack.get_tiles_atlas();
                    spritesheet_items = res_pack.get_items_atlas();
                    tex_sun = res_pack.get_sun();
                    tex_moon = res_pack.get_moon();
                    tex_clouds = res_pack.get_clouds();
                    sky_side = res_pack.get_sky_side();
                    sky_top = res_pack.get_sky_top();
                    sky_bottom = res_pack.get_sky_bottom();
                    world.mat_solid.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
                    world.mat_plants.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
                    world.mat_water.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
                    ui.spritesheet = spritesheet;
                    ui.spritesheet_items = spritesheet_items;
                    world.invalidate_all_meshes();
                    chat.add_message("Paquete de recursos aplicado!");
                }
            } else {
                res_pack.clear_pack();
                BlockRegistry::load_all("assets/data");
                spritesheet = res_pack.get_tiles_atlas();
                spritesheet_items = res_pack.get_items_atlas();
                tex_sun = res_pack.get_sun();
                tex_moon = res_pack.get_moon();
                tex_clouds = res_pack.get_clouds();
                sky_side = res_pack.get_sky_side();
                sky_top = res_pack.get_sky_top();
                sky_bottom = res_pack.get_sky_bottom();
                world.mat_solid.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
                world.mat_plants.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
                world.mat_water.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
                ui.spritesheet = spritesheet;
                ui.spritesheet_items = spritesheet_items;
                world.invalidate_all_meshes();
                chat.add_message("Texturas por defecto restauradas");
            }
        }
        

        // Auto-guardado periódico cada 5 segundos
        static float autosave_timer = 0.0f;
        autosave_timer += GetFrameTime();
        if (autosave_timer >= 5.0f) {
            autosave_timer = 0.0f;
            world.save_all();
            save_player_data();
        }

        if (!ui.is_open && !chat.is_open && !menu.is_paused()) {
            if (IsKeyPressed(KEY_Q)) {
                Vector3 forward_dir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                Vector3 throw_pos = Vector3Add(camera.position, Vector3Scale(forward_dir, 0.6f));
                Vector3 throw_vel = Vector3Add(Vector3Scale(forward_dir, 5.0f), Vector3{ 0.0f, 2.0f, 0.0f });

                if (ui.selected_slot == 0) {
                    ToolSlot* tool = ui.get_active_tool();
                    if (tool && tool->durability_current > 0) {
                        item_drops.spawn_tool(throw_pos, tool->type, tool->tier, tool->durability_current, throw_vel, 0.8f);
                        ui.remove_active_tool();
                    }
                } else if (ui.selected_slot > 0 && ui.selected_slot < (int)ui.slots.size()) {
                    if (ui.slots[ui.selected_slot].count > 0 && ui.slots[ui.selected_slot].id != Config::AIR) {
                        uint8_t drop_id = ui.slots[ui.selected_slot].id;
                        ui.slots[ui.selected_slot].count--;
                        if (ui.slots[ui.selected_slot].count <= 0) {
                            ui.slots[ui.selected_slot].id = Config::AIR;
                            ui.slots[ui.selected_slot].name = "";
                        }
                        item_drops.spawn(throw_pos, drop_id, false, 1, throw_vel, 0.8f);
                    }
                }
            }

            Vector3 old_pos = camera.position;
            float dt_cam = GetFrameTime();
            Vector2 mouse_delta = GetMouseDelta();
            float mouse_sensitivity = 0.15f; // Sensibilidad cómoda y fluida
            Vector3 rotation = { mouse_delta.x * mouse_sensitivity, mouse_delta.y * mouse_sensitivity, 0.0f };

            if (spectator_mode) {
                float spec_speed = (IsKeyDown(KEY_LEFT_SHIFT) ? 40.0f : 15.0f) * dt_cam;
                Vector3 movement = { 0.0f, 0.0f, 0.0f };
                if (IsKeyDown(KEY_W)) movement.x += spec_speed;
                if (IsKeyDown(KEY_S)) movement.x -= spec_speed;
                if (IsKeyDown(KEY_D)) movement.y += spec_speed;
                if (IsKeyDown(KEY_A)) movement.y -= spec_speed;
                if (IsKeyDown(KEY_SPACE)) movement.z += spec_speed;
                if (IsKeyDown(KEY_LEFT_CONTROL)) movement.z -= spec_speed;
                UpdateCameraPro(&camera, movement, rotation, 0.0f);
            } else {
                bool is_sprinting = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SHIFT);
                float walk_speed = (is_sprinting ? 8.5f : 4.5f) * dt_cam;
                Vector3 movement = { 0.0f, 0.0f, 0.0f };
                if (IsKeyDown(KEY_W)) movement.x += walk_speed;
                if (IsKeyDown(KEY_S)) movement.x -= walk_speed;
                if (IsKeyDown(KEY_D)) movement.y += walk_speed;
                if (IsKeyDown(KEY_A)) movement.y -= walk_speed;
                UpdateCameraPro(&camera, movement, rotation, 0.0f);
            }

            Vector3 post_update_pos = camera.position;
            
            if (!spectator_mode) {
                int cx = std::floor(camera.position.x / Config::CHUNK_SIZE);
                int cz = std::floor(camera.position.z / Config::CHUNK_SIZE);
                Chunk* current_chunk = world.get_chunk(cx, cz);
                
                if (current_chunk && current_chunk->is_ready) {
                    float r = 0.28f; // Radio horizontal del jugador

                    // === Interpolación trilineal de la densidad del campo escalar ===
                    auto sample_density = [&](float x, float y, float z) -> float {
                        int ix = (int)std::floor(x);
                        int iy = (int)std::floor(y);
                        int iz = (int)std::floor(z);
                        float fx = x - ix;
                        float fy = y - iy;
                        float fz = z - iz;

                        float d000 = world.get_density(ix,   iy,   iz);
                        float d100 = world.get_density(ix+1, iy,   iz);
                        float d010 = world.get_density(ix,   iy+1, iz);
                        float d110 = world.get_density(ix+1, iy+1, iz);
                        float d001 = world.get_density(ix,   iy,   iz+1);
                        float d101 = world.get_density(ix+1, iy,   iz+1);
                        float d011 = world.get_density(ix,   iy+1, iz+1);
                        float d111 = world.get_density(ix+1, iy+1, iz+1);

                        float c00 = d000 * (1.0f - fx) + d100 * fx;
                        float c10 = d010 * (1.0f - fx) + d110 * fx;
                        float c01 = d001 * (1.0f - fx) + d101 * fx;
                        float c11 = d011 * (1.0f - fx) + d111 * fx;
                        float c0  = c00  * (1.0f - fz) + c01  * fz;
                        float c1  = c10  * (1.0f - fz) + c11  * fz;
                        return c0 * (1.0f - fy) + c1 * fy;
                    };

                    // === is_solid: híbrido (densidad para terreno, cúbico para construcción) ===
                    auto is_solid = [&](float x, float y, float z) -> bool {
                        int bx = (int)std::floor(x + 0.5f);
                        int by = (int)std::floor(y + 0.5f);
                        int bz = (int)std::floor(z + 0.5f);
                        uint8_t b = world.get_block(bx, by, bz);
                        if (b != AIR && b != WATER && BLOCKS.count(b) && BLOCKS.at(b).shape != Config::SHAPE_TERRAIN) {
                            if (BLOCKS.at(b).shape == Config::SHAPE_DOOR) {
                                uint8_t rot = world.get_rotation(bx, by, bz);
                                if (rot & 4) return false; // Puerta abierta: paso libre
                            }
                            return true; // Colisión cúbica para bloques de construcción
                        }
                        return sample_density(x, y, z) >= Config::ISO_SURFACE;
                    };

                    auto check_wall = [&](float vx, float vz, float y) {
                        return is_solid(vx - r, y, vz - r) || is_solid(vx + r, y, vz - r) ||
                               is_solid(vx - r, y, vz + r) || is_solid(vx + r, y, vz + r) ||
                               is_solid(vx - r, y, vz)     || is_solid(vx + r, y, vz)     ||
                               is_solid(vx, y, vz - r)     || is_solid(vx, y, vz + r);
                    };

                    // === Escaneo robusto de superficie del terreno para deslizamiento suave ===
                    auto sample_surface_at = [&](float px, float pz, float ref_feet_y) -> float {
                        float ground_y = -999.0f;
                        float scan_top = ref_feet_y + 0.80f;
                        float scan_bot = ref_feet_y - 1.20f;
                        
                        float prev_y = scan_bot;
                        float prev_d = sample_density(px, prev_y, pz);
                        
                        for (float y = scan_bot + 0.12f; y <= scan_top + 0.05f; y += 0.12f) {
                            float d = sample_density(px, y, pz);
                            if (prev_d >= Config::ISO_SURFACE && d < Config::ISO_SURFACE) {
                                float lo = prev_y;
                                float hi = y;
                                for (int i = 0; i < 8; i++) {
                                    float mid = (lo + hi) * 0.5f;
                                    if (sample_density(px, mid, pz) >= Config::ISO_SURFACE) lo = mid;
                                    else hi = mid;
                                }
                                ground_y = (lo + hi) * 0.5f;
                                break;
                            }
                            prev_y = y;
                            prev_d = d;
                        }
                        
                        if (ground_y < -900.0f) {
                            float d_bot = sample_density(px, scan_bot, pz);
                            float d_top = sample_density(px, scan_top, pz);
                            if (d_bot >= Config::ISO_SURFACE && d_top < Config::ISO_SURFACE) {
                                float lo = scan_bot;
                                float hi = scan_top;
                                for (int i = 0; i < 10; i++) {
                                    float mid = (lo + hi) * 0.5f;
                                    if (sample_density(px, mid, pz) >= Config::ISO_SURFACE) lo = mid;
                                    else hi = mid;
                                }
                                ground_y = (lo + hi) * 0.5f;
                            }
                        }
                        
                        // Bloques de construcción
                        int bx = (int)std::floor(px + 0.5f);
                        int bz = (int)std::floor(pz + 0.5f);
                        for (int dy = -1; dy <= 1; dy++) {
                            int by = (int)std::floor(ref_feet_y + 0.5f) + dy;
                            uint8_t b = world.get_block(bx, by, bz);
                            if (b != AIR && b != WATER && BLOCKS.count(b) && BLOCKS.at(b).shape != Config::SHAPE_TERRAIN) {
                                float block_top = (float)by + 0.5f;
                                if (block_top > ground_y && block_top <= ref_feet_y + 0.80f) {
                                    ground_y = block_top;
                                }
                            }
                        }
                        
                        return ground_y;
                    };

                    auto get_surface_height = [&](float px, float pz, float ref_feet_y) -> float {
                        float max_ground = sample_surface_at(px, pz, ref_feet_y);
                        float off = 0.18f;
                        float g1 = sample_surface_at(px - off, pz - off, ref_feet_y);
                        float g2 = sample_surface_at(px + off, pz - off, ref_feet_y);
                        float g3 = sample_surface_at(px - off, pz + off, ref_feet_y);
                        float g4 = sample_surface_at(px + off, pz + off, ref_feet_y);
                        if (g1 > max_ground) max_ground = g1;
                        if (g2 > max_ground) max_ground = g2;
                        if (g3 > max_ground) max_ground = g3;
                        if (g4 > max_ground) max_ground = g4;
                        return max_ground;
                    };

                    float eye_y = camera.position.y;
                    float feet_bottom = eye_y - Config::PLAYER_EYE_HEIGHT;
                    float step_height = 0.55f; // Altura máxima que el jugador puede subir suavemente
                    float feet_test_y = feet_bottom + step_height; // Evaluar paredes solo por encima del escalón
                    float waist_y = eye_y - 0.75f;
                    float head_y = eye_y + Config::PLAYER_HEAD_OFFSET - 0.06f;

                    float new_x = camera.position.x;
                    float old_x = old_pos.x;
                    float new_z = camera.position.z;
                    float old_z = old_pos.z;
                    
                    // Colisión horizontal (X) con deslizamiento sobre paredes
                    bool blocked_x = check_wall(new_x, old_z, feet_test_y) || check_wall(new_x, old_z, waist_y) || check_wall(new_x, old_z, head_y);
                    if (blocked_x) {
                        camera.position.x = old_x;
                        new_x = old_x;
                    }
                    
                    // Colisión horizontal (Z) con deslizamiento sobre paredes
                    bool blocked_z = check_wall(new_x, new_z, feet_test_y) || check_wall(new_x, new_z, waist_y) || check_wall(new_x, new_z, head_y);
                    if (blocked_z) {
                        camera.position.z = old_z;
                    }
                    
                    // Colisión vertical (Y) con delta time protegido contra caídas por tirones de framerate
                    float dt = std::min(GetFrameTime(), 0.033f);
                    player_vel_y -= 30.0f * dt;
                    camera.position.y += player_vel_y * dt;
                    
                    // Techo
                    float head_top = camera.position.y + Config::PLAYER_HEAD_OFFSET;
                    if (player_vel_y > 0 && check_wall(camera.position.x, camera.position.z, head_top)) {
                        camera.position.y = old_pos.y;
                        player_vel_y = 0.0f;
                    }
                    
                    // Suelo y escalado suave
                    is_grounded = false;
                    feet_bottom = camera.position.y - Config::PLAYER_EYE_HEIGHT;
                    float ground_y = get_surface_height(camera.position.x, camera.position.z, feet_bottom);

                    if (ground_y > -900.0f && player_vel_y <= 0.0f) {
                        float diff = ground_y - feet_bottom;
                        // Si está a nivel de suelo, subiendo rampa o sobre pequeño obstáculo
                        if (diff >= -0.40f && diff <= 0.85f) {
                            float target_cam_y = ground_y + Config::PLAYER_EYE_HEIGHT;
                            float dy = target_cam_y - camera.position.y;
                            
                            camera.position.y = target_cam_y;
                            if (dy > 0.02f) {
                                // Deslizamiento visual suave: absorbe el desnivel para que la cámara no salte
                                smooth_step_offset -= dy;
                            }
                            player_vel_y = 0.0f;
                            is_grounded = true;
                        }
                    }
                    
                    if (is_grounded && IsKeyPressed(KEY_SPACE)) {
                        player_vel_y = 10.5f;
                    }
                    
                    if (camera.position.y < -20.0f) {
                        camera.position.y = 80.0f;
                        player_vel_y = 0.0f;
                    }
                } // End physics pause if chunk is loading
                
                // Compensate camera.target so the view pitch/yaw doesn't get messed up by physical displacements (gravity/collisions)
                camera.target.x += (camera.position.x - post_update_pos.x);
                camera.target.y += (camera.position.y - post_update_pos.y);
                camera.target.z += (camera.position.z - post_update_pos.z);
            } else {
                if (IsKeyDown(KEY_LEFT_SHIFT)) {
                    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
                    
                    float ds = 40.0f * GetFrameTime();
                    Vector3 move = {0,0,0};
                    if (IsKeyDown(KEY_W)) move = Vector3Add(move, Vector3Scale(forward, ds));
                    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, Vector3Scale(forward, ds));
                    if (IsKeyDown(KEY_D)) move = Vector3Add(move, Vector3Scale(right, ds));
                    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, Vector3Scale(right, ds));
                    
                    camera.position = Vector3Add(camera.position, move);
                    camera.target = Vector3Add(camera.target, move);
                }
            }

            Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            ray_hit_valid = VoxelRaycastSmooth(world, camera.position, forward, 4.5f, hit, target_solid, target_empty);
            if (ray_hit_valid) {
                // === SLOT 0: HERRAMIENTAS ===
                if (ui.selected_slot == 0) {
                    // Iniciar minado al presionar click
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                        uint8_t target_block = world.get_block(target_solid.x, target_solid.y, target_solid.z);
                        if (target_block != AIR && target_block != WATER) {
                            mining_progress = 0.0f;
                            mining_target = target_solid;
                            // Guardar coordenadas enteras del bloque para comparar
                            mining_block.x = (float)(int)target_solid.x;
                            mining_block.y = (float)(int)target_solid.y;
                            mining_block.z = (float)(int)target_solid.z;
                            is_mining = true;
                        }
                    }
                    
                    // Cancelar minado si apunta a otro bloque (comparar coordenadas enteras)
                    if (is_mining) {
                        int bx = (int)target_solid.x;
                        int by = (int)target_solid.y;
                        int bz = (int)target_solid.z;
                        if (bx != (int)mining_block.x || by != (int)mining_block.y || bz != (int)mining_block.z) {
                            mining_progress = 0.0f;
                            is_mining = false;
                        }
                    }
                    
                    // Reset al soltar click
                    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                        mining_progress = 0.0f;
                        is_mining = false;
                    }
                }
                // === SLOTS 1-9: COLOCAR BLOQUES O INTERACTUAR ===
                else if (ui.selected_slot != 0) {
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                        uint8_t held_id = ui.slots[ui.selected_slot].id;
                        if (ui.slots[ui.selected_slot].count > 0 && held_id != 254 && held_id != Config::AIR && Config::BLOCKS.find(held_id) != Config::BLOCKS.end()) {
                            uint8_t rot = 0;
                            if (std::abs(forward.z) >= std::abs(forward.x)) {
                                rot = (forward.z > 0.0f) ? 2 : 0; // Si el player mira al Sur (+Z), el bloque mira al Norte (2) hacia el player. Si mira al Norte (-Z), el bloque mira al Sur (0).
                            } else {
                                rot = (forward.x > 0.0f) ? 1 : 3; // Si el player mira al Este (+X), el bloque mira al Oeste (1) hacia el player. Si mira al Oeste (-X), el bloque mira al Este (3).
                            }
                            if (ui.slots[ui.selected_slot].id == Config::DOOR_WOOD) {
                                if (target_empty.y + 1 < Config::GRID_Y && world.get_block(target_empty.x, target_empty.y + 1, target_empty.z) == Config::AIR) {
                                    world.set_block(target_empty.x, target_empty.y, target_empty.z, Config::DOOR_WOOD, rot);
                                    world.set_block(target_empty.x, target_empty.y + 1, target_empty.z, Config::DOOR_WOOD, rot | 8);
                                    ui.slots[ui.selected_slot].count--;
                                    if (ui.slots[ui.selected_slot].count <= 0) {
                                        ui.slots[ui.selected_slot].id = AIR;
                                        ui.slots[ui.selected_slot].name = "";
                                    }
                                } else {
                                    chat.add_message("[Puerta] Requiere 2 bloques de altura libre para colocarse.");
                                }
                            } else {
                                world.set_block(target_empty.x, target_empty.y, target_empty.z, ui.slots[ui.selected_slot].id, rot);
                                ui.slots[ui.selected_slot].count--;
                                if (ui.slots[ui.selected_slot].count <= 0) {
                                    ui.slots[ui.selected_slot].id = AIR;
                                    ui.slots[ui.selected_slot].name = "";
                                }
                            }
                        }
                    }
                }

                // Interaccion con objetos funcionales (Cofre, Horno, Mesa de Crafteo, Puerta, etc.) con Click Derecho
                if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
                    int bx = (int)target_solid.x;
                    int by = (int)target_solid.y;
                    int bz = (int)target_solid.z;
                    uint8_t look_b = world.get_block(bx, by, bz);
                    if (look_b == Config::AIR) {
                        bx = std::floor(hit.x);
                        by = std::floor(hit.y);
                        bz = std::floor(hit.z);
                        look_b = world.get_block(bx, by, bz);
                    }

                    if (look_b == Config::CHEST) {
                        ui.open_chest({ (float)bx, (float)by, (float)bz });
                    } else if (look_b == Config::CRAFTING_TABLE) {
                        ui.open_crafting_table({ (float)bx, (float)by, (float)bz });
                    } else if (look_b == Config::FURNACE) {
                        ui.open_furnace({ (float)bx, (float)by, (float)bz });
                    } else if (look_b == Config::DOOR_WOOD) {
                        uint8_t rot = world.get_rotation(bx, by, bz);
                        uint8_t new_rot = rot ^ 4; // Toggle open/close bit
                        world.set_block(bx, by, bz, Config::DOOR_WOOD, new_rot);
                        if (by + 1 < Config::GRID_Y && world.get_block(bx, by + 1, bz) == Config::DOOR_WOOD) {
                            uint8_t top_rot = world.get_rotation(bx, by + 1, bz);
                            world.set_block(bx, by + 1, bz, Config::DOOR_WOOD, (top_rot & ~4) | (new_rot & 4));
                        } else if (by > 0 && world.get_block(bx, by - 1, bz) == Config::DOOR_WOOD) {
                            uint8_t bot_rot = world.get_rotation(bx, by - 1, bz);
                            world.set_block(bx, by - 1, bz, Config::DOOR_WOOD, (bot_rot & ~4) | (new_rot & 4));
                        }
                    } else if (look_b == Config::TORCH) {
                        chat.add_message("[Antorcha] Iluminando el entorno con calidez");
                    } else if (look_b == Config::FENCE_WOOD) {
                        chat.add_message("[Valla de Madera] Bloque de contencion");
                    } else if (look_b == Config::STAIRS_WOOD || look_b == Config::STAIRS_STONE) {
                        chat.add_message("[Escalera] Peldanos para subir niveles sin saltar");
                    } else if (look_b != Config::AIR && look_b != Config::WATER && Config::BLOCKS.count(look_b)) {
                        chat.add_message("[" + Config::BLOCKS.at(look_b).name + "] Bloque de construccion");
                    }
                }
            } else {
                // Raycast no impacta: NO resetear minado, solo pausar acumulacion
            }
        }
        
        if (!chat.is_open && !ui.is_open && !menu.is_paused()) {
            if (IsKeyPressed(KEY_T)) {
                chat.toggle();
            } else if (IsKeyPressed(KEY_SLASH)) {
                chat.toggle();
                chat.current_input = "/";
            } else if (IsKeyDown(KEY_F3) && IsKeyPressed(KEY_G)) {
                show_chunks = !show_chunks;
                chat.add_message(show_chunks ? "Limites de chunks visibles" : "Limites de chunks ocultos");
            } else if (IsKeyPressed(KEY_F9)) {
                show_chunks = !show_chunks;
                chat.add_message(show_chunks ? "Limites de chunks visibles" : "Limites de chunks ocultos");
            } else if (IsKeyPressed(KEY_F3)) {
                show_debug_info = !show_debug_info;
            }
        }
        
        chat.update();
        if (!chat.is_open) ui.update();
        
        CommandHandler::process(chat, day_time, camera, spectator_mode, player_vel_y, ui, show_chunks);
        world.update(camera.position);

        // ==================== 20 TPS FIXED TIMESTEP (Estilo Minecraft) ====================
        constexpr float TICK_TIME = 1.0f / 20.0f; // 0.05s por tick (20 TPS)
        static float tick_accumulator = 0.0f;
        float frame_dt = std::min(GetFrameTime(), 0.1f);
        tick_accumulator += frame_dt;

        if (menu.is_paused()) {
            tick_accumulator = 0.0f;
        }

        while (tick_accumulator >= TICK_TIME && !menu.is_paused()) {
            tick_accumulator -= TICK_TIME;

            // 1. Progresión del ciclo día/noche en ticks (24000 ticks = 20 minutos de día completo)
            day_time += (PI / 12000.0f);
            if (day_time >= 2.0f * PI) day_time -= 2.0f * PI;

            // 2. Físicas y recolección de Item Drops
            item_drops.update(TICK_TIME, world, camera.position, ui);

            // 3. Procesamiento de hornos en tiempo real
            ui.tick_furnaces();

            // 4. Daño de minado exacto por tick
            if (is_mining && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ui.selected_slot == 0) {
                uint8_t target_block = world.get_block((int)mining_block.x, (int)mining_block.y, (int)mining_block.z);
                if (target_block != AIR && target_block != WATER) {
                    const BlockType& bt = BLOCKS.at(target_block);
                    ToolSlot* tool = ui.get_active_tool();
                    bool can_break = (bt.require_tier != 255);
                    bool can_harvest = true;
                    if (can_break && bt.require_tool != 255) {
                        if (!tool || (int)tool->type != (int)bt.require_tool || (int)tool->tier < (int)bt.require_tier) {
                            can_harvest = false;
                        }
                    }

                    if (can_break) {
                        float tool_speed = 1.0f;
                        if (tool) {
                            tool_speed = tool->type == TOOL_PICKAXE ? 2.0f :
                                         tool->type == TOOL_SHOVEL ? 2.0f :
                                         tool->type == TOOL_AXE ? 2.0f :
                                         tool->type == TOOL_SWORD ? 1.5f :
                                         tool->type == TOOL_HAMMER ? 1.2f : 1.0f;
                            
                            float tier_mult = tool->tier == TIER_WOOD ? 1.0f :
                                              tool->tier == TIER_STONE ? 2.0f :
                                              tool->tier == TIER_IRON ? 3.0f :
                                              tool->tier == TIER_SILVER ? 4.0f :
                                              tool->tier == TIER_GOLD ? 6.0f :
                                              tool->tier == TIER_DIAMOND ? 5.0f : 1.0f;
                            tool_speed *= tier_mult;
                        }

                        if (tool && bt.ideal_tool != 255 && (int)tool->type == (int)bt.ideal_tool) {
                            tool_speed *= 1.5f;
                        }

                        float hardness = bt.hardness;
                        HammerArea hammer_area;
                        if (tool && tool->type == TOOL_HAMMER) {
                            hammer_area = get_hammer_area((int)tool->tier, hit, (int)mining_block.x, (int)mining_block.z);
                            hardness = world.get_hammer_mining_hardness((int)mining_block.x, (int)mining_block.y, (int)mining_block.z, hammer_area, (int)tool->tier);
                        }

                        float divisor = can_harvest ? 30.0f : 100.0f;
                        float damage_per_tick = tool_speed / std::max(hardness, 0.1f) / divisor;

                        mining_progress += damage_per_tick;

                        if (mining_progress >= 1.0f) {
                            if (tool && tool->type == TOOL_HAMMER) {
                                int broken = world.flatten_terrain((int)mining_block.x, (int)mining_block.y, (int)mining_block.z, hammer_area, (int)tool->tier, &item_drops);
                                int uses = std::max(1, broken);
                                tool->durability_current -= uses;
                                if (tool->durability_current <= 0) ui.remove_active_tool();
                            } else {
                                if (bt.drop_id != 255) {
                                    Vector3 drop_pos = { (float)mining_block.x + 0.5f, (float)mining_block.y + 0.5f, (float)mining_block.z + 0.5f };
                                    Vector3 drop_vel = {
                                        ((float)(rand() % 100) - 50.0f) / 100.0f * 2.0f,
                                        2.5f + (float)(rand() % 50) / 100.0f,
                                        ((float)(rand() % 100) - 50.0f) / 100.0f * 2.0f
                                    };
                                    item_drops.spawn(drop_pos, bt.drop_id, bt.drop_is_item, 1, drop_vel, 0.1f);
                                }
                                world.set_block((int)mining_block.x, (int)mining_block.y, (int)mining_block.z, AIR);
                                if (target_block == Config::DOOR_WOOD) {
                                    int mx = (int)mining_block.x;
                                    int my = (int)mining_block.y;
                                    int mz = (int)mining_block.z;
                                    if (my + 1 < Config::GRID_Y && world.get_block(mx, my + 1, mz) == Config::DOOR_WOOD) {
                                        world.set_block(mx, my + 1, mz, AIR);
                                    } else if (my > 0 && world.get_block(mx, my - 1, mz) == Config::DOOR_WOOD) {
                                        world.set_block(mx, my - 1, mz, AIR);
                                    }
                                }
                                if (tool) {
                                    tool->durability_current--;
                                    if (tool->durability_current <= 0) ui.remove_active_tool();
                                }
                            }
                            mining_progress = 0.0f;
                            is_mining = false;
                        }
                    } else {
                        mining_progress = 0.0f;
                        is_mining = false;
                    }
                } else {
                    mining_progress = 0.0f;
                    is_mining = false;
                }
            }
        }

        float shaderTime = GetTime();
        SetShaderValue(world.mat_solid.shader, timeLocSolid, &shaderTime, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_solid.shader, camPosLocSolid, &camera.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_plants.shader, timeLocPlants, &shaderTime, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_plants.shader, camPosLocPlants, &camera.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_water.shader, timeLocWater, &shaderTime, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_water.shader, camPosLocWater, &camera.position, SHADER_UNIFORM_VEC3);

        BeginDrawing();
        
        float sun_y = std::sin(day_time);
        float sun_x = std::cos(day_time);
        // Alinear la claridad del terreno con la del cielo
        float dayFactor = std::clamp(sun_y * 2.0f + 0.2f, 0.0f, 1.0f);
        float light_intensity = std::max(0.05f, dayFactor);
        
        // Color dinámico de niebla que coincide exactamente con el cielo diurno y nocturno (evita niebla brillante en la noche)
        float fogColorVec[3] = {
            (1.0f - dayFactor) * 0.01f + dayFactor * (135.0f / 255.0f),
            (1.0f - dayFactor) * 0.02f + dayFactor * (206.0f / 255.0f),
            (1.0f - dayFactor) * 0.05f + dayFactor * (235.0f / 255.0f)
        };
        Color sky_color = { 
            (unsigned char)(fogColorVec[0] * 255.0f), 
            (unsigned char)(fogColorVec[1] * 255.0f), 
            (unsigned char)(fogColorVec[2] * 255.0f), 
            255 
        };
        ClearBackground(sky_color);
        
        // Pass dynamic fog color to shaders
        SetShaderValue(world.mat_solid.shader, fogLocSolid, fogColorVec, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_plants.shader, fogLocPlants, fogColorVec, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_water.shader, fogLocWater, fogColorVec, SHADER_UNIFORM_VEC3);
        
        SetShaderValue(world.mat_solid.shader, fogStartLocSolid, &Config::FOG_START, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_solid.shader, fogEndLocSolid, &Config::FOG_END, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_plants.shader, fogStartLocPlants, &Config::FOG_START, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_plants.shader, fogEndLocPlants, &Config::FOG_END, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_water.shader, fogStartLocWater, &Config::FOG_START, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_water.shader, fogEndLocWater, &Config::FOG_END, SHADER_UNIFORM_FLOAT);
        
        // Materials remain pure white so the voxel light shader handles night and torch intensity accurately
        world.mat_solid.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        world.mat_water.maps[MATERIAL_MAP_DIFFUSE].color = { 255, 255, 255, 200 };
        world.mat_plants.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;

        // Apply smooth camera transition for auto-steps
        if (!spectator_mode) {
            smooth_step_offset = Lerp(smooth_step_offset, 0.0f, 15.0f * GetFrameTime());
        } else {
            smooth_step_offset = 0.0f;
        }
        
        camera.position.y += smooth_step_offset;
        camera.target.y += smooth_step_offset;

        BeginMode3D(camera);
        
        Vector3 sun_center = { camera.position.x + 400.0f * sun_x, camera.position.y + 400.0f * sun_y, camera.position.z };
        Vector3 sun_dir_vec = Vector3Normalize(Vector3Subtract(sun_center, camera.position));
        
        SetShaderValue(skyboxShader, skyboxSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_solid.shader, terrainSolidSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_plants.shader, terrainPlantsSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_water.shader, waterSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);
        
        BeginShaderMode(skyboxShader);
        DrawSkybox(camera, sky_side, sky_top, sky_bottom, light_intensity);
        EndShaderMode();
        
        rlDisableDepthMask();
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        
        // El sol/luna se mantienen 100% sólidos hasta cruzar completamente el horizonte (y = -0.1)
        // Se desvanecen rápidamente en el vacío (y = -0.15) para que no se vean por debajo del mapa
        float sun_alpha = std::clamp((sun_y + 0.15f) / 0.05f, 0.0f, 1.0f);
        float moon_alpha = std::clamp((-sun_y + 0.15f) / 0.05f, 0.0f, 1.0f);
        
        rlEnableColorBlend();
        
        rlPushMatrix();
            // Fijar la órbita celeste al nivel del agua del mundo, NO a la cámara,
            // para que no parezcan elevarse si el jugador vuela hacia arriba
            rlTranslatef(camera.position.x, camera.position.y, camera.position.z);
            
            // Sun (Perfect flat quad)
            if (sun_alpha > 0) {
                rlPushMatrix();
                    rlRotatef(day_time * RAD2DEG, 0, 0, 1);
                    rlTranslatef(400.0f, 0.0f, 0.0f);
                    rlRotatef(-90, 0, 1, 0); // Face origin correctly
                    
                    rlSetTexture(tex_sun.id);
                    rlBegin(RL_QUADS);
                        rlColor4ub(255, 255, 255, (unsigned char)(sun_alpha * 255.0f));
                        rlTexCoord2f(0, 0); rlVertex3f(-40.0f, 40.0f, 0.0f);
                        rlTexCoord2f(0, 1); rlVertex3f(-40.0f, -40.0f, 0.0f);
                        rlTexCoord2f(1, 1); rlVertex3f(40.0f, -40.0f, 0.0f);
                        rlTexCoord2f(1, 0); rlVertex3f(40.0f, 40.0f, 0.0f);
                    rlEnd();
                    rlSetTexture(0);
                rlPopMatrix();
            }
            
            // Moon
            if (moon_alpha > 0) {
                rlPushMatrix();
                    rlRotatef(day_time * RAD2DEG + 180.0f, 0, 0, 1);
                    rlTranslatef(400.0f, 0.0f, 0.0f);
                    rlRotatef(-90, 0, 1, 0); // Face origin correctly
                    
                    rlSetTexture(tex_moon.id);
                    rlBegin(RL_QUADS);
                        rlColor4ub(255, 255, 255, (unsigned char)(moon_alpha * 255.0f));
                        rlTexCoord2f(0, 0); rlVertex3f(-30.0f, 30.0f, 0.0f);
                        rlTexCoord2f(0, 1); rlVertex3f(-30.0f, -30.0f, 0.0f);
                        rlTexCoord2f(1, 1); rlVertex3f(30.0f, -30.0f, 0.0f);
                        rlTexCoord2f(1, 0); rlVertex3f(30.0f, 30.0f, 0.0f);
                    rlEnd();
                    rlSetTexture(0);
                rlPopMatrix();
            }
        rlPopMatrix();
        
        float cloud_offset = GetTime() * 0.02f;
        rlPushMatrix();
            rlTranslatef(camera.position.x, 250.0f, camera.position.z);
            rlRotatef(90, 1, 0, 0); // Draw on XY plane, then rotate to XZ
            rlSetTexture(tex_clouds.id);
            rlBegin(RL_QUADS);
                int grid = 16;
                float size = 480.0f; // Cover up to the skybox bounds
                float step = size * 2.0f / grid;
                float uv_c = 4.0f; // Scale for cloud texture density
                for (int x = 0; x < grid; x++) {
                    for (int y = 0; y < grid; y++) {
                        float x1 = -size + x * step;
                        float y1 = -size + y * step;
                        float x2 = x1 + step;
                        float y2 = y1 + step;
                        
                        float d11 = std::sqrt(x1*x1 + y1*y1);
                        float d12 = std::sqrt(x1*x1 + y2*y2);
                        float d21 = std::sqrt(x2*x2 + y1*y1);
                        float d22 = std::sqrt(x2*x2 + y2*y2);
                        
                        float max_d = 450.0f;
                        float fade_dist = 150.0f; // Fade gracefully from radius 300 to 450
                        
                        float f11 = std::clamp((max_d - d11) / fade_dist, 0.0f, 1.0f);
                        float f12 = std::clamp((max_d - d12) / fade_dist, 0.0f, 1.0f);
                        float f21 = std::clamp((max_d - d21) / fade_dist, 0.0f, 1.0f);
                        float f22 = std::clamp((max_d - d22) / fade_dist, 0.0f, 1.0f);
                        
                        // Map World Z (y1, y2) to Texture U
                        float tex_u1 = ((y1 + size) / (size*2)) * uv_c;
                        float tex_u2 = ((y2 + size) / (size*2)) * uv_c;
                        
                        // Map World X (x1, x2) to Texture V, and move along X (towards -X like the sun)
                        float tex_v1 = cloud_offset + ((x1 + size) / (size*2)) * uv_c;
                        float tex_v2 = cloud_offset + ((x2 + size) / (size*2)) * uv_c;
                        
                        rlColor4ub(255, 255, 255, (unsigned char)(f11 * 220));
                        rlTexCoord2f(tex_u1, tex_v1); rlVertex3f(x1, y1, 0.0f);
                        
                        rlColor4ub(255, 255, 255, (unsigned char)(f21 * 220));
                        rlTexCoord2f(tex_u1, tex_v2); rlVertex3f(x2, y1, 0.0f);
                        
                        rlColor4ub(255, 255, 255, (unsigned char)(f22 * 220));
                        rlTexCoord2f(tex_u2, tex_v2); rlVertex3f(x2, y2, 0.0f);
                        
                        rlColor4ub(255, 255, 255, (unsigned char)(f12 * 220));
                        rlTexCoord2f(tex_u2, tex_v1); rlVertex3f(x1, y2, 0.0f);
                    }
                }
            rlEnd();
            rlSetTexture(0);
        rlPopMatrix();
        
        rlEnableBackfaceCulling();
        
        rlEnableDepthTest();
        rlEnableDepthMask();

        world.draw(camera);
        item_drops.draw(spritesheet, spritesheet_items, light_intensity, tick_accumulator / TICK_TIME);
        
        // Minecraft-style Chunk & Sub-chunk Boundaries (F3+G)
        if (show_chunks) {
            float CS = (float)Config::CHUNK_SIZE;
            float SCS = (float)Config::SUBCHUNK_SIZE;
            float GY = (float)Config::GRID_Y;
            
            int p_cx = (int)std::floor(camera.position.x / CS);
            int p_cz = (int)std::floor(camera.position.z / CS);
            int p_sub_idx = std::clamp((int)std::floor(camera.position.y / SCS), 0, Config::NUM_SUBCHUNKS - 1);
            
            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            rlBegin(RL_LINES);
            
            // 1. Chunks vecinos (Radio 1 alrededor del jugador) - Marcos y sub-chunks
            Color neighbor_col = Fade(BLUE, 0.35f);
            Color neighbor_sub_col = Fade(BLUE, 0.18f);
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue; // Saltar chunk actual
                    float nx0 = (p_cx + dx) * CS;
                    float nz0 = (p_cz + dz) * CS;
                    float nx1 = nx0 + CS;
                    float nz1 = nz0 + CS;
                    
                    // Esquinas verticales
                    rlColor4ub(neighbor_col.r, neighbor_col.g, neighbor_col.b, neighbor_col.a);
                    rlVertex3f(nx0, 0, nz0); rlVertex3f(nx0, GY, nz0);
                    rlVertex3f(nx1, 0, nz0); rlVertex3f(nx1, GY, nz0);
                    rlVertex3f(nx1, 0, nz1); rlVertex3f(nx1, GY, nz1);
                    rlVertex3f(nx0, 0, nz1); rlVertex3f(nx0, GY, nz1);
                    
                    // Divisiones horizontales de Sub-chunks en vecinos
                    rlColor4ub(neighbor_sub_col.r, neighbor_sub_col.g, neighbor_sub_col.b, neighbor_sub_col.a);
                    for (int s = 0; s <= Config::NUM_SUBCHUNKS; ++s) {
                        float sy = s * SCS;
                        rlVertex3f(nx0, sy, nz0); rlVertex3f(nx1, sy, nz0);
                        rlVertex3f(nx1, sy, nz0); rlVertex3f(nx1, sy, nz1);
                        rlVertex3f(nx1, sy, nz1); rlVertex3f(nx0, sy, nz1);
                        rlVertex3f(nx0, sy, nz1); rlVertex3f(nx0, sy, nz0);
                    }
                }
            }
            
            // 2. Chunk Actual del Jugador
            float x0 = p_cx * CS;
            float z0 = p_cz * CS;
            float x1 = x0 + CS;
            float z1 = z0 + CS;
            
            // A. Rejilla vertical por bloque (X=1..15, Z=1..15) en las 4 paredes exteriores (Amarillo tenue)
            Color wall_grid_col = Color{ 255, 220, 0, 100 };
            rlColor4ub(wall_grid_col.r, wall_grid_col.g, wall_grid_col.b, wall_grid_col.a);
            for (int i = 1; i < Config::CHUNK_SIZE; ++i) {
                float ox = x0 + (float)i;
                float oz = z0 + (float)i;
                
                // Paredes Norte y Sur (a lo largo de X)
                rlVertex3f(ox, 0, z0); rlVertex3f(ox, GY, z0);
                rlVertex3f(ox, 0, z1); rlVertex3f(ox, GY, z1);
                
                // Paredes Este y Oeste (a lo largo de Z)
                rlVertex3f(x0, 0, oz); rlVertex3f(x0, GY, oz);
                rlVertex3f(x1, 0, oz); rlVertex3f(x1, GY, oz);
            }
            
            // B. Líneas de todas las secciones de Sub-chunk (cada 16 bloques de altura en Y: 0, 16, 32... 128)
            Color section_col = Color{ 255, 230, 0, 240 };
            rlColor4ub(section_col.r, section_col.g, section_col.b, section_col.a);
            for (int s = 0; s <= Config::NUM_SUBCHUNKS; ++s) {
                float fy = (float)(s * SCS);
                rlVertex3f(x0, fy, z0); rlVertex3f(x1, fy, z0);
                rlVertex3f(x1, fy, z0); rlVertex3f(x1, fy, z1);
                rlVertex3f(x1, fy, z1); rlVertex3f(x0, fy, z1);
                rlVertex3f(x0, fy, z1); rlVertex3f(x0, fy, z0);
            }
            
            // C. Caja cúbica completa (16x16x16) del SUB-CHUNK ACTUAL DEL JUGADOR (Cian brillante)
            float sub_y0 = p_sub_idx * SCS;
            float sub_y1 = (p_sub_idx + 1) * SCS;
            Color active_sub_col = Color{ 0, 240, 255, 255 };
            rlColor4ub(active_sub_col.r, active_sub_col.g, active_sub_col.b, active_sub_col.a);
            
            // Marco inferior
            rlVertex3f(x0, sub_y0, z0); rlVertex3f(x1, sub_y0, z0);
            rlVertex3f(x1, sub_y0, z0); rlVertex3f(x1, sub_y0, z1);
            rlVertex3f(x1, sub_y0, z1); rlVertex3f(x0, sub_y0, z1);
            rlVertex3f(x0, sub_y0, z1); rlVertex3f(x0, sub_y0, z0);
            // Marco superior
            rlVertex3f(x0, sub_y1, z0); rlVertex3f(x1, sub_y1, z0);
            rlVertex3f(x1, sub_y1, z0); rlVertex3f(x1, sub_y1, z1);
            rlVertex3f(x1, sub_y1, z1); rlVertex3f(x0, sub_y1, z1);
            rlVertex3f(x0, sub_y1, z1); rlVertex3f(x0, sub_y1, z0);
            // 4 Columnas del subchunk
            rlVertex3f(x0, sub_y0, z0); rlVertex3f(x0, sub_y1, z0);
            rlVertex3f(x1, sub_y0, z0); rlVertex3f(x1, sub_y1, z0);
            rlVertex3f(x1, sub_y0, z1); rlVertex3f(x1, sub_y1, z1);
            rlVertex3f(x0, sub_y0, z1); rlVertex3f(x0, sub_y1, z1);
            
            // Rejilla interna del subchunk activo (cada 2 bloques)
            Color sub_detail_col = Color{ 0, 210, 255, 120 };
            rlColor4ub(sub_detail_col.r, sub_detail_col.g, sub_detail_col.b, sub_detail_col.a);
            for (int y = (int)sub_y0 + 2; y < (int)sub_y1; y += 2) {
                float fy = (float)y;
                rlVertex3f(x0, fy, z0); rlVertex3f(x1, fy, z0);
                rlVertex3f(x1, fy, z0); rlVertex3f(x1, fy, z1);
                rlVertex3f(x1, fy, z1); rlVertex3f(x0, fy, z1);
                rlVertex3f(x0, fy, z1); rlVertex3f(x0, fy, z0);
            }
            
            // D. 4 Esquinas principales de la columna de chunk en Rojo Intenso (desde Y=0 hasta Y=GRID_Y)
            Color corner_col = Color{ 255, 30, 30, 255 };
            rlColor4ub(corner_col.r, corner_col.g, corner_col.b, corner_col.a);
            rlVertex3f(x0, 0, z0); rlVertex3f(x0, GY, z0);
            rlVertex3f(x1, 0, z0); rlVertex3f(x1, GY, z0);
            rlVertex3f(x1, 0, z1); rlVertex3f(x1, GY, z1);
            rlVertex3f(x0, 0, z1); rlVertex3f(x0, GY, z1);
            
            rlEnd();
            rlEnableDepthTest();
            rlEnableBackfaceCulling();
        }
        
        EndMode3D(); // Flush and close standard 3D pass
        
        bool is_valid_tool = false;
        if (ui.selected_slot == 0) {
            is_valid_tool = true;
        } else {
            uint8_t held_id = ui.slots[ui.selected_slot].id;
            if (held_id != Config::AIR && held_id != 254 && ui.slots[ui.selected_slot].count > 0 && Config::BLOCKS.find(held_id) != Config::BLOCKS.end()) {
                is_valid_tool = true;
            }
        }

        if (!ui.is_open && !chat.is_open && is_valid_tool && ray_hit_valid) {
            BeginMode3D(camera); // Start X-Ray 3D pass (this internally enables depth test!)
            
            rlDisableDepthMask(); // Disable depth mask AFTER BeginMode3D
            rlDisableDepthTest(); // Disable depth test AFTER BeginMode3D
            
            Vector3 target_node = (ui.selected_slot == 0) ? target_solid : target_empty;
            
            rlDisableBackfaceCulling(); // Re-disable to show the inside of the concave crater hologram
            
            int bx = std::floor(target_node.x);
            int by = std::floor(target_node.y);
            int bz = std::floor(target_node.z);
            
            ToolSlot* active_tool = (ui.selected_slot == 0) ? ui.get_active_tool() : nullptr;
            bool is_hammer = (active_tool && active_tool->type == TOOL_HAMMER);
            
            HammerArea h_area;
            if (is_hammer) {
                h_area = get_hammer_area((int)active_tool->tier, hit, bx, bz);
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
            
            uint8_t held_b = (ui.selected_slot != 0) ? ui.slots[ui.selected_slot].id : Config::AIR;
            bool is_construction_held = (ui.selected_slot != 0 && held_b != Config::AIR && held_b != 254 && Config::BLOCKS.find(held_b) != Config::BLOCKS.end() && Config::BLOCKS.at(held_b).shape != Config::SHAPE_TERRAIN);
            
            uint8_t target_b = (ui.selected_slot == 0) ? world.get_block(bx, by, bz) : Config::AIR;
            bool is_construction_targeted = (ui.selected_slot == 0 && target_b != AIR && target_b != WATER && BLOCKS.count(target_b) && BLOCKS.at(target_b).shape != Config::SHAPE_TERRAIN);

            // Obtener rotación del bloque objetivo
            uint8_t target_rot = 0;
            if (is_construction_targeted) {
                int cx = std::floor((float)bx / Config::CHUNK_SIZE);
                int cz = std::floor((float)bz / Config::CHUNK_SIZE);
                Chunk* chk = world.get_chunk(cx, cz);
                if (chk) {
                    int lx = bx - cx * Config::CHUNK_SIZE;
                    int lz = bz - cz * Config::CHUNK_SIZE;
                    if (lx >= 0 && lx <= Config::CHUNK_SIZE && lz >= 0 && lz <= Config::CHUNK_SIZE && by >= 0 && by < Config::GRID_Y) {
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
                
                if (held_b == Config::DOOR_WOOD) {
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
                            d_slice[idx] = world.get_density(bx + dx, by + dy, bz + dz);
                        }
                    }
                }
                
                if (is_hammer) {
                    // Simular el aplanado con martillo:
                    // 1. Vaciar elevaciones por encima de by (dy >= 1) hasta max_h si son rompibles
                    // 2. Nivelar la base en dy = 0 como sólido plano
                    for (int dx = h_area.min_dx; dx <= h_area.max_dx; dx++) {
                        for (int dz = h_area.min_dz; dz <= h_area.max_dz; dz++) {
                            for (int dy = 1; dy <= h_area.max_h; dy++) {
                                if (dy + 1 < size_y) {
                                    uint8_t b = world.get_block(bx + dx, by + dy, bz + dz);
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
                            
                            // En dy = 0, SOLO si hay terreno existente, fijar densidad a 1.0 para previsualizar plano
                            uint8_t b_base = world.get_block(bx + dx, by, bz + dz);
                            if (b_base != AIR && b_base != WATER) {
                                int idx_base = (0 + 1) * (size_x * size_z) + (dz - off_min_z) * size_x + (dx - off_min_x);
                                d_slice[idx_base] = 1.0f;
                            }
                        }
                    }
                } else if (ui.selected_slot == 0) {
                    d_slice[1 * (size_x * size_z) + 1 * size_x + 1] = -1.0f; // Preview mine
                } else {
                    d_slice[1 * (size_x * size_z) + 1 * size_x + 1] = 1.0f; // Preview place
                }
                
                std::vector<Vector3> g_verts, g_norms;
                std::vector<Vector2> g_uvs, g_uvs2; std::vector<Color> g_cols;
                mc::generate(nullptr, d_slice.data(), size_x, size_y, size_z, 0.0f, Config::AIR, g_verts, g_norms, g_uvs, g_uvs2, g_cols);
                
                rlPushMatrix();
                rlTranslatef(bx + (float)off_min_x, by - 1.0f, bz + (float)off_min_z);
                
                float pulse = 0.6f + std::sin(GetTime() * 6.0f) * 0.4f;
                unsigned char alpha = (unsigned char)(pulse * 255);
                Color wire_col = is_hammer ? Color{255, 180, 40, alpha} : ((ui.selected_slot == 0) ? Color{255, 25, 25, alpha} : Color{25, 255, 75, alpha});
                Color solid_col = is_hammer ? Color{255, 160, 0, 45} : ((ui.selected_slot == 0) ? Color{255, 0, 0, 40} : Color{0, 255, 0, 40});

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
                
                rlSetTexture(spritesheet.id);
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
            
            // 1. Flush the X-Ray batch NOW while depth test is still OFF
            rlDrawRenderBatchActive();
            
            // 2. Restore persistent states that Raylib doesn't auto-restore
            rlEnableDepthMask();
            rlEnableBackfaceCulling();
            
            // 3. Close 3D mode (auto-disables depth test for 2D UI)
            EndMode3D();
        }

        // === PRIMERA PERSONA: MANO Y OBJETO EN MANO (VIEWMODEL 3D) ===
        if (!spectator_mode && !ui.is_open && !chat.is_open) {
            DrawFirstPersonViewmodel(ui, spritesheet, spritesheet_items, light_intensity, is_mining, mining_progress, is_grounded, GetFrameTime());
        }

        int cam_x = std::floor(camera.position.x);
        int cam_y = std::floor(camera.position.y);
        int cam_z = std::floor(camera.position.z);
        if (world.get_block(cam_x, cam_y, cam_z) == WATER && camera.position.y < (WATER_LEVEL - 0.05f)) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), { 10, 50, 150, 100 });
        }

        ui.draw();
        
        // Barra de progreso de minado
        if (is_mining && mining_progress > 0.0f && mining_progress < 1.0f) {
            int cx = GetScreenWidth() / 2;
            int cy = GetScreenHeight() / 2;
            int bar_w = 60;
            int bar_h = 5;
            DrawRectangle(cx - bar_w/2, cy + 25, bar_w, bar_h, Fade(BLACK, 0.6f));
            DrawRectangle(cx - bar_w/2, cy + 25, (int)(bar_w * mining_progress), bar_h, WHITE);
        }
        
        chat.draw();
        
        // === PANTALLA DE INFORMACIÓN F3 (ESTILO MINECRAFT) ===
        if (show_debug_info) {
            int fps = GetFPS();
            Color fps_color = (fps >= 55) ? Color{80, 250, 100, 255} : ((fps >= 30) ? Color{250, 210, 60, 255} : Color{250, 70, 70, 255});
            
            // Dirección cardinal
            Vector3 look_dir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            const char* facing = "Desconocido";
            if (std::abs(look_dir.x) > std::abs(look_dir.z)) {
                facing = (look_dir.x > 0) ? "Este (+X)" : "Oeste (-X)";
            } else {
                facing = (look_dir.z > 0) ? "Sur (+Z)" : "Norte (-Z)";
            }
            
            int p_cx = (int)std::floor(camera.position.x / (float)Config::CHUNK_SIZE);
            int p_cz = (int)std::floor(camera.position.z / (float)Config::CHUNK_SIZE);
            int in_cx = ((cam_x % Config::CHUNK_SIZE) + Config::CHUNK_SIZE) % Config::CHUNK_SIZE;
            int in_cz = ((cam_z % Config::CHUNK_SIZE) + Config::CHUNK_SIZE) % Config::CHUNK_SIZE;
            
            int seed_offset = static_cast<int>(static_cast<uint32_t>(Config::WORLD_SEED) * 1000U);
            const char* biome_name = Biome::get_biome_name_at(camera.position.x, camera.position.z, seed_offset);
            
            uint8_t look_block = AIR;
            std::string block_name = "Aire";
            if (!ui.is_open && !chat.is_open && ray_hit_valid) {
                look_block = world.get_block(target_solid.x, target_solid.y, target_solid.z);
                if (look_block != AIR && BLOCKS.count(look_block)) {
                    block_name = BLOCKS.at(look_block).name;
                }
            }
            
            // Formateo de hora del día
            std::string time_str;
            float norm_time = std::fmod(day_time, 6.28318f);
            if (norm_time < 0) norm_time += 6.28318f;
            if (norm_time >= 0.78f && norm_time < 2.35f) time_str = "Dia";
            else if (norm_time >= 2.35f && norm_time < 3.92f) time_str = "Atardecer";
            else if (norm_time >= 3.92f && norm_time < 5.49f) time_str = "Noche";
            else time_str = "Amanecer";
            
            int feet_y = (int)std::floor(camera.position.y - Config::PLAYER_EYE_HEIGHT);
            std::vector<std::pair<std::string, Color>> lines;
            lines.push_back({ TextFormat("SmoothVoxelEngine C++ | %d FPS", fps), fps_color });
            lines.push_back({ TextFormat("XYZ: %.2f / %.2f / %.2f", camera.position.x, camera.position.y, camera.position.z), WHITE });
            int sub_y_idx = std::clamp((int)std::floor(camera.position.y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
            int in_sub_y = ((feet_y % Config::SUBCHUNK_SIZE) + Config::SUBCHUNK_SIZE) % Config::SUBCHUNK_SIZE;
            lines.push_back({ TextFormat("Chunk: %d, %d (Sub-chunk %d/%d)  [En sub-chunk: %d, %d, %d]", p_cx, p_cz, sub_y_idx, Config::NUM_SUBCHUNKS - 1, in_cx, in_sub_y, in_cz), Color{220, 225, 235, 255} });
            lines.push_back({ TextFormat("Orientacion: %s", facing), Color{200, 215, 235, 255} });
            lines.push_back({ TextFormat("Bioma: %s", biome_name), Color{140, 230, 160, 255} });
            
            if (look_block != AIR) {
                lines.push_back({ TextFormat("Mirando a: %s (ID %d en %d, %d, %d)", block_name.c_str(), (int)look_block, (int)target_solid.x, (int)target_solid.y, (int)target_solid.z), Color{255, 220, 100, 255} });
                
                uint8_t raw_l = world.get_light(target_empty.x, target_empty.y, target_empty.z);
                if (raw_l == 0) raw_l = world.get_light(target_solid.x, target_solid.y, target_solid.z);
                uint8_t sun_l = VoxelLighting::get_sunlight(raw_l);
                uint8_t blk_l = VoxelLighting::get_blocklight(raw_l);
                uint8_t max_l = std::max(sun_l, blk_l);
                lines.push_back({ TextFormat("Luz del bloque: %d (Sol: %d, Bloque: %d)", (int)max_l, (int)sun_l, (int)blk_l), Color{255, 235, 120, 255} });
            } else {
                lines.push_back({ "Mirando a: Aire", Color{170, 180, 195, 255} });
                uint8_t raw_l = world.get_light(cam_x, feet_y, cam_z);
                uint8_t sun_l = VoxelLighting::get_sunlight(raw_l);
                uint8_t blk_l = VoxelLighting::get_blocklight(raw_l);
                uint8_t max_l = std::max(sun_l, blk_l);
                lines.push_back({ TextFormat("Luz en jugador: %d (Sol: %d, Bloque: %d)", (int)max_l, (int)sun_l, (int)blk_l), Color{255, 235, 120, 255} });
            }
            
            lines.push_back({ TextFormat("Hora: %.2f (%s)", norm_time, time_str.c_str()), Color{240, 240, 200, 255} });
            
            if (spectator_mode) {
                lines.push_back({ "[ MODO ESPECTADOR ]", Color{100, 210, 255, 255} });
            }
            if (show_chunks) {
                lines.push_back({ "[ LIMITES DE CHUNK ACTIVOS (F3+G) ]", Color{255, 215, 80, 255} });
            }
            
            int cur_y = 10;
            int font_size = 14;
            int line_h = 20;
            for (const auto& [txt, col] : lines) {
                int tw = MeasureText(txt.c_str(), font_size);
                DrawRectangle(8, cur_y - 2, tw + 10, line_h, Fade(Color{14, 18, 24, 255}, 0.75f));
                DrawText(txt.c_str(), 12, cur_y, font_size, col);
                cur_y += line_h + 2;
            }
        }

        if (menu.is_paused()) {
            menu.draw();
        }

        EndDrawing();
        
        // Restore physical camera position
        camera.position.y -= smooth_step_offset;
        camera.target.y -= smooth_step_offset;
    }
    
    save_player_data();
    
    world.stop_simulation();
    global_thread_pool.clear_queue();
    world.save_all();
    global_thread_pool.wait_idle();
    DatabaseIO::get().wait_idle();
    
    {
        extern sqlite3* db;
        if (db) {
            // Guardar todos los cofres
            for (const auto& [pos, cd] : ui.world_chests) {
                json j;
                j["slots"] = json::array();
                for (const auto& cs : cd.slots) {
                    json sj;
                    sj["is_tool"] = cs.is_tool;
                    if (cs.is_tool) {
                        sj["tool_type"] = (int)cs.tool.type;
                        sj["tool_tier"] = (int)cs.tool.tier;
                        sj["dur_cur"] = cs.tool.durability_current;
                        sj["dur_max"] = cs.tool.durability_max;
                        sj["active"] = cs.tool.active;
                    } else {
                        sj["id"] = cs.item.id;
                        sj["count"] = cs.item.count;
                        sj["name"] = cs.item.name;
                    }
                    j["slots"].push_back(sj);
                }
                std::string json_str = j.dump();
                sqlite3_stmt* stmt;
                if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO chests (x, y, z, data) VALUES (?, ?, ?, ?)", -1, &stmt, 0) == SQLITE_OK) {
                    auto [cx, cy, cz] = pos;
                    sqlite3_bind_int(stmt, 1, cx);
                    sqlite3_bind_int(stmt, 2, cy);
                    sqlite3_bind_int(stmt, 3, cz);
                    sqlite3_bind_text(stmt, 4, json_str.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }

            sqlite3_close(db);
            db = nullptr;
        }
    }

    ItemModel3D::get().cleanup();
    UnloadTexture(spritesheet);
    UnloadTexture(spritesheet_items);
    UnloadTexture(tex_sun);
    UnloadTexture(tex_moon);
    UnloadTexture(tex_clouds);
    UnloadTexture(sky_side);
    UnloadTexture(sky_top);
    UnloadTexture(sky_bottom);
    
    CloseWindow();
    return 0;
}
