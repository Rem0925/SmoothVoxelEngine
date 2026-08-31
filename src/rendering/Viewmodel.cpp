#include "rendering/Viewmodel.hpp"
#include "ui/UI.hpp"
#include "core/Config.hpp"
#include "gameplay/ItemModel3D.hpp"
#include "rlgl.h"
#include <GL/gl.h>
#include <raylib.h>
#include <raymath.h>
#include <cmath>
#include <algorithm>

using namespace Config;

void DrawFirstPersonViewmodel(
    ViewmodelState& state,
    UI& ui,
    Texture2D spritesheet_tiles,
    Texture2D spritesheet_items,
    float light_intensity,
    bool is_mining,
    float mining_progress,
    bool is_grounded,
    float dt
) {
    bool is_moving = is_grounded && (IsKeyDown(KEY_W) || IsKeyDown(KEY_A) || IsKeyDown(KEY_S) || IsKeyDown(KEY_D));
    if (is_moving) {
        state.walk_bob_timer += dt * 9.0f;
        float target_bob_x = std::sin(state.walk_bob_timer) * 0.012f;
        float target_bob_y = std::abs(std::cos(state.walk_bob_timer)) * 0.010f;
        state.bob_x = Lerp(state.bob_x, target_bob_x, 15.0f * dt);
        state.bob_y = Lerp(state.bob_y, target_bob_y, 15.0f * dt);
    } else {
        state.bob_x = Lerp(state.bob_x, 0.0f, 10.0f * dt);
        state.bob_y = Lerp(state.bob_y, 0.0f, 10.0f * dt);
    }

    int current_tool_idx = (ui.selected_slot == 0) ? ui.selected_tool_idx : -2;
    if (ui.selected_slot != state.prev_slot || current_tool_idx != state.prev_tool_idx) {
        state.prev_slot = ui.selected_slot;
        state.prev_tool_idx = current_tool_idx;
        state.equip_anim = 0.0f;
    }
    if (state.equip_anim < 1.0f) {
        state.equip_anim = std::min(1.0f, state.equip_anim + dt * 6.0f);
    }
    float equip_y = (1.0f - state.equip_anim) * -0.32f;

    if (is_mining) {
        state.swing_timer += dt * 6.0f;
        if (state.swing_timer >= 1.0f) state.swing_timer -= 1.0f;
    } else {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && state.swing_timer <= 0.0f) {
            state.swing_timer = 0.01f;
        }
        if (state.swing_timer > 0.0f) {
            state.swing_timer += dt * 5.0f;
            if (state.swing_timer >= 1.0f) state.swing_timer = 0.0f;
        }
    }
    float swing_sin = (state.swing_timer > 0.0f) ? std::sin(state.swing_timer * PI) : 0.0f;

    float bob_x = state.bob_x;
    float bob_y = state.bob_y;

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
        rlTranslatef(-0.28f - bob_x, -0.20f + bob_y + equip_y, 0.50f);

        if (swing_sin > 0.0f) {
            rlTranslatef(swing_sin * 0.08f, swing_sin * 0.04f, -swing_sin * 0.06f);
            rlRotatef(swing_sin * 38.0f, 1.0f, 0.0f, 0.0f);
            rlRotatef(swing_sin * 20.0f, 0.0f, 1.0f, 0.0f);
            rlRotatef(-swing_sin * 14.0f, 0.0f, 0.0f, 1.0f);
        }

        rlRotatef(-14.0f, 1.0f, 0.0f, 0.0f);
        rlRotatef(14.0f, 0.0f, 1.0f, 0.0f);
        rlRotatef(-4.0f, 0.0f, 0.0f, 1.0f);

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
                rlColor4ub(c_top.r, c_top.g, c_top.b, c_top.a);
                rlVertex3f(x0, y1, z0); rlVertex3f(x0, y1, z1); rlVertex3f(x1, y1, z1); rlVertex3f(x1, y1, z0);
                rlColor4ub(c_bot.r, c_bot.g, c_bot.b, c_bot.a);
                rlVertex3f(x0, y0, z1); rlVertex3f(x0, y0, z0); rlVertex3f(x1, y0, z0); rlVertex3f(x1, y0, z1);
                rlColor4ub(c_front.r, c_front.g, c_front.b, c_front.a);
                rlVertex3f(x0, y0, z1); rlVertex3f(x1, y0, z1); rlVertex3f(x1, y1, z1); rlVertex3f(x0, y1, z1);
                rlColor4ub(c_front.r, c_front.g, c_front.b, c_front.a);
                rlVertex3f(x1, y0, z0); rlVertex3f(x0, y0, z0); rlVertex3f(x0, y1, z0); rlVertex3f(x1, y1, z0);
                rlColor4ub(c_right.r, c_right.g, c_right.b, c_right.a);
                rlVertex3f(x1, y0, z1); rlVertex3f(x1, y0, z0); rlVertex3f(x1, y1, z0); rlVertex3f(x1, y1, z1);
                rlColor4ub(c_left.r, c_left.g, c_left.b, c_left.a);
                rlVertex3f(x0, y0, z0); rlVertex3f(x0, y0, z1); rlVertex3f(x0, y1, z1); rlVertex3f(x0, y1, z0);
            rlEnd();
        };

        draw_shaded_box(-0.065f, -0.065f, -0.50f, 0.065f, 0.065f, -0.16f, Color{ 0, 155, 175, 255 });
        draw_shaded_box(-0.060f, -0.060f, -0.16f, 0.060f, 0.060f, 0.06f, Color{ 215, 155, 125, 255 });

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

        auto draw_tex_box = [&](float x0, float y0, float z0, float x1, float y1, float z1,
                                 int top_tx, int top_ty, int bot_tx, int bot_ty,
                                 int front_tx, int front_ty, int back_tx, int back_ty,
                                 int right_tx, int right_ty, int left_tx, int left_ty) {
            rlSetTexture(spritesheet_tiles.id);
            rlBegin(RL_QUADS);
                unsigned char c_t = (unsigned char)(255 * light);
                rlColor4ub(c_t, c_t, c_t, 255);
                auto uvs = Config::get_tile_uv(top_tx, top_ty);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y1, z1);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y1, z1);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z0);

                unsigned char c_b = (unsigned char)(140 * light);
                rlColor4ub(c_b, c_b, c_b, 255);
                uvs = Config::get_tile_uv(bot_tx, bot_ty);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z1);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y0, z0);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y0, z0);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);

                unsigned char c_f = (unsigned char)(215 * light);
                rlColor4ub(c_f, c_f, c_f, 255);
                uvs = Config::get_tile_uv(front_tx, front_ty);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z1);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z1);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z1);

                unsigned char c_bk = (unsigned char)(190 * light);
                rlColor4ub(c_bk, c_bk, c_bk, 255);
                uvs = Config::get_tile_uv(back_tx, back_ty);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z0);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z0);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z0);

                unsigned char c_r = (unsigned char)(200 * light);
                rlColor4ub(c_r, c_r, c_r, 255);
                uvs = Config::get_tile_uv(right_tx, right_ty);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x1, y0, z0);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x1, y1, z0);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z1);

                unsigned char c_l = (unsigned char)(175 * light);
                rlColor4ub(c_l, c_l, c_l, 255);
                uvs = Config::get_tile_uv(left_tx, left_ty);
                rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z0);
                rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x0, y0, z1);
                rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x0, y1, z1);
                rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
            rlEnd();
            rlSetTexture(0);
        };

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

            Color block_tint = WHITE;
            if (bt.is_foliage || held_block == Config::LEAVES) {
                block_tint = Color{ 85, 168, 55, 255 };
            } else if (bt.is_grass || held_block == Config::GRASS || held_block == Config::TALL_GRASS) {
                block_tint = Color{ 117, 185, 68, 255 };
            }

            auto tint_c = [&](unsigned char light_v, Color t) -> Color {
                return Color{
                    (unsigned char)((light_v * t.r) / 255),
                    (unsigned char)((light_v * t.g) / 255),
                    (unsigned char)((light_v * t.b) / 255),
                    255
                };
            };

            if (is_plant) {
                auto uvs = Config::get_tile_uv(bt.tex_x, bt.tex_y);
                unsigned char c_plant = (unsigned char)(255 * light);
                Color col_p = tint_c(c_plant, block_tint);

                rlPushMatrix();
                    rlTranslatef(0.01f, 0.06f, 0.04f);
                    float ps_w = 0.20f;
                    float ps_h = 0.28f;
                    rlSetTexture(spritesheet_tiles.id);
                    rlBegin(RL_QUADS);
                        rlColor4ub(col_p.r, col_p.g, col_p.b, 255);
                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h,  ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h, -ps_w*0.5f);

                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h, -ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h,  ps_w*0.5f);

                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h, -ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h,  ps_w*0.5f);

                        rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( ps_w*0.5f, 0.0f, -ps_w*0.5f);
                        rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-ps_w*0.5f, 0.0f,  ps_w*0.5f);
                        rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-ps_w*0.5f, ps_h,  ps_w*0.5f);
                        rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( ps_w*0.5f, ps_h, -ps_w*0.5f);
                    rlEnd();
                    rlSetTexture(0);
                rlPopMatrix();
            }
            else if (bt.shape != Config::SHAPE_TERRAIN) {
                int def_tx = bt.tex_x, def_ty = bt.tex_y;
                int top_tx = bt.tex_top_x >= 0 ? bt.tex_top_x : def_tx, top_ty = bt.tex_top_y >= 0 ? bt.tex_top_y : def_ty;
                int bot_tx = bt.tex_bottom_x >= 0 ? bt.tex_bottom_x : def_tx, bot_ty = bt.tex_bottom_y >= 0 ? bt.tex_bottom_y : def_ty;
                int front_tx = bt.tex_front_x >= 0 ? bt.tex_front_x : def_tx, front_ty = bt.tex_front_y >= 0 ? bt.tex_front_y : def_ty;

                rlPushMatrix();
                    rlTranslatef(0.02f, 0.06f, 0.06f);
                    rlRotatef(45.0f, 0.0f, 1.0f, 0.0f);
                    rlRotatef(22.0f, 1.0f, 0.0f, 0.0f);
                    rlRotatef(-10.0f, 0.0f, 0.0f, 1.0f);

                    float scale = 0.72f;
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

                    float n_scale = 1.20f;
                    float r_top = 0.04f * n_scale;
                    float r_mid = 0.06f * n_scale;
                    float r_bot = 0.045f * n_scale;
                    float y_peak = +0.06f * n_scale;
                    float y_top  = +0.035f * n_scale;
                    float y_mid  =  0.00f;
                    float y_bot  = -0.04f * n_scale;

                    auto uvs_top = Config::get_tile_uv(top_tx, top_ty);
                    auto uvs_mid = Config::get_tile_uv(def_tx, def_ty);
                    auto uvs_bot = Config::get_tile_uv(bot_tx, bot_ty);

                    unsigned char c_t = (unsigned char)(255 * light);
                    unsigned char c_s = (unsigned char)(200 * light);
                    unsigned char c_ls = (unsigned char)(160 * light);
                    unsigned char c_b = (unsigned char)(120 * light);

                    Color col_t = tint_c(c_t, block_tint);
                    Color col_s = tint_c(c_s, block_tint);
                    Color col_ls = (held_block == Config::GRASS) ? Color{ c_ls, c_ls, c_ls, 255 } : tint_c(c_ls, block_tint);
                    Color col_b = (held_block == Config::GRASS) ? Color{ c_b, c_b, c_b, 255 } : tint_c(c_b, block_tint);

                    rlSetTexture(spritesheet_tiles.id);
                    rlBegin(RL_QUADS);

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

                        rlColor4ub(col_t.r, col_t.g, col_t.b, 255);
                        rlTexCoord2f(u_cen_top, v_cen_top); rlVertex3f(0.0f, y_peak, 0.0f);
                        rlTexCoord2f(u_cen_top, v_cen_top); rlVertex3f(0.0f, y_peak, 0.0f);
                        rlTexCoord2f(uv_x1, uv_y1); rlVertex3f(r_top * std::cos(a1), y_top, r_top * std::sin(a1));
                        rlTexCoord2f(uv_x0, uv_y0); rlVertex3f(r_top * std::cos(a0), y_top, r_top * std::sin(a0));
                    }

                    for(int i=0; i<8; i++) {
                        int next = (i+1)%8;
                        float a0 = i * (PI / 4.0f);
                        float a1 = next * (PI / 4.0f);

                        rlColor4ub(col_s.r, col_s.g, col_s.b, 255);
                        rlTexCoord2f(uvs_mid[3].x, uvs_mid[3].y); rlVertex3f(r_top * std::cos(a0), y_top, r_top * std::sin(a0));
                        rlTexCoord2f(uvs_mid[2].x, uvs_mid[2].y); rlVertex3f(r_top * std::cos(a1), y_top, r_top * std::sin(a1));
                        rlTexCoord2f(uvs_mid[1].x, uvs_mid[1].y); rlVertex3f(r_mid * std::cos(a1), y_mid, r_mid * std::sin(a1));
                        rlTexCoord2f(uvs_mid[0].x, uvs_mid[0].y); rlVertex3f(r_mid * std::cos(a0), y_mid, r_mid * std::sin(a0));
                    }

                    for(int i=0; i<8; i++) {
                        int next = (i+1)%8;
                        float a0 = i * (PI / 4.0f);
                        float a1 = next * (PI / 4.0f);

                        rlColor4ub(col_ls.r, col_ls.g, col_ls.b, 255);
                        rlTexCoord2f(uvs_mid[3].x, uvs_mid[3].y); rlVertex3f(r_mid * std::cos(a0), y_mid, r_mid * std::sin(a0));
                        rlTexCoord2f(uvs_mid[2].x, uvs_mid[2].y); rlVertex3f(r_mid * std::cos(a1), y_mid, r_mid * std::sin(a1));
                        rlTexCoord2f(uvs_mid[1].x, uvs_mid[1].y); rlVertex3f(r_bot * std::cos(a1), y_bot, r_bot * std::sin(a1));
                        rlTexCoord2f(uvs_mid[0].x, uvs_mid[0].y); rlVertex3f(r_bot * std::cos(a0), y_bot, r_bot * std::sin(a0));
                    }

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

                        rlColor4ub(col_b.r, col_b.g, col_b.b, 255);
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
