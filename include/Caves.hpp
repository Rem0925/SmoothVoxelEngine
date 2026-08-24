#pragma once
#include <vector>
#include <cstdint>

namespace Caves {
    struct Segment {
        float x, y, z;
        float radius;
        float scale_y;
        bool can_break_surface;
    };

    struct Worm {
        float sx, sy, sz;
        float yaw, pitch;
        float len;
        float base_radius;
        float turn_rate;
        float noise_a, noise_b, noise_r;
        bool can_break_surface;
    };

    class CaveMap {
    public:
        void generate(int chunk_cx, int chunk_cz, uint32_t world_seed);
        bool is_cave_at(float wx, float wy, float wz, float base_h) const;

    private:
        static constexpr int CELL = 6;
        static constexpr int CHUNK = 16;

        int gx0 = 0, gy0 = 0, gz0 = 0;
        int gnx = 0, gny = 0, gnz = 0;
        std::vector<std::vector<Segment>> cells;

        void insert(float x, float y, float z, float radius, float scale_y, bool can_break_surface);
        void add_room(float x, float y, float z, float radius, float scale_y, bool can_break_surface, uint64_t& state);
        void walk(const Worm& w, uint64_t& state, bool allow_branch);
    };
}