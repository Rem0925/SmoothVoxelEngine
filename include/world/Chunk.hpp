#pragma once
#include <raylib.h>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>
#include "core/Config.hpp"

struct SubChunk {
    Mesh solid_mesh = { 0 };
    Mesh build_mesh = { 0 };
    Mesh water_mesh = { 0 };
    Mesh plants_mesh = { 0 };

    Mesh next_solid_mesh = { 0 };
    Mesh next_build_mesh = { 0 };
    Mesh next_water_mesh = { 0 };
    Mesh next_plants_mesh = { 0 };

    std::vector<Vector3> s_vertices, s_normals;
    std::vector<Vector2> s_uvs, s_uvs2;
    std::vector<Color> s_colors;

    std::vector<Vector3> b_vertices, b_normals;
    std::vector<Vector2> b_uvs;
    std::vector<Color> b_colors;

    std::vector<Vector3> w_vertices, w_normals;
    std::vector<Vector2> w_uvs, w_uvs2;
    std::vector<Color> w_colors;

    std::vector<Vector3> p_vertices, p_normals;
    std::vector<Vector2> p_uvs;
    std::vector<Color> p_colors;
};

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
    std::atomic<uint8_t> dirty_subchunks_mask{0xFF};
    std::atomic<uint8_t> pending_subchunks_upload_mask{0xFF};
    std::atomic<bool> generating{false};
    
    std::mutex chunk_mutex;
    std::mutex rebuild_mutex;
    std::mutex mesh_mutex;

    std::vector<Config::VoxelData> voxels;
    std::vector<uint8_t> light_grid;

    std::array<SubChunk, Config::NUM_SUBCHUNKS> subchunks;

    Chunk(int x, int z);
    ~Chunk();

    static void flush_gl_delete_queue();

    void start_generation();
    void update_logic(int& upload_budget);
    void draw_solid(Material& mat_solid, Material& mat_plants, Vector3 camera_pos);
    void draw_water(Material& mat_water, Vector3 camera_pos);
    void draw_subchunk_solid(int s, Material& mat_solid, Material& mat_plants, Vector3 camera_pos);
    void draw_subchunk_water(int s, Material& mat_water, Vector3 camera_pos);

    inline int get_idx(int x, int y, int z) const {
        return y * (Config::CHUNK_SIZE + 1) * (Config::CHUNK_SIZE + 1) + z * (Config::CHUNK_SIZE + 1) + x;
    }

    inline uint8_t get_block(int x, int y, int z) const {
        if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return Config::AIR;
        return voxels[get_idx(x, y, z)].block;
    }

    inline float get_density(int x, int y, int z) const {
        if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return -1.0f;
        return voxels[get_idx(x, y, z)].density;
    }

    inline uint8_t get_water_level(int x, int y, int z) const {
        if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return 0;
        return voxels[get_idx(x, y, z)].water;
    }

    inline uint8_t get_light(int x, int y, int z) const {
        if (x < 0 || x > Config::CHUNK_SIZE || y < 0 || y >= Config::GRID_Y || z < 0 || z > Config::CHUNK_SIZE) return 0;
        if (light_grid.empty()) return 0;
        int idx = get_idx(x, y, z);
        if (idx < 0 || idx >= (int)light_grid.size()) return 0;
        return light_grid[idx];
    }

    void set_block(int x, int y, int z, uint8_t type, uint8_t rotation = 0);
    uint8_t get_rotation(int x, int y, int z);
    void set_water_node(int x, int y, int z, uint8_t level);
    void rebuild_mesh(bool water_only = false, uint8_t sub_mask = 0xFF);
    void save_to_disk();

private:
    std::future<void> gen_future;

    struct PendingEdit {
        int x, y, z;
        uint8_t type;
        uint8_t rotation;
    };
    std::vector<PendingEdit> pending_edits;

    static inline std::mutex gl_queue_mutex;
    static inline std::vector<Mesh> gl_delete_queue;

    void generate_thread();
    void rebuild_thread();
    void build_mesh_data(const Config::VoxelData* voxels_ptr, int lod = 1, uint8_t sub_mask = 0xFF);
    void build_construction_mesh(const Config::VoxelData* voxels_ptr, const uint8_t* light_grid = nullptr, uint8_t sub_mask = 0xFF);
    void build_water_mesh(const Config::VoxelData* voxels_ptr, int lod = 1, uint8_t sub_mask = 0xFF);
    void pack_meshes(int mask, uint8_t sub_mask = 0xFF);
    void upload_meshes();
};
