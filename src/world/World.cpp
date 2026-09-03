#include "world/World.hpp"
#include "ui/UI.hpp"
#include "gameplay/ItemDrop.hpp"
#include <cmath>

sqlite3* db = nullptr;
ThreadPool global_thread_pool(Config::MAX_WORKER_THREADS);
#include <rlgl.h>
#include <cmath>
#include <raymath.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <utility>
#include <thread>
#include <chrono>

using namespace Config;

World::World(Material solid, Material plants, Material water) : mat_solid(solid), mat_plants(plants), mat_water(water), fluid_sim(this) {
}

World::~World() {
    stop_simulation();
}

void World::stop_simulation() {
    fluid_sim.stop();
}

void World::update(Vector3 player_pos) {
    Chunk::flush_gl_delete_queue();
    
    int pcx = std::floor(player_pos.x / CHUNK_SIZE);
    int pcz = std::floor(player_pos.z / CHUNK_SIZE);
    
    int load_radius = Config::RENDER_DISTANCE;
    
    std::lock_guard<std::mutex> map_lock(chunks_mutex);
    
    int pending_uploads = 0;
    for (const auto& pair : chunks) {
        if (pair.second->needs_upload) pending_uploads++;
    }
    
    upload_budget = 4 + pending_uploads / 4;
    if (upload_budget > 16) upload_budget = 16;
    for (auto it = chunks.begin(); it != chunks.end();) {
        int cx = it->first.first;
        int cz = it->first.second;
        if (std::abs(cx - pcx) > load_radius + 1 || std::abs(cz - pcz) > load_radius + 1) {
            if (it->second->needs_save) {
                it->second->save_to_disk();
            }
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
    
    
    
    static std::vector<std::pair<int, int>> chunk_coords;
    static int cached_load_radius = -1;
    if (cached_load_radius != load_radius) {
        chunk_coords.clear();
        for (int x = -load_radius; x <= load_radius; x++) {
            for (int z = -load_radius; z <= load_radius; z++) {
                chunk_coords.push_back({x, z});
            }
        }
        std::sort(chunk_coords.begin(), chunk_coords.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            return (a.first * a.first + a.second * a.second) < (b.first * b.first + b.second * b.second);
        });
        cached_load_radius = load_radius;
    }
    
    for (const auto& coord : chunk_coords) {
        int cx = pcx + coord.first;
        int cz = pcz + coord.second;
        
        auto key = std::make_pair(cx, cz);
        if (chunks.find(key) == chunks.end()) {
            chunks[key] = std::make_shared<Chunk>(cx, cz);
            chunks[key]->current_lod = 1;
            chunks[key]->start_generation();
        } else {
            chunks[key]->update_logic(upload_budget);
        }
    }
}

struct FrustumPlane {
    float a, b, c, d;
    void normalize() {
        float mag = std::sqrt(a * a + b * b + c * c);
        if (mag > 0.000001f) {
            a /= mag;
            b /= mag;
            c /= mag;
            d /= mag;
        }
    }
};

struct Frustum {
    FrustumPlane planes[6];
};

static Frustum extract_camera_frustum(Camera3D camera) {
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);
    float aspect = (float)GetScreenWidth() / (float)GetScreenHeight();
    if (aspect <= 0.0f) aspect = 16.0f / 9.0f;
    Matrix proj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, 0.05f, Config::FOG_END + 16.0f);
    Matrix m = MatrixMultiply(view, proj);

    Frustum f;
    // Left
    f.planes[0] = { m.m3 + m.m0, m.m7 + m.m4, m.m11 + m.m8, m.m15 + m.m12 };
    // Right
    f.planes[1] = { m.m3 - m.m0, m.m7 - m.m4, m.m11 - m.m8, m.m15 - m.m12 };
    // Bottom
    f.planes[2] = { m.m3 + m.m1, m.m7 + m.m5, m.m11 + m.m9, m.m15 + m.m13 };
    // Top
    f.planes[3] = { m.m3 - m.m1, m.m7 - m.m5, m.m11 - m.m9, m.m15 - m.m13 };
    // Near
    f.planes[4] = { m.m3 + m.m2, m.m7 + m.m6, m.m11 + m.m10, m.m15 + m.m14 };
    // Far
    f.planes[5] = { m.m3 - m.m2, m.m7 - m.m6, m.m11 - m.m10, m.m15 - m.m14 };

    for (int i = 0; i < 6; ++i) f.planes[i].normalize();
    return f;
}

static bool is_chunk_in_frustum(const Frustum& f, int cx, int cz) {
    Vector3 min_pt = { (float)(cx * Config::CHUNK_SIZE), 0.0f, (float)(cz * Config::CHUNK_SIZE) };
    Vector3 max_pt = { (float)((cx + 1) * Config::CHUNK_SIZE), (float)Config::GRID_Y, (float)((cz + 1) * Config::CHUNK_SIZE) };
    for (int i = 0; i < 6; ++i) {
        Vector3 p;
        p.x = (f.planes[i].a >= 0.0f) ? max_pt.x : min_pt.x;
        p.y = (f.planes[i].b >= 0.0f) ? max_pt.y : min_pt.y;
        p.z = (f.planes[i].c >= 0.0f) ? max_pt.z : min_pt.z;
        if (f.planes[i].a * p.x + f.planes[i].b * p.y + f.planes[i].c * p.z + f.planes[i].d < 0.0f) {
            return false;
        }
    }
    return true;
}

static bool is_subchunk_in_frustum(const Frustum& f, int cx, int sy, int cz) {
    Vector3 min_pt = { (float)(cx * Config::CHUNK_SIZE), (float)(sy * Config::SUBCHUNK_SIZE), (float)(cz * Config::CHUNK_SIZE) };
    Vector3 max_pt = { (float)((cx + 1) * Config::CHUNK_SIZE), (float)((sy + 1) * Config::SUBCHUNK_SIZE), (float)((cz + 1) * Config::CHUNK_SIZE) };
    for (int i = 0; i < 6; ++i) {
        Vector3 p;
        p.x = (f.planes[i].a >= 0.0f) ? max_pt.x : min_pt.x;
        p.y = (f.planes[i].b >= 0.0f) ? max_pt.y : min_pt.y;
        p.z = (f.planes[i].c >= 0.0f) ? max_pt.z : min_pt.z;
        if (f.planes[i].a * p.x + f.planes[i].b * p.y + f.planes[i].c * p.z + f.planes[i].d < 0.0f) {
            return false;
        }
    }
    return true;
}

void World::draw(Camera3D camera) {
    Frustum frustum = extract_camera_frustum(camera);

    std::vector<std::shared_ptr<Chunk>> snapshot;
    {
        std::lock_guard<std::mutex> lock(chunks_mutex);
        snapshot.reserve(chunks.size());
        for (auto& [key, chunk] : chunks) {
            snapshot.push_back(chunk);
        }
    }

    // Pass 1: Opaque geometry (Solid and Plants)
    for (auto& c : snapshot) {
        if (c && c->is_ready && is_chunk_in_frustum(frustum, c->cx, c->cz)) {
            uint8_t mask = c->solid_subchunks_mask.load(std::memory_order_relaxed);
            if (!mask) continue;
            for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
                if ((mask & (1 << s)) && is_subchunk_in_frustum(frustum, c->cx, s, c->cz)) {
                    c->draw_subchunk_solid(s, mat_solid, mat_plants, camera.position);
                }
            }
        }
    }
    
    // Pass 2: Transparent cutout geometry (Leaves, Glass)
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    for (auto& c : snapshot) {
        if (c && c->is_ready && is_chunk_in_frustum(frustum, c->cx, c->cz)) {
            uint8_t mask = c->trans_subchunks_mask.load(std::memory_order_relaxed);
            if (!mask) continue; // Si este chunk no tiene hojas en ningún subchunk, saltar inmediatamente
            for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
                if ((mask & (1 << s)) && is_subchunk_in_frustum(frustum, c->cx, s, c->cz)) {
                    c->draw_subchunk_trans(s, mat_plants, camera.position);
                }
            }
        }
    }
    rlEnableBackfaceCulling();

    // Pass 3: Blended transparent geometry (Water)
    // El agua debe verse por ambas caras (tanto desde fuera del agua como desde dentro al sumergirse)
    rlDisableBackfaceCulling();
    rlDisableDepthMask();

    for (auto& c : snapshot) {
        if (c && c->is_ready && is_chunk_in_frustum(frustum, c->cx, c->cz)) {
            uint8_t mask = c->water_subchunks_mask.load(std::memory_order_relaxed);
            if (!mask) continue; // Si este chunk no tiene agua, saltar inmediatamente
            for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
                if ((mask & (1 << s)) && is_subchunk_in_frustum(frustum, c->cx, s, c->cz)) {
                    c->draw_subchunk_water(s, mat_water, camera.position);
                }
            }
        }
    }

    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_BACK);
}

void World::save_all() {
    std::vector<std::shared_ptr<Chunk>> snapshot;
    {
        std::lock_guard<std::mutex> lock(chunks_mutex);
        for (auto& [key, chunk] : chunks) {
            snapshot.push_back(chunk);
        }
    }
    for (auto& chunk : snapshot) {
        if (chunk->needs_save || chunk->is_dirty) {
            chunk->save_to_disk();
        }
    }
}

void World::invalidate_all_meshes() {
    std::lock_guard<std::mutex> lock(chunks_mutex);
    for (auto& pair : chunks) {
        pair.second->is_dirty = true;
        pair.second->dirty_subchunks_mask = 0xFF;
    }
}

Chunk* World::get_chunk(int cx, int cz) {
    std::lock_guard<std::mutex> lock(chunks_mutex);
    auto key = std::make_pair(cx, cz);
    auto it = chunks.find(key);
    return (it != chunks.end()) ? it->second.get() : nullptr;
}

uint8_t World::get_block(int wx, int wy, int wz) {


    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    
    Chunk* chunk = get_chunk(cx, cz);
    if (chunk) {
        int lx = wx - cx * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;
        return chunk->get_block(lx, wy, lz);
    }
    return AIR;
}

uint8_t World::get_rotation(int wx, int wy, int wz) {
    if (wy < 0 || wy >= GRID_Y) return 0;
    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    
    Chunk* chunk = get_chunk(cx, cz);
    if (chunk) {
        int lx = wx - cx * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;
        return chunk->get_rotation(lx, wy, lz);
    }
    return 0;
}

void World::set_block(int wx, int wy, int wz, uint8_t type, uint8_t rotation) {
    if (wy < 0 || wy >= GRID_Y) return;
    
    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    
    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;
    
    

    auto update_chunk = [&](int c_x, int c_z, int l_x, int l_z) {
        Chunk* c = get_chunk(c_x, c_z);
        if (c) c->set_block(l_x, wy, l_z, type, rotation);
    };
    
    update_chunk(cx, cz, lx, lz);
    if (lx == 0) update_chunk(cx - 1, cz, CHUNK_SIZE, lz);
    if (lz == 0) update_chunk(cx, cz - 1, lx, CHUNK_SIZE);
    if (lx == 0 && lz == 0) update_chunk(cx - 1, cz - 1, CHUNK_SIZE, CHUNK_SIZE);

    int sub_y = std::clamp(wy / Config::SUBCHUNK_SIZE, 0, Config::NUM_SUBCHUNKS - 1);
    uint8_t sub_mask = (1 << sub_y);
    if (sub_y > 0) sub_mask |= (1 << (sub_y - 1));
    if (sub_y < Config::NUM_SUBCHUNKS - 1) sub_mask |= (1 << (sub_y + 1));

    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (dx == 0 && dz == 0) continue;
            Chunk* c = get_chunk(cx + dx, cz + dz);
            if (c) {
                c->dirty_subchunks_mask.fetch_or(sub_mask);
                c->is_dirty = true;
            }
        }
    }

    fluid_sim.activate_neighbors(wx, wy, wz);
}

HammerArea get_hammer_area(int tier, Vector3 hit, int bx, int bz) {
    HammerArea area;
    if (tier == TIER_WOOD || tier == TIER_STONE) {
        // 1x1
        area.min_dx = 0; area.max_dx = 0;
        area.min_dz = 0; area.max_dz = 0;
        area.max_h = 1;
    } else if (tier == TIER_IRON || tier == TIER_SILVER || tier == TIER_GOLD) {
        // 2x2 orientado según cuadrante del cursor
        int off_x = (hit.x >= (float)bx + 0.5f) ? 0 : -1;
        int off_z = (hit.z >= (float)bz + 0.5f) ? 0 : -1;
        area.min_dx = off_x; area.max_dx = off_x + 1;
        area.min_dz = off_z; area.max_dz = off_z + 1;
        area.max_h = 2;
    } else {
        // 3x3 para diamante
        area.min_dx = -1; area.max_dx = 1;
        area.min_dz = -1; area.max_dz = 1;
        area.max_h = 2;
    }
    return area;
}

float World::get_hammer_mining_hardness(int wx, int wy, int wz, const HammerArea& area, int tool_tier) {
    uint8_t target_b = get_block(wx, wy, wz);
    float base_hardness = 0.5f;
    if (target_b != AIR && target_b != WATER && BLOCKS.count(target_b)) {
        base_hardness = BLOCKS.at(target_b).hardness;
    }
    
    float extra_hardness = 0.0f;
    for (int dx = area.min_dx; dx <= area.max_dx; dx++) {
        for (int dz = area.min_dz; dz <= area.max_dz; dz++) {
            for (int dy = area.max_h; dy >= 1; dy--) {
                int bx = wx + dx;
                int by = wy + dy;
                int bz = wz + dz;
                if (by < 0 || by >= GRID_Y) continue;
                uint8_t b = get_block(bx, by, bz);
                if (b != AIR && b != WATER && BLOCKS.count(b)) {
                    const auto& bt = BLOCKS.at(b);
                    if (bt.require_tier != 255 && bt.require_tier <= tool_tier) {
                        extra_hardness += bt.hardness * 0.75f;
                    }
                }
            }
        }
    }
    return base_hardness + extra_hardness;
}

int World::flatten_terrain(int wx, int wy, int wz, const HammerArea& area, int tool_tier, ItemDropManager* item_drops) {
    // Aplana el terreno tomando wy como piso plano de referencia.
    // 1. Desbasta y recolecta las elevaciones por encima de wy hasta area.max_h.
    // 2. Nivela la base en wy a una superficie 100% plana con su bloque natural SOLO si ya es sólida.
    int broken_count = 0;
    
    for (int dx = area.min_dx; dx <= area.max_dx; dx++) {
        for (int dz = area.min_dz; dz <= area.max_dz; dz++) {
            int bx = wx + dx;
            int bz = wz + dz;

            // 1. Limpiar elevaciones superiores
            for (int dy = area.max_h; dy >= 1; dy--) {
                int by = wy + dy;
                if (by < 0 || by >= GRID_Y) continue;
                
                uint8_t b = get_block(bx, by, bz);
                if (b != AIR && b != WATER && BLOCKS.count(b)) {
                    const auto& bt = BLOCKS.at(b);
                    if (bt.require_tier != 255 && bt.require_tier > tool_tier) {
                        continue;
                    }
                    
                    if (item_drops && bt.drop_id != 255) {
                        Vector3 drop_pos = { (float)bx + 0.5f, (float)by + 0.5f, (float)bz + 0.5f };
                        Vector3 drop_vel = {
                            Config::rand_float(-1.0f, 1.0f),
                            Config::rand_float(2.5f, 3.0f),
                            Config::rand_float(-1.0f, 1.0f)
                        };
                        item_drops->spawn(drop_pos, bt.drop_id, bt.drop_is_item, 1, drop_vel, 0.1f);
                    }
                    set_block(bx, by, bz, AIR);
                    broken_count++;
                } else {
                    // Asegurar que cualquier residuo de rampa en alturas superiores quede en aire puro (-1.0)
                    set_block(bx, by, bz, AIR);
                }
            }
            
            // 2. Nivelar la base en wy SOLO si ya es sólida (sin inventar material de la nada)
            int by = wy;
            if (by >= 0 && by < GRID_Y) {
                uint8_t b = get_block(bx, by, bz);
                if (b != AIR && b != WATER) {
                    set_block(bx, by, bz, b);
                }
            }
        }
    }
    return broken_count;
}

float World::get_density(int x, int y, int z) const {
    if (y < 0 || y >= Config::GRID_Y) return -1.0f;
    int cx = std::floor((float)x / Config::CHUNK_SIZE);
    int cz = std::floor((float)z / Config::CHUNK_SIZE);
    int lx = x - cx * Config::CHUNK_SIZE;
    int lz = z - cz * Config::CHUNK_SIZE;
    auto it = chunks.find({cx, cz});
    if (it != chunks.end()) {
        return it->second->get_density(lx, y, lz);
    }
    return -1.0f;
}

uint8_t World::get_light(int wx, int wy, int wz) {
    int cx = std::floor((float)wx / Config::CHUNK_SIZE);
    int cz = std::floor((float)wz / Config::CHUNK_SIZE);
    
    Chunk* chunk = get_chunk(cx, cz);
    if (chunk) {
        int lx = wx - cx * Config::CHUNK_SIZE;
        int lz = wz - cz * Config::CHUNK_SIZE;
        return chunk->get_light(lx, wy, lz);
    }
    return 0;
}

float World::sample_density_trilinear(float x, float y, float z) const {
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    int iz = (int)std::floor(z);
    float fx = x - ix;
    float fy = y - iy;
    float fz = z - iz;

    float d000 = get_density(ix,   iy,   iz);
    float d100 = get_density(ix+1, iy,   iz);
    float d010 = get_density(ix,   iy+1, iz);
    float d110 = get_density(ix+1, iy+1, iz);
    float d001 = get_density(ix,   iy,   iz+1);
    float d101 = get_density(ix+1, iy,   iz+1);
    float d011 = get_density(ix,   iy+1, iz+1);
    float d111 = get_density(ix+1, iy+1, iz+1);

    float c00 = d000 * (1.0f - fx) + d100 * fx;
    float c10 = d010 * (1.0f - fx) + d110 * fx;
    float c01 = d001 * (1.0f - fx) + d101 * fx;
    float c11 = d011 * (1.0f - fx) + d111 * fx;
    float c0  = c00  * (1.0f - fz) + c01  * fz;
    float c1  = c10  * (1.0f - fz) + c11  * fz;
    return c0 * (1.0f - fy) + c1 * fy;
}

float World::get_spawn_surface_y(float x, float z) {
    int ix = (int)std::floor(x);
    int iz = (int)std::floor(z);
    // Escanear de arriba hacia abajo para encontrar la superficie del suelo
    for (int y = Config::GRID_Y - 3; y >= 2; --y) {
        uint8_t b = get_block(ix, y, iz);
        if (b != Config::AIR && b != Config::WATER) {
            return (float)y + 1.0f;
        }
        float d = sample_density_trilinear(x, (float)y, z);
        if (d >= Config::ISO_SURFACE) {
            return (float)y + 0.5f;
        }
    }
    return 60.0f;
}
