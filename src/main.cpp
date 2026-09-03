#include <raylib.h>
#include <raymath.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "core/Config.hpp"
#include "data/BlockRegistry.hpp"
#include "world/MarchingCubes.hpp"
#include "generation/Biome.hpp"
#include "world/World.hpp"
#include "ui/UI.hpp"
#include "ui/Chat.hpp"
#include "gameplay/CommandHandler.hpp"
#include <rlgl.h>
#include <GL/gl.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include "core/sqlite3.h"
#include "data/DatabaseIO.hpp"
#include "gameplay/ItemDrop.hpp"
#include "gameplay/ItemModel3D.hpp"
#include "world/VoxelLighting.hpp"
#include "core/json.hpp"
#include "ui/MenuManager.hpp"
#include "ui/MenuResourcePacks.hpp"
#include "data/ResourcePackManager.hpp"
#include "rendering/Raycast.hpp"
#include "rendering/Skybox.hpp"
#include "rendering/Viewmodel.hpp"
#include "rendering/BlockHighlight.hpp"
#include "rendering/CelestialRenderer.hpp"
#include "rendering/ChunkDebugDraw.hpp"
#include "ui/DebugOverlay.hpp"
#include "gameplay/PlayerSaveData.hpp"
#include "gameplay/PlayerPhysics.hpp"
#include "core/AudioManager.hpp"

using json = nlohmann::json;
using namespace Config;

World* g_world = nullptr;
static ViewmodelState g_viewmodel_state;

int main() {
    InitWindow(1280, 720, "Smooth Voxel Engine C++");
    SetExitKey(0);
    SetTargetFPS(MAX_FPS); 
    DisableCursor();
    AudioManager::get().init();

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
    int skyboxViewRotLoc = GetShaderLocation(skyboxShader, "viewRotation");
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

    // ==================== Game State ====================
    struct GameState {
        float player_vel_y = 0.0f;
        bool is_grounded = false;
        float day_time = 1.5f; // Start at noon
        bool spectator_mode = false;
        bool show_chunks = false;
        bool show_debug_info = false;
        float smooth_step_offset = 0.0f;
        float mining_progress = 0.0f;
        Vector3 mining_target = {0, 0, 0};
        Vector3 mining_block = {0, 0, 0};
        bool is_mining = false;
        bool was_paused = false;
        std::string last_pack_path;
        float autosave_timer = 0.0f;
        float tick_accumulator = 0.0f;
        float mining_hit_timer = 0.0f;

        // Supervivencia (estilo Minecraft Beta pre-adventure)
        int health = 20;
        int max_health = 20;
        float hurt_timer = 0.0f;
        float hurt_flash_timer = 0.0f;
        float air_supply = 15.0f;
        float max_air = 15.0f;
        float drown_damage_timer = 0.0f;
        float fall_distance = 0.0f;
        bool has_landed_initial = false;
        bool is_dead = false;
        std::string death_reason = "";
    };
    GameState gs;
    gs.last_pack_path = MenuResourcePacks::get_active_pack_path();

    load_player_data(save_dir, camera, ui, gs.day_time, gs.health);

    // Si la posición guardada estaba en el aire (ej. y >= 90.0f o primera vez), posicionar en la superficie sólida
    world.update(camera.position);
    if (camera.position.y >= 85.0f) {
        float surface_y = world.get_spawn_surface_y(camera.position.x, camera.position.z);
        camera.position.y = surface_y + Config::PLAYER_EYE_HEIGHT;
        camera.target.y = camera.position.y;
    }
    gs.player_vel_y = 0.0f;
    gs.is_grounded = true;
    gs.has_landed_initial = false;

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
        
        if (menu.is_paused()) {
            menu.update();
            if (menu.wants_quit) break;
            gs.was_paused = true;
        } else if (gs.was_paused) {
            gs.was_paused = false;
        }
        
        std::string current_pack = MenuResourcePacks::get_active_pack_path();
        if (current_pack != gs.last_pack_path || (current_pack.empty() && res_pack.get_is_active()) || (!current_pack.empty() && !res_pack.get_is_active() && current_pack != res_pack.get_active_pack_path())) {
            gs.last_pack_path = current_pack;
            auto reload_textures = [&]() {
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
            };
            if (!current_pack.empty()) {
                if (res_pack.apply_pack(current_pack)) {
                    reload_textures();
                    chat.add_message("Paquete de recursos aplicado!");
                }
            } else {
                res_pack.clear_pack();
                reload_textures();
                chat.add_message("Texturas por defecto restauradas");
            }
        }
        

        // Auto-guardado periódico cada 5 segundos
        gs.autosave_timer += GetFrameTime();
        if (gs.autosave_timer >= 5.0f) {
            gs.autosave_timer = 0.0f;
            world.save_all();
            save_player_data(save_dir, camera, ui, gs.day_time, gs.health);
        }

        auto drop_all_inventory = [&]() {
            for (size_t i = 0; i < ui.slots.size(); ++i) {
                if (ui.slots[i].count > 0 && ui.slots[i].id != Config::AIR) {
                    Vector3 drop_pos = camera.position;
                    Vector3 drop_vel = { Config::rand_float(-2.0f, 2.0f), Config::rand_float(3.0f, 5.0f), Config::rand_float(-2.0f, 2.0f) };
                    item_drops.spawn(drop_pos, ui.slots[i].id, (ui.slots[i].id == 254), ui.slots[i].count, drop_vel, 2.0f);
                    ui.slots[i].count = 0;
                    ui.slots[i].id = Config::AIR;
                    ui.slots[i].name = "";
                }
            }
            for (size_t i = 0; i < ui.storage.size(); ++i) {
                if (ui.storage[i].count > 0 && ui.storage[i].id != Config::AIR) {
                    Vector3 drop_pos = camera.position;
                    Vector3 drop_vel = { Config::rand_float(-2.0f, 2.0f), Config::rand_float(3.0f, 5.0f), Config::rand_float(-2.0f, 2.0f) };
                    item_drops.spawn(drop_pos, ui.storage[i].id, (ui.storage[i].id == 254), ui.storage[i].count, drop_vel, 2.0f);
                    ui.storage[i].count = 0;
                    ui.storage[i].id = Config::AIR;
                    ui.storage[i].name = "";
                }
            }
            for (auto& t : ui.tool_inventory) {
                Vector3 drop_pos = camera.position;
                Vector3 drop_vel = { Config::rand_float(-2.0f, 2.0f), Config::rand_float(3.0f, 5.0f), Config::rand_float(-2.0f, 2.0f) };
                item_drops.spawn_tool(drop_pos, t.type, t.tier, t.durability_current, drop_vel, 2.0f);
            }
            ui.tool_inventory.clear();
            ui.selected_tool_idx = -1;
        };

        auto apply_damage = [&](int amount, const std::string& reason) {
            if (gs.spectator_mode || gs.is_dead || gs.hurt_timer > 0.0f || amount <= 0) return;
            gs.health -= amount;
            gs.hurt_timer = 0.4f;
            gs.hurt_flash_timer = 0.25f;
            AudioManager::get().play(SoundType::PLAYER_HURT, 0.9f, 0.15f);
            if (gs.health <= 0) {
                gs.health = 0;
                gs.is_dead = true;
                gs.death_reason = reason;
                drop_all_inventory();
                EnableCursor();
                chat.add_message(TextFormat("[Muerte] %s", reason.c_str()));
            }
        };

        float dt_frame = GetFrameTime();
        if (gs.hurt_timer > 0.0f) gs.hurt_timer -= dt_frame;
        if (gs.hurt_flash_timer > 0.0f) gs.hurt_flash_timer -= dt_frame;

        if (!ui.is_open && !chat.is_open && !menu.is_paused() && !gs.is_dead) {
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
                        ui.consume_held_item();
                        item_drops.spawn(throw_pos, drop_id, false, 1, throw_vel, 0.8f);
                    }
                }
            }

            Vector3 old_pos = camera.position;
            float dt_cam = GetFrameTime();
            Vector2 mouse_delta = GetMouseDelta();
            float mouse_sensitivity = 0.15f; // Sensibilidad cómoda y fluida
            Vector3 rotation = { mouse_delta.x * mouse_sensitivity, mouse_delta.y * mouse_sensitivity, 0.0f };

            if (gs.spectator_mode) {
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
                bool is_sprinting = IsKeyDown(KEY_LEFT_SHIFT);
                float walk_speed = (is_sprinting ? 8.5f : 4.5f) * dt_cam;
                Vector3 movement = { 0.0f, 0.0f, 0.0f };
                if (IsKeyDown(KEY_W)) movement.x += walk_speed;
                if (IsKeyDown(KEY_S)) movement.x -= walk_speed;
                if (IsKeyDown(KEY_D)) movement.y += walk_speed;
                if (IsKeyDown(KEY_A)) movement.y -= walk_speed;
                UpdateCameraPro(&camera, movement, rotation, 0.0f);
            }

            {
                Vector3 post_update_pos = camera.position;
                PlayerPhysicsState pp_state = { gs.player_vel_y, gs.is_grounded, gs.smooth_step_offset, gs.spectator_mode, gs.fall_distance };
                UpdatePlayerPhysics(world, camera, pp_state, old_pos, post_update_pos);
                gs.player_vel_y = pp_state.player_vel_y;
                gs.is_grounded = pp_state.is_grounded;
                gs.smooth_step_offset = pp_state.smooth_step_offset;
                gs.fall_distance = pp_state.fall_distance;

                // Daño por caída al tocar el suelo
                if (pp_state.just_landed && !gs.spectator_mode) {
                    if (!gs.has_landed_initial) {
                        gs.has_landed_initial = true;
                    } else if (pp_state.landed_fall_distance > 3.0f) {
                        int fall_dmg = (int)std::floor(pp_state.landed_fall_distance - 3.0f);
                        apply_damage(fall_dmg, "Caída desde gran altura");
                    }
                }

                if (!gs.spectator_mode) {
                    float dx = camera.position.x - old_pos.x;
                    float dz = camera.position.z - old_pos.z;
                    float horiz_dist = std::sqrt(dx * dx + dz * dz);
                    float horiz_speed = (dt_cam > 1e-4f) ? (horiz_dist / dt_cam) : 0.0f;

                    int cam_x = std::floor(camera.position.x);
                    int cam_y = std::floor(camera.position.y);
                    int cam_z = std::floor(camera.position.z);
                    int feet_y = std::floor(camera.position.y - Config::PLAYER_EYE_HEIGHT);

                    bool in_water = (world.get_block(cam_x, feet_y, cam_z) == Config::WATER ||
                                     world.get_block(cam_x, cam_y, cam_z) == Config::WATER ||
                                     camera.position.y < (Config::WATER_LEVEL + 0.1f));

                    uint8_t block_below = world.get_block(cam_x, std::floor(camera.position.y - Config::PLAYER_EYE_HEIGHT - 0.1f), cam_z);
                    if (block_below == Config::AIR) {
                        block_below = world.get_block(cam_x, std::floor(camera.position.y - Config::PLAYER_EYE_HEIGHT - 0.5f), cam_z);
                    }

                    AudioManager::get().update_footsteps(dt_cam, gs.is_grounded, in_water, horiz_speed, block_below);

                    // Daño por ahogamiento
                    int head_x = std::floor(camera.position.x);
                    int head_y = std::floor(camera.position.y);
                    int head_z = std::floor(camera.position.z);
                    bool head_in_water = (world.get_block(head_x, head_y, head_z) == Config::WATER);
                    if (head_in_water && !gs.is_dead) {
                        gs.air_supply -= dt_cam;
                        if (gs.air_supply <= 0.0f) {
                            gs.air_supply = 0.0f;
                            gs.drown_damage_timer += dt_cam;
                            if (gs.drown_damage_timer >= 1.0f) {
                                gs.drown_damage_timer = 0.0f;
                                apply_damage(2, "Ahogado");
                            }
                        }
                    } else {
                        gs.air_supply = gs.max_air;
                        gs.drown_damage_timer = 0.0f;
                    }

                    // Daño por vacío
                    if (camera.position.y < -15.0f && !gs.is_dead) {
                        apply_damage(4, "Caído al vacío");
                    }
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
                            gs.mining_progress = 0.0f;
                            gs.mining_target = target_solid;
                            // Guardar coordenadas enteras del bloque para comparar
                            gs.mining_block.x = (float)(int)target_solid.x;
                            gs.mining_block.y = (float)(int)target_solid.y;
                            gs.mining_block.z = (float)(int)target_solid.z;
                            gs.is_mining = true;
                            gs.mining_hit_timer = 0.0f;
                            AudioManager::get().play_hit(target_block);
                        }
                    }
                    
                    // Cancelar minado si apunta a otro bloque (comparar coordenadas enteras)
                    if (gs.is_mining) {
                        int bx = (int)target_solid.x;
                        int by = (int)target_solid.y;
                        int bz = (int)target_solid.z;
                        if (bx != (int)gs.mining_block.x || by != (int)gs.mining_block.y || bz != (int)gs.mining_block.z) {
                            gs.mining_progress = 0.0f;
                            gs.is_mining = false;
                        }
                    }
                    
                    // Reset al soltar click
                    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                        gs.mining_progress = 0.0f;
                        gs.is_mining = false;
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
                                    AudioManager::get().play_place(Config::DOOR_WOOD);
                                    ui.consume_held_item();
                                } else {
                                    chat.add_message("[Puerta] Requiere 2 bloques de altura libre para colocarse.");
                                }
                            } else {
                                world.set_block(target_empty.x, target_empty.y, target_empty.z, ui.slots[ui.selected_slot].id, rot);
                                AudioManager::get().play_place(ui.slots[ui.selected_slot].id);
                                ui.consume_held_item();
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

                    bool interacted = false;
                    if (look_b == Config::CHEST) {
                        interacted = true;
                        AudioManager::get().play(SoundType::CHEST_OPEN);
                        ui.open_chest({ (float)bx, (float)by, (float)bz });
                    } else if (look_b == Config::CRAFTING_TABLE) {
                        interacted = true;
                        AudioManager::get().play(SoundType::STEP_WOOD);
                        ui.open_crafting_table({ (float)bx, (float)by, (float)bz });
                    } else if (look_b == Config::FURNACE) {
                        interacted = true;
                        AudioManager::get().play(SoundType::STEP_STONE);
                        ui.open_furnace({ (float)bx, (float)by, (float)bz });
                    } else if (look_b == Config::DOOR_WOOD) {
                        interacted = true;
                        uint8_t rot = world.get_rotation(bx, by, bz);
                        uint8_t new_rot = rot ^ 4; // Toggle open/close bit
                        world.set_block(bx, by, bz, Config::DOOR_WOOD, new_rot);
                        bool is_opening = !(new_rot & 4);
                        if (is_opening) AudioManager::get().play(SoundType::DOOR_OPEN);
                        else AudioManager::get().play(SoundType::DOOR_CLOSE);

                        if (by + 1 < Config::GRID_Y && world.get_block(bx, by + 1, bz) == Config::DOOR_WOOD) {
                            uint8_t top_rot = world.get_rotation(bx, by + 1, bz);
                            world.set_block(bx, by + 1, bz, Config::DOOR_WOOD, (top_rot & ~4) | (new_rot & 4));
                        } else if (by > 0 && world.get_block(bx, by - 1, bz) == Config::DOOR_WOOD) {
                            uint8_t bot_rot = world.get_rotation(bx, by - 1, bz);
                            world.set_block(bx, by - 1, bz, Config::DOOR_WOOD, (bot_rot & ~4) | (new_rot & 4));
                        }
                    }

                    // Comer comida (clic derecho) estilo Minecraft Beta (sin hambre, cura vida directa)
                    if (!interacted && ui.selected_slot != 0 && ui.slots[ui.selected_slot].count > 0) {
                        uint8_t held_id = ui.slots[ui.selected_slot].id;
                        const std::string& held_name = ui.slots[ui.selected_slot].name;
                        int heal_val = 0;
                        if (held_id == Config::RED_MUSHROOM || held_id == Config::BROWN_MUSHROOM) heal_val = 2;
                        else if (held_id == Config::ITEM_APPLE || held_name == "Manzana" || held_name == "Apple") heal_val = 4;

                        if (heal_val > 0 && gs.health < gs.max_health) {
                            gs.health = std::min(gs.max_health, gs.health + heal_val);
                            ui.consume_held_item();
                            AudioManager::get().play(SoundType::PLAYER_EAT);
                        }
                    }
                }
            } else if (!ray_hit_valid && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && ui.selected_slot != 0 && ui.slots[ui.selected_slot].count > 0) {
                uint8_t held_id = ui.slots[ui.selected_slot].id;
                const std::string& held_name = ui.slots[ui.selected_slot].name;
                int heal_val = 0;
                if (held_id == Config::RED_MUSHROOM || held_id == Config::BROWN_MUSHROOM) heal_val = 2;
                else if (held_id == Config::ITEM_APPLE || held_name == "Manzana" || held_name == "Apple") heal_val = 4;

                if (heal_val > 0 && gs.health < gs.max_health) {
                    gs.health = std::min(gs.max_health, gs.health + heal_val);
                    ui.consume_held_item();
                    AudioManager::get().play(SoundType::PLAYER_EAT);
                }
            }
        }
        
        if (!chat.is_open && !ui.is_open && !menu.is_paused()) {
            if (IsKeyPressed(KEY_T)) {
                chat.toggle();
            } else if (IsKeyPressed(KEY_SLASH)) {
                chat.toggle();
                chat.current_input = "/";
            } else if (IsKeyDown(KEY_F3) && IsKeyPressed(KEY_G)) {
                gs.show_chunks = !gs.show_chunks;
                chat.add_message(gs.show_chunks ? "Limites de chunks visibles" : "Limites de chunks ocultos");
            } else if (IsKeyPressed(KEY_F9)) {
                gs.show_chunks = !gs.show_chunks;
                chat.add_message(gs.show_chunks ? "Limites de chunks visibles" : "Limites de chunks ocultos");
            } else if (IsKeyPressed(KEY_F3)) {
                gs.show_debug_info = !gs.show_debug_info;
            }
        }
        
        chat.update();
        if (!chat.is_open) ui.update();
        
        CommandHandler::process(chat, gs.day_time, camera, gs.spectator_mode, gs.player_vel_y, ui, gs.show_chunks);
        world.update(camera.position);

        // ==================== 20 TPS FIXED TIMESTEP (Estilo Minecraft) ====================
        constexpr float TICK_TIME = 1.0f / 20.0f; // 0.05s por tick (20 TPS)
        float frame_dt = std::min(GetFrameTime(), 0.1f);
        gs.tick_accumulator += frame_dt;

        if (menu.is_paused()) {
            gs.tick_accumulator = 0.0f;
        }

        while (gs.tick_accumulator >= TICK_TIME && !menu.is_paused()) {
            gs.tick_accumulator -= TICK_TIME;

            // 1. Progresión del ciclo día/noche en ticks (24000 ticks = 20 minutos de día completo)
            gs.day_time += (PI / 12000.0f);
            if (gs.day_time >= 2.0f * PI) gs.day_time -= 2.0f * PI;

            // 2. Físicas y recolección de Item Drops (si está muerto, no se magnetizan ni se recogen)
            Vector3 collect_pos = gs.is_dead ? Vector3{ -99999.0f, -99999.0f, -99999.0f } : camera.position;
            item_drops.update(TICK_TIME, world, collect_pos, ui);

            // 3. Procesamiento de hornos en tiempo real
            ui.tick_furnaces();

            // 4. Daño de minado exacto por tick
            if (gs.is_mining && IsMouseButtonDown(MOUSE_LEFT_BUTTON) && ui.selected_slot == 0) {
                uint8_t target_block = world.get_block((int)gs.mining_block.x, (int)gs.mining_block.y, (int)gs.mining_block.z);
                if (target_block != AIR && target_block != WATER) {
                    gs.mining_hit_timer += TICK_TIME;
                    if (gs.mining_hit_timer >= 0.28f) {
                        gs.mining_hit_timer = 0.0f;
                        AudioManager::get().play_hit(target_block);
                    }

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
                            hammer_area = get_hammer_area((int)tool->tier, hit, (int)gs.mining_block.x, (int)gs.mining_block.z);
                            hardness = world.get_hammer_mining_hardness((int)gs.mining_block.x, (int)gs.mining_block.y, (int)gs.mining_block.z, hammer_area, (int)tool->tier);
                        }

                        float divisor = can_harvest ? 30.0f : 100.0f;
                        float damage_per_tick = tool_speed / std::max(hardness, 0.1f) / divisor;

                        gs.mining_progress += damage_per_tick;

                        if (gs.mining_progress >= 1.0f) {
                            AudioManager::get().play_break(target_block);
                            if (tool && tool->type == TOOL_HAMMER) {
                                int broken = world.flatten_terrain((int)gs.mining_block.x, (int)gs.mining_block.y, (int)gs.mining_block.z, hammer_area, (int)tool->tier, &item_drops);
                                int uses = std::max(1, broken);
                                tool->durability_current -= uses;
                                if (tool->durability_current <= 0) ui.remove_active_tool();
                            } else {
                                if (bt.drop_id != 255) {
                                    Vector3 drop_pos = { (float)gs.mining_block.x + 0.5f, (float)gs.mining_block.y + 0.5f, (float)gs.mining_block.z + 0.5f };
                                    Vector3 drop_vel = {
                                        Config::rand_float(-1.0f, 1.0f),
                                        Config::rand_float(2.5f, 3.0f),
                                        Config::rand_float(-1.0f, 1.0f)
                                    };
                                    item_drops.spawn(drop_pos, bt.drop_id, bt.drop_is_item, 1, drop_vel, 0.1f);
                                }
                                if (target_block == Config::LEAVES) {
                                    if (Config::rand_float(0.0f, 1.0f) < 0.25f) {
                                        Vector3 apple_pos = { (float)gs.mining_block.x + 0.5f, (float)gs.mining_block.y + 0.5f, (float)gs.mining_block.z + 0.5f };
                                        Vector3 apple_vel = { Config::rand_float(-0.6f, 0.6f), 2.5f, Config::rand_float(-0.6f, 0.6f) };
                                        item_drops.spawn(apple_pos, Config::ITEM_APPLE, true, 1, apple_vel, 0.1f);
                                    }
                                }
                                world.set_block((int)gs.mining_block.x, (int)gs.mining_block.y, (int)gs.mining_block.z, AIR);
                                if (target_block == Config::DOOR_WOOD) {
                                    int mx = (int)gs.mining_block.x;
                                    int my = (int)gs.mining_block.y;
                                    int mz = (int)gs.mining_block.z;
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
                            gs.mining_progress = 0.0f;
                            gs.is_mining = false;
                        }
                    } else {
                        gs.mining_progress = 0.0f;
                        gs.is_mining = false;
                    }
                } else {
                    gs.mining_progress = 0.0f;
                    gs.is_mining = false;
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
        
        float sun_y = std::sin(gs.day_time);
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
        if (!gs.spectator_mode) {
            gs.smooth_step_offset = Lerp(gs.smooth_step_offset, 0.0f, 15.0f * GetFrameTime());
        } else {
            gs.smooth_step_offset = 0.0f;
        }
        
        camera.position.y += gs.smooth_step_offset;
        camera.target.y += gs.smooth_step_offset;

        BeginMode3D(camera);
        
        DrawCelestialScene(camera, gs.day_time, light_intensity, tex_sun, tex_moon, tex_clouds,
                           sky_side, sky_top, sky_bottom, skyboxShader, skyboxSunDirLoc, skyboxViewRotLoc,
                           world.mat_solid, terrainSolidSunDirLoc, world.mat_plants, terrainPlantsSunDirLoc,
                           world.mat_water, waterSunDirLoc);

        world.draw(camera);
        item_drops.draw(spritesheet, light_intensity, gs.tick_accumulator / TICK_TIME);
        
        if (gs.show_chunks) {
            DrawChunkDebug(camera);
        }
        
        EndMode3D(); // Flush and close standard 3D pass
        
        {
            BlockHighlightParams bh_params = { world, ui, camera, ray_hit_valid, target_solid, target_empty, hit, spritesheet };
            DrawBlockHighlight(bh_params);
        }

        // === PRIMERA PERSONA: MANO Y OBJETO EN MANO (VIEWMODEL 3D) ===
        if (!gs.spectator_mode && !ui.is_open && !chat.is_open) {
            DrawFirstPersonViewmodel(g_viewmodel_state, ui, spritesheet, spritesheet_items, light_intensity, gs.is_mining, gs.mining_progress, gs.is_grounded, GetFrameTime());
        }

        int cam_x = std::floor(camera.position.x);
        int cam_y = std::floor(camera.position.y);
        int cam_z = std::floor(camera.position.z);
        if (world.get_block(cam_x, cam_y, cam_z) == WATER && camera.position.y < (WATER_LEVEL - 0.05f)) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), { 10, 50, 150, 100 });
        }

        // Destello rojo de daño en pantalla
        if (gs.hurt_flash_timer > 0.0f) {
            float flash_alpha = (gs.hurt_flash_timer / 0.25f) * 0.35f;
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(RED, flash_alpha));
        }

        ui.draw();

        // HUD de supervivencia (Corazones puros estilo Beta y burbujas)
        if (!gs.spectator_mode) {
            bool in_water = (world.get_block(cam_x, cam_y, cam_z) == Config::WATER ||
                             world.get_block(cam_x, std::floor(camera.position.y - Config::PLAYER_EYE_HEIGHT), cam_z) == Config::WATER ||
                             camera.position.y < (Config::WATER_LEVEL + 0.1f));
            ui.draw_survival_hud(gs.health, gs.max_health, gs.air_supply, gs.max_air, in_water, gs.hurt_flash_timer);
        }
        
        // Barra de progreso de minado
        if (gs.is_mining && gs.mining_progress > 0.0f && gs.mining_progress < 1.0f) {
            int cx = GetScreenWidth() / 2;
            int cy = GetScreenHeight() / 2;
            int bar_w = 60;
            int bar_h = 5;
            DrawRectangle(cx - bar_w/2, cy + 25, bar_w, bar_h, Fade(BLACK, 0.6f));
            DrawRectangle(cx - bar_w/2, cy + 25, (int)(bar_w * gs.mining_progress), bar_h, WHITE);
        }
        
        chat.draw();
        
        if (gs.show_debug_info) {
            DebugOverlayParams do_params = { camera, world, ui, chat, gs.day_time, gs.show_chunks, gs.spectator_mode, ray_hit_valid, target_solid, target_empty, cam_x, cam_z };
            DrawDebugOverlay(do_params);
        }

        if (menu.is_paused()) {
            menu.draw();
        }

        // Pantalla de Game Over si el jugador ha muerto
        if (gs.is_dead) {
            int sw = GetScreenWidth();
            int sh = GetScreenHeight();
            DrawRectangle(0, 0, sw, sh, Color{ 110, 0, 0, 190 });

            const char* dead_title = "¡HAS MUERTO!";
            int title_fs = 44;
            int tw = MeasureText(dead_title, title_fs);
            DrawText(dead_title, sw / 2 - tw / 2 + 2, sh / 2 - 98, title_fs, BLACK);
            DrawText(dead_title, sw / 2 - tw / 2, sh / 2 - 100, title_fs, Color{ 255, 60, 60, 255 });

            if (!gs.death_reason.empty()) {
                int rtw = MeasureText(gs.death_reason.c_str(), 18);
                DrawText(gs.death_reason.c_str(), sw / 2 - rtw / 2, sh / 2 - 40, 18, Color{ 220, 220, 220, 255 });
            }

            int bw = 220;
            int bh = 48;
            int bx = sw / 2 - bw / 2;
            int by = sh / 2 + 25;
            Vector2 m = GetMousePosition();
            bool hover = (m.x >= bx && m.x <= bx + bw && m.y >= by && m.y <= by + bh);

            DrawRectangle(bx, by, bw, bh, hover ? Color{ 70, 85, 105, 255 } : Color{ 35, 42, 54, 255 });
            DrawRectangleLines(bx, by, bw, bh, hover ? GOLD : Color{ 110, 130, 160, 255 });

            const char* respawn_txt = "Reaparecer";
            int r_tw = MeasureText(respawn_txt, 20);
            DrawText(respawn_txt, bx + (bw - r_tw) / 2, by + 14, 20, WHITE);

            if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                // Asegurar inventario vacío al reaparecer (los drops quedan en el sitio de muerte)
                for (auto& s : ui.slots) { s.count = 0; s.id = Config::AIR; s.name = ""; }
                for (auto& s : ui.storage) { s.count = 0; s.id = Config::AIR; s.name = ""; }
                ui.tool_inventory.clear();
                ui.selected_tool_idx = -1;

                // Respawn seguro exactamente en la superficie sólida
                float safe_y = world.get_spawn_surface_y(0.0f, 0.0f);
                camera.position = { 0.5f, safe_y + Config::PLAYER_EYE_HEIGHT, 0.5f };
                camera.target = { 0.5f, safe_y + Config::PLAYER_EYE_HEIGHT, 1.5f };
                gs.player_vel_y = 0.0f;
                gs.is_grounded = true;
                gs.has_landed_initial = false;
                gs.fall_distance = 0.0f;
                gs.health = gs.max_health;
                gs.air_supply = gs.max_air;
                gs.hurt_timer = 0.0f;
                gs.hurt_flash_timer = 0.0f;
                gs.is_dead = false;
                DisableCursor();
                chat.add_message("[Supervivencia] ¡Has reaparecido en la superficie!");
            }
        }

        EndDrawing();
        
        // Restore physical camera position
        camera.position.y -= gs.smooth_step_offset;
        camera.target.y -= gs.smooth_step_offset;
    }
    
    save_player_data(save_dir, camera, ui, gs.day_time, gs.health);
    
    world.stop_simulation();
    global_thread_pool.clear_queue();
    world.save_all();
    global_thread_pool.wait_idle();
    DatabaseIO::get().wait_idle();
    
    {
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
    
    AudioManager::get().cleanup();
    CloseWindow();
    return 0;
}
