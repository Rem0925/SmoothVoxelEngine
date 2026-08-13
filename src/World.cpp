#include "World.hpp"
#include <cmath>

sqlite3* db = nullptr;
std::mutex sqlite_mutex;
ThreadPool global_thread_pool(Config::MAX_WORKER_THREADS);
#include <cmath>
#include <raymath.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include <utility>
#include <thread>
#include <chrono>

using namespace Config;

World::World(Material solid, Material water) : mat_solid(solid), mat_water(water) {
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
    int pcx = std::floor(player_pos.x / CHUNK_SIZE);
    int pcz = std::floor(player_pos.z / CHUNK_SIZE);
    
    int load_radius = Config::RENDER_DISTANCE;
    upload_budget = 2;
    
    std::lock_guard<std::mutex> map_lock(chunks_mutex);
    for (auto it = chunks.begin(); it != chunks.end();) {
        int cx = it->first.first;
        int cz = it->first.second;
        if (std::abs(cx - pcx) > load_radius + 1 || std::abs(cz - pcz) > load_radius + 1) {
            if (it->second->is_dirty) {
                it->second->save_to_disk();
            }
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
    
    int chunks_started = 0;
    
    std::vector<std::pair<int, int>> chunk_coords;
    for (int x = -load_radius; x <= load_radius; x++) {
        for (int z = -load_radius; z <= load_radius; z++) {
            chunk_coords.push_back({x, z});
        }
    }
    
    std::sort(chunk_coords.begin(), chunk_coords.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
        return (a.first * a.first + a.second * a.second) < (b.first * b.first + b.second * b.second);
    });
    
    for (const auto& coord : chunk_coords) {
        int cx = pcx + coord.first;
        int cz = pcz + coord.second;
        
        auto key = std::make_pair(cx, cz);
        if (chunks.find(key) == chunks.end()) {
            chunks[key] = std::make_shared<Chunk>(cx, cz);
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
    return (uint64_t)(wx + (1 << 20)) | ((uint64_t)(wy + 32) << 21) | ((uint64_t)(wz + (1 << 20)) << 42);
}

static inline void cell_decode(uint64_t k, int& wx, int& wy, int& wz) {
    wx = (int)(k & 0x1FFFFF) - (1 << 20);
    wy = (int)((k >> 21) & 0x3F) - 32;
    wz = (int)((k >> 42) & 0x1FFFFF) - (1 << 20);
}

void World::activate(int wx, int wy, int wz) {
    if (wx < -(1 << 20) || wx >= (1 << 20) || wz < -(1 << 20) || wz >= (1 << 20)) return;
    if (wy < 0 || wy >= Config::GRID_Y) return;
    std::lock_guard<std::mutex> lock(active_mutex);
    if (active_cells.size() < 5000) active_cells.push_back(cell_key(wx, wy, wz));
}

void World::simulate_water() {
    std::unordered_map<std::pair<int, int>, std::shared_ptr<Chunk>, pair_hash> snap;
    {
        std::lock_guard<std::mutex> lock(chunks_mutex);
        snap = chunks;
    }
    if (snap.empty()) return;

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
        auto it = snap.find({ccx, ccz});
        if (it == snap.end() || !it->second->is_ready) continue;
        Chunk* c = it->second.get();
        int lx = wx - ccx * Config::CHUNK_SIZE;
        int lz = wz - ccz * Config::CHUNK_SIZE;

        uint8_t L = c->get_water_level(lx, wy, lz);
        if (L == 0) continue;

        auto get_target = [&](int wwx, int wwz) -> Chunk* {
            int ncx = std::floor((float)wwx / Config::CHUNK_SIZE);
            int ncz = std::floor((float)wwz / Config::CHUNK_SIZE);
            auto tit = snap.find({ncx, ncz});
            return (tit != snap.end() && tit->second->is_ready) ? tit->second.get() : nullptr;
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

        // Fall
        if (read_block(wx, wy - 1, wz) == Config::AIR) {
            edits.push_back({wx, wy, wz, 0});
            edits.push_back({wx, wy - 1, wz, L});
            add_neighborhood(wx, wy, wz);
            add_neighborhood(wx, wy - 1, wz);
            continue;
        }

        // Ocean source refill (below sea level never drains)
        if (wy <= (int)Config::WATER_LEVEL && L < 8) {
            edits.push_back({wx, wy, wz, 8});
            add_neighborhood(wx, wy, wz);
            continue;
        }

        // Horizontal spread (levels decay, flow terminates)
        if (L >= 2) {
            for (int d = 0; d < 4; ++d) {
                int nwx = wx + dirs[d][0];
                int nwz = wz + dirs[d][1];
                if (!get_target(nwx, nwz)) continue;
                if (read_block(nwx, wy, nwz) != Config::AIR) continue;
                uint8_t nl = read_level(nwx, wy, nwz);
                if (nl < L - 1) {
                    edits.push_back({nwx, wy, nwz, (uint8_t)(L - 1)});
                    add_neighborhood(nwx, wy, nwz);
                }
            }
        }
    }

    for (const auto& e : edits) {
        set_water_node(snap, e.x, e.y, e.z, e.level);
    }

    {
        std::lock_guard<std::mutex> lock(active_mutex);
        for (uint64_t k : next_active) {
            if (active_cells.size() < 5000) active_cells.push_back(k);
        }
    }
}

void World::set_water_node(const std::unordered_map<std::pair<int, int>, std::shared_ptr<Chunk>, pair_hash>& snap, int wx, int wy, int wz, uint8_t level) {
    if (wy < 0 || wy >= Config::GRID_Y) return;
    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;

    auto apply = [&](int ccx, int ccz, int llx, int llz) {
        auto it = snap.find({ccx, ccz});
        if (it != snap.end() && it->second->is_ready) it->second->set_water_node(llx, wy, llz, level);
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
            pair.second->draw_solid(mat_solid, camera.position);
        }
    }
    
    // Pass 2: Transparent geometry (Water)
    for (auto& pair : chunks) {
        if (is_visible(pair.second.get())) {
            pair.second->draw_water(mat_water, camera.position);
        }
    }
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
    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    
    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;
    
    auto update_chunk = [&](int c_x, int c_z, int l_x, int l_z) {
        Chunk* c = get_chunk(c_x, c_z);
        if (c) c->set_block(l_x, wy, l_z, type);
    };
    
    update_chunk(cx, cz, lx, lz);
    
    if (lx == 0) update_chunk(cx - 1, cz, CHUNK_SIZE, lz);
    if (lz == 0) update_chunk(cx, cz - 1, lx, CHUNK_SIZE);
    if (lx == 0 && lz == 0) update_chunk(cx - 1, cz - 1, CHUNK_SIZE, CHUNK_SIZE);

    activate(wx, wy, wz);
    activate(wx + 1, wy, wz);
    activate(wx - 1, wy, wz);
    activate(wx, wy, wz + 1);
    activate(wx, wy, wz - 1);
    activate(wx, wy + 1, wz);
    activate(wx, wy - 1, wz);
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
