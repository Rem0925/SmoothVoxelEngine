#include <raylib.h>
#include <raymath.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "Config.hpp"
#include "MarchingCubes.hpp"
#include "Biome.hpp"
#include "World.hpp"
#include "UI.hpp"
#include "Chat.hpp"
#include "CommandHandler.hpp"
#include <rlgl.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <climits>
#include <sqlite3.h>
#include "DatabaseIO.hpp"

using namespace Config;

bool VoxelRaycastSmooth(World& world, Vector3 origin, Vector3 dir, float max_dist, Vector3& out_hit, Vector3& out_solid, Vector3& out_empty) {
    Ray ray = { origin, dir };
    
    RayCollision closest_hit = { 0 };
    closest_hit.distance = max_dist;
    closest_hit.hit = false;
    
    for (auto& pair : world.chunks) {
        Chunk* c = pair.second.get();
        if (!c->is_ready || c->solid_mesh.vertexCount == 0) continue;
        
        float cx_center = c->cx * Config::CHUNK_SIZE + Config::CHUNK_SIZE / 2.0f;
        float cz_center = c->cz * Config::CHUNK_SIZE + Config::CHUNK_SIZE / 2.0f;
        float dist_sq = (origin.x - cx_center)*(origin.x - cx_center) + (origin.z - cz_center)*(origin.z - cz_center);
        
        // Fast radial discard (max_dist + chunk_diagonal)
        float max_r = max_dist + (Config::CHUNK_SIZE * 1.5f);
        if (dist_sq > max_r * max_r) continue;
        
        BoundingBox box = { 
            { (float)(c->cx * Config::CHUNK_SIZE), 0.0f, (float)(c->cz * Config::CHUNK_SIZE) },
            { (float)((c->cx + 1) * Config::CHUNK_SIZE), (float)Config::GRID_Y, (float)((c->cz + 1) * Config::CHUNK_SIZE) }
        };
        if (!GetRayCollisionBox(ray, box).hit) continue;

        RayCollision mesh_hit = GetRayCollisionMesh(ray, c->solid_mesh, MatrixIdentity());
        if (mesh_hit.hit && mesh_hit.distance < closest_hit.distance) {
            closest_hit = mesh_hit;
        }
    }
    
    if (closest_hit.hit) {
        out_hit = closest_hit.point;
        
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

int main() {
    InitWindow(1280, 720, "Smooth Voxel Engine C++");
    SetExitKey(0);
    SetTargetFPS(MAX_FPS); 
    DisableCursor();

    Texture2D spritesheet = LoadTexture("assets/textures/spritesheet_tiles.png");
    SetTextureFilter(spritesheet, TEXTURE_FILTER_POINT);
    
    Texture2D spritesheet_items = LoadTexture("assets/textures/spritesheet_items.png");
    SetTextureFilter(spritesheet_items, TEXTURE_FILTER_POINT);
    
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
    UI ui(spritesheet, spritesheet_items);
    Chat chat;
    
    std::string world_name = "world1";
    std::string save_dir = "worlds/" + world_name;
    std::filesystem::create_directories(save_dir);
    
    if (sqlite3_open_v2((save_dir + "/chunks.db").c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) == SQLITE_OK) {
        sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);
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
    bool show_chunks = false;
    bool show_debug_info = false;
    float smooth_step_offset = 0.0f;
    float mining_progress = 0.0f;
    Vector3 mining_target = {0, 0, 0};
    Vector3 mining_block = {0, 0, 0}; // coordenadas enteras del bloque siendo minado
    bool is_mining = false;

    std::ifstream file(save_dir + "/player.json");
    if (file.is_open()) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto get_val = [&](std::string key, float def) -> float {
            size_t pos = content.find("\"" + key + "\"");
            if (pos != std::string::npos) {
                size_t colon = content.find(":", pos);
                try {
                    return std::stof(content.substr(colon + 1));
                } catch (const std::exception& e) {
                    return def;
                }
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
                if (ui.slots[i].id == 254) {
                    size_t name_pos = content.find("\"slot_" + std::to_string(i) + "_name\"");
                    if (name_pos != std::string::npos) {
                        size_t q1 = content.find("\"", name_pos + 10);
                        size_t q2 = content.find("\"", q1 + 1);
                        if (q1 != std::string::npos && q2 != std::string::npos) {
                            ui.slots[i].name = content.substr(q1 + 1, q2 - q1 - 1);
                        }
                    }
                } else if (Config::BLOCKS.count(ui.slots[i].id)) {
                    ui.slots[i].name = Config::BLOCKS.at(ui.slots[i].id).name;
                }
            }
        }
        
        // Load tools
        int num_tools = (int)get_val("tool_count", 0);
        for (int t = 0; t < num_tools && t < 18; t++) {
            int ttype = (int)get_val("tool_" + std::to_string(t) + "_type", 0);
            int ttier = (int)get_val("tool_" + std::to_string(t) + "_tier", 0);
            int tdur = (int)get_val("tool_" + std::to_string(t) + "_dur", 100);
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
        ui.selected_tool_idx = (int)get_val("selected_tool", 0);
        
        // Load storage
        for (size_t i = 0; i < ui.storage.size(); i++) {
            size_t st_pos = content.find("\"storage_" + std::to_string(i) + "_id\"");
            if (st_pos != std::string::npos) {
                ui.storage[i].id = (uint8_t)get_val("storage_" + std::to_string(i) + "_id", Config::AIR);
                ui.storage[i].count = (int)get_val("storage_" + std::to_string(i) + "_count", 0);
                if (ui.storage[i].id == 254) {
                    // Item: name is stored separately
                    size_t name_pos = content.find("\"storage_" + std::to_string(i) + "_name\"");
                    if (name_pos != std::string::npos) {
                        size_t q1 = content.find("\"", name_pos + 10);
                        size_t q2 = content.find("\"", q1 + 1);
                        if (q1 != std::string::npos && q2 != std::string::npos) {
                            ui.storage[i].name = content.substr(q1 + 1, q2 - q1 - 1);
                        }
                    }
                } else if (Config::BLOCKS.count(ui.storage[i].id)) {
                    ui.storage[i].name = Config::BLOCKS.at(ui.storage[i].id).name;
                }
            }
        }
    }

    while (!WindowShouldClose()) {
        bool ray_hit_valid = false;
        Vector3 hit = {0}, target_solid = {0}, target_empty = {0};

        // Ciclo completo (2*PI) en 20 minutos (1200 segundos) como en Minecraft
        // Velocidad = 2*PI / 1200 = PI / 600
        day_time += GetFrameTime() * (PI / 600.0f);
        
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (chat.is_open) {
                chat.toggle();
            } else if (ui.is_open) {
                ui.toggle_inventory();
            } else {
                break; // Exit the game
            }
        }
        

        
        if (!ui.is_open && !chat.is_open) {
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
                        return b != AIR && b != WATER;
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
            ray_hit_valid = VoxelRaycastSmooth(world, camera.position, forward, 15.0f, hit, target_solid, target_empty);
            if (ray_hit_valid) {
                // === SLOT 0: HERRAMIENTAS ===
                if (ui.selected_slot == 0) {
                    ToolSlot* tool = ui.get_active_tool();
                    
                    // Iniciar minado al presionar click
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                        uint8_t target_block = world.get_block(target_solid.x, target_solid.y, target_solid.z);
                        
                        if (tool && tool->type == TOOL_HAMMER) {
                            world.flatten_terrain(target_solid.x, target_solid.y, target_solid.z, 2, STONE);
                            tool->durability_current--;
                            if (tool->durability_current <= 0) ui.remove_active_tool();
                            mining_progress = 0.0f;
                            is_mining = false;
                        } else if (target_block != AIR && target_block != WATER) {
                            mining_progress = 0.0f;
                            mining_target = target_solid;
                            // Guardar coordenadas enteras del bloque para comparar
                            mining_block.x = (float)(int)target_solid.x;
                            mining_block.y = (float)(int)target_solid.y;
                            mining_block.z = (float)(int)target_solid.z;
                            is_mining = true;
                        }
                    }
                    
                    // Acumular progreso mientras se mantiene el click
                    if (is_mining && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
                        uint8_t target_block = world.get_block((int)mining_block.x, (int)mining_block.y, (int)mining_block.z);
                        if (target_block != AIR && target_block != WATER) {
                            const BlockType& bt = BLOCKS.at(target_block);
                            
                            // Paso 1: Verificar si se puede romper
                            bool can_break = (bt.require_tier != 255);
                            bool can_harvest = true;
                            if (can_break && bt.require_tool != 255) {
                                if (!tool || (int)tool->type != (int)bt.require_tool || (int)tool->tier < (int)bt.require_tier) {
                                    can_harvest = false;
                                }
                            }
                            
                            if (can_break) {
                                // Paso 2: Calcular velocidad (estilo Minecraft, ajustado para ritmo del juego)
                                float tool_speed = 1.0f; // mano
                                if (tool) {
                                    tool_speed = tool->type == TOOL_PICKAXE ? 2.0f :
                                                 tool->type == TOOL_SHOVEL ? 2.0f :
                                                 tool->type == TOOL_AXE ? 2.0f :
                                                 tool->type == TOOL_SWORD ? 1.5f :
                                                 tool->type == TOOL_FLAIL ? 0.8f :
                                                 tool->type == TOOL_HAMMER ? 1.0f : 1.0f;
                                    
                                    float tier_mult = tool->tier == TIER_WOOD ? 1.0f :
                                                      tool->tier == TIER_STONE ? 2.0f :
                                                      tool->tier == TIER_IRON ? 3.0f :
                                                      tool->tier == TIER_SILVER ? 4.0f :
                                                      tool->tier == TIER_GOLD ? 6.0f :
                                                      tool->tier == TIER_DIAMOND ? 5.0f : 1.0f;
                                    tool_speed *= tier_mult;
                                }
                                
                                // Bonus si tiene la herramienta ideal
                                if (tool && bt.ideal_tool != 255 && (int)tool->type == (int)bt.ideal_tool) {
                                    tool_speed *= 1.5f;
                                }
                                
                                // MC: damage_per_tick = speed / hardness / divisor
                                // ×20 para convertir a damage-per-second
                                float divisor = can_harvest ? 30.0f : 100.0f;
                                float damage = tool_speed / std::max(bt.hardness, 0.1f) / divisor * 20.0f;
                                
                                mining_progress += GetFrameTime() * damage;
                            
                                if (mining_progress >= 1.0f) {
                                    if (bt.drop_id != 255) {
                                        if (bt.drop_is_item) {
                                            ui.add_item(bt.drop_id);
                                        } else {
                                            ui.add_resource(bt.drop_id);
                                        }
                                    }
                                    world.set_block((int)mining_block.x, (int)mining_block.y, (int)mining_block.z, AIR);
                                    if (tool) {
                                        tool->durability_current--;
                                        if (tool->durability_current <= 0) ui.remove_active_tool();
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
                // === SLOTS 1-9: COLOCAR BLOQUES ===
                else if (ui.selected_slot != 0) {
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                        if (ui.slots[ui.selected_slot].count > 0 && ui.slots[ui.selected_slot].id != 254) {
                            world.set_block(target_empty.x, target_empty.y, target_empty.z, ui.slots[ui.selected_slot].id);
                            ui.slots[ui.selected_slot].count--;
                            if (ui.slots[ui.selected_slot].count <= 0) {
                                ui.slots[ui.selected_slot].id = AIR;
                                ui.slots[ui.selected_slot].name = "";
                            }
                        }
                    }
                }
            } else {
                // Raycast no impacta: NO resetear minado, solo pausar acumulacion
            }
        }
        
        if (!chat.is_open && !ui.is_open) {
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
        float light_intensity = std::max(0.2f, dayFactor);
        Color sky_color = { 
            (unsigned char)(135 * light_intensity), 
            (unsigned char)(206 * light_intensity), 
            (unsigned char)(235 * light_intensity), 
            255 
        };
        ClearBackground(sky_color);
        
        // Pass dynamic fog color to shaders
        float fogColorVec[3] = { sky_color.r / 255.0f, sky_color.g / 255.0f, sky_color.b / 255.0f };
        SetShaderValue(world.mat_solid.shader, fogLocSolid, fogColorVec, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_plants.shader, fogLocPlants, fogColorVec, SHADER_UNIFORM_VEC3);
        SetShaderValue(world.mat_water.shader, fogLocWater, fogColorVec, SHADER_UNIFORM_VEC3);
        
        SetShaderValue(world.mat_solid.shader, fogStartLocSolid, &Config::FOG_START, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_solid.shader, fogEndLocSolid, &Config::FOG_END, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_plants.shader, fogStartLocPlants, &Config::FOG_START, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_plants.shader, fogEndLocPlants, &Config::FOG_END, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_water.shader, fogStartLocWater, &Config::FOG_START, SHADER_UNIFORM_FLOAT);
        SetShaderValue(world.mat_water.shader, fogEndLocWater, &Config::FOG_END, SHADER_UNIFORM_FLOAT);
        
        // Dynamically update material colors for lighting
        unsigned char light_val = (unsigned char)(255 * light_intensity);
        world.mat_solid.maps[MATERIAL_MAP_DIFFUSE].color = { light_val, light_val, light_val, 255 };
        world.mat_water.maps[MATERIAL_MAP_DIFFUSE].color = { light_val, light_val, light_val, 200 };
        world.mat_plants.maps[MATERIAL_MAP_DIFFUSE].color = { light_val, light_val, light_val, 255 };

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
        
        // Minecraft-style Chunk Boundaries (F3+G)
        if (show_chunks) {
            float CS = (float)Config::CHUNK_SIZE;
            float GY = (float)Config::GRID_Y;
            
            int p_cx = (int)std::floor(camera.position.x / CS);
            int p_cz = (int)std::floor(camera.position.z / CS);
            
            rlDisableDepthTest();
            rlDisableBackfaceCulling();
            rlBegin(RL_LINES);
            
            // 1. Chunks vecinos (Radio 1 alrededor del jugador) - Marco tenue azul
            Color neighbor_col = Fade(BLUE, 0.35f);
            rlColor4ub(neighbor_col.r, neighbor_col.g, neighbor_col.b, neighbor_col.a);
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue; // Saltar chunk actual
                    float nx0 = (p_cx + dx) * CS;
                    float nz0 = (p_cz + dz) * CS;
                    float nx1 = nx0 + CS;
                    float nz1 = nz0 + CS;
                    
                    // Esquinas verticales
                    rlVertex3f(nx0, 0, nz0); rlVertex3f(nx0, GY, nz0);
                    rlVertex3f(nx1, 0, nz0); rlVertex3f(nx1, GY, nz0);
                    rlVertex3f(nx1, 0, nz1); rlVertex3f(nx1, GY, nz1);
                    rlVertex3f(nx0, 0, nz1); rlVertex3f(nx0, GY, nz1);
                    
                    // Borde inferior y superior
                    rlVertex3f(nx0, 0, nz0); rlVertex3f(nx1, 0, nz0);
                    rlVertex3f(nx1, 0, nz0); rlVertex3f(nx1, 0, nz1);
                    rlVertex3f(nx1, 0, nz1); rlVertex3f(nx0, 0, nz1);
                    rlVertex3f(nx0, 0, nz1); rlVertex3f(nx0, 0, nz0);
                    
                    rlVertex3f(nx0, GY, nz0); rlVertex3f(nx1, GY, nz0);
                    rlVertex3f(nx1, GY, nz0); rlVertex3f(nx1, GY, nz1);
                    rlVertex3f(nx1, GY, nz1); rlVertex3f(nx0, GY, nz1);
                    rlVertex3f(nx0, GY, nz1); rlVertex3f(nx0, GY, nz0);
                }
            }
            
            // 2. Chunk Actual del Jugador
            float x0 = p_cx * CS;
            float z0 = p_cz * CS;
            float x1 = x0 + CS;
            float z1 = z0 + CS;
            
            // A. Rejilla vertical por bloque (X=1..15, Z=1..15) en las 4 paredes exteriores (Amarillo)
            Color wall_grid_col = Color{ 255, 220, 0, 160 };
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
            
            // B. Líneas horizontales de subdivisiones de Sub-chunk (cada 16 bloques de altura en Y: 0, 16, 32... 128)
            Color section_col = Color{ 255, 230, 0, 240 };
            rlColor4ub(section_col.r, section_col.g, section_col.b, section_col.a);
            for (int y = 0; y <= Config::GRID_Y; y += 16) {
                float fy = (float)y;
                rlVertex3f(x0, fy, z0); rlVertex3f(x1, fy, z0);
                rlVertex3f(x1, fy, z0); rlVertex3f(x1, fy, z1);
                rlVertex3f(x1, fy, z1); rlVertex3f(x0, fy, z1);
                rlVertex3f(x0, fy, z1); rlVertex3f(x0, fy, z0);
            }
            
            // C. Rejilla horizontal detallada en el sub-chunk actual del jugador (cada 2 bloques de altura)
            int p_sub_y = std::clamp((int)std::floor(camera.position.y / 16.0f) * 16, 0, Config::GRID_Y - 16);
            Color sub_detail_col = Color{ 0, 210, 255, 180 };
            rlColor4ub(sub_detail_col.r, sub_detail_col.g, sub_detail_col.b, sub_detail_col.a);
            for (int y = p_sub_y + 2; y < p_sub_y + 16; y += 2) {
                float fy = (float)y;
                rlVertex3f(x0, fy, z0); rlVertex3f(x1, fy, z0);
                rlVertex3f(x1, fy, z0); rlVertex3f(x1, fy, z1);
                rlVertex3f(x1, fy, z1); rlVertex3f(x0, fy, z1);
                rlVertex3f(x0, fy, z1); rlVertex3f(x0, fy, z0);
            }
            
            // D. 4 Esquinas principales en Rojo Intenso (desde Y=0 hasta Y=GRID_Y)
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
            if (ui.slots[ui.selected_slot].id != Config::AIR && ui.slots[ui.selected_slot].count > 0) {
                is_valid_tool = true;
            }
        }

        if (!ui.is_open && !chat.is_open && is_valid_tool && ray_hit_valid) {
            BeginMode3D(camera); // Start X-Ray 3D pass (this internally enables depth test!)
            
            rlDisableDepthMask(); // Disable depth mask AFTER BeginMode3D
            rlDisableDepthTest(); // Disable depth test AFTER BeginMode3D
            
            Vector3 target_node = (ui.selected_slot == 0) ? target_solid : target_empty;
            
            float scale = 0.2f + std::sin(GetTime() * 8.0f) * 0.05f;
            Color cursor_color = (ui.selected_slot == 0) ? Color{255, 40, 40, 255} : Color{40, 255, 100, 255};
            DrawCube(target_node, scale, scale, scale, cursor_color);
            
            rlDisableBackfaceCulling(); // Re-disable to show the inside of the concave crater hologram
            
            int bx = std::floor(target_node.x);
            int by = std::floor(target_node.y);
            int bz = std::floor(target_node.z);
            
            float d_slice[27];
            for(int i=0; i<27; i++) d_slice[i] = -1.0f;
            for(int dx=-1; dx<=1; dx++){
                for(int dy=-1; dy<=1; dy++){
                    for(int dz=-1; dz<=1; dz++){
                        d_slice[(dy+1)*9 + (dz+1)*3 + (dx+1)] = world.get_density(bx+dx, by+dy, bz+dz);
                    }
                }
            }
            if (ui.selected_slot == 0) {
                d_slice[1*9 + 1*3 + 1] = -1.0f; // Preview mine
            } else {
                d_slice[1*9 + 1*3 + 1] = 1.0f; // Preview place
            }
            
            std::vector<Vector3> g_verts, g_norms;
            std::vector<Vector2> g_uvs, g_uvs2; std::vector<Color> g_cols;
            mc::generate(nullptr, d_slice, 3, 3, 3, 0.0f, Config::AIR, g_verts, g_norms, g_uvs, g_uvs2, g_cols);
            
            rlPushMatrix();
            rlTranslatef(bx - 1.0f, by - 1.0f, bz - 1.0f);
            
            float pulse = 0.6f + std::sin(GetTime() * 6.0f) * 0.4f;
            unsigned char alpha = (unsigned char)(pulse * 255);
            Color wire_col = (ui.selected_slot == 0) ? Color{255, 25, 25, alpha} : Color{25, 255, 75, alpha};
            Color solid_col = (ui.selected_slot == 0) ? Color{255, 0, 0, 40} : Color{0, 255, 0, 40};

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
            
            // 1. Flush the X-Ray batch NOW while depth test is still OFF
            rlDrawRenderBatchActive();
            
            // 2. Restore persistent states that Raylib doesn't auto-restore
            rlEnableDepthMask();
            rlEnableBackfaceCulling();
            
            // 3. Close 3D mode (auto-disables depth test for 2D UI)
            EndMode3D();
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
            
            std::vector<std::pair<std::string, Color>> lines;
            lines.push_back({ TextFormat("SmoothVoxelEngine C++ | %d FPS", fps), fps_color });
            lines.push_back({ TextFormat("XYZ: %.2f / %.2f / %.2f", camera.position.x, camera.position.y, camera.position.z), WHITE });
            lines.push_back({ TextFormat("Bloque: %d, %d, %d", cam_x, cam_y, cam_z), Color{220, 225, 235, 255} });
            lines.push_back({ TextFormat("Chunk: %d, %d  [En chunk: %d, %d, %d]", p_cx, p_cz, in_cx, cam_y, in_cz), Color{220, 225, 235, 255} });
            lines.push_back({ TextFormat("Orientacion: %s", facing), Color{200, 215, 235, 255} });
            lines.push_back({ TextFormat("Bioma: %s", biome_name), Color{140, 230, 160, 255} });
            
            if (look_block != AIR) {
                lines.push_back({ TextFormat("Mirando a: %s (ID %d en %d, %d, %d)", block_name.c_str(), (int)look_block, (int)target_solid.x, (int)target_solid.y, (int)target_solid.z), Color{255, 220, 100, 255} });
            } else {
                lines.push_back({ "Mirando a: Aire", Color{170, 180, 195, 255} });
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
            out << "  \"slot_" << i << "_count\": " << ui.slots[i].count << ",\n";
            out << "  \"slot_" << i << "_name\": \"" << ui.slots[i].name << "\",\n";
        }
        // Save tools
        out << "  \"tool_count\": " << ui.tool_inventory.size() << ",\n";
        for (size_t t=0; t<ui.tool_inventory.size(); t++) {
            out << "  \"tool_" << t << "_type\": " << (int)ui.tool_inventory[t].type << ",\n";
            out << "  \"tool_" << t << "_tier\": " << (int)ui.tool_inventory[t].tier << ",\n";
            out << "  \"tool_" << t << "_dur\": " << ui.tool_inventory[t].durability_current;
            out << (t == ui.tool_inventory.size()-1 ? "\n" : ",\n");
        }
        out << "  \"selected_tool\": " << ui.selected_tool_idx << ",\n";
        // Save storage
        for (size_t i=0; i<ui.storage.size(); i++) {
            out << "  \"storage_" << i << "_id\": " << (int)ui.storage[i].id << ",\n";
            out << "  \"storage_" << i << "_count\": " << ui.storage[i].count << ",\n";
            out << "  \"storage_" << i << "_name\": \"" << ui.storage[i].name << "\"";
            out << (i == ui.storage.size()-1 ? "\n" : ",\n");
        }
        out << "}\n";
        out.close();
    }
    
    world.stop_simulation();
    global_thread_pool.clear_queue();
    world.save_all();
    global_thread_pool.wait_idle();
    DatabaseIO::get().wait_idle();
    
    {
        extern sqlite3* db;
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }

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
