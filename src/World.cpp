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

using namespace Config;

World::World(Material solid, Material water) : mat_solid(solid), mat_water(water) {
}

World::~World() {
}

void World::update(Vector3 player_pos) {
    int pcx = std::floor(player_pos.x / CHUNK_SIZE);
    int pcz = std::floor(player_pos.z / CHUNK_SIZE);
    
    int load_radius = Config::RENDER_DISTANCE;
    
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
            chunks[key] = std::make_unique<Chunk>(cx, cz);
            chunks[key]->start_generation();
        } else {
            chunks[key]->update_logic();
        }
    }
    
    water_timer += GetFrameTime();
    if (water_timer >= 0.15f) {
        water_timer = 0.0f;
        simulate_water();
    }
}

struct WaterEdit {
    int x, y, z;
    uint8_t level;
};

void World::simulate_water() {
    std::vector<WaterEdit> edits;
    edits.reserve(1024);

    for (auto& pair : chunks) {
        Chunk* c = pair.second.get();
        if (!c->is_ready) continue;
        if (edits.size() > 2000) break;

        int cx = c->cx;
        int cz = c->cz;
        Chunk* nx_p = get_chunk(cx + 1, cz);
        Chunk* nx_n = get_chunk(cx - 1, cz);
        Chunk* nz_p = get_chunk(cx, cz + 1);
        Chunk* nz_n = get_chunk(cx, cz - 1);

        auto read_block = [&](int wx, int wy, int wz) -> uint8_t {
            if (wy < 0 || wy >= Config::GRID_Y) return Config::AIR;
            int lx = wx - cx * Config::CHUNK_SIZE;
            int lz = wz - cz * Config::CHUNK_SIZE;
            Chunk* target = c;
            if (lx < 0) { target = nx_n; lx += Config::CHUNK_SIZE; }
            else if (lx > Config::CHUNK_SIZE) { target = nx_p; lx -= Config::CHUNK_SIZE; }
            if (lz < 0) { target = nz_n; lz += Config::CHUNK_SIZE; }
            else if (lz > Config::CHUNK_SIZE) { target = nz_p; lz -= Config::CHUNK_SIZE; }
            if (!target || !target->is_ready) return Config::AIR;
            return target->get_block(lx, wy, lz);
        };

        auto read_level = [&](int wx, int wy, int wz) -> uint8_t {
            if (wy < 0 || wy >= Config::GRID_Y) return 0;
            int lx = wx - cx * Config::CHUNK_SIZE;
            int lz = wz - cz * Config::CHUNK_SIZE;
            Chunk* target = c;
            if (lx < 0) { target = nx_n; lx += Config::CHUNK_SIZE; }
            else if (lx > Config::CHUNK_SIZE) { target = nx_p; lx -= Config::CHUNK_SIZE; }
            if (lz < 0) { target = nz_n; lz += Config::CHUNK_SIZE; }
            else if (lz > Config::CHUNK_SIZE) { target = nz_p; lz -= Config::CHUNK_SIZE; }
            if (!target || !target->is_ready) return 0;
            return target->get_water_level(lx, wy, lz);
        };

        const int stride = Config::CHUNK_SIZE + 1;
        for (int y = 1; y < Config::GRID_Y; ++y) {
            for (int lz = 0; lz <= Config::CHUNK_SIZE; ++lz) {
                for (int lx = 0; lx <= Config::CHUNK_SIZE; ++lx) {
                    int i = y * stride * stride + lz * stride + lx;
                    uint8_t L = c->water_level[i];
                    if (L == 0) continue;

                    int wx = cx * Config::CHUNK_SIZE + lx;
                    int wz = cz * Config::CHUNK_SIZE + lz;

                    // Fall
                    if (read_block(wx, y - 1, wz) == Config::AIR) {
                        edits.push_back({wx, y, wz, 0});
                        edits.push_back({wx, y - 1, wz, L});
                        continue;
                    }

                    // Ocean source refill (below sea level never drains)
                    if (y <= (int)Config::WATER_LEVEL && L < 8) {
                        edits.push_back({wx, y, wz, 8});
                        continue;
                    }

                    // Horizontal spread (levels decay, flow terminates)
                    if (L >= 2) {
                        const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
                        for (int d = 0; d < 4; ++d) {
                            int nwx = wx + dirs[d][0];
                            int nwz = wz + dirs[d][1];
                            int ncx = std::floor((float)nwx / Config::CHUNK_SIZE);
                            int ncz = std::floor((float)nwz / Config::CHUNK_SIZE);
                            Chunk* target_chunk = get_chunk(ncx, ncz);
                            if (!target_chunk || !target_chunk->is_ready) continue;
                            if (read_block(nwx, y, nwz) != Config::AIR) continue;
                            uint8_t nl = read_level(nwx, y, nwz);
                            if (nl < L - 1) {
                                edits.push_back({nwx, y, nwz, (uint8_t)(L - 1)});
                            }
                        }
                    }
                }
            }
        }
    }

    for (const auto& e : edits) {
        set_water_node(e.x, e.y, e.z, e.level);
    }
}

void World::set_water_node(int wx, int wy, int wz, uint8_t level) {
    if (wy < 0 || wy >= Config::GRID_Y) return;
    int cx = std::floor((float)wx / CHUNK_SIZE);
    int cz = std::floor((float)wz / CHUNK_SIZE);
    int lx = wx - cx * CHUNK_SIZE;
    int lz = wz - cz * CHUNK_SIZE;

    auto apply = [&](int ccx, int ccz, int llx, int llz) {
        Chunk* ch = get_chunk(ccx, ccz);
        if (ch && ch->is_ready) ch->set_water_node(llx, wy, llz, level);
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
