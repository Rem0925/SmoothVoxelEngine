#include "Chunk.hpp"
#include "Noise.hpp"
#include "MarchingCubes.hpp"
#include <rlgl.h>
#include <raymath.h>
#include <cmath>
#include <iostream>
#include <cstring>
#include <sqlite3.h>
#include "World.hpp"

using namespace Config;

Chunk::Chunk(int x, int z) : cx(x), cz(z) {
    int total_blocks = (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1);
    density_grid = new float[total_blocks];
    block_grid = new uint8_t[total_blocks];
    water_level = new uint8_t[total_blocks];
    std::memset(water_level, 0, total_blocks * sizeof(uint8_t));
}

Chunk::~Chunk() {
    if (gen_future.valid()) gen_future.wait();
    if (density_grid) delete[] density_grid;
    if (block_grid) delete[] block_grid;
    if (water_level) delete[] water_level;
    if (solid_mesh.vboId) UnloadMesh(solid_mesh);
    if (water_mesh.vboId) UnloadMesh(water_mesh);
    if (plants_mesh.vboId) UnloadMesh(plants_mesh);
}

void Chunk::start_generation() {
    gen_future = global_thread_pool.enqueue([this] {
        this->generate_thread();
    });
}

inline int idx(int x, int y, int z) {
    return y * (CHUNK_SIZE + 1) * (CHUNK_SIZE + 1) + z * (CHUNK_SIZE + 1) + x;
}

void Chunk::generate_thread() {
    int total_blocks = (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1);
    bool loaded = false;
    {
        extern sqlite3* db;
        extern std::mutex sqlite_mutex;
        std::lock_guard<std::mutex> lock(sqlite_mutex);
        if (db) {
            sqlite3_stmt* stmt;
            const char* sql = "SELECT chunk_data FROM chunks WHERE cx = ? AND cz = ?";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, cx);
                sqlite3_bind_int(stmt, 2, cz);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const void* blob = sqlite3_column_blob(stmt, 0);
                    int bytes = sqlite3_column_bytes(stmt, 0);
                    if (bytes == total_blocks * (sizeof(float) + sizeof(uint8_t))) {
                        memcpy(density_grid, blob, total_blocks * sizeof(float));
                        memcpy(block_grid, (const char*)blob + total_blocks * sizeof(float), total_blocks * sizeof(uint8_t));
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

        for (int lx = 0; lx <= CHUNK_SIZE; ++lx) {
            for (int lz = 0; lz <= CHUNK_SIZE; ++lz) {
                double wx = cx * CHUNK_SIZE + lx + seed_offset;
                double wz = cz * CHUNK_SIZE + lz + seed_offset;
                
                double n_base = pnoise3(wx * 0.005, 0.0, wz * 0.005, 4, 0.5);
                double n_detail = pnoise3(wx * 0.02, 0.0, wz * 0.02, 4, 0.5);
                double n_mountains = std::abs(pnoise3(wx * 0.008, 0.0, wz * 0.008, 4, 0.5)); // Smoother mountains
                
                double valley_base = (n_base < 0) ? (n_base * 16.0) : (n_base * 10.0);
                double raw_h = 42.0 + valley_base + (n_detail * 3.0) + (n_mountains * 16.0); // Less intense
                float base_h = std::round(raw_h) * 0.20f + raw_h * 0.80f;
                
                int highest_y = 0;
                
                for (int y = 0; y < GRID_Y; ++y) {
                    float n3d = pnoise3(wx * 0.05f, y * 0.05f, wz * 0.05f, 2, 0.4f);
                    float cave_noise = pnoise3(wx * 0.04f, y * 0.04f, wz * 0.04f, 2, 0.5f);
                    
                    float true_depth = (base_h - y) + (n3d * 2.5f);
                    float d = true_depth;
                    
                    if (y == 0) d = 1.0f;
                    if (y == GRID_Y - 1) d = -1.0f;
                    
                    d = std::max(-1.0f, std::min(1.0f, d));
                    
                    float depth_below = base_h - y;
                    float cave_suppression = std::max(0.5f, std::min(1.0f, 0.5f + depth_below / 15.0f));
                    
                    if ((cave_noise * cave_suppression) > 0.4f) {
                        d = -1.0f;
                    }
                    
                    density_grid[idx(lx, y, lz)] = d;
                    
                    if (d >= ISO_SURFACE) {
                        highest_y = y;
                        has_solid[lz * (CHUNK_SIZE + 1) + lx] = true;
                    }
                }
                top_solid_y[lz * (CHUNK_SIZE + 1) + lx] = highest_y;
            }
        }

        std::vector<bool> is_water( (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1), false );
        std::vector<bool> water_columns( (CHUNK_SIZE + 1) * (CHUNK_SIZE + 1), false );

        for (int lx = 0; lx <= CHUNK_SIZE; ++lx) {
            for (int lz = 0; lz <= CHUNK_SIZE; ++lz) {
                int top = top_solid_y[lz * (CHUNK_SIZE + 1) + lx];
                bool solid_exists = has_solid[lz * (CHUNK_SIZE + 1) + lx];
                
                for (int y = 0; y < GRID_Y; ++y) {
                    int i = idx(lx, y, lz);
                    block_grid[i] = AIR;
                    
                    if (density_grid[i] >= ISO_SURFACE) {
                        block_grid[i] = STONE;
                        if (solid_exists) {
                            if (y > top - 2 && y <= top) {
                                block_grid[i] = GRASS;
                            } else if (y > top - 6 && y <= top - 2) {
                                block_grid[i] = DIRT;
                            }
                        }
                    }
                    
                    if (y <= (int)WATER_LEVEL && density_grid[i] < ISO_SURFACE && y >= top) {
                        block_grid[i] = WATER;
                        is_water[i] = true;
                        water_columns[lz * (CHUNK_SIZE + 1) + lx] = true;
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
                for (int y = 0; y < GRID_Y; ++y) {
                    int i = idx(lx, y, lz);
                    if (block_grid[i] == GRASS) {
                        if (y < (int)WATER_LEVEL || is_shore) {
                            block_grid[i] = DIRT;
                        }
                    }
                }
            }
        }
        
        // Trees
        for (int lx = 3; lx < CHUNK_SIZE - 3; ++lx) {
            for (int lz = 3; lz < CHUNK_SIZE - 3; ++lz) {
                int top = top_solid_y[lz * (CHUNK_SIZE + 1) + lx];
                if (top > WATER_LEVEL && block_grid[idx(lx, top, lz)] == GRASS) {
                    long hash = ( (cx * CHUNK_SIZE + lx + WORLD_SEED) * 73856 + (cz * CHUNK_SIZE + lz) * 1920 ) % 1000;
                    if (hash < 15) { // 1.5% probability
                        int tree_h = 5 + (hash % 4);
                        for (int ty = 1; ty <= tree_h; ++ty) {
                            if (top + ty < GRID_Y) {
                                block_grid[idx(lx, top + ty, lz)] = WOOD;
                                density_grid[idx(lx, top + ty, lz)] = 1.0f;
                            }
                        }
                        int r = 2;
                        for (int dx = -r; dx <= r; ++dx) {
                            for (int dy = -r; dy <= r; ++dy) {
                                for (int dz = -r; dz <= r; ++dz) {
                                    if (dx*dx + dy*dy + dz*dz <= r*r + 1) {
                                        int nx = lx + dx;
                                        int ny = top + tree_h + dy;
                                        int nz = lz + dz;
                                        if (nx >= 0 && nx <= CHUNK_SIZE && nz >= 0 && nz <= CHUNK_SIZE && ny >= 0 && ny < GRID_Y) {
                                            if (density_grid[idx(nx, ny, nz)] < 0.0f) {
                                                density_grid[idx(nx, ny, nz)] = 1.0f;
                                                block_grid[idx(nx, ny, nz)] = LEAVES;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Save to DB
        {
            extern sqlite3* db;
            extern std::mutex sqlite_mutex;
            std::lock_guard<std::mutex> lock(sqlite_mutex);
            if (db) {
                sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);
                sqlite3_stmt* stmt;
                const char* sql = "INSERT OR REPLACE INTO chunks (cx, cz, chunk_data) VALUES (?, ?, ?)";
                if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
                    sqlite3_bind_int(stmt, 1, cx);
                    sqlite3_bind_int(stmt, 2, cz);
                    std::vector<char> buffer(total_blocks * (sizeof(float) + sizeof(uint8_t)));
                    memcpy(buffer.data(), density_grid, total_blocks * sizeof(float));
                    memcpy(buffer.data() + total_blocks * sizeof(float), block_grid, total_blocks * sizeof(uint8_t));
                    sqlite3_bind_blob(stmt, 3, buffer.data(), buffer.size(), SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
                sqlite3_exec(db, "COMMIT;", 0, 0, 0);
            }
        }
    }

    for (int i = 0; i < total_blocks; ++i) {
        water_level[i] = (block_grid[i] == Config::WATER) ? 8 : 0;
    }

    build_mesh_data(density_grid, block_grid, water_level);
    needs_upload = true;
    is_ready = true;
}

void Chunk::rebuild_thread() {
    int total_blocks = (CHUNK_SIZE + 1) * GRID_Y * (CHUNK_SIZE + 1);
    std::vector<float> d_copy(total_blocks);
    std::vector<uint8_t> b_copy(total_blocks);
    std::vector<uint8_t> w_copy(total_blocks);
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
            memcpy(d_copy.data(), density_grid, total_blocks * sizeof(float));
            memcpy(b_copy.data(), block_grid, total_blocks * sizeof(uint8_t));
            memcpy(w_copy.data(), water_level, total_blocks * sizeof(uint8_t));
        }
        if (mode >= 2) {
            build_mesh_data(d_copy.data(), b_copy.data(), w_copy.data());
            pending_upload_mask.fetch_or(3);
        } else {
            build_water_mesh(d_copy.data(), b_copy.data(), w_copy.data());
            pending_upload_mask.fetch_or(1);
        }
    }
    rebuild_running = false;
    needs_upload = true;
}

void Chunk::build_mesh_data(const float* density, const uint8_t* blocks, const uint8_t* water) {
    std::vector<Vector3> ls_vertices, ls_normals;
    std::vector<Vector2> ls_uvs, ls_uvs2;
    std::vector<Color> ls_colors;

    std::vector<Vector3> lp_vertices, lp_normals;
    std::vector<Vector2> lp_uvs;
    std::vector<Color> lp_colors;

    // 1. Terrain Mesh (Marching Cubes)
    mc::generate(density, blocks, CHUNK_SIZE + 1, GRID_Y, CHUNK_SIZE + 1, ISO_SURFACE, Config::GRASS, ls_vertices, ls_normals, ls_uvs, ls_uvs2, ls_colors);
    
    // Remap vertices to global coords
    for (auto& v : ls_vertices) {
        v.x += cx * CHUNK_SIZE;
        v.z += cz * CHUNK_SIZE;
    }

    // [Water and plants... omitted for brevity, I will update water separately]
    
    // Tall Grass Generation
    for (size_t i = 0; i < ls_vertices.size(); i += 3) {
        Vector3 n = ls_normals[i];
        if (n.y > 0.8f) { // Allow grass on slightly steeper slopes
            Vector3 center = { (ls_vertices[i].x + ls_vertices[i+1].x + ls_vertices[i+2].x) / 3.0f,
                               (ls_vertices[i].y + ls_vertices[i+1].y + ls_vertices[i+2].y) / 3.0f,
                               (ls_vertices[i].z + ls_vertices[i+1].z + ls_vertices[i+2].z) / 3.0f };
                               
            int lx = std::floor(center.x) - cx * CHUNK_SIZE;
            int ly = std::floor(center.y);
            int lz = std::floor(center.z) - cz * CHUNK_SIZE;
            
            uint8_t b1 = get_block(lx, ly, lz);
            uint8_t b2 = get_block(lx, ly - 1, lz);
            bool on_grass = (b1 == Config::GRASS || b2 == Config::GRASS);
            bool is_explicit_grass = (b1 == Config::TALL_GRASS || b2 == Config::TALL_GRASS);
            
            if (on_grass || is_explicit_grass) {
                // Deterministic hash based on world position
                long hash = std::abs((long)(center.x * 73856.0f + center.z * 1920.0f)) % 1000;
                
                if (hash < 30 || is_explicit_grass) { 
                    float hw = 0.4f; float h = 0.8f;
                    Vector3 base = center; // Start exactly on the triangle surface!
                    
                    Vector3 up = n;
                    Vector3 right = {1, 0, 0};
                    if (std::abs(up.y) < 0.999f) {
                        right = Vector3Normalize(Vector3CrossProduct({0,1,0}, up));
                    }
                    Vector3 fwd = Vector3Normalize(Vector3CrossProduct(right, up));
                    
                    // Construct 4 vectors for the quad corners from the center
                    Vector3 d1 = Vector3Add(Vector3Scale(right, hw), Vector3Scale(fwd, hw));
                    Vector3 d2 = Vector3Subtract(Vector3Scale(right, hw), Vector3Scale(fwd, hw));
                    Vector3 ht = Vector3Scale(up, h);
                    
                    Vector3 q1_v[] = {
                        Vector3Subtract(base, d1),                          // bottom-left
                        Vector3Add(base, d1),                               // bottom-right
                        Vector3Add(Vector3Add(base, d1), ht),               // top-right
                        Vector3Add(Vector3Subtract(base, d1), ht)           // top-left
                    };
                    
                    Vector3 q2_v[] = {
                        Vector3Add(base, d2),                               // bottom-right
                        Vector3Subtract(base, d2),                          // bottom-left
                        Vector3Add(Vector3Subtract(base, d2), ht),          // top-left
                        Vector3Add(Vector3Add(base, d2), ht)                // top-right
                    };
                    
                    BlockType bt = Config::BLOCKS.at(Config::TALL_GRASS);
                    float tw = 1.0f / 9.0f; float th = 1.0f / 10.0f;
                    float sway = bt.is_waving ? 10.0f : 0.0f;
                    float u0 = bt.tex_x * tw; 
                    float v0 = (10.0f - 1.0f - bt.tex_y) * th; 
                    if (!is_explicit_grass) {
                        v0 = (10.0f - 1.0f - (bt.tex_y - (hash % 3))) * th; 
                    }
                    float u1 = u0 + tw; float v1 = v0 + th;
                    
                    // index 0: bottom-left (no sway)
                    // index 1: bottom-right (no sway)
                    // index 2: top-right (sway)
                    // index 3: top-left (sway)
                    Vector2 uvs[] = { {u1, v1}, {u0, v1}, {u0 + sway, v0}, {u1 + sway, v0} };
                    Vector3 nor = {0, 1, 0};
                    Color col = WHITE;
                    
                    auto add_quad = [&](Vector3* v) {
                        lp_vertices.push_back(v[0]); lp_vertices.push_back(v[1]); lp_vertices.push_back(v[2]);
                        lp_vertices.push_back(v[0]); lp_vertices.push_back(v[2]); lp_vertices.push_back(v[3]);
                        
                        for(int j=0; j<6; j++) {
                            lp_normals.push_back(nor); lp_colors.push_back(col);
                        }
                        
                        lp_uvs.push_back(uvs[0]); lp_uvs.push_back(uvs[1]); lp_uvs.push_back(uvs[2]);
                        lp_uvs.push_back(uvs[0]); lp_uvs.push_back(uvs[2]); lp_uvs.push_back(uvs[3]);
                    };
                    
                    add_quad(q1_v);
                    add_quad(q2_v);
                }
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

    build_water_mesh(density, blocks, water);
}

void Chunk::build_water_mesh(const float* density, const uint8_t* blocks, const uint8_t* water) {
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
                int i = idx(x, y, z);
                if (water_top < -50.0f && water[i] > 0) {
                    float drop = 0.05f + (8 - water[i]) * 0.10f;
                    water_top = (float)y - drop;
                }
                if (density[i] >= ISO_SURFACE) {
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
        if (blocks[i] == Config::WATER) {
            water_density[i] = -1.0f;
        }
    }

    mc::generate(water_density.data(), blocks, wx, GRID_Y, wz, ISO_SURFACE, Config::WATER, lw_vertices, lw_normals, lw_uvs, lw_uvs2, lw_colors);

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
            return density[idx];
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

void Chunk::upload_meshes() {
    int mask = pending_upload_mask.exchange(0);
    if (mask == 0) mask = 3; // Fallback to all if called directly

    std::lock_guard<std::mutex> lock(mesh_mutex);

    if ((mask & 2) != 0) {
        if (solid_mesh.vboId) UnloadMesh(solid_mesh);
        if (plants_mesh.vboId) UnloadMesh(plants_mesh);
        solid_mesh = { 0 }; plants_mesh = { 0 };
    }
    if ((mask & 1) != 0) {
        if (water_mesh.vboId) UnloadMesh(water_mesh);
        water_mesh = { 0 };
    }

    if (!s_vertices.empty()) {
        solid_mesh.vertexCount = s_vertices.size();
        solid_mesh.triangleCount = s_vertices.size() / 3;
        
        solid_mesh.vertices = (float*)MemAlloc(s_vertices.size() * 3 * sizeof(float));
        solid_mesh.normals = (float*)MemAlloc(s_normals.size() * 3 * sizeof(float));
        solid_mesh.texcoords = (float*)MemAlloc(s_uvs.size() * 2 * sizeof(float));
        solid_mesh.texcoords2 = (float*)MemAlloc(s_uvs2.size() * 2 * sizeof(float));
        solid_mesh.colors = (unsigned char*)MemAlloc(s_colors.size() * 4 * sizeof(unsigned char));

        for (size_t i = 0; i < s_vertices.size(); i++) {
            solid_mesh.vertices[i*3] = s_vertices[i].x;
            solid_mesh.vertices[i*3+1] = s_vertices[i].y;
            solid_mesh.vertices[i*3+2] = s_vertices[i].z;
            
            solid_mesh.normals[i*3] = s_normals[i].x;
            solid_mesh.normals[i*3+1] = s_normals[i].y;
            solid_mesh.normals[i*3+2] = s_normals[i].z;
            
            solid_mesh.texcoords[i*2] = s_uvs[i].x;
            solid_mesh.texcoords[i*2+1] = s_uvs[i].y;

            solid_mesh.texcoords2[i*2] = s_uvs2[i].x;
            solid_mesh.texcoords2[i*2+1] = s_uvs2[i].y;
            
            solid_mesh.colors[i*4] = s_colors[i].r;
            solid_mesh.colors[i*4+1] = s_colors[i].g;
            solid_mesh.colors[i*4+2] = s_colors[i].b;
            solid_mesh.colors[i*4+3] = s_colors[i].a;
        }
        UploadMesh(&solid_mesh, false);
    }

    if (!w_vertices.empty()) {
        water_mesh.vertexCount = w_vertices.size();
        water_mesh.triangleCount = w_vertices.size() / 3;
        
        water_mesh.vertices = (float*)MemAlloc(w_vertices.size() * 3 * sizeof(float));
        water_mesh.normals = (float*)MemAlloc(w_normals.size() * 3 * sizeof(float));
        water_mesh.texcoords = (float*)MemAlloc(w_uvs.size() * 2 * sizeof(float));
        water_mesh.texcoords2 = (float*)MemAlloc(w_uvs2.size() * 2 * sizeof(float));
        water_mesh.colors = (unsigned char*)MemAlloc(w_colors.size() * 4 * sizeof(unsigned char));

        for (size_t i = 0; i < w_vertices.size(); i++) {
            water_mesh.vertices[i*3] = w_vertices[i].x;
            water_mesh.vertices[i*3+1] = w_vertices[i].y;
            water_mesh.vertices[i*3+2] = w_vertices[i].z;
            
            water_mesh.normals[i*3] = w_normals[i].x;
            water_mesh.normals[i*3+1] = w_normals[i].y;
            water_mesh.normals[i*3+2] = w_normals[i].z;
            
            water_mesh.texcoords[i*2] = w_uvs[i].x;
            water_mesh.texcoords[i*2+1] = w_uvs[i].y;

            water_mesh.texcoords2[i*2] = w_uvs2[i].x;
            water_mesh.texcoords2[i*2+1] = w_uvs2[i].y;
            
            water_mesh.colors[i*4] = w_colors[i].r;
            water_mesh.colors[i*4+1] = w_colors[i].g;
            water_mesh.colors[i*4+2] = w_colors[i].b;
            water_mesh.colors[i*4+3] = w_colors[i].a;
        }
        UploadMesh(&water_mesh, false);
    }
    
    if (!p_vertices.empty()) {
        plants_mesh.vertexCount = p_vertices.size();
        plants_mesh.triangleCount = p_vertices.size() / 3;
        
        plants_mesh.vertices = (float*)MemAlloc(p_vertices.size() * 3 * sizeof(float));
        plants_mesh.normals = (float*)MemAlloc(p_normals.size() * 3 * sizeof(float));
        plants_mesh.texcoords = (float*)MemAlloc(p_uvs.size() * 2 * sizeof(float));
        plants_mesh.colors = (unsigned char*)MemAlloc(p_colors.size() * 4 * sizeof(unsigned char));

        for (size_t i = 0; i < p_vertices.size(); i++) {
            plants_mesh.vertices[i*3] = p_vertices[i].x;
            plants_mesh.vertices[i*3+1] = p_vertices[i].y;
            plants_mesh.vertices[i*3+2] = p_vertices[i].z;
            
            plants_mesh.normals[i*3] = p_normals[i].x;
            plants_mesh.normals[i*3+1] = p_normals[i].y;
            plants_mesh.normals[i*3+2] = p_normals[i].z;
            
            plants_mesh.texcoords[i*2] = p_uvs[i].x;
            plants_mesh.texcoords[i*2+1] = p_uvs[i].y;
            
            plants_mesh.colors[i*4] = p_colors[i].r;
            plants_mesh.colors[i*4+1] = p_colors[i].g;
            plants_mesh.colors[i*4+2] = p_colors[i].b;
            plants_mesh.colors[i*4+3] = p_colors[i].a;
        }
        UploadMesh(&plants_mesh, false);
    }

    if (!rebuild_running) {
        if ((mask & 2) != 0) {
            std::vector<Vector3>().swap(s_vertices);
            std::vector<Vector3>().swap(s_normals);
            std::vector<Vector2>().swap(s_uvs);
            std::vector<Vector2>().swap(s_uvs2);
            std::vector<Color>().swap(s_colors);
            std::vector<Vector3>().swap(p_vertices);
            std::vector<Vector3>().swap(p_normals);
            std::vector<Vector2>().swap(p_uvs);
            std::vector<Color>().swap(p_colors);
        }
        if ((mask & 1) != 0) {
            std::vector<Vector3>().swap(w_vertices);
            std::vector<Vector3>().swap(w_normals);
            std::vector<Vector2>().swap(w_uvs);
            std::vector<Vector2>().swap(w_uvs2);
            std::vector<Color>().swap(w_colors);
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

void Chunk::draw_solid(Material& mat_solid, Vector3 camera_pos) {
    if (!is_ready || (needs_upload && !s_vertices.empty())) return;
    
    if (solid_mesh.vboId) {
        DrawMesh(solid_mesh, mat_solid, MatrixIdentity());
    }
    
    if (plants_mesh.vboId) {
        float cx_center = cx * CHUNK_SIZE + CHUNK_SIZE / 2.0f;
        float cz_center = cz * CHUNK_SIZE + CHUNK_SIZE / 2.0f;
        
        if (std::abs(cx_center - camera_pos.x) <= 35.0f && std::abs(cz_center - camera_pos.z) <= 35.0f) {
            rlDisableBackfaceCulling();
            DrawMesh(plants_mesh, mat_solid, MatrixIdentity()); // Uses same material
            rlEnableBackfaceCulling();
        }
    }
}

void Chunk::draw_water(Material& mat_water, Vector3 camera_pos) {
    if (!is_ready || (needs_upload && !w_vertices.empty())) return;
    
    if (water_mesh.vboId) {
        DrawMesh(water_mesh, mat_water, MatrixIdentity());
    }
}

uint8_t Chunk::get_block(int x, int y, int z) const {
    if (x < 0 || x > CHUNK_SIZE || y < 0 || y >= GRID_Y || z < 0 || z > CHUNK_SIZE) return AIR;
    return block_grid[idx(x, y, z)];
}

void Chunk::set_block(int x, int y, int z, uint8_t type) {
    if (x < 0 || x > CHUNK_SIZE || y < 0 || y >= GRID_Y || z < 0 || z > CHUNK_SIZE) return;
    
    std::lock_guard<std::mutex> lock(chunk_mutex);
    int i = idx(x, y, z);
    block_grid[i] = type;
    
    if (type == AIR || type == Config::WATER) {
        density_grid[i] = -1.0f;
    } else {
        density_grid[i] = 1.0f;
    }
    
    water_level[i] = (type == Config::WATER) ? 8 : 0;
    
    is_dirty = true;
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
    global_thread_pool.enqueue([this] {
        this->rebuild_thread();
    });
}

void Chunk::save_to_disk() {
    int total_blocks = (Config::CHUNK_SIZE + 1) * Config::GRID_Y * (Config::CHUNK_SIZE + 1);
    std::vector<char> buffer(total_blocks * (sizeof(float) + sizeof(uint8_t)));
    {
        std::lock_guard<std::mutex> lock(chunk_mutex);
        memcpy(buffer.data(), density_grid, total_blocks * sizeof(float));
        memcpy(buffer.data() + total_blocks * sizeof(float), block_grid, total_blocks * sizeof(uint8_t));
    }
    
    int cap_cx = cx;
    int cap_cz = cz;
    
    global_thread_pool.enqueue([cap_cx, cap_cz, buffer = std::move(buffer)]() {
        extern sqlite3* db;
        extern std::mutex sqlite_mutex;
        std::lock_guard<std::mutex> lock(sqlite_mutex);
        if (db) {
            sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, 0);
            sqlite3_stmt* stmt;
            const char* sql = "INSERT OR REPLACE INTO chunks (cx, cz, chunk_data) VALUES (?, ?, ?)";
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
                sqlite3_bind_int(stmt, 1, cap_cx);
                sqlite3_bind_int(stmt, 2, cap_cz);
                sqlite3_bind_blob(stmt, 3, buffer.data(), buffer.size(), SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            sqlite3_exec(db, "COMMIT;", 0, 0, 0);
        }
    });
    is_dirty = false;
}

float Chunk::get_density(int x, int y, int z) const {
    if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return -1.0f;
    return density_grid[idx(x, y, z)];
}

uint8_t Chunk::get_water_level(int x, int y, int z) const {
    if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return 0;
    return water_level[idx(x, y, z)];
}

void Chunk::set_water_node(int x, int y, int z, uint8_t level) {
    if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return;
    std::lock_guard<std::mutex> lock(chunk_mutex);
    int i = idx(x, y, z);
    if (water_level[i] == level && (level > 0) == (block_grid[i] == Config::WATER)) return;
    
    if (level > 0) {
        block_grid[i] = Config::WATER;
        water_level[i] = (level > 8) ? 8 : level;
        density_grid[i] = -1.0f;
    } else {
        block_grid[i] = Config::AIR;
        water_level[i] = 0;
        density_grid[i] = -1.0f;
    }
    
    is_dirty = true;
    water_only_rebuild = true;
}
