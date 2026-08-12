#include <raylib.h>
#include <raymath.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "Config.hpp"
#include "MarchingCubes.hpp"
#include "World.hpp"
#include "UI.hpp"
#include <rlgl.h>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>

using namespace Config;

bool VoxelRaycast(World& world, Vector3 origin, Vector3 dir, float max_dist, Vector3& out_hit, Vector3& out_prev) {
    float t = 0.0f;
    float step = 0.05f;
    
    Vector3 current = origin;
    Vector3 previous = origin;
    
    while (t <= max_dist) {
        current = Vector3Add(origin, Vector3Scale(dir, t));
        
        int bx = std::floor(current.x);
        int by = std::floor(current.y);
        int bz = std::floor(current.z);
        
        uint8_t block = world.get_block(bx, by, bz);
        
        if (block != AIR && block != WATER) { 
            out_hit = { (float)bx, (float)by, (float)bz };
            out_prev = { std::floor(previous.x), std::floor(previous.y), std::floor(previous.z) };
            return true;
        }
        
        previous = current;
        t += step;
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

int main() {
    InitWindow(1280, 720, "Smooth Voxel Engine C++");
    SetTargetFPS(60);
    DisableCursor();

    Texture2D spritesheet = LoadTexture("assets/textures/spritesheet_tiles.png");
    SetTextureFilter(spritesheet, TEXTURE_FILTER_POINT);
    
    Texture2D tex_sun = LoadTexture("assets/textures/sun.png");
    SetTextureFilter(tex_sun, TEXTURE_FILTER_POINT);
    
    Texture2D tex_moon = LoadTexture("assets/textures/moon.png");
    SetTextureFilter(tex_moon, TEXTURE_FILTER_POINT);
    
    Texture2D tex_clouds = LoadTexture("assets/textures/clouds.png");
    SetTextureFilter(tex_clouds, TEXTURE_FILTER_POINT);
    SetTextureWrap(tex_clouds, TEXTURE_WRAP_REPEAT);
    
    Texture2D sky_side = LoadTexture("assets/textures/skybox_sideClouds.png");
    SetTextureFilter(sky_side, TEXTURE_FILTER_POINT);
    SetTextureWrap(sky_side, TEXTURE_WRAP_CLAMP);
    Texture2D sky_top = LoadTexture("assets/textures/skybox_top.png");
    SetTextureFilter(sky_top, TEXTURE_FILTER_POINT);
    SetTextureWrap(sky_top, TEXTURE_WRAP_CLAMP);
    Texture2D sky_bottom = LoadTexture("assets/textures/skybox_bottom.png");
    SetTextureFilter(sky_bottom, TEXTURE_FILTER_POINT);
    SetTextureWrap(sky_bottom, TEXTURE_WRAP_CLAMP);
    

    Shader terrainShader = LoadShader("assets/shaders/terrain.vs", "assets/shaders/terrain.fs");
    Shader waterShader = LoadShader("assets/shaders/water.vs", "assets/shaders/water.fs");
    
    Material mat_solid = LoadMaterialDefault();
    mat_solid.shader = terrainShader;

    mat_solid.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
    
    Material mat_water = LoadMaterialDefault();
    mat_water.shader = waterShader;
    mat_water.maps[MATERIAL_MAP_DIFFUSE].texture = spritesheet;
    mat_water.maps[MATERIAL_MAP_DIFFUSE].color = { 255, 255, 255, 200 };

    World world(mat_solid, mat_water);
    UI ui(spritesheet);
    
    std::string world_name = "world1";
    std::string save_dir = "worlds/" + world_name;
    std::filesystem::create_directories(save_dir);
    
    if (sqlite3_open((save_dir + "/chunks.db").c_str(), &db) == SQLITE_OK) {
        const char* sql = "CREATE TABLE IF NOT EXISTS chunks (cx INTEGER, cz INTEGER, chunk_data BLOB, PRIMARY KEY(cx, cz))";
        sqlite3_exec(db, sql, 0, 0, 0);
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
    float smooth_step_offset = 0.0f;

    std::ifstream file(save_dir + "/player.json");
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto get_val = [&](std::string key, float def) -> float {
            size_t pos = content.find("\"" + key + "\"");
            if (pos != std::string::npos) {
                size_t colon = content.find(":", pos);
                return std::stof(content.substr(colon + 1));
            }
            return def;
        };
        camera.position.x = get_val("pos_x", 0.0f);
        camera.position.y = get_val("pos_y", 100.0f);
        camera.position.z = get_val("pos_z", 0.0f);
        
        camera.target.x = get_val("target_x", camera.position.x);
        camera.target.y = get_val("target_y", camera.position.y);
        camera.target.z = get_val("target_z", camera.position.z + 1.0f);
        
        day_time = get_val("day_time", 1.5f);
        
        for (size_t i = 0; i < ui.slots.size(); i++) {
            size_t slot_pos = content.find("\"slot_" + std::to_string(i) + "_id\"");
            if (slot_pos != std::string::npos) {
                ui.slots[i].id = (uint8_t)get_val("slot_" + std::to_string(i) + "_id", Config::AIR);
                ui.slots[i].count = (int)get_val("slot_" + std::to_string(i) + "_count", 0);
                if (Config::BLOCKS.count(ui.slots[i].id)) {
                    ui.slots[i].name = Config::BLOCKS.at(ui.slots[i].id).name;
                }
            }
        }
    }

    while (!WindowShouldClose()) {
        // Ciclo completo (2*PI) en 20 minutos (1200 segundos) como en Minecraft
        // Velocidad = 2*PI / 1200 = PI / 600
        day_time += GetFrameTime() * (PI / 600.0f);
        
        if (IsKeyPressed(KEY_G)) {
            spectator_mode = !spectator_mode;
            player_vel_y = 0.0f;
        }
        
        if (!ui.is_open) {
            Vector3 old_pos = camera.position;
            UpdateCamera(&camera, spectator_mode ? CAMERA_FREE : CAMERA_FIRST_PERSON);
            Vector3 post_update_pos = camera.position;
            
            if (!spectator_mode) {
                int cx = std::floor(camera.position.x / Config::CHUNK_SIZE);
                int cz = std::floor(camera.position.z / Config::CHUNK_SIZE);
                Chunk* current_chunk = world.get_chunk(cx, cz);
                
                if (current_chunk && current_chunk->is_ready) {
                    float r = 0.25f; // cylinder radius
                    auto is_solid = [&](float x, float y, float z) {
                        uint8_t b = world.get_block(std::floor(x), std::floor(y), std::floor(z));
                        return b != AIR && b != WATER && b != LEAVES;
                    };
                auto check_wall = [&](float vx, float vz, float y) {
                    return is_solid(vx - r, y, vz - r) || is_solid(vx + r, y, vz - r) ||
                           is_solid(vx - r, y, vz + r) || is_solid(vx + r, y, vz + r);
                };

                float new_x = camera.position.x;
                float old_x = old_pos.x;
                float new_z = camera.position.z;
                float old_z = old_pos.z;
                
                // Horizontal collision (X)
                float base_y = camera.position.y - 1.4f;
                float head_y = camera.position.y - 0.2f;
                if (check_wall(new_x, old_z, base_y) || check_wall(new_x, old_z, head_y)) {
                    if (!check_wall(new_x, old_z, base_y + 1.05f) && !check_wall(new_x, old_z, head_y + 1.05f)) {
                        camera.position.y += 1.05f; // Auto-step
                        smooth_step_offset -= 1.05f;
                    } else {
                        camera.position.x = old_x;
                        new_x = old_x;
                    }
                }
                
                // Horizontal collision (Z)
                base_y = camera.position.y - 1.4f; 
                head_y = camera.position.y - 0.2f;
                if (check_wall(new_x, new_z, base_y) || check_wall(new_x, new_z, head_y)) {
                    if (!check_wall(new_x, new_z, base_y + 1.05f) && !check_wall(new_x, new_z, head_y + 1.05f)) {
                        camera.position.y += 1.05f; // Auto-step
                        smooth_step_offset -= 1.05f;
                    } else {
                        camera.position.z = old_z;
                    }
                }
                
                // Vertical collision (Y)
                player_vel_y -= 30.0f * GetFrameTime();
                camera.position.y += player_vel_y * GetFrameTime();
                
                // Ceiling collision
                if (player_vel_y > 0 && check_wall(camera.position.x, camera.position.z, camera.position.y + 0.2f)) {
                    camera.position.y = std::floor(camera.position.y + 0.2f) - 0.21f;
                    player_vel_y = 0.0f;
                }
                
                // Floor collision
                is_grounded = false;
                if (player_vel_y <= 0 && check_wall(camera.position.x, camera.position.z, camera.position.y - 1.5f)) {
                    camera.position.y = std::floor(camera.position.y - 1.5f) + 1.0f + 1.5f;
                    player_vel_y = 0.0f;
                    is_grounded = true;
                }
                
                if (is_grounded && IsKeyPressed(KEY_SPACE)) {
                    player_vel_y = 11.0f;
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
            Vector3 hit, prev;
            if (VoxelRaycast(world, camera.position, forward, 8.0f, hit, prev)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && ui.selected_slot == 0) {
                    uint8_t broken_block = world.get_block(std::floor(hit.x), std::floor(hit.y), std::floor(hit.z));
                    if (broken_block != AIR && broken_block != WATER) {
                        ui.add_resource(broken_block);
                    }
                    bool fill_water = false;
                    if (hit.y <= Config::WATER_LEVEL) { 
                        if (world.get_block(hit.x+1, hit.y, hit.z) == Config::WATER ||
                            world.get_block(hit.x-1, hit.y, hit.z) == Config::WATER ||
                            world.get_block(hit.x, hit.y, hit.z+1) == Config::WATER ||
                            world.get_block(hit.x, hit.y, hit.z-1) == Config::WATER ||
                            world.get_block(hit.x, hit.y+1, hit.z) == Config::WATER ||
                            world.get_block(hit.x, hit.y-1, hit.z) == Config::WATER) {
                            fill_water = true;
                        }
                    }
                    world.set_block(hit.x, hit.y, hit.z, fill_water ? Config::WATER : AIR);
                } 
                else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && ui.selected_slot != 0) {
                    if (ui.slots[ui.selected_slot].count > 0) {
                        world.set_block(prev.x, prev.y, prev.z, ui.slots[ui.selected_slot].id);
                        ui.slots[ui.selected_slot].count--;
                    }
                }
            }
        }
        
        ui.update();
        world.update(camera.position);

        float shaderTime = GetTime();
        int timeLocSolid = GetShaderLocation(world.mat_solid.shader, "time");
        SetShaderValue(world.mat_solid.shader, timeLocSolid, &shaderTime, SHADER_UNIFORM_FLOAT);
        int camPosLocSolid = GetShaderLocation(world.mat_solid.shader, "cameraPos");
        SetShaderValue(world.mat_solid.shader, camPosLocSolid, &camera.position, SHADER_UNIFORM_VEC3);
        
        int timeLocWater = GetShaderLocation(world.mat_water.shader, "time");
        SetShaderValue(world.mat_water.shader, timeLocWater, &shaderTime, SHADER_UNIFORM_FLOAT);
        int camPosLocWater = GetShaderLocation(world.mat_water.shader, "cameraPos");
        SetShaderValue(world.mat_water.shader, camPosLocWater, &camera.position, SHADER_UNIFORM_VEC3);

        BeginDrawing();
        
        float sun_y = std::sin(day_time);
        float light_intensity = std::max(0.1f, sun_y);
        Color sky_color = { 
            (unsigned char)(135 * light_intensity), 
            (unsigned char)(206 * light_intensity), 
            (unsigned char)(235 * light_intensity), 
            255 
        };
        ClearBackground(sky_color);
        
        // Pass dynamic fog color to shaders
        float fogColorVec[3] = { sky_color.r / 255.0f, sky_color.g / 255.0f, sky_color.b / 255.0f };
        int fogLocSolid = GetShaderLocation(world.mat_solid.shader, "fogColor");
        SetShaderValue(world.mat_solid.shader, fogLocSolid, fogColorVec, SHADER_UNIFORM_VEC3);
        int fogLocWater = GetShaderLocation(world.mat_water.shader, "fogColor");
        SetShaderValue(world.mat_water.shader, fogLocWater, fogColorVec, SHADER_UNIFORM_VEC3);
        
        // Dynamically update material colors for lighting
        unsigned char light_val = (unsigned char)(255 * light_intensity);
        world.mat_solid.maps[MATERIAL_MAP_DIFFUSE].color = { light_val, light_val, light_val, 255 };
        world.mat_water.maps[MATERIAL_MAP_DIFFUSE].color = { light_val, light_val, light_val, 200 };

        // Apply smooth camera transition for auto-steps
        if (!spectator_mode) {
            smooth_step_offset = Lerp(smooth_step_offset, 0.0f, 15.0f * GetFrameTime());
        } else {
            smooth_step_offset = 0.0f;
        }
        
        camera.position.y += smooth_step_offset;
        camera.target.y += smooth_step_offset;

        BeginMode3D(camera);
        
        DrawSkybox(camera, sky_side, sky_top, sky_bottom, light_intensity);
        
        rlDisableDepthMask();
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        
        float sun_alpha = std::clamp(sun_y / 0.15f, 0.0f, 1.0f);
        float moon_alpha = std::clamp(-sun_y / 0.15f, 0.0f, 1.0f);
        
        rlEnableColorBlend();
        
        rlPushMatrix();
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

        world.draw(camera.position);
        
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        Vector3 hit, prev;

        if (!ui.is_open && VoxelRaycast(world, camera.position, forward, 8.0f, hit, prev)) {
            rlDisableDepthMask();
            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            if (ui.selected_slot == 0) { // Mine
                float d_slice[27];
                for(int i=0; i<27; i++) d_slice[i] = -1.0f;
                int bx = std::floor(hit.x);
                int by = std::floor(hit.y);
                int bz = std::floor(hit.z);
                for(int dx=-1; dx<=1; dx++){
                    for(int dy=-1; dy<=1; dy++){
                        for(int dz=-1; dz<=1; dz++){
                            d_slice[(dy+1)*9 + (dz+1)*3 + (dx+1)] = world.get_density(bx+dx, by+dy, bz+dz);
                        }
                    }
                }
                d_slice[1*9 + 1*3 + 1] = -1.0f; // Preview
                
                std::vector<Vector3> g_verts, g_norms;
                std::vector<Vector2> g_uvs, g_uvs2; std::vector<Color> g_cols;
                mc::generate(d_slice, nullptr, 3, 3, 3, 0.0f, Config::AIR, g_verts, g_norms, g_uvs, g_uvs2, g_cols);
                
                rlPushMatrix();
                rlTranslatef(bx - 1.0f, by - 1.0f, bz - 1.0f);
                rlSetTexture(spritesheet.id);
                rlBegin(RL_TRIANGLES);
                rlColor4ub(255, 100, 100, 150);
                for (size_t i = 0; i < g_verts.size(); ++i) {
                    rlTexCoord2f(g_uvs[i].x, g_uvs[i].y);
                    rlVertex3f(g_verts[i].x, g_verts[i].y, g_verts[i].z);
                }
                rlEnd();
                rlSetTexture(0);
                rlPopMatrix();
            } else if (ui.slots[ui.selected_slot].count > 0) { // Place
                float d_slice[27];
                for(int i=0; i<27; i++) d_slice[i] = -1.0f;
                int bx = std::floor(prev.x);
                int by = std::floor(prev.y);
                int bz = std::floor(prev.z);
                for(int dx=-1; dx<=1; dx++){
                    for(int dy=-1; dy<=1; dy++){
                        for(int dz=-1; dz<=1; dz++){
                            d_slice[(dy+1)*9 + (dz+1)*3 + (dx+1)] = world.get_density(bx+dx, by+dy, bz+dz);
                        }
                    }
                }
                d_slice[1*9 + 1*3 + 1] = 1.0f; // Preview
                
                std::vector<Vector3> g_verts, g_norms;
                std::vector<Vector2> g_uvs, g_uvs2; std::vector<Color> g_cols;
                mc::generate(d_slice, nullptr, 3, 3, 3, 0.0f, ui.slots[ui.selected_slot].id, g_verts, g_norms, g_uvs, g_uvs2, g_cols);
                
                rlPushMatrix();
                rlTranslatef(bx - 1.0f, by - 1.0f, bz - 1.0f);
                rlSetTexture(spritesheet.id);
                rlBegin(RL_TRIANGLES);
                rlColor4ub(100, 255, 100, 150);
                for (size_t i = 0; i < g_verts.size(); ++i) {
                    rlTexCoord2f(g_uvs[i].x, g_uvs[i].y);
                    rlVertex3f(g_verts[i].x, g_verts[i].y, g_verts[i].z);
                }
                rlEnd();
                rlSetTexture(0);
                rlPopMatrix();
            }
            rlEnableDepthTest();
            rlEnableDepthMask();
            rlEnableBackfaceCulling();
        }
        EndMode3D();

        int cam_x = std::floor(camera.position.x);
        int cam_y = std::floor(camera.position.y);
        int cam_z = std::floor(camera.position.z);
        if (world.get_block(cam_x, cam_y, cam_z) == WATER && camera.position.y < (WATER_LEVEL - 0.05f)) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), { 10, 50, 150, 100 });
        }

        ui.draw();
        
        DrawFPS(10, 10);
        const char* spec_txt = spectator_mode ? "  [MODO ESPECTADOR]" : "";
        DrawText(TextFormat("X: %d  Y: %d  Z: %d%s", cam_x, cam_y, cam_z, spec_txt), 10, 40, 20, BLACK);

        uint8_t look_block = AIR;
        if (!ui.is_open && VoxelRaycast(world, camera.position, forward, 8.0f, hit, prev)) {
            look_block = world.get_block(std::floor(hit.x), std::floor(hit.y), std::floor(hit.z));
        }
        std::string block_name = (look_block != AIR && BLOCKS.count(look_block)) ? BLOCKS.at(look_block).name : "Aire";
        DrawText(TextFormat("Mirando: %s", block_name.c_str()), 10, 70, 20, BLACK);
        
        EndDrawing();
        
        // Restore physical camera position
        camera.position.y -= smooth_step_offset;
        camera.target.y -= smooth_step_offset;
    }
    
    std::ofstream out(save_dir + "/player.json");
    if (out.is_open()) {
        out << "{\n";
        out << "  \"pos_x\": " << camera.position.x << ",\n";
        out << "  \"pos_y\": " << camera.position.y << ",\n";
        out << "  \"pos_z\": " << camera.position.z << ",\n";
        out << "  \"target_x\": " << camera.target.x << ",\n";
        out << "  \"target_y\": " << camera.target.y << ",\n";
        out << "  \"target_z\": " << camera.target.z << ",\n";
        out << "  \"day_time\": " << day_time << ",\n";
        for (size_t i=0; i<ui.slots.size(); i++) {
            out << "  \"slot_" << i << "_id\": " << (int)ui.slots[i].id << ",\n";
            out << "  \"slot_" << i << "_count\": " << (int)ui.slots[i].count << (i == ui.slots.size()-1 ? "\n" : ",\n");
        }
        out << "}\n";
        out.close();
    }
    
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }

    UnloadTexture(spritesheet);
    UnloadTexture(tex_sun);
    UnloadTexture(tex_moon);
    UnloadTexture(tex_clouds);
    UnloadTexture(sky_side);
    UnloadTexture(sky_top);
    UnloadTexture(sky_bottom);
    
    CloseWindow();
    return 0;
}
