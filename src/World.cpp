#include "World.hpp"
#include "UI.hpp"
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

World::World(Material solid, Material plants, Material water) : mat_solid(solid), mat_plants(plants), mat_water(water) {
    sim_thread = std::thread([this] { sim_loop(); });
}

World::~World() {
    stop_simulation();
}

void World::stop_simulation() {
    sim_running = false;
    if (sim_thread.joinable()) sim_thread.join();
}

void World::sim_loop() {
    while (sim_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        simulate_water();
    }
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
    
    upload_budget = 2 + pending_uploads / 12;
    if (upload_budget > 6) upload_budget = 6;
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
    
    int chunks_started = 0;
    
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

struct WaterEdit {
    int x, y, z;
    uint8_t level;
};

static inline uint64_t cell_key(int wx, int wy, int wz) {
    // wx: 20 bits (0-19), wy: 10 bits (20-29, offset 64 cubre 0..959), wz: 20 bits (30-49)
    return (uint64_t)(wx + (1 << 19)) | ((uint64_t)(wy + 64) << 20) | ((uint64_t)(wz + (1 << 19)) << 30);
}

static inline void cell_decode(uint64_t k, int& wx, int& wy, int& wz) {
    wx = (int)(k & 0xFFFFF) - (1 << 19);
    wy = (int)((k >> 20) & 0x3FF) - 64;
    wz = (int)((k >> 30) & 0xFFFFF) - (1 << 19);
}

void World::activate(int wx, int wy, int wz) {
    if (wx < -(1 << 19) || wx >= (1 << 19) || wz < -(1 << 19) || wz >= (1 << 19)) return;
    if (wy < 0 || wy >= Config::GRID_Y) return;
    std::lock_guard<std::mutex> lock(active_mutex);
    if (active_cells.size() < 5000) active_cells.push_back(cell_key(wx, wy, wz));
}

void World::simulate_water() {
    std::vector<std::pair<std::pair<int,int>, std::shared_ptr<Chunk>>> snap;
    {
        std::lock_guard<std::mutex> lock(chunks_mutex);
        snap.reserve(chunks.size());
        for (auto& kv : chunks) {
            snap.emplace_back(kv.first, kv.second);
        }
    }
    if (snap.empty()) return;

    std::sort(snap.begin(), snap.end(), [](const auto& a, const auto& b) {
        if (a.first.first != b.first.first) return a.first.first < b.first.first;
        return a.first.second < b.first.second;
    });

    auto snap_find = [&](int cx, int cz) -> std::shared_ptr<Chunk>* {
        auto key = std::make_pair(cx, cz);
        auto it = std::lower_bound(snap.begin(), snap.end(), key, [](const auto& a, const auto& b_key) {
            if (a.first.first != b_key.first) return a.first.first < b_key.first;
            return a.first.second < b_key.second;
        });
        if (it != snap.end() && it->first == key && it->second->is_ready) return &it->second;
        return nullptr;
    };

    std::vector<uint64_t> to_process;
    {
        std::lock_guard<std::mutex> lock(active_mutex);
        to_process.swap(active_cells);
    }
    if (to_process.empty()) return;

    std::vector<WaterEdit> edits;
    edits.reserve(512);
    std::vector<uint64_t> next_active;
    next_active.reserve(512);

    const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    auto add_neighborhood = [&](int wx, int wy, int wz) {
        next_active.push_back(cell_key(wx, wy, wz));
        for (int d = 0; d < 4; ++d) {
            next_active.push_back(cell_key(wx + dirs[d][0], wy, wz + dirs[d][1]));
        }
        if (wy > 0) next_active.push_back(cell_key(wx, wy - 1, wz));
        if (wy < Config::GRID_Y - 1) next_active.push_back(cell_key(wx, wy + 1, wz));
    };

    for (uint64_t k : to_process) {
        if (edits.size() >= 2000) { next_active.push_back(k); continue; }
        int wx, wy, wz;
        cell_decode(k, wx, wy, wz);
        if (wy <= 0 || wy >= Config::GRID_Y) continue;

        int ccx = std::floor((float)wx / Config::CHUNK_SIZE);
        int ccz = std::floor((float)wz / Config::CHUNK_SIZE);
        std::shared_ptr<Chunk>* found = snap_find(ccx, ccz);
        if (!found) continue;
        Chunk* c = found->get();
        int lx = wx - ccx * Config::CHUNK_SIZE;
        int lz = wz - ccz * Config::CHUNK_SIZE;

        uint8_t L = c->get_water_level(lx, wy, lz);
        if (L == 0) continue;

        auto get_target = [&](int wwx, int wwz) -> Chunk* {
            int ncx = std::floor((float)wwx / Config::CHUNK_SIZE);
            int ncz = std::floor((float)wwz / Config::CHUNK_SIZE);
            std::shared_ptr<Chunk>* nb = snap_find(ncx, ncz);
            return nb ? nb->get() : nullptr;
        };

        auto read_block = [&](int wx2, int wy2, int wz2) -> uint8_t {
            if (wy2 < 0 || wy2 >= Config::GRID_Y) return Config::AIR;
            int tl = wx2 - ccx * Config::CHUNK_SIZE;
            int tlz = wz2 - ccz * Config::CHUNK_SIZE;
            Chunk* target = c;
            if (tl < 0) { target = get_target(wx2, wz2); tl += Config::CHUNK_SIZE; }
            else if (tl > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tl -= Config::CHUNK_SIZE; }
            if (tlz < 0) { target = get_target(wx2, wz2); tlz += Config::CHUNK_SIZE; }
            else if (tlz > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tlz -= Config::CHUNK_SIZE; }
            if (!target) return Config::AIR;
            return target->get_block(tl, wy2, tlz);
        };

        auto read_level = [&](int wx2, int wy2, int wz2) -> uint8_t {
            if (wy2 < 0 || wy2 >= Config::GRID_Y) return 0;
            int tl = wx2 - ccx * Config::CHUNK_SIZE;
            int tlz = wz2 - ccz * Config::CHUNK_SIZE;
            Chunk* target = c;
            if (tl < 0) { target = get_target(wx2, wz2); tl += Config::CHUNK_SIZE; }
            else if (tl > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tl -= Config::CHUNK_SIZE; }
            if (tlz < 0) { target = get_target(wx2, wz2); tlz += Config::CHUNK_SIZE; }
            else if (tlz > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tlz -= Config::CHUNK_SIZE; }
            if (!target) return 0;
            return target->get_water_level(tl, wy2, tlz);
        };

        bool is_source = (L == 8);
        uint8_t expected = 0;

        if (is_source) {
            expected = 8;
        } else {
            // Check for infinite water source generation (2 adjacent sources + solid below)
            int source_count = 0;
            for (int d = 0; d < 4; ++d) {
                if (read_level(wx + dirs[d][0], wy, wz + dirs[d][1]) == 8) source_count++;
            }
            
            bool has_solid_below = false;
            if (wy > 0) {
                uint8_t below = read_block(wx, wy - 1, wz);
                if (below != Config::AIR && below != Config::WATER) has_solid_below = true;
            } else {
                has_solid_below = true;
            }

            if (source_count >= 2 && has_solid_below) {
                expected = 8;
            } else {
                uint8_t above = read_level(wx, wy + 1, wz);
                if (above > 0) {
                    expected = 7;
                } else {
                    for (int d = 0; d < 4; ++d) {
                        uint8_t nL = read_level(wx + dirs[d][0], wy, wz + dirs[d][1]);
                        if (nL == 8) expected = std::max(expected, (uint8_t)7);
                        else if (nL > 1 && nL <= 7) expected = std::max(expected, (uint8_t)(nL - 1));
                    }
                }
            }
        }

        if (L != expected) {
            edits.push_back({wx, wy, wz, expected});
            add_neighborhood(wx, wy, wz);
        }

        if (expected > 0) {
            bool can_fall = (wy > 0 && (read_block(wx, wy - 1, wz) == Config::AIR || read_block(wx, wy - 1, wz) == Config::WATER));
            
            if (can_fall) {
                uint8_t below = read_level(wx, wy - 1, wz);
                if (below != 7 && below != 8) {
                    edits.push_back({wx, wy - 1, wz, 7});
                    add_neighborhood(wx, wy - 1, wz);
                }
            } else if (expected > 1) {
                for (int d = 0; d < 4; ++d) {
                    int nwx = wx + dirs[d][0];
                    int nwz = wz + dirs[d][1];
                    uint8_t nb = read_block(nwx, wy, nwz);
                    if (nb == Config::AIR || nb == Config::WATER) {
                        uint8_t nl = read_level(nwx, wy, nwz);
                        if (nl < expected - 1) {
                            edits.push_back({nwx, wy, nwz, (uint8_t)(expected - 1)});
                            add_neighborhood(nwx, wy, nwz);
                        }
                    }
                }
            }
        }
    }

    for (const auto& e : edits) {
        set_water_node(snap, e.x, e.y, e.z, e.level);
    }

    {
        std::lock_guard<std::mutex> lock(active_mutex);
        std::sort(next_active.begin(), next_active.end());
        next_active.erase(std::unique(next_active.begin(), next_active.end()), next_active.end());
        for (uint64_t k : next_active) {
            if (active_cells.size() < 5000) active_cells.push_back(k);
        }
    }
}

void World::set_water_node(const std::vector<std::pair<std::pair<int,int>, std::shared_ptr<Chunk>>>& snap, int wx, int wy, int wz, uint8_t level) {
    if (wy < 0 || wy >= Config::GRID_Y) return;
    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;

    auto apply = [&](int ccx, int ccz, int llx, int llz) {
        auto key = std::make_pair(ccx, ccz);
        auto it = std::lower_bound(snap.begin(), snap.end(), key, [](const auto& a, const auto& b_key) {
            if (a.first.first != b_key.first) return a.first.first < b_key.first;
            return a.first.second < b_key.second;
        });
        if (it != snap.end() && it->first == key && it->second->is_ready) {
            it->second->set_water_node(llx, wy, llz, level);
        }
    };

    apply(cx, cz, lx, lz);
    if (lx == 0) apply(cx - 1, cz, CHUNK_SIZE, lz);
    if (lz == 0) apply(cx, cz - 1, lx, CHUNK_SIZE);
    if (lx == 0 && lz == 0) apply(cx - 1, cz - 1, CHUNK_SIZE, CHUNK_SIZE);
}

void World::draw(Camera3D camera) {
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    float half_w = Config::CHUNK_SIZE / 2.0f;
    float half_h = Config::GRID_Y / 2.0f;
    float radius = std::sqrt(half_w * half_w * 2 + half_h * half_h);
    
    auto is_visible = [&](Chunk* c) {
        Vector3 center = { c->cx * Config::CHUNK_SIZE + half_w, half_h, c->cz * Config::CHUNK_SIZE + half_w };
        Vector3 d = Vector3Subtract(center, camera.position);
        float dist = Vector3Length(d);
        if (dist <= radius) return true;
        
        Vector3 dir = Vector3Scale(d, 1.0f / dist);
        float dot = Vector3DotProduct(forward, dir);
        
        float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
        float sphere_angle = std::asin(std::clamp(radius / dist, 0.0f, 1.0f));
        float max_angle = 1.2f + sphere_angle;
        
        return angle <= max_angle;
    };

    // Pass 1: Opaque geometry (Solid and Plants)
    for (auto& pair : chunks) {
        if (is_visible(pair.second.get())) {
            pair.second->draw_solid(mat_solid, mat_plants, camera.position);
        }
    }
    
    // Pass 2: Transparent geometry (Water)
    int cam_x = std::floor(camera.position.x);
    int cam_y = std::floor(camera.position.y);
    int cam_z = std::floor(camera.position.z);
    bool is_underwater = (get_block(cam_x, cam_y, cam_z) == Config::WATER);

    rlEnableBackfaceCulling();
    rlSetCullFace(is_underwater ? RL_CULL_FACE_BACK : RL_CULL_FACE_FRONT);

    for (auto& pair : chunks) {
        if (is_visible(pair.second.get())) {
            pair.second->draw_water(mat_water, camera.position);
        }
    }

    rlSetCullFace(RL_CULL_FACE_BACK);
}

void World::save_all() {
    for (auto& pair : chunks) {
        if (pair.second->is_dirty) {
            pair.second->save_to_disk();
        }
    }
}

Chunk* World::get_chunk(int cx, int cz) {
    auto key = std::make_pair(cx, cz);
    if (chunks.find(key) != chunks.end()) {
        return chunks[key].get();
    }
    return nullptr;
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

void World::set_block(int wx, int wy, int wz, uint8_t type) {
    if (wy < 0 || wy >= GRID_Y) return;
    
    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    
    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;
    
    auto update_chunk = [&](int c_x, int c_z, int l_x, int l_z) {
        Chunk* c = get_chunk(c_x, c_z);
        if (c) c->set_block(l_x, wy, l_z, type);
    };
    
    // 1. Chunk principal
    update_chunk(cx, cz, lx, lz);
    
    // 2. Sincronizar vóxeles de frontera compartidos con chunks adyacentes
    if (lx == 0) update_chunk(cx - 1, cz, CHUNK_SIZE, lz);
    if (lz == 0) update_chunk(cx, cz - 1, lx, CHUNK_SIZE);
    if (lx == 0 && lz == 0) update_chunk(cx - 1, cz - 1, CHUNK_SIZE, CHUNK_SIZE);

    activate(wx, wy, wz);
    activate(wx + 1, wy, wz);
    activate(wx - 1, wy, wz);
    activate(wx, wy, wz + 1);
    activate(wx, wy, wz - 1);
    if (wy + 1 < GRID_Y) activate(wx, wy + 1, wz);
    if (wy > 0) activate(wx, wy - 1, wz);
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

int World::flatten_terrain(int wx, int wy, int wz, const HammerArea& area, int tool_tier, UI* ui) {
    // Aplana el terreno tomando wy como piso plano de referencia.
    // 1. Desbasta y recolecta las elevaciones por encima de wy hasta area.max_h.
    // 2. Nivela la pendiente del suelo en wy a una superficie 100% plana con su bloque natural.
    int broken_count = 0;
    
    for (int dx = area.min_dx; dx <= area.max_dx; dx++) {
        for (int dz = area.min_dz; dz <= area.max_dz; dz++) {
            // 1. Limpiar elevaciones superiores
            for (int dy = area.max_h; dy >= 1; dy--) {
                int bx = wx + dx;
                int by = wy + dy;
                int bz = wz + dz;
                if (by < 0 || by >= GRID_Y) continue;
                
                uint8_t b = get_block(bx, by, bz);
                if (b != AIR && b != WATER && BLOCKS.count(b)) {
                    const auto& bt = BLOCKS.at(b);
                    if (bt.require_tier != 255 && bt.require_tier > tool_tier) {
                        continue;
                    }
                    
                    if (ui && bt.drop_id != 255) {
                        if (bt.drop_is_item) {
                            ui->add_item(bt.drop_id);
                        } else {
                            ui->add_resource(bt.drop_id);
                        }
                    }
                    set_block(bx, by, bz, AIR);
                    broken_count++;
                } else if (b == AIR) {
                    // Asegurar que cualquier residuo de rampa en alturas superiores quede en aire puro
                    set_block(bx, by, bz, AIR);
                }
            }
            
            // 2. Nivelar la base en wy (convirtiendo cualquier rampa/inclinación en plano horizontal perfecto)
            int bx = wx + dx;
            int by = wy;
            int bz = wz + dz;
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
