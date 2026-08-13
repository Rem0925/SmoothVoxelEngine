#include "World.hpp"
#include <cmath>

sqlite3* db = nullptr;
std::mutex sqlite_mutex;
ThreadPool global_thread_pool(Config::MAX_WORKER_THREADS);
#include <cmath>
#include <raymath.h>
#include <algorithm>
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
