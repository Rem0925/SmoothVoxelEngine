#include "World.hpp"
#include <cmath>

sqlite3* db = nullptr;
std::mutex sqlite_mutex;
#include <cmath>

using namespace Config;

World::World(Material solid, Material water) : mat_solid(solid), mat_water(water) {
}

World::~World() {
}

void World::update(Vector3 player_pos) {
    int pcx = std::floor(player_pos.x / CHUNK_SIZE);
    int pcz = std::floor(player_pos.z / CHUNK_SIZE);
    
    int load_radius = 4;
    
    for (auto it = chunks.begin(); it != chunks.end();) {
        int cx = it->first.first;
        int cz = it->first.second;
        if (std::abs(cx - pcx) > load_radius + 1 || std::abs(cz - pcz) > load_radius + 1) {
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }
    
    int chunks_started = 0;
    
    for (int x = -load_radius; x <= load_radius; x++) {
        for (int z = -load_radius; z <= load_radius; z++) {
            int cx = pcx + x;
            int cz = pcz + z;
            
            auto key = std::make_pair(cx, cz);
            if (chunks.find(key) == chunks.end()) {
                if (chunks_started < 2) {
                    chunks[key] = std::make_unique<Chunk>(cx, cz);
                    chunks[key]->start_generation();
                    chunks_started++;
                }
            } else {
                chunks[key]->update_logic();
            }
        }
    }
}

void World::draw(Vector3 camera_pos) {
    // Pass 1: Opaque geometry (Solid and Plants)
    for (auto& pair : chunks) {
        pair.second->draw_solid(mat_solid, camera_pos);
    }
    
    // Pass 2: Transparent geometry (Water)
    for (auto& pair : chunks) {
        pair.second->draw_water(mat_water, camera_pos);
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
