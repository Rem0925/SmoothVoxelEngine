#include "Caves.hpp"
#include "Noise.hpp"
#include <cmath>

namespace Caves {

static inline uint64_t splitmix64(uint64_t& x) {
    x += 0x9E3779B97F4A7C15ULL;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline float hash_unit(uint64_t& state) {
    return (float)((splitmix64(state) >> 11) * (1.0 / 9007199254740992.0));
}

void CaveMap::walk(const Worm& w, uint64_t& state, bool allow_branch) {
    const float STEP = 1.5f;
    float x = w.sx, y = w.sy, z = w.sz;
    float yaw = w.yaw, pitch = w.pitch;
    float travelled = 0.0f;

    bool branch = allow_branch && (hash_unit(state) < 0.35f);
    float branch_at = w.len * (0.4f + hash_unit(state) * 0.3f);
    bool branch_taken = false;

    while (travelled < w.len) {
        float n_r = (float)pnoise3(x * 0.08f + w.noise_r, y * 0.08f, z * 0.08f, 2, 0.5f);
        float n_room = (float)pnoise3(x * 0.022f + w.noise_r + 31.0f, y * 0.022f, z * 0.022f + 17.0f, 2, 0.5f);
        float r = w.base_radius * (0.60f + 0.40f * n_r);
        if (n_room > 0.0f) r *= 1.0f + 2.2f * n_room;
        if (r < 2.0f) r = 2.0f;
        if (r > 12.0f) r = 12.0f;
        insert(x, y, z, r);

        if (branch && !branch_taken && travelled >= branch_at) {
            branch_taken = true;
            Worm b = w;
            b.sx = x;
            b.sy = y;
            b.sz = z;
            b.yaw = yaw + (hash_unit(state) - 0.5f) * 1.6f;
            b.pitch = (hash_unit(state) - 0.5f) * 0.9f;
            b.len = w.len * (0.30f + hash_unit(state) * 0.30f);
            b.base_radius = w.base_radius * 0.7f;
            b.turn_rate = w.turn_rate * 1.15f;
            b.noise_a = hash_unit(state) * 200.0f;
            b.noise_b = hash_unit(state) * 200.0f;
            b.noise_r = hash_unit(state) * 200.0f;
            walk(b, state, false);
        }

        if (y < 3.0f) pitch = std::fabs(pitch);
        if (y > 72.0f) pitch = -std::fabs(pitch);

        float ny = (float)pnoise3(x * 0.055f + w.noise_a, y * 0.055f + w.noise_b, z * 0.055f, 3, 0.5f);
        float np = (float)pnoise3(x * 0.055f + w.noise_b + 71.0f, y * 0.055f + w.noise_a, z * 0.055f + 13.0f, 3, 0.5f);
        yaw += ny * w.turn_rate;
        pitch += np * w.turn_rate * 0.8f;
        if (pitch > 1.1f) pitch = 1.1f;
        if (pitch < -1.1f) pitch = -1.1f;

        x += std::cos(yaw) * std::cos(pitch) * STEP;
        y += std::sin(pitch) * STEP;
        z += std::sin(yaw) * std::cos(pitch) * STEP;
        travelled += STEP;
    }
}

void CaveMap::insert(float x, float y, float z, float radius) {
    int cx = (int)std::floor((x - gx0) / CELL);
    int cy = (int)std::floor((y - gy0) / CELL);
    int cz = (int)std::floor((z - gz0) / CELL);
    if (cx < 0 || cy < 0 || cz < 0 || cx >= gnx || cy >= gny || cz >= gnz) return;
    cells[((size_t)cy * gnz + cz) * gnx + cx].push_back({x, y, z, radius});
}

void CaveMap::generate(int chunk_cx, int chunk_cz, uint32_t world_seed) {
    cells.clear();

    const float MAX_REACH = 115.0f;
    int cell_range = (int)std::ceil(MAX_REACH / CHUNK);

    int wx0 = chunk_cx * CHUNK - 8;
    int wz0 = chunk_cz * CHUNK - 8;
    int wy0 = -8;
    int wx1 = chunk_cx * CHUNK + CHUNK + 8;
    int wz1 = chunk_cz * CHUNK + CHUNK + 8;
    int wy1 = 128 + 8;

    gx0 = wx0;
    gy0 = wy0;
    gz0 = wz0;
    gnx = (wx1 - wx0) / CELL;
    gny = (wy1 - wy0) / CELL;
    gnz = (wz1 - wz0) / CELL;
    cells.resize((size_t)gnx * gny * gnz);

    for (int ocx = chunk_cx - cell_range; ocx <= chunk_cx + cell_range; ++ocx) {
        for (int ocz = chunk_cz - cell_range; ocz <= chunk_cz + cell_range; ++ocz) {
            uint64_t state = (uint64_t)world_seed * 0x9E3779B97F4A7C15ULL;
            state ^= (uint64_t)(uint32_t)ocx * 0xBF58476D1CE4E5B9ULL;
            state ^= (uint64_t)(uint32_t)ocz * 0x94D049BB133111EBULL;
            state = splitmix64(state);

            float start_y = 6.0f + hash_unit(state) * 54.0f;
            float depth_chance = 0.15f + 0.35f * ((start_y - 6.0f) / 54.0f);
            if (hash_unit(state) >= depth_chance) continue;

            auto spawn_worm = [&]() {
                Worm w;
                w.sx = ocx * CHUNK + hash_unit(state) * CHUNK;
                w.sz = ocz * CHUNK + hash_unit(state) * CHUNK;
                w.sy = start_y;
                w.yaw = hash_unit(state) * 6.2831853f;
                w.pitch = (hash_unit(state) - 0.5f) * 0.4f;
                w.len = 45.0f + hash_unit(state) * 75.0f;
                w.base_radius = 2.4f + hash_unit(state) * 2.6f;
                w.turn_rate = 0.30f + hash_unit(state) * 0.25f;
                w.noise_a = hash_unit(state) * 200.0f;
                w.noise_b = hash_unit(state) * 200.0f;
                w.noise_r = hash_unit(state) * 200.0f;
                walk(w, state, true);
            };
            spawn_worm();
            if (hash_unit(state) < 0.30f) spawn_worm();
        }
    }
}

float CaveMap::carve_at(float wx, float wy, float wz) const {
    int cx = (int)std::floor((wx - gx0) / CELL);
    int cy = (int)std::floor((wy - gy0) / CELL);
    int cz = (int)std::floor((wz - gz0) / CELL);

    float best = 0.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        int zz = cz + dz;
        if (zz < 0 || zz >= gnz) continue;
        for (int dy = -1; dy <= 1; ++dy) {
            int yy = cy + dy;
            if (yy < 0 || yy >= gny) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                int xx = cx + dx;
                if (xx < 0 || xx >= gnx) continue;
                const auto& segs = cells[((size_t)yy * gnz + zz) * gnx + xx];
                for (const auto& s : segs) {
                    float ddx = wx - s.x, ddy = wy - s.y, ddz = wz - s.z;
                    float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
                    float r2 = s.radius * s.radius;
                    if (d2 < r2) {
                        float t = 1.0f - std::sqrt(d2) / s.radius;
                        float carve = t * t * (3.0f - 2.0f * t);
                        if (carve > best) {
                            best = carve;
                            if (best >= 0.999f) return best;
                        }
                    }
                }
            }
        }
    }
    return best;
}

} // namespace Caves