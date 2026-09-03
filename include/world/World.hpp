#pragma once
#include <unordered_map>
#include "core/sqlite3.h"
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include "world/Chunk.hpp"
#include "core/ThreadPool.hpp"

extern ThreadPool global_thread_pool;
extern class World* g_world;

struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2> &p) const {
        std::size_t h1 = std::hash<T1>{}(p.first);
        std::size_t h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

struct HammerArea {
    int min_dx, max_dx;
    int min_dz, max_dz;
    int max_h;
};

HammerArea get_hammer_area(int tier, Vector3 hit, int bx, int bz);

#include "world/FluidSimulation.hpp"

class World {
public:
    std::mutex chunks_mutex;
    Material mat_solid;
    Material mat_plants;
    Material mat_water;
    FluidSimulator fluid_sim;
    
    World(Material solid, Material plants, Material water);
    ~World();

    void update(Vector3 player_pos);
    void draw(Camera3D camera);
    void save_all();
    void stop_simulation();

    uint8_t get_block(int wx, int wy, int wz);
    uint8_t get_rotation(int wx, int wy, int wz);
    uint8_t get_light(int wx, int wy, int wz);
    float get_density(int wx, int wy, int wz) const;
    float sample_density_trilinear(float x, float y, float z) const;
    void set_block(int wx, int wy, int wz, uint8_t type, uint8_t rotation = 0);
    int flatten_terrain(int wx, int wy, int wz, const HammerArea& area, int tool_tier, class ItemDropManager* item_drops = nullptr);
    float get_hammer_mining_hardness(int wx, int wy, int wz, const HammerArea& area, int tool_tier);
    void invalidate_all_meshes();
    float get_spawn_surface_y(float x, float z);
    
    Chunk* get_chunk(int cx, int cz);
    std::shared_ptr<Chunk> get_chunk_shared(int cx, int cz) {
        std::lock_guard<std::mutex> lock(chunks_mutex);
        auto key = std::make_pair(cx, cz);
        if (chunks.find(key) != chunks.end()) {
            return chunks[key];
        }
        return nullptr;
    }

    const auto& get_chunks() const { return chunks; }
    size_t chunk_count() const { return chunks.size(); }
    void activate(int wx, int wy, int wz) { fluid_sim.activate(wx, wy, wz); }

private:
    friend class Chunk;
    friend class FluidSimulator;
    std::unordered_map<std::pair<int, int>, std::shared_ptr<Chunk>, pair_hash> chunks;
    int upload_budget = 2;
};
