#pragma once
#include <raylib.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>
#include "Config.hpp"

class Chunk {
public:
    int cx, cz;
    std::atomic<bool> is_ready{false};
    std::atomic<bool> is_dirty{false};
    std::atomic<bool> needs_upload{false};
    
    float* density_grid = nullptr;
    uint8_t* block_grid = nullptr;

    Mesh solid_mesh = { 0 };
    Mesh water_mesh = { 0 };
    Mesh plants_mesh = { 0 }; // For tall grass

    std::vector<Vector3> s_vertices, s_normals;
    std::vector<Vector2> s_uvs, s_uvs2;
    std::vector<Color> s_colors;

    std::vector<Vector3> w_vertices, w_normals;
    std::vector<Vector2> w_uvs, w_uvs2;
    std::vector<Color> w_colors;

    std::vector<Vector3> p_vertices, p_normals;
    std::vector<Vector2> p_uvs;
    std::vector<Color> p_colors;

    Chunk(int x, int z);
    ~Chunk();

    void start_generation();
    void update_logic();
    void draw_solid(Material& mat_solid, Vector3 camera_pos);
    void draw_water(Material& mat_water, Vector3 camera_pos);

    uint8_t get_block(int x, int y, int z) const;
    float get_density(int x, int y, int z) const;
    void set_block(int x, int y, int z, uint8_t type);
    void rebuild_mesh();
    void save_to_disk();

private:
    std::future<void> gen_future;
    std::future<void> rebuild_future;
    
    void generate_thread();
    void rebuild_thread();
    void build_mesh_data();
    void upload_meshes();
};
