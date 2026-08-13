#pragma once
#include <unordered_map>
#include <sqlite3.h>
#include <mutex>
#include <memory>
#include "Chunk.hpp"
#include "ThreadPool.hpp"

extern ThreadPool global_thread_pool;

struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2> &p) const {
        return std::hash<T1>{}(p.first) ^ (std::hash<T2>{}(p.second) << 1);
    }
};

extern sqlite3* db;
extern std::mutex sqlite_mutex;

class World {
public:
    std::unordered_map<std::pair<int, int>, std::unique_ptr<Chunk>, pair_hash> chunks;
    Material mat_solid;
    Material mat_water;
    
    World(Material solid, Material water);
    ~World();

    void update(Vector3 player_pos);
    void draw(Camera3D camera);
    void save_all();

    uint8_t get_block(int wx, int wy, int wz);
    float get_density(int wx, int wy, int wz) const;
    void set_block(int wx, int wy, int wz, uint8_t type);
    
    Chunk* get_chunk(int cx, int cz);

private:
    void simulate_water();
    void set_water_node(int wx, int wy, int wz, uint8_t level);
    float water_timer = 0.0f;
};
