#include "Chunk.hpp"
#include "Noise.hpp"
#include "MarchingCubes.hpp"
#include "Biome.hpp"
#include "Caves.hpp"
#include <rlgl.h>
#include <raymath.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <sqlite3.h>
#include "World.hpp"
#include "DatabaseIO.hpp"

using namespace Config;

Chunk::Chunk(int x, int z) : cx(x), cz(z) {
    int total_blocks = (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1);
    voxels.resize(total_blocks);
}

Chunk::~Chunk() {
    if (gen_future.valid()) gen_future.wait();
    voxels.clear();
    auto free_if_packed = [](Mesh& m) {
        if (m.vboId) {
            std::lock_guard<std::mutex> lock(gl_queue_mutex);
            gl_delete_queue.push_back(m);
        } else {
            if (m.vertices) { MemFree(m.vertices); m.vertices = nullptr; }
            if (m.normals) { MemFree(m.normals); m.normals = nullptr; }
            if (m.texcoords) { MemFree(m.texcoords); m.texcoords = nullptr; }
            if (m.texcoords2) { MemFree(m.texcoords2); m.texcoords2 = nullptr; }
            if (m.colors) { MemFree(m.colors); m.colors = nullptr; }
        }
        m = { 0 };
    };
    free_if_packed(solid_mesh);
    free_if_packed(water_mesh);
    free_if_packed(plants_mesh);
}

void Chunk::flush_gl_delete_queue() {
    std::vector<Mesh> pending;
    {
        std::lock_guard<std::mutex> lock(gl_queue_mutex);
        pending.swap(gl_delete_queue);
    }
    for (Mesh& m : pending) {
        UnloadMesh(m);
        if (m.vertices) { MemFree(m.vertices); m.vertices = nullptr; }
        if (m.normals) { MemFree(m.normals); m.normals = nullptr; }
        if (m.texcoords) { MemFree(m.texcoords); m.texcoords = nullptr; }
        if (m.texcoords2) { MemFree(m.texcoords2); m.texcoords2 = nullptr; }
        if (m.colors) { MemFree(m.colors); m.colors = nullptr; }
    }
}

void Chunk::start_generation() {
    generating = true;
    std::shared_ptr<Chunk> holder = shared_from_this();
    gen_future = global_thread_pool.enqueue([holder, this] {
        this->generate_thread();
    });
}


void Chunk::generate_thread() {
    int total_blocks = (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1);
    bool loaded = false;
    {
        extern sqlite3* db;
        if (db) {
            sqlite3_stmt* stmt;
            const char* sql = "SELECT chunk_data FROM chunks WHERE cx = ? AND cz = ?";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, cx);
                sqlite3_bind_int(stmt, 2, cz);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const void* blob = sqlite3_column_blob(stmt, 0);
                    int bytes = sqlite3_column_bytes(stmt, 0);
                    if (bytes == total_blocks * sizeof(Config::VoxelData)) {
                        memcpy(voxels.data(), blob, bytes);
                        loaded = true;
                    } else if (bytes == total_blocks * (sizeof(float) + sizeof(uint8_t))) {
                        const float* old_d = (const float*)blob;
                        const uint8_t* old_b = (const uint8_t*)((const char*)blob + total_blocks * sizeof(float));
                        for (int i = 0; i < total_blocks; i++) {
                            voxels[i].density = old_d[i];
                            voxels[i].block = old_b[i];
                            voxels[i].water = 0;
                        }
                        loaded = true;
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
    }

    if (!loaded) {
        int seed_offset = static_cast<int>(static_cast<uint32_t>(Config::WORLD_SEED) * 1000U);
        
        std::vector<int> top_solid_y( (CHUNK_SIZE + 1) * (CHUNK_SIZE + 1), 0 );
        std::vector<bool> has_solid( (CHUNK_SIZE + 1) * (CHUNK_SIZE + 1), false );
        std::vector<BiomeConfig> col_biomes( (CHUNK_SIZE + 1) * (CHUNK_SIZE + 1) );
        std::vector<bool> cave_air( (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1), false );

        Caves::CaveMap cave_map;
        cave_map.generate(cx, cz, (uint32_t)Config::WORLD_SEED);

        for (int lx = 0; lx <= CHUNK_SIZE; ++lx) {
            for (int lz = 0; lz <= CHUNK_SIZE; ++lz) {
                double wx = cx * CHUNK_SIZE + lx;
                double wz = cz * CHUNK_SIZE + lz;
                
                BiomeConfig biome = Biome::get_biome_at(wx, wz, seed_offset);
                col_biomes[lz * (CHUNK_SIZE + 1) + lx] = biome;

                double sx = wx + seed_offset;
                double sz = wz + seed_offset;
                
                // Relieve base
                double n_base = pnoise3(sx * 0.005, 0.0, sz * 0.005, 4, 0.5);
                double n_detail = pnoise3(sx * 0.02, 0.0, sz * 0.02, 4, 0.5);
                
                // Mascara de macizos montanosos circulares y suaves
                double n_mount_mask = pnoise3(sx * 0.0011, 750.0, sz * 0.0011, 3, 0.5);
                float mount_t = std::clamp((float)((n_mount_mask - 0.15) / 0.35), 0.0f, 1.0f);
                float smooth_mount = mount_t * mount_t * (3.0f - 2.0f * mount_t); // Smoothstep
                
                // Perfilado de crestas afiladas
                double n_mount_raw = std::abs(pnoise3(sx * 0.007, 0.0, sz * 0.007, 4, 0.5));
                double n_mount_sharp = std::pow(n_mount_raw, 1.3) * 1.7;
                
                double valley_base = (n_base < 0) ? (n_base * biome.valley_neg_scale) : (n_base * biome.valley_pos_scale);
                
                // La montaña nace desde el nivel base del valle y asciende de forma continua y suave
                double mountain_elevation = smooth_mount * n_mount_sharp * biome.mountain_scale;
                
                double raw_h = biome.base_height + valley_base + (n_detail * biome.detail_scale) + mountain_elevation;
                float base_h = (float)raw_h; // 100% suave, sin saltos de redondeo
                
                int highest_y = 0;

                for (int y = 0; y < GRID_Y; ++y) {
                    float n3d = pnoise3(sx * 0.05f, y * 0.05f, sz * 0.05f, 2, 0.4f);
                    
                    float true_depth = (base_h - y) + (n3d * 2.5f);
                    float d = true_depth;
                    
                    if (y == 0) d = 1.0f;
                    if (y == GRID_Y - 1) d = -1.0f;
                    
                    d = std::max(-1.0f, std::min(1.0f, d));
                    
                    // Determinar la cota real de la superficie del terreno antes de tallar cuevas
                    if (d >= ISO_SURFACE) {
                        highest_y = y;
                        has_solid[lz * (CHUNK_SIZE + 1) + lx] = true;
                    }
                    
                    // Comprobación de cuevas subterráneas limpias y contiguas en 3D
                    bool in_cave = false;
                    if (y >= 3 && y <= 72) {
                        in_cave = cave_map.is_cave_at((float)wx, (float)y, (float)wz, base_h, seed_offset);
                    }
                    
                    // Límite de Bedrock irregular para que se vea bien
                    float bedrock_noise = pnoise3(sx * 0.3f, y * 0.3f, sz * 0.3f, 1, 0.5f);
                    if (y <= 1 || (y <= 4 && bedrock_noise > -0.2f)) {
                        d = 1.0f;
                        in_cave = false; // Evitar huecos en el piso de bedrock
                    }
                    
                    if (in_cave) {
                        d = -1.0f; // Bloque de aire / vacío puro
                        cave_air[get_idx(lx, y, lz)] = true;
                    } else {
                        cave_air[get_idx(lx, y, lz)] = false;
                    }
                    
                    voxels[get_idx(lx, y, lz)].density = d;
                }
                top_solid_y[lz * (CHUNK_SIZE + 1) + lx] = highest_y;
            }
        }

        std::vector<bool> is_water( (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1), false );
        std::vector<bool> water_columns( (CHUNK_SIZE + 1) * (CHUNK_SIZE + 1), false );

        for (int lx = 0; lx <= CHUNK_SIZE; ++lx) {
            for (int lz = 0; lz <= CHUNK_SIZE; ++lz) {
                int col_idx = lz * (CHUNK_SIZE + 1) + lx;
                int top = top_solid_y[col_idx];
                bool solid_exists = has_solid[col_idx];
                const BiomeConfig& biome = col_biomes[col_idx];
                
                // Calcular pendiente local con vecinos
                int max_slope = 0;
                for (int dx = -1; dx <= 1; dx++) {
                    for (int dz = -1; dz <= 1; dz++) {
                        int nx = std::clamp(lx + dx, 0, CHUNK_SIZE);
                        int nz = std::clamp(lz + dz, 0, CHUNK_SIZE);
                        max_slope = std::max(max_slope, std::abs(top - top_solid_y[nz * (CHUNK_SIZE + 1) + nx]));
                    }
                }
                // Acantilado de roca solo si es fuerte pendiente vertical o pico extremo (>108)
                bool is_steep_cliff = (biome.type == BIOME_MOUNTAINS && (max_slope >= 3 || top > 108));
                
                for (int y = 0; y < GRID_Y; ++y) {
                    int i = get_idx(lx, y, lz);
                    voxels[i].block = AIR;
                    
                    if (voxels[i].density >= ISO_SURFACE) {
                        voxels[i].block = STONE;
                        if (solid_exists) {
                            if (is_steep_cliff) {
                                voxels[i].block = STONE;
                            } else if (biome.type == BIOME_OCEAN || biome.type == BIOME_BEACH) {
                                if (y > top - 3 && y <= top) {
                                    voxels[i].block = Config::SAND;
                                } else if (y > top - 6 && y <= top - 3) {
                                    voxels[i].block = Config::GRAVEL; // Grava bajo la arena marina
                                }
                            } else if (biome.type == BIOME_DESERT) {
                                if (y > top - 3 && y <= top) {
                                    voxels[i].block = Config::SAND;
                                } else if (y > top - 7 && y <= top - 3) {
                                    voxels[i].block = Config::RED_CLAY; // Arcilla roja en el desierto
                                }
                            } else {
                                if (y > top - 2 && y <= top) {
                                    voxels[i].block = biome.surface_block;
                                } else if (y > top - 6 && y <= top - 2) {
                                    voxels[i].block = biome.subsurface_block;
                                }
                            }
                        }
                    }
                    
                    if (y <= (int)WATER_LEVEL && voxels[i].density < ISO_SURFACE && y >= top && !cave_air[i]) {
                        voxels[i].block = WATER;
                        is_water[i] = true;
                        water_columns[col_idx] = true;
                    }
                }
            }
        }

        std::vector<bool> shore_columns( (CHUNK_SIZE + 1) * (CHUNK_SIZE + 1), false );
        for (int lx = 0; lx <= CHUNK_SIZE; ++lx) {
            for (int lz = 0; lz <= CHUNK_SIZE; ++lz) {
                if (water_columns[lz * (CHUNK_SIZE+1) + lx]) {
                    for (int dx = -1; dx <= 1; dx++) {
                        for (int dz = -1; dz <= 1; dz++) {
                            int nx = lx + dx, nz = lz + dz;
                            if (nx >= 0 && nx <= CHUNK_SIZE && nz >= 0 && nz <= CHUNK_SIZE) {
                                shore_columns[nz * (CHUNK_SIZE+1) + nx] = true;
                            }
                        }
                    }
                }
            }
        }

        for (int lx = 0; lx <= CHUNK_SIZE; ++lx) {
            for (int lz = 0; lz <= CHUNK_SIZE; ++lz) {
                bool is_shore = shore_columns[lz * (CHUNK_SIZE + 1) + lx];
                const BiomeConfig& biome = col_biomes[lz * (CHUNK_SIZE + 1) + lx];
                for (int y = 0; y < GRID_Y; ++y) {
                    int i = get_idx(lx, y, lz);
                    if (voxels[i].block == GRASS) {
                        if (y < (int)WATER_LEVEL || is_shore) {
                            voxels[i].block = (biome.type == BIOME_BEACH || biome.type == BIOME_DESERT || biome.type == BIOME_OCEAN) ? Config::SAND : Config::DIRT;
                        }
                    }
                }
            }
        }
        
        // Generacion de minerales (vetas subterraneas)
        // Orden: primero los profundos, luego los superficiales (carbon al final)
        {
            int seed_offset = (int)((int64_t)Config::WORLD_SEED * 7919);
            for (int lx = 0; lx <= CHUNK_SIZE; ++lx) {
                for (int lz = 0; lz <= CHUNK_SIZE; ++lz) {
                    for (int y = 1; y < GRID_Y - 1; ++y) {
                        int i = get_idx(lx, y, lz);
                        if (voxels[i].block != STONE) continue;

                        int wx = cx * CHUNK_SIZE + lx;
                        int wz = cz * CHUNK_SIZE + lz;

                        // Diamante: prof 110+, el mas raro primero
                        if (y >= 110) {
                            float n = (float)pnoise3(wx * 0.04f + seed_offset + 2000, y * 0.04f, wz * 0.04f + seed_offset + 2000, 3, 0.5f);
                            if (n > 0.78f) { voxels[i].block = DIAMOND_ORE; continue; }
                        }
                        // Oro: prof 80-110
                        if (y >= 80 && y < 110) {
                            float n = (float)pnoise3(wx * 0.05f + seed_offset + 1500, y * 0.05f, wz * 0.05f + seed_offset + 1500, 3, 0.5f);
                            if (n > 0.73f) { voxels[i].block = GOLD_ORE; continue; }
                        }
                        // Plata: prof 60-90
                        if (y >= 60 && y < 90) {
                            float n = (float)pnoise3(wx * 0.06f + seed_offset + 1000, y * 0.06f, wz * 0.06f + seed_offset + 1000, 3, 0.5f);
                            if (n > 0.70f) { voxels[i].block = SILVER_ORE; continue; }
                        }
                        // Hierro: prof 40-80
                        if (y >= 40 && y < 80) {
                            float n = (float)pnoise3(wx * 0.07f + seed_offset + 500, y * 0.07f, wz * 0.07f + seed_offset + 500, 3, 0.5f);
                            if (n > 0.65f) { voxels[i].block = IRON_ORE; continue; }
                        }
                        // Carbon: prof 0-60, comun pero al final para no comerse otros
                        if (y < 60) {
                            float n = (float)pnoise3(wx * 0.09f + seed_offset, y * 0.09f, wz * 0.09f + seed_offset, 3, 0.5f);
                            if (n > 0.72f) { voxels[i].block = COAL_ORE; continue; }
                        }
                    }
                }
            }
        }

        // Arboles (Roble y Abedul)
        for (int lx = 3; lx < CHUNK_SIZE - 3; ++lx) {
            for (int lz = 3; lz < CHUNK_SIZE - 3; ++lz) {
                int col_idx = lz * (CHUNK_SIZE + 1) + lx;
                int top = top_solid_y[col_idx];
                const BiomeConfig& biome = col_biomes[col_idx];
                
                if (top > WATER_LEVEL) {
                    uint8_t surface_block = voxels[get_idx(lx, top, lz)].block;
                    bool can_tree = (surface_block == Config::GRASS && biome.tree_chance > 0);
                    bool can_cactus = (surface_block == Config::SAND && biome.cactus_chance > 0);
                    
                    if (can_tree || can_cactus) {
                        int g_wx = cx * CHUNK_SIZE + lx;
                        int g_wz = cz * CHUNK_SIZE + lz;
                        
                        // Hash 2D de alta entropia para distribucion organica uniforme
                        uint32_t th = (static_cast<uint32_t>(g_wx) * 374761393U) ^ 
                                      (static_cast<uint32_t>(g_wz) * 668265263U) ^ 
                                      (static_cast<uint32_t>(seed_offset) * 1274126177U);
                        th = (th ^ (th >> 13)) * 1274126177U;
                        th = th ^ (th >> 16);
                        int spawn_hash = (int)(th % 1000U);
                        
                        if (can_tree && spawn_hash < biome.tree_chance) {
                            int h_range = std::max(1, biome.tree_max_h - biome.tree_min_h + 1);
                            int tree_h = biome.tree_min_h + (spawn_hash % h_range);
                            
                            // Determinar especie: Roble (WOOD) o Abedul (BIRCH_WOOD)
                            int species_val = (int)((th / 1000U) % 100U);
                            bool is_birch = (biome.birch_tree_ratio > 0.0f) && (species_val < (int)(biome.birch_tree_ratio * 100.0f));
                            uint8_t trunk_block = is_birch ? Config::BIRCH_WOOD : Config::WOOD;
                            
                            for (int ty = 1; ty <= tree_h; ++ty) {
                                if (top + ty < GRID_Y) {
                                    voxels[get_idx(lx, top + ty, lz)].block = trunk_block;
                                    voxels[get_idx(lx, top + ty, lz)].density = 1.0f;
                                }
                            }
                            int r = (biome.type == BIOME_JUNGLE) ? 3 : 2;
                            for (int dx = -r; dx <= r; ++dx) {
                                for (int dy = -r; dy <= r; ++dy) {
                                    for (int dz = -r; dz <= r; ++dz) {
                                        if (dx*dx + dy*dy + dz*dz <= r*r + 1) {
                                            int nx = lx + dx;
                                            int ny = top + tree_h + dy;
                                            int nz = lz + dz;
                                            if (nx >= 0 && nx <= CHUNK_SIZE && nz >= 0 && nz <= CHUNK_SIZE && ny >= 0 && ny < GRID_Y) {
                                                if (voxels[get_idx(nx, ny, nz)].density < 0.0f) {
                                                    voxels[get_idx(nx, ny, nz)].density = 1.0f;
                                                    voxels[get_idx(nx, ny, nz)].block = Config::LEAVES;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        else if (can_cactus && spawn_hash < biome.cactus_chance) {
                            int h_range = 3; // 4 - 2 + 1
                            int cactus_h = 2 + (spawn_hash % h_range);
                            
                            for (int ty = 1; ty <= cactus_h; ++ty) {
                                if (top + ty < GRID_Y) {
                                    voxels[get_idx(lx, top + ty, lz)].block = Config::CACTUS;
                                    voxels[get_idx(lx, top + ty, lz)].density = 1.0f;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Save to DB
        {
            std::vector<char> buffer(total_blocks * sizeof(Config::VoxelData));
            memcpy(buffer.data(), voxels.data(), buffer.size());
            
            int cap_cx = cx;
            int cap_cz = cz;
            
            DatabaseIO::get().enqueue([cap_cx, cap_cz, buffer = std::move(buffer)]() {
                extern sqlite3* db;
                if (db) {
                    sqlite3_stmt* stmt;
                    const char* sql = "INSERT OR REPLACE INTO chunks (cx, cz, chunk_data) VALUES (?, ?, ?)";
                    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
                        sqlite3_bind_int(stmt, 1, cap_cx);
                        sqlite3_bind_int(stmt, 2, cap_cz);
                        sqlite3_bind_blob(stmt, 3, buffer.data(), buffer.size(), SQLITE_TRANSIENT);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
            });
        }
    }

    for (int i = 0; i < total_blocks; ++i) {
        voxels[i].water = (voxels[i].block == Config::WATER) ? 8 : 0;
    }

    {
        std::lock_guard<std::mutex> lock(chunk_mutex);
        if (!pending_edits.empty()) {
            for (const auto& e : pending_edits) {
                int i = get_idx(e.x, e.y, e.z);
                voxels[i].block = e.type;
                voxels[i].density = (e.type == Config::AIR || e.type == Config::WATER) ? -1.0f : 1.0f;
                voxels[i].water = (e.type == Config::WATER) ? 8 : 0;
            }
            pending_edits.clear();
            needs_save = true;
        }
        generating = false;
    }

    build_mesh_data(voxels.data(), current_lod.load());
    pack_meshes(3);
    needs_upload = true;
    is_ready = true;
}

void Chunk::rebuild_thread() {
    int total_blocks = (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1);
    std::vector<Config::VoxelData> v_copy(total_blocks);
    rebuild_running = true;
    int mode;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(rebuild_mutex);
            mode = rebuild_mode;
            rebuild_mode = 0;
        }
        if (mode == 0) break;
        {
            std::lock_guard<std::mutex> lock(chunk_mutex);
            v_copy = voxels;
        }
        int lod = current_lod.load();
        if (mode >= 2) {
            build_mesh_data(v_copy.data(), lod);
            pack_meshes(3);
        } else {
            build_water_mesh(v_copy.data(), lod);
            pack_meshes(1);
        }
    }
    rebuild_running = false;
    needs_upload = true;
}

void Chunk::build_mesh_data(const Config::VoxelData* voxels_ptr, int lod) {
    std::vector<Vector3> ls_vertices, ls_normals;
    std::vector<Vector2> ls_uvs, ls_uvs2;
    std::vector<Color> ls_colors;

    std::vector<Vector3> lp_vertices, lp_normals;
    std::vector<Vector2> lp_uvs;
    std::vector<Color> lp_colors;

    // 1. Terrain Mesh (Marching Cubes)
    int seed_offset = static_cast<int>(static_cast<uint32_t>(Config::WORLD_SEED) * 1000U);

    // Pre-compute tint cache per column (17x17 for CHUNK_SIZE+1) using fast grid convolution
    const int CACHE_SIZE = CHUNK_SIZE + 1;
    std::vector<Color> grass_tint_cache(CACHE_SIZE * CACHE_SIZE);
    std::vector<Color> foliage_tint_cache(CACHE_SIZE * CACHE_SIZE);
    Biome::compute_chunk_tint_cache(cx, cz, seed_offset, grass_tint_cache.data(), foliage_tint_cache.data());

    mc::generate(voxels_ptr, nullptr, CHUNK_SIZE + 1, GRID_Y, CHUNK_SIZE + 1, ISO_SURFACE, Config::GRASS, ls_vertices, ls_normals, ls_uvs, ls_uvs2, ls_colors, cx * CHUNK_SIZE, cz * CHUNK_SIZE, seed_offset, lod, grass_tint_cache.data(), foliage_tint_cache.data());
    
    // Remap vertices to global coords
    for (auto& v : ls_vertices) {
        v.x += cx * CHUNK_SIZE;
        v.z += cz * CHUNK_SIZE;
    }

    // [Water and plants... omitted for brevity, I will update water separately]
    
    // Plant / Vegetation Cross-Quad Generation (Pasto Alto, Setas, Arbustos)
    for (size_t i = 0; i < ls_vertices.size(); i += 3) {
        Vector3 n = ls_normals[i];
        if (n.y > 0.8f) { // Superficies horizontales o suaves
            Vector3 center = { (ls_vertices[i].x + ls_vertices[i+1].x + ls_vertices[i+2].x) / 3.0f,
                               (ls_vertices[i].y + ls_vertices[i+1].y + ls_vertices[i+2].y) / 3.0f,
                               (ls_vertices[i].z + ls_vertices[i+1].z + ls_vertices[i+2].z) / 3.0f };
                               
            int lx = std::clamp((int)std::floor(center.x) - cx * CHUNK_SIZE, 0, CHUNK_SIZE);
            int ly = (int)std::floor(center.y);
            int lz = std::clamp((int)std::floor(center.z) - cz * CHUNK_SIZE, 0, CHUNK_SIZE);
            
            uint8_t b1 = get_block(lx, ly, lz);
            uint8_t b2 = get_block(lx, ly - 1, lz);
            bool on_grass = (b1 == Config::GRASS || b2 == Config::GRASS);
            bool on_sand  = (b1 == Config::SAND  || b2 == Config::SAND);
            
            int cache_idx = lz * CACHE_SIZE + lx;
            BiomeConfig biome = Biome::get_discrete_biome(center.x, center.z, seed_offset);
            Color plant_grass_tint = grass_tint_cache[cache_idx];
            
            // Hash 2D de alta entropia para plantas (sin lineas diagonales)
            int cell_x = (int)std::floor(center.x * 3.0f);
            int cell_z = (int)std::floor(center.z * 3.0f);
            uint32_t ph = (static_cast<uint32_t>(cell_x) * 374761393U) ^ 
                          (static_cast<uint32_t>(cell_z) * 668265263U) ^ 
                          (static_cast<uint32_t>(seed_offset) * 1274126177U);
            ph = (ph ^ (ph >> 13)) * 1274126177U;
            ph = ph ^ (ph >> 16);
            int hash = (int)(ph % 1000U);
            
            uint8_t plant_type = Config::AIR;
            Color plant_color = WHITE;
            float plant_height = 0.8f;
            float plant_width = 0.4f;
            int tile_var = 0;
            
            if (b1 == Config::TALL_GRASS || b2 == Config::TALL_GRASS) {
                plant_type = Config::TALL_GRASS;
                plant_color = plant_grass_tint;
            } else if (b1 == Config::RED_MUSHROOM || b2 == Config::RED_MUSHROOM) {
                plant_type = Config::RED_MUSHROOM;
                plant_height = 0.55f;
            } else if (b1 == Config::BROWN_MUSHROOM || b2 == Config::BROWN_MUSHROOM) {
                plant_type = Config::BROWN_MUSHROOM;
                plant_height = 0.55f;
            } else if (b1 == Config::DEAD_BUSH || b2 == Config::DEAD_BUSH) {
                plant_type = Config::DEAD_BUSH;
                plant_height = 0.7f;
            } else if (on_grass) {
                if (hash < biome.tall_grass_chance) {
                    plant_type = Config::TALL_GRASS;
                    plant_color = plant_grass_tint;
                    tile_var = hash % 3;
                } else if (hash < biome.tall_grass_chance + biome.red_mushroom_chance) {
                    plant_type = Config::RED_MUSHROOM;
                    plant_height = 0.55f;
                } else if (hash < biome.tall_grass_chance + biome.red_mushroom_chance + biome.brown_mushroom_chance) {
                    plant_type = Config::BROWN_MUSHROOM;
                    plant_height = 0.55f;
                }
            } else if (on_sand) {
                if (hash < biome.dead_bush_chance) {
                    plant_type = Config::DEAD_BUSH;
                    plant_height = 0.7f;
                }
            }
            
            if (plant_type != Config::AIR) {
                BlockType bt = Config::BLOCKS.at(plant_type);
                float tw = 1.0f / 9.0f;
                float th = 1.0f / 10.0f;
                float sway = bt.is_waving ? 10.0f : 0.0f;
                float u0 = bt.tex_x * tw;
                float v0 = (10.0f - 1.0f - (bt.tex_y - tile_var)) * th;
                float u1 = u0 + tw;
                float v1 = v0 + th;
                
                Vector3 base = center;
                Vector3 up = n;
                Vector3 right = {1, 0, 0};
                if (std::abs(up.y) < 0.999f) {
                    right = Vector3Normalize(Vector3CrossProduct({0,1,0}, up));
                }
                Vector3 fwd = Vector3Normalize(Vector3CrossProduct(right, up));
                
                Vector3 d1 = Vector3Add(Vector3Scale(right, plant_width), Vector3Scale(fwd, plant_width));
                Vector3 d2 = Vector3Subtract(Vector3Scale(right, plant_width), Vector3Scale(fwd, plant_width));
                Vector3 ht = Vector3Scale(up, plant_height);
                
                Vector3 q1_v[] = {
                    Vector3Subtract(base, d1),
                    Vector3Add(base, d1),
                    Vector3Add(Vector3Add(base, d1), ht),
                    Vector3Add(Vector3Subtract(base, d1), ht)
                };
                
                Vector3 q2_v[] = {
                    Vector3Add(base, d2),
                    Vector3Subtract(base, d2),
                    Vector3Add(Vector3Subtract(base, d2), ht),
                    Vector3Add(Vector3Add(base, d2), ht)
                };
                
                bool is_foliage_plant = (plant_type == Config::TALL_GRASS);
                float foliage_offset = is_foliage_plant ? 10.0f : 0.0f;
                
                Vector2 uvs[] = { 
                    {u1, v1 + foliage_offset}, 
                    {u0, v1 + foliage_offset}, 
                    {u0 + sway, v0 + foliage_offset}, 
                    {u1 + sway, v0 + foliage_offset} 
                };
                Vector3 nor = {0, 1, 0};
                
                auto add_quad = [&](Vector3* v) {
                    lp_vertices.push_back(v[0]); lp_vertices.push_back(v[1]); lp_vertices.push_back(v[2]);
                    lp_vertices.push_back(v[0]); lp_vertices.push_back(v[2]); lp_vertices.push_back(v[3]);
                    
                    for(int j=0; j<6; j++) {
                        lp_normals.push_back(nor); lp_colors.push_back(plant_color);
                    }
                    
                    lp_uvs.push_back(uvs[0]); lp_uvs.push_back(uvs[1]); lp_uvs.push_back(uvs[2]);
                    lp_uvs.push_back(uvs[0]); lp_uvs.push_back(uvs[2]); lp_uvs.push_back(uvs[3]);
                };
                
                add_quad(q1_v);
                add_quad(q2_v);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(mesh_mutex);
        s_vertices = std::move(ls_vertices);
        s_normals = std::move(ls_normals);
        s_uvs = std::move(ls_uvs);
        s_uvs2 = std::move(ls_uvs2);
        s_colors = std::move(ls_colors);

        p_vertices = std::move(lp_vertices);
        p_normals = std::move(lp_normals);
        p_uvs = std::move(lp_uvs);
        p_colors = std::move(lp_colors);
    }

    build_water_mesh(voxels_ptr, lod);
}

void Chunk::build_water_mesh(const Config::VoxelData* voxels_ptr, int lod) {
    std::vector<Vector3> lw_vertices, lw_normals;
    std::vector<Vector2> lw_uvs, lw_uvs2;
    std::vector<Color> lw_colors;

    int wx = CHUNK_SIZE + 1;
    int wz = CHUNK_SIZE + 1;
    std::vector<float> col_height(wx * wz, -100.0f);
    
    for (int x = 0; x < wx; ++x) {
        for (int z = 0; z < wz; ++z) {
            float water_top = -100.0f;
            for (int y = GRID_Y - 1; y >= 0; --y) {
                int i = get_idx(x, y, z);
                if (water_top < -50.0f && voxels_ptr[i].water > 0) {
                    float drop = 0.05f + (8 - voxels_ptr[i].water) * 0.10f;
                    water_top = (float)y - drop;
                }
                
                if (voxels_ptr[i].density >= ISO_SURFACE) {
                    break;
                }
            }
            if (water_top > -50.0f) {
                col_height[z * wx + x] = water_top;
            }
        }
    }

    // Extend water heights to adjacent land columns so shore bilinear interpolation remains flat
    std::vector<float> smoothed_height = col_height;
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<float> temp = smoothed_height;
        for (int x = 0; x < wx; ++x) {
            for (int z = 0; z < wz; ++z) {
                if (smoothed_height[z * wx + x] < -50.0f) {
                    float max_h = -100.0f;
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dz = -1; dz <= 1; ++dz) {
                            int nx = x + dx;
                            int nz = z + dz;
                            if (nx >= 0 && nx < wx && nz >= 0 && nz < wz) {
                                if (smoothed_height[nz * wx + nx] > -50.0f) {
                                    max_h = std::max(max_h, smoothed_height[nz * wx + nx]);
                                }
                            }
                        }
                    }
                    if (max_h > -50.0f) {
                        temp[z * wx + x] = max_h;
                    }
                }
            }
        }
        smoothed_height = temp;
    }
    col_height = smoothed_height;

    // 3D volumetric Marching Cubes
    int total_blocks = wx * GRID_Y * wz;
    std::vector<float> water_density(total_blocks, 1.0f);
    for (int i = 0; i < total_blocks; ++i) {
        if (voxels_ptr[i].block == Config::WATER) {
            water_density[i] = -1.0f;
        }
    }

    mc::generate(voxels_ptr, water_density.data(), wx, GRID_Y, wz, ISO_SURFACE, Config::WATER, lw_vertices, lw_normals, lw_uvs, lw_uvs2, lw_colors, 0.0f, 0.0f, 0, lod);

    std::vector<Vector3> fw_vertices, fw_normals;
    std::vector<Vector2> fw_uvs, fw_uvs2;
    std::vector<Color> fw_colors;

    auto sample_height = [&](float px, float pz) -> float {
        px = std::max(0.0f, std::min((float)Config::CHUNK_SIZE, px));
        pz = std::max(0.0f, std::min((float)Config::CHUNK_SIZE, pz));
        int x0 = (int)px;
        int z0 = (int)pz;
        int x1 = std::min(x0 + 1, (int)Config::CHUNK_SIZE);
        int z1 = std::min(z0 + 1, (int)Config::CHUNK_SIZE);
        float tx = px - x0;
        float tz = pz - z0;
        float h00 = col_height[z0 * wx + x0];
        float h10 = col_height[z0 * wx + x1];
        float h01 = col_height[z1 * wx + x0];
        float h11 = col_height[z1 * wx + x1];
        float h0 = h00 * (1.0f - tx) + h10 * tx;
        float h1 = h01 * (1.0f - tx) + h11 * tx;
        return h0 * (1.0f - tz) + h1 * tz;
    };

    auto get_foam_alpha = [&](Vector3 v) -> unsigned char {
        float fx = std::max(0.0f, std::min((float)Config::CHUNK_SIZE, v.x));
        float fz = std::max(0.0f, std::min((float)Config::CHUNK_SIZE, v.z));
        
        int x0 = (int)fx;
        int z0 = (int)fz;
        int x1 = std::min(x0 + 1, (int)Config::CHUNK_SIZE);
        int z1 = std::min(z0 + 1, (int)Config::CHUNK_SIZE);
        
        float tx = fx - x0;
        float tz = fz - z0;
        
        int w = Config::CHUNK_SIZE + 1;
        auto get_d = [&](int x, int y, int z) {
            if (y < 0 || y >= Config::GRID_Y) return 0.0f;
            int idx = y * w * w + z * w + x;
            return voxels_ptr[idx].density;
        };
        
        float exact_depth = 3.0f; // Default deep
        int base_y = std::round(v.y);
        
        for (int dy = 0; dy <= 2; ++dy) {
            int y = base_y - dy;
            float d00 = get_d(x0, y, z0);
            float d10 = get_d(x1, y, z0);
            float d01 = get_d(x0, y, z1);
            float d11 = get_d(x1, y, z1);
            
            float d0 = d00 * (1.0f - tx) + d10 * tx;
            float d1 = d01 * (1.0f - tx) + d11 * tx;
            float d = d0 * (1.0f - tz) + d1 * tz;
            
            if (d >= 0.0f) {
                exact_depth = (float)dy - d;
                if (exact_depth < 0.0f) exact_depth = 0.0f;
                break;
            }
        }
        
        float max_depth = 2.0f; 
        float normalized = exact_depth / max_depth;
        normalized = std::max(0.0f, std::min(1.0f, normalized));
        
        return (unsigned char)((1.0f - normalized) * 255.0f);
    };

    for (size_t i = 0; i < lw_vertices.size(); i += 3) {
        Vector3 v0 = lw_vertices[i];
        Vector3 v1 = lw_vertices[i+1];
        Vector3 v2 = lw_vertices[i+2];

        auto apply_slant = [&](Vector3& v) {
            float sh = sample_height(v.x, v.z);
            if (sh > 0.0f && v.y > sh - 0.5f) {
                v.y = sh;
            }
        };

        apply_slant(v0);
        apply_slant(v1);
        apply_slant(v2);

        // Recalculate normal for triangles
        Vector3 u = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
        Vector3 v = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
        Vector3 n = {
            u.y * v.z - u.z * v.y,
            u.z * v.x - u.x * v.z,
            u.x * v.y - u.y * v.x
        };
        float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 0.0001f) {
            continue;
        }
        n.x /= len; n.y /= len; n.z /= len;

        fw_vertices.push_back(v0); fw_vertices.push_back(v1); fw_vertices.push_back(v2);
        fw_normals.push_back(n); fw_normals.push_back(n); fw_normals.push_back(n);
        fw_uvs.push_back(lw_uvs[i]); fw_uvs.push_back(lw_uvs[i+1]); fw_uvs.push_back(lw_uvs[i+2]);
        fw_uvs2.push_back(lw_uvs2[i]); fw_uvs2.push_back(lw_uvs2[i+1]); fw_uvs2.push_back(lw_uvs2[i+2]);
        
        Color c0 = lw_colors[i]; c0.a = get_foam_alpha(v0);
        Color c1 = lw_colors[i+1]; c1.a = get_foam_alpha(v1);
        Color c2 = lw_colors[i+2]; c2.a = get_foam_alpha(v2);
        fw_colors.push_back(c0); fw_colors.push_back(c1); fw_colors.push_back(c2);
    }

    // Reposition water (heights already sampled, only global x/z offset)
    for (auto& v : fw_vertices) {
        v.x += cx * CHUNK_SIZE;
        v.z += cz * CHUNK_SIZE;
    }

    {
        std::lock_guard<std::mutex> lock(mesh_mutex);
        w_vertices = std::move(fw_vertices);
        w_normals = std::move(fw_normals);
        w_uvs = std::move(fw_uvs);
        w_uvs2 = std::move(fw_uvs2);
        w_colors = std::move(fw_colors);
    }
}

void Chunk::pack_meshes(int mask) {
    std::lock_guard<std::mutex> lock(mesh_mutex);

    auto free_arrays = [](Mesh& m) {
        if (m.vertices) { MemFree(m.vertices); m.vertices = nullptr; }
        if (m.normals) { MemFree(m.normals); m.normals = nullptr; }
        if (m.texcoords) { MemFree(m.texcoords); m.texcoords = nullptr; }
        if (m.texcoords2) { MemFree(m.texcoords2); m.texcoords2 = nullptr; }
        if (m.colors) { MemFree(m.colors); m.colors = nullptr; }
    };

    if ((mask & 2) != 0) {
        free_arrays(next_solid_mesh);
        next_solid_mesh = {0};
        
        if (!s_vertices.empty()) {
            next_solid_mesh.vertexCount = s_vertices.size();
            next_solid_mesh.triangleCount = s_vertices.size() / 3;

            next_solid_mesh.vertices = (float*)MemAlloc(s_vertices.size() * 3 * sizeof(float));
            next_solid_mesh.normals = (float*)MemAlloc(s_normals.size() * 3 * sizeof(float));
            next_solid_mesh.texcoords = (float*)MemAlloc(s_uvs.size() * 2 * sizeof(float));
            next_solid_mesh.texcoords2 = (float*)MemAlloc(s_uvs2.size() * 2 * sizeof(float));
            next_solid_mesh.colors = (unsigned char*)MemAlloc(s_colors.size() * 4 * sizeof(unsigned char));

            for (size_t i = 0; i < s_vertices.size(); i++) {
                next_solid_mesh.vertices[i*3] = s_vertices[i].x;
                next_solid_mesh.vertices[i*3+1] = s_vertices[i].y;
                next_solid_mesh.vertices[i*3+2] = s_vertices[i].z;

                next_solid_mesh.normals[i*3] = s_normals[i].x;
                next_solid_mesh.normals[i*3+1] = s_normals[i].y;
                next_solid_mesh.normals[i*3+2] = s_normals[i].z;

                next_solid_mesh.texcoords[i*2] = s_uvs[i].x;
                next_solid_mesh.texcoords[i*2+1] = s_uvs[i].y;

                next_solid_mesh.texcoords2[i*2] = s_uvs2[i].x;
                next_solid_mesh.texcoords2[i*2+1] = s_uvs2[i].y;

                next_solid_mesh.colors[i*4] = s_colors[i].r;
                next_solid_mesh.colors[i*4+1] = s_colors[i].g;
                next_solid_mesh.colors[i*4+2] = s_colors[i].b;
                next_solid_mesh.colors[i*4+3] = s_colors[i].a;
            }

            std::vector<Vector3>().swap(s_vertices);
            std::vector<Vector3>().swap(s_normals);
            std::vector<Vector2>().swap(s_uvs);
            std::vector<Vector2>().swap(s_uvs2);
            std::vector<Color>().swap(s_colors);
        }

        free_arrays(next_plants_mesh);
        next_plants_mesh = {0};

        if (!p_vertices.empty()) {
            next_plants_mesh.vertexCount = p_vertices.size();
            next_plants_mesh.triangleCount = p_vertices.size() / 3;

            next_plants_mesh.vertices = (float*)MemAlloc(p_vertices.size() * 3 * sizeof(float));
            next_plants_mesh.normals = (float*)MemAlloc(p_normals.size() * 3 * sizeof(float));
            next_plants_mesh.texcoords = (float*)MemAlloc(p_uvs.size() * 2 * sizeof(float));
            next_plants_mesh.colors = (unsigned char*)MemAlloc(p_colors.size() * 4 * sizeof(unsigned char));

            for (size_t i = 0; i < p_vertices.size(); i++) {
                next_plants_mesh.vertices[i*3] = p_vertices[i].x;
                next_plants_mesh.vertices[i*3+1] = p_vertices[i].y;
                next_plants_mesh.vertices[i*3+2] = p_vertices[i].z;

                next_plants_mesh.normals[i*3] = p_normals[i].x;
                next_plants_mesh.normals[i*3+1] = p_normals[i].y;
                next_plants_mesh.normals[i*3+2] = p_normals[i].z;

                next_plants_mesh.texcoords[i*2] = p_uvs[i].x;
                next_plants_mesh.texcoords[i*2+1] = p_uvs[i].y;

                next_plants_mesh.colors[i*4] = p_colors[i].r;
                next_plants_mesh.colors[i*4+1] = p_colors[i].g;
                next_plants_mesh.colors[i*4+2] = p_colors[i].b;
                next_plants_mesh.colors[i*4+3] = p_colors[i].a;
            }

            std::vector<Vector3>().swap(p_vertices);
            std::vector<Vector3>().swap(p_normals);
            std::vector<Vector2>().swap(p_uvs);
            std::vector<Color>().swap(p_colors);
        }
    }

    if ((mask & 1) != 0) {
        free_arrays(next_water_mesh);
        next_water_mesh = {0};

        if (!w_vertices.empty()) {
            next_water_mesh.vertexCount = w_vertices.size();
            next_water_mesh.triangleCount = w_vertices.size() / 3;

            next_water_mesh.vertices = (float*)MemAlloc(w_vertices.size() * 3 * sizeof(float));
            next_water_mesh.normals = (float*)MemAlloc(w_normals.size() * 3 * sizeof(float));
            next_water_mesh.texcoords = (float*)MemAlloc(w_uvs.size() * 2 * sizeof(float));
            next_water_mesh.texcoords2 = (float*)MemAlloc(w_uvs2.size() * 2 * sizeof(float));
            next_water_mesh.colors = (unsigned char*)MemAlloc(w_colors.size() * 4 * sizeof(unsigned char));

            for (size_t i = 0; i < w_vertices.size(); i++) {
                next_water_mesh.vertices[i*3] = w_vertices[i].x;
                next_water_mesh.vertices[i*3+1] = w_vertices[i].y;
                next_water_mesh.vertices[i*3+2] = w_vertices[i].z;

                next_water_mesh.normals[i*3] = w_normals[i].x;
                next_water_mesh.normals[i*3+1] = w_normals[i].y;
                next_water_mesh.normals[i*3+2] = w_normals[i].z;

                next_water_mesh.texcoords[i*2] = w_uvs[i].x;
                next_water_mesh.texcoords[i*2+1] = w_uvs[i].y;

                next_water_mesh.texcoords2[i*2] = w_uvs2[i].x;
                next_water_mesh.texcoords2[i*2+1] = w_uvs2[i].y;

                next_water_mesh.colors[i*4] = w_colors[i].r;
                next_water_mesh.colors[i*4+1] = w_colors[i].g;
                next_water_mesh.colors[i*4+2] = w_colors[i].b;
                next_water_mesh.colors[i*4+3] = w_colors[i].a;
            }

            std::vector<Vector3>().swap(w_vertices);
            std::vector<Vector3>().swap(w_normals);
            std::vector<Vector2>().swap(w_uvs);
            std::vector<Vector2>().swap(w_uvs2);
            std::vector<Color>().swap(w_colors);
        }
    }

    pending_upload_mask.fetch_or(mask);
}

void Chunk::upload_meshes() {
    int mask = pending_upload_mask.exchange(0);
    if (mask == 0) mask = 3; // Fallback to all if called directly

    std::lock_guard<std::mutex> lock(mesh_mutex);

    auto upload_and_swap = [](Mesh& current, Mesh& next) {
        if (current.vboId) {
            rlUnloadVertexArray(current.vaoId);
            for (int i = 0; i < 9; i++) rlUnloadVertexBuffer(current.vboId[i]);
            MemFree(current.vboId);
            if (current.vertices) MemFree(current.vertices);
            if (current.normals) MemFree(current.normals);
            if (current.texcoords) MemFree(current.texcoords);
            if (current.texcoords2) MemFree(current.texcoords2);
            if (current.colors) MemFree(current.colors);
        }
        
        current = next;
        next = {0};
        
        if (current.vertexCount > 0) {
            UploadMesh(&current, false);
        }
    };

    if ((mask & 2) != 0) {
        upload_and_swap(solid_mesh, next_solid_mesh);
        upload_and_swap(plants_mesh, next_plants_mesh);
    }
    if ((mask & 1) != 0) {
        upload_and_swap(water_mesh, next_water_mesh);
    }
}

void Chunk::update_logic(int& upload_budget) {
    if (needs_upload && upload_budget > 0) {
        upload_meshes();
        needs_upload = false;
        --upload_budget;
    }
    
    if (is_dirty) {
        rebuild_mesh(water_only_rebuild.load());
        water_only_rebuild = false;
        is_dirty = false;
    }
}

void Chunk::draw_solid(Material& mat_solid, Material& mat_plants, Vector3 camera_pos) {
    if (!is_ready) return;
    
    if (solid_mesh.vboId) {
        DrawMesh(solid_mesh, mat_solid, MatrixIdentity());
    }
    
    if (plants_mesh.vboId) {
        float cx_center = cx * CHUNK_SIZE + CHUNK_SIZE / 2.0f;
        float cz_center = cz * CHUNK_SIZE + CHUNK_SIZE / 2.0f;
        
        if (std::abs(cx_center - camera_pos.x) <= 35.0f && std::abs(cz_center - camera_pos.z) <= 35.0f) {
            rlDisableBackfaceCulling();
            DrawMesh(plants_mesh, mat_plants, MatrixIdentity());
            rlEnableBackfaceCulling();
        }
    }
}

void Chunk::draw_water(Material& mat_water, Vector3 camera_pos) {
    if (!is_ready) return;
    
    if (water_mesh.vboId) {
        DrawMesh(water_mesh, mat_water, MatrixIdentity());
    }
}


void Chunk::set_block(int x, int y, int z, uint8_t type) {
    if (x < 0 || x > CHUNK_SIZE || y < 0 || y >= GRID_Y || z < 0 || z > CHUNK_SIZE) return;
    
    std::lock_guard<std::mutex> lock(chunk_mutex);
    if (generating) {
        pending_edits.push_back({x, y, z, type});
        needs_save = true;
        return;
    }
    int i = get_idx(x, y, z);
    voxels[i].block = type;
    
    if (type == AIR || type == Config::WATER) {
        voxels[i].density = -1.0f;
    } else {
        voxels[i].density = 1.0f;
    }
    
    voxels[i].water = (type == Config::WATER) ? 8 : 0;
    
    is_dirty = true;
    needs_save = true;
    water_only_rebuild = false;
}

void Chunk::rebuild_mesh(bool water_only) {
    int want = water_only ? 1 : 2;
    std::lock_guard<std::mutex> lock(rebuild_mutex);
    int current = rebuild_mode.load();
    if (current != 0) {
        rebuild_mode.store(current > want ? current : want);
        return;
    }
    rebuild_mode.store(want);
    std::shared_ptr<Chunk> holder = shared_from_this();
    global_thread_pool.enqueue([holder] {
        holder->rebuild_thread();
    });
}

void Chunk::save_to_disk() {
    int cap_cx = cx;
    int cap_cz = cz;
    std::shared_ptr<Chunk> holder = shared_from_this();
    
    global_thread_pool.enqueue([holder, cap_cx, cap_cz]() {
        int total_blocks = (Config::CHUNK_SIZE + 1) * Config::GRID_Y * (Config::CHUNK_SIZE + 1);
        std::vector<char> buffer(total_blocks * sizeof(Config::VoxelData));
        {
            std::lock_guard<std::mutex> lock(holder->chunk_mutex);
            memcpy(buffer.data(), holder->voxels.data(), buffer.size());
        }
        
        DatabaseIO::get().enqueue([cap_cx, cap_cz, buffer = std::move(buffer)]() {
            extern sqlite3* db;
            if (db) {
                sqlite3_stmt* stmt;
                const char* sql = "INSERT OR REPLACE INTO chunks (cx, cz, chunk_data) VALUES (?, ?, ?)";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, cap_cx);
                    sqlite3_bind_int(stmt, 2, cap_cz);
                    sqlite3_bind_blob(stmt, 3, buffer.data(), buffer.size(), SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
        });
    });
    is_dirty = false;
}



void Chunk::set_water_node(int x, int y, int z, uint8_t level) {
    if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return;
    std::lock_guard<std::mutex> lock(chunk_mutex);
    int i = get_idx(x, y, z);
    if (voxels[i].water == level && (level > 0) == (voxels[i].block == Config::WATER)) return;
    
    if (level > 0) {
        voxels[i].block = Config::WATER;
        voxels[i].water = (level > 8) ? 8 : level;
        voxels[i].density = -1.0f;
    } else {
        voxels[i].block = Config::AIR;
        voxels[i].water = 0;
        voxels[i].density = -1.0f;
    }
    
    is_dirty = true;
    water_only_rebuild = true;
}
