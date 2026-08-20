#pragma once
#include <raylib.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>
#include "Config.hpp"

class Chunk : public std::enable_shared_from_this<Chunk> {
public:
    int cx, cz;
    std::atomic<bool> is_ready{false};
    std::atomic<bool> is_dirty{false};
    std::atomic<bool> needs_save{false};
    std::atomic<bool> needs_upload{false};
    std::atomic<int> current_lod{1};
    std::atomic<int> rebuild_mode{0};
    std::atomic<bool> rebuild_running{false};
    std::atomic<bool> water_only_rebuild{false};
    std::atomic<int> pending_upload_mask{0};
    std::atomic<bool> generating{false};
    
    std::mutex chunk_mutex;
    std::mutex rebuild_mutex;
    std::mutex mesh_mutex;

    std::vector<Config::VoxelData> voxels;

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

    static void flush_gl_delete_queue();

    void start_generation();
    void update_logic(int& upload_budget);
    void draw_solid(Material& mat_solid, Material& mat_plants, Vector3 camera_pos);
    void draw_water(Material& mat_water, Vector3 camera_pos);

    uint8_t get_block(int x, int y, int z) const;
    float get_density(int x, int y, int z) const;
    void set_block(int x, int y, int z, uint8_t type);
    uint8_t get_water_level(int x, int y, int z) const;
    void set_water_node(int x, int y, int z, uint8_t level);
    void rebuild_mesh(bool water_only = false);
    void save_to_disk();

private:
    std::future<void> gen_future;

    struct PendingEdit {
        int x, y, z;
        uint8_t type;
    };
    std::vector<PendingEdit> pending_edits;

    static inline std::mutex gl_queue_mutex;
    static inline std::vector<Mesh> gl_delete_queue;

    void generate_thread();
    void rebuild_thread();
    void build_mesh_data(const Config::VoxelData* voxels_ptr, int lod = 1);
    void build_water_mesh(const Config::VoxelData* voxels_ptr, int lod = 1);
    void pack_meshes(int mask);
    void upload_meshes();
};
