#include "ItemDrop.hpp"
#include "World.hpp"
#include <rlgl.h>
#include <raymath.h>
#include <cmath>
#include <algorithm>
#include <iostream>

void ItemDropManager::spawn(Vector3 pos, uint8_t id, bool is_item, int count, Vector3 initial_vel, float pickup_delay) {
    if (id == Config::AIR) return;
    ItemDrop drop;
    drop.position = pos;
    drop.velocity = initial_vel;
    drop.id = id;
    drop.is_item = is_item;
    drop.is_tool = false;
    drop.count = count;
    drop.lifetime = (float)(rand() % 100) * 0.05f;
    drop.pickup_delay = pickup_delay;
    drop.rot_y = (float)(rand() % 360);
    drops.push_back(drop);
}

void ItemDropManager::spawn_tool(Vector3 pos, Config::ToolType type, Config::ToolTier tier, int durability, Vector3 initial_vel, float pickup_delay) {
    ItemDrop drop;
    drop.position = pos;
    drop.velocity = initial_vel;
    drop.id = 0;
    drop.is_item = false;
    drop.is_tool = true;
    drop.tool_type = type;
    drop.tool_tier = tier;
    drop.durability = durability;
    drop.count = 1;
    drop.lifetime = (float)(rand() % 100) * 0.05f;
    drop.pickup_delay = pickup_delay;
    drop.rot_y = (float)(rand() % 360);
    drops.push_back(drop);
}

void ItemDropManager::clear() {
    drops.clear();
}

void ItemDropManager::update(float dt, World& world, Vector3 player_pos, UI& ui) {
    // Interpolación trilineal para físicas del drop en terreno suave
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

    auto get_ground_y = [&](float px, float pz, float ref_y) -> float {
        float ground_y = -999.0f;
        float scan_top = ref_y + 0.8f;
        float scan_bot = ref_y - 1.0f;
        float prev_y = scan_bot;
        float prev_d = sample_density(px, prev_y, pz);

        for (float y = scan_bot + 0.15f; y <= scan_top + 0.05f; y += 0.15f) {
            float d = sample_density(px, y, pz);
            if (prev_d >= Config::ISO_SURFACE && d < Config::ISO_SURFACE) {
                float lo = prev_y, hi = y;
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

        // Bloques de construcción
        int bx = (int)std::floor(px + 0.5f);
        int bz = (int)std::floor(pz + 0.5f);
        for (int dy = -1; dy <= 1; dy++) {
            int by = (int)std::floor(ref_y + 0.5f) + dy;
            uint8_t b = world.get_block(bx, by, bz);
            if (b != Config::AIR && b != Config::WATER && Config::BLOCKS.count(b) && Config::BLOCKS.at(b).shape != Config::SHAPE_TERRAIN) {
                float block_top = (float)by + 0.5f;
                if (block_top > ground_y && block_top <= ref_y + 0.8f) {
                    ground_y = block_top;
                }
            }
        }
        return ground_y;
    };

    Vector3 player_collect_pos = { player_pos.x, player_pos.y - 0.8f, player_pos.z };

    // 1. Físicas y movimiento de cada drop
    for (size_t i = 0; i < drops.size(); ++i) {
        auto& d = drops[i];
        d.lifetime += dt;
        d.rot_y += 75.0f * dt;
        if (d.rot_y >= 360.0f) d.rot_y -= 360.0f;
        if (d.pickup_delay > 0.0f) d.pickup_delay -= dt;

        // Gravedad
        d.velocity.y -= 14.0f * dt;
        d.position.x += d.velocity.x * dt;
        d.position.y += d.velocity.y * dt;
        d.position.z += d.velocity.z * dt;

        // Fricción aérea
        d.velocity.x *= (1.0f - 1.5f * dt);
        d.velocity.z *= (1.0f - 1.5f * dt);

        // Colisión con el terreno
        float ground = get_ground_y(d.position.x, d.position.z, d.position.y);
        if (ground > -900.0f && d.position.y <= ground + 0.15f) {
            d.position.y = ground + 0.15f;
            d.velocity.y = 0.0f;
            d.velocity.x *= 0.6f;
            d.velocity.z *= 0.6f;
        }

        // Magnetismo hacia el jugador
        float dist_to_player = Vector3Distance(d.position, player_collect_pos);
        if (dist_to_player < 2.5f && d.pickup_delay <= 0.0f) {
            Vector3 dir = Vector3Normalize(Vector3Subtract(player_collect_pos, d.position));
            float pull_speed = std::clamp((3.0f - dist_to_player) * 4.0f, 3.0f, 12.0f);
            d.position = Vector3Add(d.position, Vector3Scale(dir, pull_speed * dt));
        }
    }

    // 2. Fusión y apilado de drops cercanos del mismo tipo (Stacking) - excepto herramientas
    for (size_t i = 0; i < drops.size(); ++i) {
        if (drops[i].is_tool) continue;
        for (size_t j = i + 1; j < drops.size();) {
            if (!drops[j].is_tool && drops[i].id == drops[j].id && drops[i].is_item == drops[j].is_item) {
                float dist = Vector3Distance(drops[i].position, drops[j].position);
                if (dist < 1.2f) {
                    // Acumular cantidad en el drop principal y eliminar el secundario
                    drops[i].count += drops[j].count;
                    drops.erase(drops.begin() + j);
                    continue;
                }
            }
            ++j;
        }
    }

    // 3. Recolección al entrar en contacto con el jugador
    for (size_t i = 0; i < drops.size();) {
        auto& d = drops[i];
        float dist = Vector3Distance(d.position, player_collect_pos);
        if (dist < 0.6f && d.pickup_delay <= 0.0f) {
            if (d.is_tool) {
                ui.add_tool(d.tool_type, d.tool_tier, d.durability);
            } else if (d.is_item) {
                for (int c = 0; c < d.count; ++c) ui.add_item(d.id);
            } else {
                for (int c = 0; c < d.count; ++c) ui.add_resource(d.id);
            }
            drops.erase(drops.begin() + i);
        } else {
            ++i;
        }
    }
}

void ItemDropManager::draw(Texture2D spritesheet_tiles, Texture2D spritesheet_items, float light) {
    if (drops.empty()) return;

    float tw = 1.0f / 9.0f;
    float th = 1.0f / 10.0f;
    auto get_tile_uv = [&](int tx, int ty) -> std::array<Vector2, 4> {
        float u0 = (float)tx * tw;
        float v0 = (10.0f - 1.0f - (float)ty) * th;
        float u1 = u0 + tw;
        float v1 = v0 + th;
        return { Vector2{u0, v1}, Vector2{u1, v1}, Vector2{u1, v0}, Vector2{u0, v0} };
    };

    float itw = 1.0f / 7.0f;
    float ith = 1.0f / 8.0f;
    auto get_item_uv = [&](int ix, int iy) -> std::array<Vector2, 4> {
        float u0 = (float)ix * itw;
        float v0 = (8.0f - 1.0f - (float)iy) * ith;
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
            unsigned char c_t = (unsigned char)(255 * light);
            rlColor4ub(c_t, c_t, c_t, 255);
            auto uvs = get_tile_uv(top_tx, top_ty);
            rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
            rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y1, z1);
            rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y1, z1);
            rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z0);

            unsigned char c_b = (unsigned char)(140 * light);
            rlColor4ub(c_b, c_b, c_b, 255);
            uvs = get_tile_uv(bot_tx, bot_ty);
            rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z1);
            rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y0, z0);
            rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y0, z0);
            rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);

            unsigned char c_f = (unsigned char)(215 * light);
            rlColor4ub(c_f, c_f, c_f, 255);
            uvs = get_tile_uv(front_tx, front_ty);
            rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z1);
            rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);
            rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z1);
            rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z1);

            unsigned char c_bk = (unsigned char)(190 * light);
            rlColor4ub(c_bk, c_bk, c_bk, 255);
            uvs = get_tile_uv(back_tx, back_ty);
            rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z0);
            rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x0, y0, z0);
            rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x0, y1, z0);
            rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z0);

            unsigned char c_r = (unsigned char)(200 * light);
            rlColor4ub(c_r, c_r, c_r, 255);
            uvs = get_tile_uv(right_tx, right_ty);
            rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f(x1, y0, z1);
            rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(x1, y0, z0);
            rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(x1, y1, z0);
            rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f(x1, y1, z1);

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

    for (const auto& d : drops) {
        float bob_y = std::sin(d.lifetime * 3.5f) * 0.05f;
        Vector3 render_pos = { d.position.x, d.position.y + bob_y, d.position.z };

        rlPushMatrix();
            rlTranslatef(render_pos.x, render_pos.y, render_pos.z);
            rlRotatef(d.rot_y, 0.0f, 1.0f, 0.0f);

            if (d.is_tool) {
                // === HERRAMIENTA FLOTANTE ===
                int ix = 0, iy = 7;
                for (const auto& ti : Config::TOOLS) {
                    if (ti.type == d.tool_type && ti.tier == d.tool_tier) {
                        ix = ti.item_tex_x;
                        iy = ti.item_tex_y;
                        break;
                    }
                }
                auto uvs = get_item_uv(ix, iy);
                float s = 0.22f;

                rlSetTexture(spritesheet_items.id);
                rlBegin(RL_QUADS);
                    unsigned char c = (unsigned char)(255 * light);
                    rlColor4ub(c, c, c, 255);
                    // Cara frontal
                    rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-s, -s, 0.0f);
                    rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( s, -s, 0.0f);
                    rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( s,  s, 0.0f);
                    rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-s,  s, 0.0f);
                    // Cara trasera
                    rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( s, -s, 0.0f);
                    rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-s, -s, 0.0f);
                    rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-s,  s, 0.0f);
                    rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( s,  s, 0.0f);
                rlEnd();
                rlSetTexture(0);
            } else if (d.is_item) {
                // === ÍTEM BASE: QUAD 2D CON TEXTURA EN EL MUNDO ===
                int ix = 0, iy = 1;
                if (Config::ITEMS.count(d.id)) {
                    ix = Config::ITEMS.at(d.id).item_tex_x;
                    iy = Config::ITEMS.at(d.id).item_tex_y;
                }
                auto uvs = get_item_uv(ix, iy);
                float s = 0.18f;

                rlSetTexture(spritesheet_items.id);
                rlBegin(RL_QUADS);
                    unsigned char c = (unsigned char)(255 * light);
                    rlColor4ub(c, c, c, 255);
                    // Cara frontal
                    rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-s, -s, 0.0f);
                    rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( s, -s, 0.0f);
                    rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( s,  s, 0.0f);
                    rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-s,  s, 0.0f);
                    // Cara trasera
                    rlTexCoord2f(uvs[1].x, uvs[1].y); rlVertex3f( s, -s, 0.0f);
                    rlTexCoord2f(uvs[0].x, uvs[0].y); rlVertex3f(-s, -s, 0.0f);
                    rlTexCoord2f(uvs[3].x, uvs[3].y); rlVertex3f(-s,  s, 0.0f);
                    rlTexCoord2f(uvs[2].x, uvs[2].y); rlVertex3f( s,  s, 0.0f);
                rlEnd();
                rlSetTexture(0);
            } else {
                // === BLOQUE ===
                if (!Config::BLOCKS.count(d.id)) {
                    rlPopMatrix();
                    continue;
                }
                const auto& bt = Config::BLOCKS.at(d.id);
                int def_tx = bt.tex_x, def_ty = bt.tex_y;
                int top_tx = (bt.tex_top_x >= 0) ? bt.tex_top_x : def_tx;
                int top_ty = (bt.tex_top_y >= 0) ? bt.tex_top_y : def_ty;
                int bot_tx = (bt.tex_bottom_x >= 0) ? bt.tex_bottom_x : def_tx;
                int bot_ty = (bt.tex_bottom_y >= 0) ? bt.tex_bottom_y : def_ty;
                int front_tx = (bt.tex_front_x >= 0) ? bt.tex_front_x : def_tx;
                int front_ty = (bt.tex_front_y >= 0) ? bt.tex_front_y : def_ty;

                if (bt.shape == Config::SHAPE_TERRAIN) {
                    // === BLOQUE NATURAL: ROCA FACETADA 8 LADOS ===
                    if (d.id == Config::GRASS) {
                        top_tx = 6; top_ty = 8;
                        def_tx = 6; def_ty = 8;
                        bot_tx = 7; bot_ty = 4;
                    }

                    float n_scale = 1.8f;
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
                        // 1. Tapa Superior
                        for(int k=0; k<8; k++) {
                            int next = (k+1)%8;
                            float a0 = k * (PI / 4.0f);
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
                        // 2. Laterales Superiores
                        for(int k=0; k<8; k++) {
                            int next = (k+1)%8;
                            float a0 = k * (PI / 4.0f);
                            float a1 = next * (PI / 4.0f);
                            rlColor4ub(c_s, c_s, c_s, 255);
                            rlTexCoord2f(uvs_mid[3].x, uvs_mid[3].y); rlVertex3f(r_top * std::cos(a0), y_top, r_top * std::sin(a0));
                            rlTexCoord2f(uvs_mid[2].x, uvs_mid[2].y); rlVertex3f(r_top * std::cos(a1), y_top, r_top * std::sin(a1));
                            rlTexCoord2f(uvs_mid[1].x, uvs_mid[1].y); rlVertex3f(r_mid * std::cos(a1), y_mid, r_mid * std::sin(a1));
                            rlTexCoord2f(uvs_mid[0].x, uvs_mid[0].y); rlVertex3f(r_mid * std::cos(a0), y_mid, r_mid * std::sin(a0));
                        }
                        // 3. Laterales Inferiores
                        for(int k=0; k<8; k++) {
                            int next = (k+1)%8;
                            float a0 = k * (PI / 4.0f);
                            float a1 = next * (PI / 4.0f);
                            rlColor4ub(c_ls, c_ls, c_ls, 255);
                            rlTexCoord2f(uvs_mid[3].x, uvs_mid[3].y); rlVertex3f(r_mid * std::cos(a0), y_mid, r_mid * std::sin(a0));
                            rlTexCoord2f(uvs_mid[2].x, uvs_mid[2].y); rlVertex3f(r_mid * std::cos(a1), y_mid, r_mid * std::sin(a1));
                            rlTexCoord2f(uvs_mid[1].x, uvs_mid[1].y); rlVertex3f(r_bot * std::cos(a1), y_bot, r_bot * std::sin(a1));
                            rlTexCoord2f(uvs_mid[0].x, uvs_mid[0].y); rlVertex3f(r_bot * std::cos(a0), y_bot, r_bot * std::sin(a0));
                        }
                        // 4. Base Inferior
                        for(int k=0; k<8; k++) {
                            int next = (k+1)%8;
                            float a0 = k * (PI / 4.0f);
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
                } else if (bt.shape == Config::SHAPE_TORCH) {
                    draw_tex_box(-0.03f, -0.15f, -0.03f, 0.03f, 0.15f, 0.03f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                } else if (bt.shape == Config::SHAPE_CHEST) {
                    float cs = 0.10f;
                    int latch_tx = bt.tex_latch_x >= 0 ? bt.tex_latch_x : 0;
                    int latch_ty = bt.tex_latch_y >= 0 ? bt.tex_latch_y : 8;
                    draw_tex_box(-0.02f, -0.02f, cs, 0.02f, 0.04f, cs + 0.02f, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty, latch_tx, latch_ty);
                    draw_tex_box(-cs, -cs, -cs, cs, cs, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                } else if (bt.shape == Config::SHAPE_STAIRS) {
                    float cs = 0.10f;
                    draw_tex_box(-cs, -cs, -cs, cs, 0.0f, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                    draw_tex_box(-cs, 0.0f, 0.0f, cs, cs, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                } else {
                    float cs = 0.10f;
                    draw_tex_box(-cs, -cs, -cs, cs, cs, cs, top_tx, top_ty, bot_tx, bot_ty, front_tx, front_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty);
                }
            }

        rlPopMatrix();
    }
}
