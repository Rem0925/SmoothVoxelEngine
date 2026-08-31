#include "world/Chunk.hpp"
#include "generation/Noise.hpp"
#include "world/MarchingCubes.hpp"
#include "generation/Biome.hpp"
#include "generation/Caves.hpp"
#include "world/VoxelLighting.hpp"
#include <rlgl.h>
#include <raymath.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>
#include "core/sqlite3.h"
#include "world/World.hpp"
#include "data/DatabaseIO.hpp"

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
    for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
        free_if_packed(subchunks[s].solid_mesh);
        free_if_packed(subchunks[s].build_mesh);
        free_if_packed(subchunks[s].water_mesh);
        free_if_packed(subchunks[s].plants_mesh);
        free_if_packed(subchunks[s].trans_mesh);
        free_if_packed(subchunks[s].next_solid_mesh);
        free_if_packed(subchunks[s].next_build_mesh);
        free_if_packed(subchunks[s].next_water_mesh);
        free_if_packed(subchunks[s].next_plants_mesh);
        free_if_packed(subchunks[s].next_trans_mesh);
    }
}

void Chunk::flush_gl_delete_queue() {
    std::vector<Mesh> pending;
    {
        std::lock_guard<std::mutex> lock(gl_queue_mutex);
        pending.swap(gl_delete_queue);
    }
    for (Mesh& m : pending) {
        UnloadMesh(m);
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
        
        // Generacion de minerales (vetas y menas 3D organicas en clusters)
        {
            struct OreVeinConfig {
                Config::BlockID block;
                int min_y;
                int max_y;
                int veins_per_chunk;
                int min_size;
                int max_size;
                float radius_scale;
            };

            static const OreVeinConfig ORE_CONFIGS[] = {
                // Diamante: Muy profundo, cerca del Bedrock (Y=2..18), vetas compactas de 2 a 5 bloques
                { Config::DIAMOND_ORE, 2,  18,  2,  2,  5, 1.4f },
                // Oro: Subsuelo profundo (Y=5..35), vetas de 3 a 7 bloques
                { Config::GOLD_ORE,    5,  35,  4,  3,  7, 1.7f },
                // Plata: Subsuelo medio-bajo (Y=10..52), vetas de 4 a 8 bloques
                { Config::SILVER_ORE,  10, 52,  5,  4,  8, 1.8f },
                // Hierro: Amplia presencia en cuevas y subsuelo (Y=5..75), vetas de 5 a 12 bloques
                { Config::IRON_ORE,    5,  75, 12,  5, 12, 2.2f },
                // Carbon: Comun en subsuelo y expuesto en montañas (Y=15..115), vetas de 8 a 18 bloques
                { Config::COAL_ORE,   15, 115, 16,  8, 18, 2.8f },
            };

            for (size_t ore_idx = 0; ore_idx < sizeof(ORE_CONFIGS)/sizeof(ORE_CONFIGS[0]); ++ore_idx) {
                const auto& ore = ORE_CONFIGS[ore_idx];
                
                for (int v = 0; v < ore.veins_per_chunk; ++v) {
                    // Semilla determinista de alta entropía por veta, ore y chunk
                    uint64_t rng_state = (static_cast<uint64_t>(cx) * 73856093ULL) ^
                                         (static_cast<uint64_t>(cz) * 19349663ULL) ^
                                         (static_cast<uint64_t>(Config::WORLD_SEED) * 83492791ULL) ^
                                         (static_cast<uint64_t>(ore_idx) * 2654435761ULL) ^
                                         (static_cast<uint64_t>(v) * 961748941ULL);

                    auto next_u32 = [&rng_state]() -> uint32_t {
                        rng_state ^= (rng_state >> 12);
                        rng_state ^= (rng_state << 25);
                        rng_state ^= (rng_state >> 27);
                        return static_cast<uint32_t>((rng_state * 0x2545F4914F6CDD1DULL) >> 32);
                    };

                    auto next_float = [&next_u32]() -> float {
                        return (next_u32() & 0xFFFFFF) / 16777216.0f;
                    };

                    float cx_local = next_float() * (float)CHUNK_SIZE;
                    float cz_local = next_float() * (float)CHUNK_SIZE;
                    float cy = (float)ore.min_y + next_float() * (float)(ore.max_y - ore.min_y);

                    int count = ore.min_size + (int)(next_float() * (float)(ore.max_size - ore.min_size + 1));
                    
                    float angle = next_float() * 3.14159265f;
                    float dx = std::sin(angle) * ((float)count / 8.0f);
                    float dz = std::cos(angle) * ((float)count / 8.0f);
                    float dy = (next_float() - 0.5f) * 2.0f;

                    float x0 = cx_local - dx, x1 = cx_local + dx;
                    float z0 = cz_local - dz, z1 = cz_local + dz;
                    float y0 = cy - dy,       y1 = cy + dy;

                    for (int step = 0; step < count; ++step) {
                        float t = (float)step / (float)count;
                        float cur_x = x0 + (x1 - x0) * t;
                        float cur_y = y0 + (y1 - y0) * t;
                        float cur_z = z0 + (z1 - z0) * t;

                        float radius_factor = std::sin(t * 3.14159265f);
                        float rx = (radius_factor * ore.radius_scale + 1.0f) * 0.5f;
                        float ry = (radius_factor * ore.radius_scale + 1.0f) * 0.5f;
                        float rz = (radius_factor * ore.radius_scale + 1.0f) * 0.5f;

                        int min_bx = std::max(0, (int)std::floor(cur_x - rx));
                        int max_bx = std::min(CHUNK_SIZE, (int)std::ceil(cur_x + rx));
                        int min_by = std::max(1, (int)std::floor(cur_y - ry));
                        int max_by = std::min(GRID_Y - 2, (int)std::ceil(cur_y + ry));
                        int min_bz = std::max(0, (int)std::floor(cur_z - rz));
                        int max_bz = std::min(CHUNK_SIZE, (int)std::ceil(cur_z + rz));

                        for (int bx = min_bx; bx <= max_bx; ++bx) {
                            float ddx = (bx - cur_x) / rx;
                            if (ddx * ddx > 1.0f) continue;
                            for (int bz = min_bz; bz <= max_bz; ++bz) {
                                float ddz = (bz - cur_z) / rz;
                                if (ddx * ddx + ddz * ddz > 1.0f) continue;
                                for (int by = min_by; by <= max_by; ++by) {
                                    float ddy = (by - cur_y) / ry;
                                    if (ddx * ddx + ddy * ddy + ddz * ddz <= 1.0f) {
                                        int idx = get_idx(bx, by, bz);
                                        if (voxels[idx].block == Config::STONE) {
                                            voxels[idx].block = ore.block;
                                        }
                                    }
                                }
                            }
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
                voxels[i].rotation = e.rotation;
                voxels[i].density = (e.type == Config::AIR || e.type == Config::WATER) ? -1.0f : 1.0f;
                voxels[i].water = (e.type == Config::WATER) ? 8 : 0;
            }
            pending_edits.clear();
            needs_save = true;
        }
        generating = false;
    }

    build_mesh_data(voxels.data(), current_lod.load(), 0xFF);
    pack_meshes(3, 0xFF);
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
        uint8_t sub_mask = dirty_subchunks_mask.exchange(0);
        if (sub_mask == 0) sub_mask = 0xFF;

        {
            std::lock_guard<std::mutex> lock(chunk_mutex);
            std::copy(voxels.begin(), voxels.end(), v_copy.begin());
        }
        int lod = current_lod.load();
        if (mode >= 2) {
            build_mesh_data(v_copy.data(), lod, sub_mask);
            pack_meshes(3, sub_mask);
        } else {
            build_water_mesh(v_copy.data(), lod, sub_mask);
            pack_meshes(1, sub_mask);
        }
    }
    rebuild_running = false;
    needs_upload = true;
}

void Chunk::build_mesh_data(const Config::VoxelData* voxels_ptr, int lod, uint8_t sub_mask) {
    std::vector<Vector3> ls_vertices, ls_normals;
    std::vector<Vector2> ls_uvs, ls_uvs2;
    std::vector<Color> ls_colors;

    std::vector<Vector3> lp_vertices, lp_normals;
    std::vector<Vector2> lp_uvs;
    std::vector<Color> lp_colors;

    std::vector<Vector3> lt_vertices, lt_normals;
    std::vector<Vector2> lt_uvs, lt_uvs2;
    std::vector<Color> lt_colors;

    // 1. Terrain Mesh (Marching Cubes)
    int seed_offset = static_cast<int>(static_cast<uint32_t>(Config::WORLD_SEED) * 1000U);

    // Pre-compute tint cache per column (17x17 for CHUNK_SIZE+1) using fast grid convolution
    const int CACHE_SIZE = CHUNK_SIZE + 1;
    std::vector<Color> grass_tint_cache(CACHE_SIZE * CACHE_SIZE);
    std::vector<Color> foliage_tint_cache(CACHE_SIZE * CACHE_SIZE);
    Biome::compute_chunk_tint_cache(cx, cz, seed_offset, grass_tint_cache.data(), foliage_tint_cache.data());

    // Calculo de Iluminacion Voxel (Luz Solar + Luz de Bloques / Antorchas)
    VoxelLighting::LightCache l_cache;
    l_cache.base_cx = cx;
    l_cache.base_cz = cz;
    
    std::shared_ptr<Chunk> neighbors[3][3];
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) {
                l_cache.chunks[1][1] = voxels_ptr;
            } else {
                neighbors[dx + 1][dz + 1] = g_world->get_chunk_shared(cx + dx, cz + dz);
                if (neighbors[dx + 1][dz + 1] && !neighbors[dx + 1][dz + 1]->voxels.empty()) {
                    l_cache.chunks[dx + 1][dz + 1] = neighbors[dx + 1][dz + 1]->voxels.data();
                } else {
                    l_cache.chunks[dx + 1][dz + 1] = nullptr;
                }
            }
        }
    }

    std::vector<uint8_t> local_light_grid((Config::CHUNK_SIZE + 1) * Config::GRID_Y * (Config::CHUNK_SIZE + 1));
    VoxelLighting::compute_chunk_lighting(l_cache, local_light_grid.data());

    int min_sub = 0;
    while (min_sub < Config::NUM_SUBCHUNKS && !(sub_mask & (1 << min_sub))) min_sub++;
    int max_sub = Config::NUM_SUBCHUNKS - 1;
    while (max_sub >= 0 && !(sub_mask & (1 << max_sub))) max_sub--;

    if (min_sub <= max_sub) {
        int mc_min_y = min_sub * Config::SUBCHUNK_SIZE;
        int mc_max_y = (max_sub + 1) * Config::SUBCHUNK_SIZE;

        mc::generate(voxels_ptr, nullptr, CHUNK_SIZE + 1, GRID_Y, CHUNK_SIZE + 1, ISO_SURFACE, Config::GRASS, 
                     ls_vertices, ls_normals, ls_uvs, ls_uvs2, ls_colors, 
                     lt_vertices, lt_normals, lt_uvs, lt_uvs2, lt_colors,
                     cx * CHUNK_SIZE, cz * CHUNK_SIZE, seed_offset, lod, 
                     grass_tint_cache.data(), foliage_tint_cache.data(), local_light_grid.data(),
                     mc_min_y, mc_max_y);
    }
    
    // Remap vertices to global coords
    for (auto& v : ls_vertices) {
        v.x += cx * CHUNK_SIZE;
        v.z += cz * CHUNK_SIZE;
    }
    for (auto& v : lt_vertices) {
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

            auto is_invalid_vicinity = [&]() {
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = 0; dy <= 1; ++dy) {
                        for (int dz = -1; dz <= 1; ++dz) {
                            uint8_t b = get_block(lx + dx, ly + dy, lz + dz);
                            if (b == Config::WOOD || b == Config::BIRCH_WOOD || b == Config::LEAVES || 
                                b == Config::CACTUS || b == Config::STONE || b == Config::COBBLESTONE || 
                                b == Config::PLANKS_CUBE || b == Config::WATER) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            };

            if (is_invalid_vicinity()) {
                continue;
            }

            bool on_grass = (b1 == Config::GRASS || b2 == Config::GRASS);
            bool on_sand  = (b1 == Config::SAND  || b2 == Config::SAND);
            if (!on_grass && !on_sand && b1 != Config::TALL_GRASS && b2 != Config::TALL_GRASS && 
                b1 != Config::RED_MUSHROOM && b2 != Config::RED_MUSHROOM && 
                b1 != Config::BROWN_MUSHROOM && b2 != Config::BROWN_MUSHROOM && 
                b1 != Config::DEAD_BUSH && b2 != Config::DEAD_BUSH) {
                continue;
            }
            
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
                float tw = 1.0f / (float)Config::TILES_ATLAS_COLS;
                float th = 1.0f / (float)Config::TILES_ATLAS_ROWS;
                float sway = bt.is_waving ? 10.0f : 0.0f;
                float u0 = bt.tex_x * tw;
                float v0 = ((float)Config::TILES_ATLAS_ROWS - 1.0f - (float)bt.tex_y) * th;
                float u1 = u0 + tw;
                float v1 = v0 + th;
                
                Vector3 base = center;
                Vector3 up = { 0.0f, 1.0f, 0.0f };
                Vector3 right = { 1.0f, 0.0f, 0.0f };
                Vector3 fwd = { 0.0f, 0.0f, 1.0f };
                
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
                float plx = center.x - (float)(cx * CHUNK_SIZE);
                float ply = center.y + 0.4f;
                float plz = center.z - (float)(cz * CHUNK_SIZE);
                auto plant_light = VoxelLighting::sample_smooth_light(local_light_grid.data(), CHUNK_SIZE + 1, GRID_Y, CHUNK_SIZE + 1, plx, ply, plz, {0, 1, 0});
                Vector3 nor = { 1.0f, plant_light.sunlight, plant_light.blocklight };
                
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
        for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].s_vertices.clear();
                subchunks[s].s_normals.clear();
                subchunks[s].s_uvs.clear();
                subchunks[s].s_uvs2.clear();
                subchunks[s].s_colors.clear();

                subchunks[s].p_vertices.clear();
                subchunks[s].p_normals.clear();
                subchunks[s].p_uvs.clear();
                subchunks[s].p_colors.clear();

                subchunks[s].t_vertices.clear();
                subchunks[s].t_normals.clear();
                subchunks[s].t_uvs.clear();
                subchunks[s].t_uvs2.clear();
                subchunks[s].t_colors.clear();
            }
        }
        light_grid = local_light_grid;

        for (size_t i = 0; i < ls_vertices.size(); i += 3) {
            float avg_y = (ls_vertices[i].y + ls_vertices[i+1].y + ls_vertices[i+2].y) / 3.0f;
            int s = std::clamp((int)std::floor(avg_y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].s_vertices.push_back(ls_vertices[i]);
                subchunks[s].s_vertices.push_back(ls_vertices[i+1]);
                subchunks[s].s_vertices.push_back(ls_vertices[i+2]);

                subchunks[s].s_normals.push_back(ls_normals[i]);
                subchunks[s].s_normals.push_back(ls_normals[i+1]);
                subchunks[s].s_normals.push_back(ls_normals[i+2]);

                subchunks[s].s_uvs.push_back(ls_uvs[i]);
                subchunks[s].s_uvs.push_back(ls_uvs[i+1]);
                subchunks[s].s_uvs.push_back(ls_uvs[i+2]);

                subchunks[s].s_uvs2.push_back(ls_uvs2[i]);
                subchunks[s].s_uvs2.push_back(ls_uvs2[i+1]);
                subchunks[s].s_uvs2.push_back(ls_uvs2[i+2]);

                subchunks[s].s_colors.push_back(ls_colors[i]);
                subchunks[s].s_colors.push_back(ls_colors[i+1]);
                subchunks[s].s_colors.push_back(ls_colors[i+2]);
            }
        }

        for (size_t i = 0; i < lp_vertices.size(); i += 3) {
            float avg_y = (lp_vertices[i].y + lp_vertices[i+1].y + lp_vertices[i+2].y) / 3.0f;
            int s = std::clamp((int)std::floor(avg_y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].p_vertices.push_back(lp_vertices[i]);
                subchunks[s].p_vertices.push_back(lp_vertices[i+1]);
                subchunks[s].p_vertices.push_back(lp_vertices[i+2]);

                subchunks[s].p_normals.push_back(lp_normals[i]);
                subchunks[s].p_normals.push_back(lp_normals[i+1]);
                subchunks[s].p_normals.push_back(lp_normals[i+2]);

                subchunks[s].p_uvs.push_back(lp_uvs[i]);
                subchunks[s].p_uvs.push_back(lp_uvs[i+1]);
                subchunks[s].p_uvs.push_back(lp_uvs[i+2]);

                subchunks[s].p_colors.push_back(lp_colors[i]);
                subchunks[s].p_colors.push_back(lp_colors[i+1]);
                subchunks[s].p_colors.push_back(lp_colors[i+2]);
            }
        }

        for (size_t i = 0; i < lt_vertices.size(); i += 3) {
            float avg_y = (lt_vertices[i].y + lt_vertices[i+1].y + lt_vertices[i+2].y) / 3.0f;
            int s = std::clamp((int)std::floor(avg_y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].t_vertices.push_back(lt_vertices[i]);
                subchunks[s].t_vertices.push_back(lt_vertices[i+1]);
                subchunks[s].t_vertices.push_back(lt_vertices[i+2]);

                subchunks[s].t_normals.push_back(lt_normals[i]);
                subchunks[s].t_normals.push_back(lt_normals[i+1]);
                subchunks[s].t_normals.push_back(lt_normals[i+2]);

                subchunks[s].t_uvs.push_back(lt_uvs[i]);
                subchunks[s].t_uvs.push_back(lt_uvs[i+1]);
                subchunks[s].t_uvs.push_back(lt_uvs[i+2]);

                subchunks[s].t_uvs2.push_back(lt_uvs2[i]);
                subchunks[s].t_uvs2.push_back(lt_uvs2[i+1]);
                subchunks[s].t_uvs2.push_back(lt_uvs2[i+2]);

                subchunks[s].t_colors.push_back(lt_colors[i]);
                subchunks[s].t_colors.push_back(lt_colors[i+1]);
                subchunks[s].t_colors.push_back(lt_colors[i+2]);
            }
        }
    }

    const Config::VoxelData* n_data[4] = {nullptr, nullptr, nullptr, nullptr};
    if (neighbors[1][0]) n_data[0] = neighbors[1][0]->voxels.data();
    if (neighbors[1][2]) n_data[1] = neighbors[1][2]->voxels.data();
    if (neighbors[0][1]) n_data[2] = neighbors[0][1]->voxels.data();
    if (neighbors[2][1]) n_data[3] = neighbors[2][1]->voxels.data();
    build_construction_mesh(voxels_ptr, local_light_grid.data(), sub_mask, n_data[0], n_data[1], n_data[2], n_data[3]);
    build_water_mesh(voxels_ptr, lod, sub_mask);
}

void Chunk::build_construction_mesh(const Config::VoxelData* voxels_ptr, const uint8_t* light_grid, uint8_t sub_mask,
                                     const Config::VoxelData* n_north, const Config::VoxelData* n_south,
                                     const Config::VoxelData* n_west, const Config::VoxelData* n_east) {
    int min_sub = 0;
    while (min_sub < Config::NUM_SUBCHUNKS && !(sub_mask & (1 << min_sub))) min_sub++;
    int max_sub = Config::NUM_SUBCHUNKS - 1;
    while (max_sub >= 0 && !(sub_mask & (1 << max_sub))) max_sub--;
    if (min_sub > max_sub) return;

    int ly_start = min_sub * Config::SUBCHUNK_SIZE;
    int ly_end = (max_sub + 1) * Config::SUBCHUNK_SIZE;

    std::vector<Vector3> lb_vertices, lb_normals;
    std::vector<Vector2> lb_uvs;
    std::vector<Color> lb_colors;

    std::vector<Vector3> lt_vertices, lt_normals;
    std::vector<Vector2> lt_uvs, lt_uvs2;
    std::vector<Color> lt_colors;

    lb_vertices.reserve(512);
    lb_normals.reserve(512);
    lb_uvs.reserve(512);
    lb_colors.reserve(512);
    
    lt_vertices.reserve(512);
    lt_normals.reserve(512);
    lt_uvs.reserve(512);
    lt_uvs2.reserve(512);
    lt_colors.reserve(512);

    float tw = 1.0f / (float)Config::TILES_ATLAS_COLS;
    float th = 1.0f / (float)Config::TILES_ATLAS_ROWS;

    auto get_face_uv = [&](int tx, int ty) -> std::array<Vector2, 4> {
        float u0 = (float)tx * tw;
        float v0 = ((float)Config::TILES_ATLAS_ROWS - 1.0f - (float)ty) * th;
        float u1 = u0 + tw;
        float v1 = v0 + th;
        return { Vector2{u0, v1}, Vector2{u1, v1}, Vector2{u1, v0}, Vector2{u0, v0} };
    };

    auto get_face_sub_uv = [&](int tx, int ty, float u0_16, float v0_16, float u1_16, float v1_16) -> std::array<Vector2, 4> {
        return Config::get_tile_uv(tx, ty, u0_16 / 16.0f, v0_16 / 16.0f, u1_16 / 16.0f, v1_16 / 16.0f);
    };

    auto is_opaque_cube = [&](int x, int y, int z) -> bool {
        if (y < 0 || y >= Config::GRID_Y) return false;
        if (x >= 0 && x <= Config::CHUNK_SIZE && z >= 0 && z <= Config::CHUNK_SIZE) {
            int idx = get_idx(x, y, z);
            if (voxels_ptr[idx].density >= Config::ISO_SURFACE) return true;
            uint8_t b = voxels_ptr[idx].block;
            if (b == Config::AIR || b == Config::WATER) return false;
            if (Config::BLOCKS.find(b) == Config::BLOCKS.end()) return false;
            const auto& nb_t = Config::BLOCKS.at(b);
            return (nb_t.shape == Config::SHAPE_CUBE && !nb_t.transparent);
        }
        // Cross-chunk lookup
        const Config::VoxelData* n_ptr = nullptr;
        int lx = x, lz = z;
        if (x < 0) { n_ptr = n_west; lx = x + Config::CHUNK_SIZE; }
        else if (x > Config::CHUNK_SIZE) { n_ptr = n_east; lx = x - Config::CHUNK_SIZE; }
        if (z < 0) { n_ptr = n_north; lz = z + Config::CHUNK_SIZE; }
        else if (z > Config::CHUNK_SIZE) { n_ptr = n_south; lz = z - Config::CHUNK_SIZE; }
        if (!n_ptr) return false;
        int idx = y * (Config::CHUNK_SIZE + 1) * (Config::CHUNK_SIZE + 1) + lz * (Config::CHUNK_SIZE + 1) + lx;
        if (n_ptr[idx].density >= Config::ISO_SURFACE) return true;
        uint8_t b = n_ptr[idx].block;
        if (b == Config::AIR || b == Config::WATER) return false;
        if (Config::BLOCKS.find(b) == Config::BLOCKS.end()) return false;
        const auto& nb_t = Config::BLOCKS.at(b);
        return (nb_t.shape == Config::SHAPE_CUBE && !nb_t.transparent);
    };

    auto is_same_block = [&](int x, int y, int z, uint8_t self_b) -> bool {
        if (y < 0 || y >= Config::GRID_Y) return false;
        if (x >= 0 && x <= Config::CHUNK_SIZE && z >= 0 && z <= Config::CHUNK_SIZE) {
            int idx = get_idx(x, y, z);
            return (voxels_ptr[idx].block == self_b);
        }
        const Config::VoxelData* n_ptr = nullptr;
        int lx = x, lz = z;
        if (x < 0) { n_ptr = n_west; lx = x + Config::CHUNK_SIZE; }
        else if (x > Config::CHUNK_SIZE) { n_ptr = n_east; lx = x - Config::CHUNK_SIZE; }
        if (z < 0) { n_ptr = n_north; lz = z + Config::CHUNK_SIZE; }
        else if (z > Config::CHUNK_SIZE) { n_ptr = n_south; lz = z - Config::CHUNK_SIZE; }
        if (!n_ptr) return false;
        int idx = y * (Config::CHUNK_SIZE + 1) * (Config::CHUNK_SIZE + 1) + lz * (Config::CHUNK_SIZE + 1) + lx;
        return (n_ptr[idx].block == self_b);
    };

    auto is_solid_block = [&](int x, int y, int z) -> bool {
        if (y < 0 || y >= Config::GRID_Y) return false;
        if (x >= 0 && x <= Config::CHUNK_SIZE && z >= 0 && z <= Config::CHUNK_SIZE) {
            int idx = get_idx(x, y, z);
            uint8_t b = voxels_ptr[idx].block;
            return (b != Config::AIR && b != Config::WATER);
        }
        const Config::VoxelData* n_ptr = nullptr;
        int lx = x, lz = z;
        if (x < 0) { n_ptr = n_west; lx = x + Config::CHUNK_SIZE; }
        else if (x > Config::CHUNK_SIZE) { n_ptr = n_east; lx = x - Config::CHUNK_SIZE; }
        if (z < 0) { n_ptr = n_north; lz = z + Config::CHUNK_SIZE; }
        else if (z > Config::CHUNK_SIZE) { n_ptr = n_south; lz = z - Config::CHUNK_SIZE; }
        if (!n_ptr) return false;
        int idx = y * (Config::CHUNK_SIZE + 1) * (Config::CHUNK_SIZE + 1) + lz * (Config::CHUNK_SIZE + 1) + lx;
        uint8_t b = n_ptr[idx].block;
        return (b != Config::AIR && b != Config::WATER);
    };

    auto calc_ao = [&](bool s1, bool s2, bool c) -> float {
        int level = (s1 && s2) ? 0 : 3 - (s1 + s2 + c);
        return 0.78f + 0.0733f * (float)level;
    };

    auto sample_face_light = [&](int lx, int ly, int lz, int nx, int ny, int nz) -> VoxelLighting::LightSample {
        auto self_ls = VoxelLighting::sample_block_face_light(light_grid, CHUNK_SIZE + 1, GRID_Y, CHUNK_SIZE + 1, lx, ly, lz);
        if (is_opaque_cube(nx, ny, nz)) {
            return self_ls;
        }
        auto n_ls = VoxelLighting::sample_block_face_light(light_grid, CHUNK_SIZE + 1, GRID_Y, CHUNK_SIZE + 1, nx, ny, nz);
        return { std::max(self_ls.sunlight, n_ls.sunlight), std::max(self_ls.blocklight, n_ls.blocklight) };
    };

    auto add_quad = [&](Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3, float ao0, float ao1, float ao2, float ao3, float sun, float block, int tx, int ty, bool is_trans = false) {
        auto uvs = get_face_uv(tx, ty);
        Color white = { 255, 255, 255, 255 };
        
        auto& v_vec = is_trans ? lt_vertices : lb_vertices;
        auto& n_vec = is_trans ? lt_normals : lb_normals;
        auto& u_vec = is_trans ? lt_uvs : lb_uvs;
        auto& c_vec = is_trans ? lt_colors : lb_colors;

        // Triangle 1: v0, v1, v2
        v_vec.push_back(v0); v_vec.push_back(v1); v_vec.push_back(v2);
        n_vec.push_back({ ao0, sun, block }); n_vec.push_back({ ao1, sun, block }); n_vec.push_back({ ao2, sun, block });
        u_vec.push_back(uvs[0]); u_vec.push_back(uvs[1]); u_vec.push_back(uvs[2]);
        c_vec.push_back(white); c_vec.push_back(white); c_vec.push_back(white);

        // Triangle 2: v0, v2, v3
        v_vec.push_back(v0); v_vec.push_back(v2); v_vec.push_back(v3);
        n_vec.push_back({ ao0, sun, block }); n_vec.push_back({ ao2, sun, block }); n_vec.push_back({ ao3, sun, block });
        u_vec.push_back(uvs[0]); u_vec.push_back(uvs[2]); u_vec.push_back(uvs[3]);
        c_vec.push_back(white); c_vec.push_back(white); c_vec.push_back(white);
    };

    auto add_quad_sub = [&](Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3, float ao0, float ao1, float ao2, float ao3, float sun, float block, int tx, int ty, float u0_16, float v0_16, float u1_16, float v1_16, bool is_trans = false) {
        auto uvs = get_face_sub_uv(tx, ty, u0_16, v0_16, u1_16, v1_16);
        Color white = { 255, 255, 255, 255 };
        
        auto& v_vec = is_trans ? lt_vertices : lb_vertices;
        auto& n_vec = is_trans ? lt_normals : lb_normals;
        auto& u_vec = is_trans ? lt_uvs : lb_uvs;
        auto& c_vec = is_trans ? lt_colors : lb_colors;

        // Triangle 1: v0, v1, v2
        v_vec.push_back(v0); v_vec.push_back(v1); v_vec.push_back(v2);
        n_vec.push_back({ ao0, sun, block }); n_vec.push_back({ ao1, sun, block }); n_vec.push_back({ ao2, sun, block });
        u_vec.push_back(uvs[0]); u_vec.push_back(uvs[1]); u_vec.push_back(uvs[2]);
        c_vec.push_back(white); c_vec.push_back(white); c_vec.push_back(white);

        // Triangle 2: v0, v2, v3
        v_vec.push_back(v0); v_vec.push_back(v2); v_vec.push_back(v3);
        n_vec.push_back({ ao0, sun, block }); n_vec.push_back({ ao2, sun, block }); n_vec.push_back({ ao3, sun, block });
        u_vec.push_back(uvs[0]); u_vec.push_back(uvs[2]); u_vec.push_back(uvs[3]);
        c_vec.push_back(white); c_vec.push_back(white); c_vec.push_back(white);
    };

    auto add_box = [&](int lx, int ly, int lz,
                       float x0, float y0, float z0, float x1, float y1, float z1,
                       int top_tx, int top_ty,
                       int bot_tx, int bot_ty,
                       int front_tx, int front_ty,
                       int back_tx, int back_ty,
                       int left_tx, int left_ty,
                       int right_tx, int right_ty,
                       bool cull_top = false, bool cull_bot = false,
                       bool cull_front = false, bool cull_back = false,
                       bool cull_left = false, bool cull_right = false,
                       bool is_custom_shape = false, bool is_trans = false) {
        if (!cull_top) {
            float ao0 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly+1, lz), is_solid_block(lx, ly+1, lz+1), is_solid_block(lx-1, ly+1, lz+1));
            float ao1 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly+1, lz), is_solid_block(lx, ly+1, lz+1), is_solid_block(lx+1, ly+1, lz+1));
            float ao2 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly+1, lz), is_solid_block(lx, ly+1, lz-1), is_solid_block(lx+1, ly+1, lz-1));
            float ao3 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly+1, lz), is_solid_block(lx, ly+1, lz-1), is_solid_block(lx-1, ly+1, lz-1));
            auto ls = sample_face_light(lx, ly, lz, lx, ly+1, lz);
            add_quad({x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, ao0, ao1, ao2, ao3, ls.sunlight, ls.blocklight, top_tx, top_ty, is_trans);
        }
        if (!cull_bot) {
            float ao0 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly-1, lz), is_solid_block(lx, ly-1, lz-1), is_solid_block(lx-1, ly-1, lz-1));
            float ao1 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly-1, lz), is_solid_block(lx, ly-1, lz-1), is_solid_block(lx+1, ly-1, lz-1));
            float ao2 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly-1, lz), is_solid_block(lx, ly-1, lz+1), is_solid_block(lx+1, ly-1, lz+1));
            float ao3 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly-1, lz), is_solid_block(lx, ly-1, lz+1), is_solid_block(lx-1, ly-1, lz+1));
            auto ls = sample_face_light(lx, ly, lz, lx, ly-1, lz);
            add_quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, ao0, ao1, ao2, ao3, ls.sunlight, ls.blocklight, bot_tx, bot_ty, is_trans);
        }
        if (!cull_front) {
            float ao0 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly, lz+1), is_solid_block(lx, ly-1, lz+1), is_solid_block(lx-1, ly-1, lz+1));
            float ao1 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly, lz+1), is_solid_block(lx, ly-1, lz+1), is_solid_block(lx+1, ly-1, lz+1));
            float ao2 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly, lz+1), is_solid_block(lx, ly+1, lz+1), is_solid_block(lx+1, ly+1, lz+1));
            float ao3 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly, lz+1), is_solid_block(lx, ly+1, lz+1), is_solid_block(lx-1, ly+1, lz+1));
            auto ls = sample_face_light(lx, ly, lz, lx, ly, lz+1);
            add_quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, ao0, ao1, ao2, ao3, ls.sunlight, ls.blocklight, front_tx, front_ty, is_trans);
        }
        if (!cull_back) {
            float ao0 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly, lz-1), is_solid_block(lx, ly-1, lz-1), is_solid_block(lx+1, ly-1, lz-1));
            float ao1 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly, lz-1), is_solid_block(lx, ly-1, lz-1), is_solid_block(lx-1, ly-1, lz-1));
            float ao2 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly, lz-1), is_solid_block(lx, ly+1, lz-1), is_solid_block(lx-1, ly+1, lz-1));
            float ao3 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly, lz-1), is_solid_block(lx, ly+1, lz-1), is_solid_block(lx+1, ly+1, lz-1));
            auto ls = sample_face_light(lx, ly, lz, lx, ly, lz-1);
            add_quad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, ao0, ao1, ao2, ao3, ls.sunlight, ls.blocklight, back_tx, back_ty, is_trans);
        }
        if (!cull_right) {
            float ao0 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly-1, lz), is_solid_block(lx+1, ly, lz+1), is_solid_block(lx+1, ly-1, lz+1));
            float ao1 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly-1, lz), is_solid_block(lx+1, ly, lz-1), is_solid_block(lx+1, ly-1, lz-1));
            float ao2 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly+1, lz), is_solid_block(lx+1, ly, lz-1), is_solid_block(lx+1, ly+1, lz-1));
            float ao3 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx+1, ly+1, lz), is_solid_block(lx+1, ly, lz+1), is_solid_block(lx+1, ly+1, lz+1));
            auto ls = sample_face_light(lx, ly, lz, lx+1, ly, lz);
            add_quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, ao0, ao1, ao2, ao3, ls.sunlight, ls.blocklight, right_tx, right_ty, is_trans);
        }
        if (!cull_left) {
            float ao0 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly-1, lz), is_solid_block(lx-1, ly, lz-1), is_solid_block(lx-1, ly-1, lz-1));
            float ao1 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly-1, lz), is_solid_block(lx-1, ly, lz+1), is_solid_block(lx-1, ly-1, lz+1));
            float ao2 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly+1, lz), is_solid_block(lx-1, ly, lz+1), is_solid_block(lx-1, ly+1, lz+1));
            float ao3 = is_custom_shape ? 1.0f : calc_ao(is_solid_block(lx-1, ly+1, lz), is_solid_block(lx-1, ly, lz-1), is_solid_block(lx-1, ly+1, lz-1));
            auto ls = sample_face_light(lx, ly, lz, lx-1, ly, lz);
            add_quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, ao0, ao1, ao2, ao3, ls.sunlight, ls.blocklight, left_tx, left_ty, is_trans);
        }
    };

    for (int lz = 0; lz < Config::CHUNK_SIZE; ++lz) {
        for (int ly = ly_start; ly < ly_end; ++ly) {
            for (int lx = 0; lx < Config::CHUNK_SIZE; ++lx) {
                int idx = get_idx(lx, ly, lz);
                uint8_t b = voxels_ptr[idx].block;
                if (b == Config::AIR || b == Config::WATER) continue;
                if (Config::BLOCKS.find(b) == Config::BLOCKS.end()) continue;
                const auto& bt = Config::BLOCKS.at(b);
                if (bt.shape == Config::SHAPE_TERRAIN) continue;

                float gx = (float)(cx * Config::CHUNK_SIZE + lx) - 0.5f;
                float gy = (float)ly - 0.5f;
                float gz = (float)(cz * Config::CHUNK_SIZE + lz) - 0.5f;

                int def_tx = bt.tex_x, def_ty = bt.tex_y;
                uint8_t rot = voxels_ptr[idx].rotation;

                if (!bt.elements.empty()) {
                    // Modelos 3D por JSON con rotacion automatica
                    uint8_t elem_rot = rot & 3;

                    // Tabla de remapeo de caras: face_remap[rot][element_face] = world_face
                    static const int face_remap[4][6] = {
                        {0, 1, 2, 3, 4, 5},  // rot=0: sin cambio
                        {0, 1, 5, 4, 2, 3},  // rot=1: 90 CW - N→E, S→E->W, W→N, E→S
                        {0, 1, 3, 2, 5, 4},  // rot=2: 180° - N↔S, W↔E
                        {0, 1, 4, 5, 3, 2}   // rot=3: 270 CW - N→W, S→E, W→S, E→N
                    };

                    for (const auto& elem : bt.elements) {
                        // Rotar coordenadas from/to alrededor del eje Y (centro = 8 en espacio 0..16)
                        float fx0 = elem.from.x, fz0 = elem.from.z;
                        float fx1 = elem.to.x,   fz1 = elem.to.z;
                        switch (elem_rot) {
                            case 1: { float t = fx0; fx0 = 16.0f - fz0; fz0 = t; t = fx1; fx1 = 16.0f - fz1; fz1 = t; break; }
                            case 2: fx0 = 16.0f - fx0; fz0 = 16.0f - fz0; fx1 = 16.0f - fx1; fz1 = 16.0f - fz1; break;
                            case 3: { float t = fx0; fx0 = fz0; fz0 = 16.0f - t; t = fx1; fx1 = fz1; fz1 = 16.0f - t; break; }
                            default: break;
                        }
                        // Calcular bounding box rotado (Y no rota)
                        float rx0 = fx0 < fx1 ? fx0 : fx1, rx1 = fx0 < fx1 ? fx1 : fx0;
                        float ry0 = elem.from.y, ry1 = elem.to.y;
                        float rz0 = fz0 < fz1 ? fz0 : fz1, rz1 = fz0 < fz1 ? fz1 : fz0;
                        // Coordenadas world-space
                        float ex0 = gx + rx0 / 16.0f, ey0 = gy + ry0 / 16.0f, ez0 = gz + rz0 / 16.0f;
                        float ex1 = gx + rx1 / 16.0f, ey1 = gy + ry1 / 16.0f, ez1 = gz + rz1 / 16.0f;

                        // Renderizar cada cara del elemento con remapeo de rotacion
                        for (int ef = 0; ef < 6; ef++) {
                            if (!elem.faces[ef].enabled) continue;
                            const auto& f = elem.faces[ef];
                            int wf = face_remap[elem_rot][ef]; // Direccion world de esta cara

                            // Culling: si cullface esta definido, verificar vecino en la direccion world
                            bool cull = false;
                            if (!f.cullface.empty()) {
                                if (bt.transparent) {
                                    switch (wf) {
                                        case 1: cull = is_same_block(lx, ly + 1, lz, b); break;
                                        case 0: cull = is_same_block(lx, ly - 1, lz, b); break;
                                        case 2: cull = is_same_block(lx, ly, lz - 1, b); break;
                                        case 3: cull = is_same_block(lx, ly, lz + 1, b); break;
                                        case 4: cull = is_same_block(lx - 1, ly, lz, b); break;
                                        case 5: cull = is_same_block(lx + 1, ly, lz, b); break;
                                    }
                                } else {
                                    switch (wf) {
                                        case 1: cull = is_opaque_cube(lx, ly + 1, lz); break;
                                        case 0: cull = is_opaque_cube(lx, ly - 1, lz); break;
                                        case 2: cull = is_opaque_cube(lx, ly, lz - 1); break;
                                        case 3: cull = is_opaque_cube(lx, ly, lz + 1); break;
                                        case 4: cull = is_opaque_cube(lx - 1, ly, lz); break;
                                        case 5: cull = is_opaque_cube(lx + 1, ly, lz); break;
                                    }
                                }
                            }
                            if (cull) continue;

                            // Muestreo de luz segun la direccion world de la cara
                            int nx = lx, ny = ly, nz = lz;
                            switch (wf) {
                                case 1: ny++; break;
                                case 0: ny--; break;
                                case 2: nz--; break;
                                case 3: nz++; break;
                                case 4: nx--; break;
                                case 5: nx++; break;
                            }
                            auto ls = sample_face_light(lx, ly, lz, nx, ny, nz);

                            auto add_face_quad = [&](Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3) {
                                add_quad_sub(v0, v1, v2, v3, 1.0f, 1.0f, 1.0f, 1.0f, ls.sunlight, ls.blocklight,
                                             f.tex_x, f.tex_y, f.uv[0], f.uv[1], f.uv[2], f.uv[3], bt.transparent);
                            };

                            // Renderizar cara segun su direccion world
                            switch (wf) {
                                case 1: // UP (+Y)
                                    add_face_quad({ex0, ey1, ez1}, {ex1, ey1, ez1}, {ex1, ey1, ez0}, {ex0, ey1, ez0});
                                    break;
                                case 0: // DOWN (-Y)
                                    add_face_quad({ex0, ey0, ez0}, {ex1, ey0, ez0}, {ex1, ey0, ez1}, {ex0, ey0, ez1});
                                    break;
                                case 2: // NORTH (-Z)
                                    add_face_quad({ex1, ey0, ez0}, {ex0, ey0, ez0}, {ex0, ey1, ez0}, {ex1, ey1, ez0});
                                    break;
                                case 3: // SOUTH (+Z)
                                    add_face_quad({ex0, ey0, ez1}, {ex1, ey0, ez1}, {ex1, ey1, ez1}, {ex0, ey1, ez1});
                                    break;
                                case 4: // WEST (-X)
                                    add_face_quad({ex0, ey0, ez0}, {ex0, ey0, ez1}, {ex0, ey1, ez1}, {ex0, ey1, ez0});
                                    break;
                                case 5: // EAST (+X)
                                    add_face_quad({ex1, ey0, ez1}, {ex1, ey0, ez0}, {ex1, ey1, ez0}, {ex1, ey1, ez1});
                                    break;
                            }
                        }
                    }
                } else {
                    switch (bt.shape) {
                    case Config::SHAPE_DOOR: {
                        int facing = rot & 3;
                        bool is_open = (rot & 4) != 0;
                        float T = 0.1875f; // Door thickness (3/16 block)
                        
                        float x0 = gx, x1 = gx + 1.0f;
                        float z0 = gz, z1 = gz + 1.0f;
                        
                        if (facing == 0) { // Facing North (-Z)
                            if (!is_open) {
                                z0 = gz;
                                z1 = gz + T;
                            } else {
                                x0 = gx;
                                x1 = gx + T;
                            }
                        } else if (facing == 1) { // Facing East (+X)
                            if (!is_open) {
                                x0 = gx + 1.0f - T;
                                x1 = gx + 1.0f;
                            } else {
                                z0 = gz;
                                z1 = gz + T;
                            }
                        } else if (facing == 2) { // Facing South (+Z)
                            if (!is_open) {
                                z0 = gz + 1.0f - T;
                                z1 = gz + 1.0f;
                            } else {
                                x0 = gx + 1.0f - T;
                                x1 = gx + 1.0f;
                            }
                        } else { // Facing West (-X)
                            if (!is_open) {
                                x0 = gx;
                                x1 = gx + T;
                            } else {
                                z0 = gz + 1.0f - T;
                                z1 = gz + 1.0f;
                            }
                        }
                        
                        add_box(lx, ly, lz,
                                x0, gy, z0, x1, gy + 1.0f, z1,
                                def_tx, def_ty, def_tx, def_ty,
                                def_tx, def_ty, def_tx, def_ty,
                                def_tx, def_ty, def_tx, def_ty,
                                false, false, false, false, false, false, true, bt.transparent);
                        break;
                    }
                    case Config::SHAPE_FENCE: {
                        // Poste central
                        add_box(lx, ly, lz,
                                gx + 0.375f, gy, gz + 0.375f, gx + 0.625f, gy + 1.0f, gz + 0.625f,
                                def_tx, def_ty, def_tx, def_ty,
                                def_tx, def_ty, def_tx, def_ty,
                                def_tx, def_ty, def_tx, def_ty,
                                false, false, false, false, false, false, true);
                        
                        auto is_fence_or_solid = [&](int x, int y, int z) {
                            if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return false;
                            uint8_t nb_b = voxels_ptr[get_idx(x, y, z)].block;
                            if (nb_b == Config::AIR || nb_b == Config::WATER) return false;
                            if (Config::BLOCKS.find(nb_b) == Config::BLOCKS.end()) return false;
                            const auto& st = Config::BLOCKS.at(nb_b);
                            return st.shape == Config::SHAPE_FENCE || st.shape == Config::SHAPE_CUBE;
                        };

                        if (is_fence_or_solid(lx + 1, ly, lz)) {
                            add_box(lx, ly, lz, gx + 0.625f, gy + 0.2f, gz + 0.44f, gx + 1.0f, gy + 0.38f, gz + 0.56f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                            add_box(lx, ly, lz, gx + 0.625f, gy + 0.65f, gz + 0.44f, gx + 1.0f, gy + 0.83f, gz + 0.56f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                        }
                        if (is_fence_or_solid(lx - 1, ly, lz)) {
                            add_box(lx, ly, lz, gx, gy + 0.2f, gz + 0.44f, gx + 0.375f, gy + 0.38f, gz + 0.56f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                            add_box(lx, ly, lz, gx, gy + 0.65f, gz + 0.44f, gx + 0.375f, gy + 0.83f, gz + 0.56f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                        }
                        if (is_fence_or_solid(lx, ly, lz + 1)) {
                            add_box(lx, ly, lz, gx + 0.44f, gy + 0.2f, gz + 0.625f, gx + 0.56f, gy + 0.38f, gz + 1.0f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                            add_box(lx, ly, lz, gx + 0.44f, gy + 0.65f, gz + 0.625f, gx + 0.56f, gy + 0.83f, gz + 1.0f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                        }
                        if (is_fence_or_solid(lx, ly, lz - 1)) {
                            add_box(lx, ly, lz, gx + 0.44f, gy + 0.2f, gz, gx + 0.56f, gy + 0.38f, gz + 0.375f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                            add_box(lx, ly, lz, gx + 0.44f, gy + 0.65f, gz, gx + 0.56f, gy + 0.83f, gz + 0.375f, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, def_tx, def_ty, false, false, false, false, false, false, true);
                        }
                        break;
                    }
                    default:
                        break;
                }
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(mesh_mutex);
        for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].b_vertices.clear();
                subchunks[s].b_normals.clear();
                subchunks[s].b_uvs.clear();
                subchunks[s].b_colors.clear();
            }
        }

        for (size_t i = 0; i < lb_vertices.size(); i += 3) {
            float avg_y = (lb_vertices[i].y + lb_vertices[i+1].y + lb_vertices[i+2].y) / 3.0f;
            int s = std::clamp((int)std::floor(avg_y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].b_vertices.push_back(lb_vertices[i]);
                subchunks[s].b_vertices.push_back(lb_vertices[i+1]);
                subchunks[s].b_vertices.push_back(lb_vertices[i+2]);

                subchunks[s].b_normals.push_back(lb_normals[i]);
                subchunks[s].b_normals.push_back(lb_normals[i+1]);
                subchunks[s].b_normals.push_back(lb_normals[i+2]);

                subchunks[s].b_uvs.push_back(lb_uvs[i]);
                subchunks[s].b_uvs.push_back(lb_uvs[i+1]);
                subchunks[s].b_uvs.push_back(lb_uvs[i+2]);

                subchunks[s].b_colors.push_back(lb_colors[i]);
                subchunks[s].b_colors.push_back(lb_colors[i+1]);
                subchunks[s].b_colors.push_back(lb_colors[i+2]);
            }
        }

        for (size_t i = 0; i < lt_vertices.size(); i += 3) {
            float avg_y = (lt_vertices[i].y + lt_vertices[i+1].y + lt_vertices[i+2].y) / 3.0f;
            int s = std::clamp((int)std::floor(avg_y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].t_vertices.push_back(lt_vertices[i]);
                subchunks[s].t_vertices.push_back(lt_vertices[i+1]);
                subchunks[s].t_vertices.push_back(lt_vertices[i+2]);

                subchunks[s].t_normals.push_back(lt_normals[i]);
                subchunks[s].t_normals.push_back(lt_normals[i+1]);
                subchunks[s].t_normals.push_back(lt_normals[i+2]);

                subchunks[s].t_uvs.push_back(lt_uvs[i]);
                subchunks[s].t_uvs.push_back(lt_uvs[i+1]);
                subchunks[s].t_uvs.push_back(lt_uvs[i+2]);

                subchunks[s].t_uvs2.push_back({0,0});
                subchunks[s].t_uvs2.push_back({0,0});
                subchunks[s].t_uvs2.push_back({0,0});

                subchunks[s].t_colors.push_back(lt_colors[i]);
                subchunks[s].t_colors.push_back(lt_colors[i+1]);
                subchunks[s].t_colors.push_back(lt_colors[i+2]);
            }
        }
    }
}

void Chunk::build_water_mesh(const Config::VoxelData* voxels_ptr, int lod, uint8_t sub_mask) {
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

    std::vector<Vector3> dummy_t_verts, dummy_t_norms;
    std::vector<Vector2> dummy_t_uvs, dummy_t_uvs2;
    std::vector<Color> dummy_t_cols;
    mc::generate(voxels_ptr, water_density.data(), wx, GRID_Y, wz, ISO_SURFACE, Config::WATER, 
                 lw_vertices, lw_normals, lw_uvs, lw_uvs2, lw_colors, 
                 dummy_t_verts, dummy_t_norms, dummy_t_uvs, dummy_t_uvs2, dummy_t_cols,
                 0.0f, 0.0f, 0, lod);

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
        for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].w_vertices.clear();
                subchunks[s].w_normals.clear();
                subchunks[s].w_uvs.clear();
                subchunks[s].w_uvs2.clear();
                subchunks[s].w_colors.clear();
            }
        }

        for (size_t i = 0; i < fw_vertices.size(); i += 3) {
            float avg_y = (fw_vertices[i].y + fw_vertices[i+1].y + fw_vertices[i+2].y) / 3.0f;
            int s = std::clamp((int)std::floor(avg_y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
            if ((sub_mask & (1 << s)) != 0) {
                subchunks[s].w_vertices.push_back(fw_vertices[i]);
                subchunks[s].w_vertices.push_back(fw_vertices[i+1]);
                subchunks[s].w_vertices.push_back(fw_vertices[i+2]);

                subchunks[s].w_normals.push_back(fw_normals[i]);
                subchunks[s].w_normals.push_back(fw_normals[i+1]);
                subchunks[s].w_normals.push_back(fw_normals[i+2]);

                subchunks[s].w_uvs.push_back(fw_uvs[i]);
                subchunks[s].w_uvs.push_back(fw_uvs[i+1]);
                subchunks[s].w_uvs.push_back(fw_uvs[i+2]);

                subchunks[s].w_uvs2.push_back(fw_uvs2[i]);
                subchunks[s].w_uvs2.push_back(fw_uvs2[i+1]);
                subchunks[s].w_uvs2.push_back(fw_uvs2[i+2]);

                subchunks[s].w_colors.push_back(fw_colors[i]);
                subchunks[s].w_colors.push_back(fw_colors[i+1]);
                subchunks[s].w_colors.push_back(fw_colors[i+2]);
            }
        }
    }
}

void Chunk::pack_meshes(int mask, uint8_t sub_mask) {
    std::lock_guard<std::mutex> lock(mesh_mutex);

    auto free_arrays = [](Mesh& m) {
        if (m.vertices) { MemFree(m.vertices); m.vertices = nullptr; }
        if (m.normals) { MemFree(m.normals); m.normals = nullptr; }
        if (m.texcoords) { MemFree(m.texcoords); m.texcoords = nullptr; }
        if (m.texcoords2) { MemFree(m.texcoords2); m.texcoords2 = nullptr; }
        if (m.colors) { MemFree(m.colors); m.colors = nullptr; }
    };

    for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
        if ((sub_mask & (1 << s)) == 0) continue;
        auto& sc = subchunks[s];

        if ((mask & 2) != 0) {
            free_arrays(sc.next_solid_mesh);
            sc.next_solid_mesh = {0};
            
            if (!sc.s_vertices.empty()) {
                sc.next_solid_mesh.vertexCount = sc.s_vertices.size();
                sc.next_solid_mesh.triangleCount = sc.s_vertices.size() / 3;

                sc.next_solid_mesh.vertices = (float*)MemAlloc(sc.s_vertices.size() * 3 * sizeof(float));
                sc.next_solid_mesh.normals = (float*)MemAlloc(sc.s_normals.size() * 3 * sizeof(float));
                sc.next_solid_mesh.texcoords = (float*)MemAlloc(sc.s_uvs.size() * 2 * sizeof(float));
                sc.next_solid_mesh.texcoords2 = (float*)MemAlloc(sc.s_uvs2.size() * 2 * sizeof(float));
                sc.next_solid_mesh.colors = (unsigned char*)MemAlloc(sc.s_colors.size() * 4 * sizeof(unsigned char));

                for (size_t i = 0; i < sc.s_vertices.size(); i++) {
                    sc.next_solid_mesh.vertices[i*3] = sc.s_vertices[i].x;
                    sc.next_solid_mesh.vertices[i*3+1] = sc.s_vertices[i].y;
                    sc.next_solid_mesh.vertices[i*3+2] = sc.s_vertices[i].z;

                    sc.next_solid_mesh.normals[i*3] = sc.s_normals[i].x;
                    sc.next_solid_mesh.normals[i*3+1] = sc.s_normals[i].y;
                    sc.next_solid_mesh.normals[i*3+2] = sc.s_normals[i].z;

                    sc.next_solid_mesh.texcoords[i*2] = sc.s_uvs[i].x;
                    sc.next_solid_mesh.texcoords[i*2+1] = sc.s_uvs[i].y;

                    sc.next_solid_mesh.texcoords2[i*2] = sc.s_uvs2[i].x;
                    sc.next_solid_mesh.texcoords2[i*2+1] = sc.s_uvs2[i].y;

                    sc.next_solid_mesh.colors[i*4] = sc.s_colors[i].r;
                    sc.next_solid_mesh.colors[i*4+1] = sc.s_colors[i].g;
                    sc.next_solid_mesh.colors[i*4+2] = sc.s_colors[i].b;
                    sc.next_solid_mesh.colors[i*4+3] = sc.s_colors[i].a;
                }

                std::vector<Vector3>().swap(sc.s_vertices);
                std::vector<Vector3>().swap(sc.s_normals);
                std::vector<Vector2>().swap(sc.s_uvs);
                std::vector<Vector2>().swap(sc.s_uvs2);
                std::vector<Color>().swap(sc.s_colors);
            }

            free_arrays(sc.next_build_mesh);
            sc.next_build_mesh = {0};

            if (!sc.b_vertices.empty()) {
                sc.next_build_mesh.vertexCount = sc.b_vertices.size();
                sc.next_build_mesh.triangleCount = sc.b_vertices.size() / 3;

                sc.next_build_mesh.vertices = (float*)MemAlloc(sc.b_vertices.size() * 3 * sizeof(float));
                sc.next_build_mesh.normals = (float*)MemAlloc(sc.b_normals.size() * 3 * sizeof(float));
                sc.next_build_mesh.texcoords = (float*)MemAlloc(sc.b_uvs.size() * 2 * sizeof(float));
                sc.next_build_mesh.colors = (unsigned char*)MemAlloc(sc.b_colors.size() * 4 * sizeof(unsigned char));

                for (size_t i = 0; i < sc.b_vertices.size(); i++) {
                    sc.next_build_mesh.vertices[i*3] = sc.b_vertices[i].x;
                    sc.next_build_mesh.vertices[i*3+1] = sc.b_vertices[i].y;
                    sc.next_build_mesh.vertices[i*3+2] = sc.b_vertices[i].z;

                    sc.next_build_mesh.normals[i*3] = sc.b_normals[i].x;
                    sc.next_build_mesh.normals[i*3+1] = sc.b_normals[i].y;
                    sc.next_build_mesh.normals[i*3+2] = sc.b_normals[i].z;

                    sc.next_build_mesh.texcoords[i*2] = sc.b_uvs[i].x;
                    sc.next_build_mesh.texcoords[i*2+1] = sc.b_uvs[i].y;

                    sc.next_build_mesh.colors[i*4] = sc.b_colors[i].r;
                    sc.next_build_mesh.colors[i*4+1] = sc.b_colors[i].g;
                    sc.next_build_mesh.colors[i*4+2] = sc.b_colors[i].b;
                    sc.next_build_mesh.colors[i*4+3] = sc.b_colors[i].a;
                }

                std::vector<Vector3>().swap(sc.b_vertices);
                std::vector<Vector3>().swap(sc.b_normals);
                std::vector<Vector2>().swap(sc.b_uvs);
                std::vector<Color>().swap(sc.b_colors);
            }

            free_arrays(sc.next_plants_mesh);
            sc.next_plants_mesh = {0};

            if (!sc.p_vertices.empty()) {
                sc.next_plants_mesh.vertexCount = sc.p_vertices.size();
                sc.next_plants_mesh.triangleCount = sc.p_vertices.size() / 3;

                sc.next_plants_mesh.vertices = (float*)MemAlloc(sc.p_vertices.size() * 3 * sizeof(float));
                sc.next_plants_mesh.normals = (float*)MemAlloc(sc.p_normals.size() * 3 * sizeof(float));
                sc.next_plants_mesh.texcoords = (float*)MemAlloc(sc.p_uvs.size() * 2 * sizeof(float));
                sc.next_plants_mesh.colors = (unsigned char*)MemAlloc(sc.p_colors.size() * 4 * sizeof(unsigned char));

                for (size_t i = 0; i < sc.p_vertices.size(); i++) {
                    sc.next_plants_mesh.vertices[i*3] = sc.p_vertices[i].x;
                    sc.next_plants_mesh.vertices[i*3+1] = sc.p_vertices[i].y;
                    sc.next_plants_mesh.vertices[i*3+2] = sc.p_vertices[i].z;

                    sc.next_plants_mesh.normals[i*3] = sc.p_normals[i].x;
                    sc.next_plants_mesh.normals[i*3+1] = sc.p_normals[i].y;
                    sc.next_plants_mesh.normals[i*3+2] = sc.p_normals[i].z;

                    sc.next_plants_mesh.texcoords[i*2] = sc.p_uvs[i].x;
                    sc.next_plants_mesh.texcoords[i*2+1] = sc.p_uvs[i].y;

                    sc.next_plants_mesh.colors[i*4] = sc.p_colors[i].r;
                    sc.next_plants_mesh.colors[i*4+1] = sc.p_colors[i].g;
                    sc.next_plants_mesh.colors[i*4+2] = sc.p_colors[i].b;
                    sc.next_plants_mesh.colors[i*4+3] = sc.p_colors[i].a;
                }

                std::vector<Vector3>().swap(sc.p_vertices);
                std::vector<Vector3>().swap(sc.p_normals);
                std::vector<Vector2>().swap(sc.p_uvs);
                std::vector<Color>().swap(sc.p_colors);
            }
            
            free_arrays(sc.next_trans_mesh);
            sc.next_trans_mesh = {0};

            if (!sc.t_vertices.empty()) {
                sc.next_trans_mesh.vertexCount = sc.t_vertices.size();
                sc.next_trans_mesh.triangleCount = sc.t_vertices.size() / 3;

                sc.next_trans_mesh.vertices = (float*)MemAlloc(sc.t_vertices.size() * 3 * sizeof(float));
                sc.next_trans_mesh.normals = (float*)MemAlloc(sc.t_normals.size() * 3 * sizeof(float));
                sc.next_trans_mesh.texcoords = (float*)MemAlloc(sc.t_uvs.size() * 2 * sizeof(float));
                sc.next_trans_mesh.texcoords2 = (float*)MemAlloc(sc.t_uvs2.size() * 2 * sizeof(float));
                sc.next_trans_mesh.colors = (unsigned char*)MemAlloc(sc.t_colors.size() * 4 * sizeof(unsigned char));

                for (size_t i = 0; i < sc.t_vertices.size(); i++) {
                    sc.next_trans_mesh.vertices[i*3] = sc.t_vertices[i].x;
                    sc.next_trans_mesh.vertices[i*3+1] = sc.t_vertices[i].y;
                    sc.next_trans_mesh.vertices[i*3+2] = sc.t_vertices[i].z;

                    sc.next_trans_mesh.normals[i*3] = sc.t_normals[i].x;
                    sc.next_trans_mesh.normals[i*3+1] = sc.t_normals[i].y;
                    sc.next_trans_mesh.normals[i*3+2] = sc.t_normals[i].z;

                    sc.next_trans_mesh.texcoords[i*2] = sc.t_uvs[i].x;
                    sc.next_trans_mesh.texcoords[i*2+1] = sc.t_uvs[i].y;

                    sc.next_trans_mesh.texcoords2[i*2] = sc.t_uvs2[i].x;
                    sc.next_trans_mesh.texcoords2[i*2+1] = sc.t_uvs2[i].y;

                    sc.next_trans_mesh.colors[i*4] = sc.t_colors[i].r;
                    sc.next_trans_mesh.colors[i*4+1] = sc.t_colors[i].g;
                    sc.next_trans_mesh.colors[i*4+2] = sc.t_colors[i].b;
                    sc.next_trans_mesh.colors[i*4+3] = sc.t_colors[i].a;
                }

                std::vector<Vector3>().swap(sc.t_vertices);
                std::vector<Vector3>().swap(sc.t_normals);
                std::vector<Vector2>().swap(sc.t_uvs);
                std::vector<Vector2>().swap(sc.t_uvs2);
                std::vector<Color>().swap(sc.t_colors);
            }
        }

        if ((mask & 1) != 0) {
            free_arrays(sc.next_water_mesh);
            sc.next_water_mesh = {0};

            if (!sc.w_vertices.empty()) {
                sc.next_water_mesh.vertexCount = sc.w_vertices.size();
                sc.next_water_mesh.triangleCount = sc.w_vertices.size() / 3;

                sc.next_water_mesh.vertices = (float*)MemAlloc(sc.w_vertices.size() * 3 * sizeof(float));
                sc.next_water_mesh.normals = (float*)MemAlloc(sc.w_normals.size() * 3 * sizeof(float));
                sc.next_water_mesh.texcoords = (float*)MemAlloc(sc.w_uvs.size() * 2 * sizeof(float));
                sc.next_water_mesh.texcoords2 = (float*)MemAlloc(sc.w_uvs2.size() * 2 * sizeof(float));
                sc.next_water_mesh.colors = (unsigned char*)MemAlloc(sc.w_colors.size() * 4 * sizeof(unsigned char));

                for (size_t i = 0; i < sc.w_vertices.size(); i++) {
                    sc.next_water_mesh.vertices[i*3] = sc.w_vertices[i].x;
                    sc.next_water_mesh.vertices[i*3+1] = sc.w_vertices[i].y;
                    sc.next_water_mesh.vertices[i*3+2] = sc.w_vertices[i].z;

                    sc.next_water_mesh.normals[i*3] = sc.w_normals[i].x;
                    sc.next_water_mesh.normals[i*3+1] = sc.w_normals[i].y;
                    sc.next_water_mesh.normals[i*3+2] = sc.w_normals[i].z;

                    sc.next_water_mesh.texcoords[i*2] = sc.w_uvs[i].x;
                    sc.next_water_mesh.texcoords[i*2+1] = sc.w_uvs[i].y;

                    sc.next_water_mesh.texcoords2[i*2] = sc.w_uvs2[i].x;
                    sc.next_water_mesh.texcoords2[i*2+1] = sc.w_uvs2[i].y;

                    sc.next_water_mesh.colors[i*4] = sc.w_colors[i].r;
                    sc.next_water_mesh.colors[i*4+1] = sc.w_colors[i].g;
                    sc.next_water_mesh.colors[i*4+2] = sc.w_colors[i].b;
                    sc.next_water_mesh.colors[i*4+3] = sc.w_colors[i].a;
                }

                std::vector<Vector3>().swap(sc.w_vertices);
                std::vector<Vector3>().swap(sc.w_normals);
                std::vector<Vector2>().swap(sc.w_uvs);
                std::vector<Vector2>().swap(sc.w_uvs2);
                std::vector<Color>().swap(sc.w_colors);
            }
        }
    }

    pending_subchunks_upload_mask.fetch_or(sub_mask);
    pending_upload_mask.fetch_or(mask);
}

void Chunk::upload_meshes() {
    int mask = pending_upload_mask.exchange(0);
    uint8_t sub_mask = pending_subchunks_upload_mask.exchange(0);
    if (mask == 0 && sub_mask == 0) return;

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

    for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
        if ((sub_mask & (1 << s)) == 0) continue;
        auto& sc = subchunks[s];
        if ((mask & 2) != 0) {
            upload_and_swap(sc.solid_mesh, sc.next_solid_mesh);
            upload_and_swap(sc.build_mesh, sc.next_build_mesh);
            upload_and_swap(sc.plants_mesh, sc.next_plants_mesh);
            upload_and_swap(sc.trans_mesh, sc.next_trans_mesh);
        }
        if ((mask & 1) != 0) {
            upload_and_swap(sc.water_mesh, sc.next_water_mesh);
        }
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

void Chunk::draw_subchunk_solid(int s, Material& mat_solid, Material& mat_plants, Vector3 camera_pos) {
    if (!is_ready || s < 0 || s >= Config::NUM_SUBCHUNKS) return;
    auto& sc = subchunks[s];
    
    if (sc.solid_mesh.vboId) {
        DrawMesh(sc.solid_mesh, mat_solid, MatrixIdentity());
    }

    if (sc.build_mesh.vboId) {
        DrawMesh(sc.build_mesh, mat_solid, MatrixIdentity());
    }
    
    if (sc.plants_mesh.vboId) {
        float cx_center = cx * CHUNK_SIZE + CHUNK_SIZE / 2.0f;
        float cz_center = cz * CHUNK_SIZE + CHUNK_SIZE / 2.0f;
        
        if (std::abs(cx_center - camera_pos.x) <= 35.0f && std::abs(cz_center - camera_pos.z) <= 35.0f) {
            rlDisableBackfaceCulling();
            DrawMesh(sc.plants_mesh, mat_plants, MatrixIdentity());
            rlEnableBackfaceCulling();
        }
    }
}

void Chunk::draw_subchunk_water(int s, Material& mat_water, Vector3 camera_pos) {
    if (!is_ready || s < 0 || s >= Config::NUM_SUBCHUNKS) return;
    auto& sc = subchunks[s];
    
    if (sc.water_mesh.vboId) {
        DrawMesh(sc.water_mesh, mat_water, MatrixIdentity());
    }
}

void Chunk::draw_subchunk_trans(int s, Material& mat_plants, Vector3 camera_pos) {
    if (!is_ready || s < 0 || s >= Config::NUM_SUBCHUNKS) return;
    auto& sc = subchunks[s];
    
    if (sc.trans_mesh.vboId) {
        rlDisableBackfaceCulling();
        DrawMesh(sc.trans_mesh, mat_plants, MatrixIdentity());
        rlEnableBackfaceCulling();
    }
}

void Chunk::draw_solid(Material& mat_solid, Material& mat_plants, Vector3 camera_pos) {
    if (!is_ready) return;
    for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
        draw_subchunk_solid(s, mat_solid, mat_plants, camera_pos);
    }
}

void Chunk::draw_water(Material& mat_water, Vector3 camera_pos) {
    if (!is_ready) return;
    for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
        draw_subchunk_water(s, mat_water, camera_pos);
    }
}


void Chunk::set_block(int x, int y, int z, uint8_t type, uint8_t rotation) {
    if (x < 0 || x > CHUNK_SIZE || y < 0 || y >= GRID_Y || z < 0 || z > CHUNK_SIZE) return;
    
    std::lock_guard<std::mutex> lock(chunk_mutex);
    if (generating) {
        pending_edits.push_back({x, y, z, type, rotation});
        needs_save = true;
        return;
    }
    int i = get_idx(x, y, z);
    voxels[i].block = type;
    voxels[i].rotation = rotation;
    
    if (type == AIR || type == Config::WATER) {
        voxels[i].density = -1.0f;
    } else {
        bool is_terrain = true;
        if (Config::BLOCKS.find(type) != Config::BLOCKS.end()) {
            is_terrain = (Config::BLOCKS.at(type).shape == Config::SHAPE_TERRAIN);
        }
        if (is_terrain) {
            voxels[i].density = 1.0f;
        } else {
            voxels[i].density = -1.0f;
        }
    }
    
    voxels[i].water = (type == Config::WATER) ? 8 : 0;
    
    int sub_y = std::clamp(y / Config::SUBCHUNK_SIZE, 0, Config::NUM_SUBCHUNKS - 1);
    uint8_t sub_mask = (1 << sub_y);
    if (sub_y > 0) sub_mask |= (1 << (sub_y - 1));
    if (sub_y < Config::NUM_SUBCHUNKS - 1) sub_mask |= (1 << (sub_y + 1));
    dirty_subchunks_mask.fetch_or(sub_mask);

    is_dirty = true;
    needs_save = true;
    water_only_rebuild = false;
}

void Chunk::rebuild_mesh(bool water_only, uint8_t sub_mask) {
    dirty_subchunks_mask.fetch_or(sub_mask);
    int want = water_only ? 1 : 2;
    std::lock_guard<std::mutex> lock(rebuild_mutex);
    int current = rebuild_mode.load();
    if (current != 0 || rebuild_running.load()) {
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

uint8_t Chunk::get_rotation(int x, int y, int z) {
    if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return 0;
    std::lock_guard<std::mutex> lock(chunk_mutex);
    if (voxels.empty()) return 0;
    return voxels[get_idx(x, y, z)].rotation;
}
