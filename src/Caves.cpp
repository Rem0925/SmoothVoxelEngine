#include "Caves.hpp"
#include "Noise.hpp"
#include <cmath>
#include <algorithm>

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

void CaveMap::add_room(float x, float y, float z, float radius, float scale_y, bool can_break_surface, uint64_t& state) {
    insert(x, y, z, radius, scale_y, can_break_surface);
    float ox = (hash_unit(state) - 0.5f) * radius * 0.6f;
    float oy = (hash_unit(state) - 0.5f) * radius * 0.3f;
    float oz = (hash_unit(state) - 0.5f) * radius * 0.6f;
    insert(x + ox, y + oy, z + oz, radius * 0.85f, scale_y * 1.1f, can_break_surface);
}

void CaveMap::walk(const Worm& w, uint64_t& state, bool allow_branch) {
    const float STEP = 1.5f;
    float x = w.sx, y = w.sy, z = w.sz;
    float yaw = w.yaw, pitch = w.pitch;
    float travelled = 0.0f;

    bool branch = allow_branch && (hash_unit(state) < 0.35f);
    float branch_at = w.len * (0.35f + hash_unit(state) * 0.35f);
    bool branch_taken = false;

    while (travelled < w.len) {
        float n_r = (float)pnoise3(x * 0.08f + w.noise_r, y * 0.08f, z * 0.08f, 2, 0.5f);
        float scale_noise = (float)pnoise3(x * 0.045f + w.noise_a, y * 0.045f + w.noise_b, z * 0.045f, 2, 0.5f);
        
        // Escala vertical variable para evitar secciones circulares perfectas
        float scale_y = 1.0f + 0.40f * scale_noise; // Entre 0.60 (cueva ancha/plana) y 1.40 (grieta alta)

        float r = w.base_radius * (0.85f + 0.30f * n_r);
        if (r < 2.2f) r = 2.2f;
        if (r > 6.0f) r = 6.0f;
        insert(x, y, z, r, scale_y, w.can_break_surface);

        if (branch && !branch_taken && travelled >= branch_at) {
            branch_taken = true;
            Worm b = w;
            b.sx = x;
            b.sy = y;
            b.sz = z;
            b.yaw = yaw + (hash_unit(state) - 0.5f) * 1.4f;
            b.pitch = (hash_unit(state) - 0.5f) * 0.7f;
            b.len = w.len * (0.40f + hash_unit(state) * 0.30f);
            b.base_radius = std::max(2.2f, w.base_radius * 0.80f);
            b.turn_rate = w.turn_rate * 1.15f;
            b.noise_a = hash_unit(state) * 200.0f;
            b.noise_b = hash_unit(state) * 200.0f;
            b.noise_r = hash_unit(state) * 200.0f;
            b.can_break_surface = w.can_break_surface && (hash_unit(state) < 0.5f);
            walk(b, state, false);
        }

        if (y < 6.0f) pitch = std::fabs(pitch);
        if (y > 65.0f) pitch = -std::fabs(pitch);

        float ny = (float)pnoise3(x * 0.055f + w.noise_a, y * 0.055f + w.noise_b, z * 0.055f, 3, 0.5f);
        float np = (float)pnoise3(x * 0.055f + w.noise_b + 71.0f, y * 0.055f + w.noise_a, z * 0.055f + 13.0f, 3, 0.5f);
        yaw += ny * w.turn_rate;
        pitch += np * w.turn_rate * 0.8f;
        if (pitch > 1.0f) pitch = 1.0f;
        if (pitch < -1.0f) pitch = -1.0f;

        x += std::cos(yaw) * std::cos(pitch) * STEP;
        y += std::sin(pitch) * STEP;
        z += std::sin(yaw) * std::cos(pitch) * STEP;
        travelled += STEP;
    }

    // Remate de cueva: sala terminal natural y proporcionada
    add_room(x, y, z, std::min(5.5f, w.base_radius * 1.45f), 1.1f, w.can_break_surface, state);
}

void CaveMap::insert(float x, float y, float z, float radius, float scale_y, bool can_break_surface) {
    int cx = (int)std::floor((x - gx0) / CELL);
    int cy = (int)std::floor((y - gy0) / CELL);
    int cz = (int)std::floor((z - gz0) / CELL);
    if (cx < 0 || cy < 0 || cz < 0 || cx >= gnx || cy >= gny || cz >= gnz) return;
    cells[((size_t)cy * gnz + cz) * gnx + cx].push_back({x, y, z, radius, scale_y, can_break_surface});
}

void CaveMap::generate(int chunk_cx, int chunk_cz, uint32_t world_seed) {
    cells.clear();

    const float MAX_REACH = 65.0f;
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

            // Probabilidad realista de inicio de cueva por chunk (~14%)
            if (hash_unit(state) >= 0.14f) continue;

            int num_worms = (hash_unit(state) < 0.25f) ? 2 : 1;
            for (int nw = 0; nw < num_worms; ++nw) {
                float start_y = 10.0f + hash_unit(state) * 45.0f;
                Worm w;
                w.sx = ocx * CHUNK + hash_unit(state) * CHUNK;
                w.sz = ocz * CHUNK + hash_unit(state) * CHUNK;
                w.sy = start_y;
                w.yaw = hash_unit(state) * 6.2831853f;
                w.pitch = (hash_unit(state) - 0.5f) * 0.4f;
                w.len = 40.0f + hash_unit(state) * 65.0f;
                w.base_radius = 2.3f + hash_unit(state) * 1.3f;
                w.turn_rate = 0.26f + hash_unit(state) * 0.20f;
                w.noise_a = hash_unit(state) * 200.0f;
                w.noise_b = hash_unit(state) * 200.0f;
                w.noise_r = hash_unit(state) * 200.0f;
                // Solo ~20% de los túneles tienen permiso para abrir boca hacia la superficie
                w.can_break_surface = (hash_unit(state) < 0.20f);
                walk(w, state, true);
            }
        }
    }
}

bool CaveMap::is_cave_at(float wx, float wy, float wz, float base_h) const {
    if (wy <= 2.0f) return false;

    int cx = (int)std::floor((wx - gx0) / CELL);
    int cy = (int)std::floor((wy - gy0) / CELL);
    int cz = (int)std::floor((wz - gz0) / CELL);

    float depth = base_h - wy;

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
                    float ddx = wx - s.x;
                    float ddy = (wy - s.y) / s.scale_y;
                    float ddz = wz - s.z;
                    float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
                    float r2 = s.radius * s.radius;
                    
                    if (d2 < r2 * 1.35f) {
                        // Rugosidad orgánica en las paredes para romper la simetría de tubo perfecto
                        float wall_noise = (float)pnoise3((double)wx * 0.12, (double)wy * 0.12, (double)wz * 0.12, 2, 0.5);
                        float effective_r = s.radius * (0.88f + 0.24f * wall_noise);
                        
                        if (d2 < effective_r * effective_r) {
                            // Si el túnel no tiene permiso de abrir a superficie, proteger la corteza
                            if (!s.can_break_surface && depth < 4.5f) {
                                continue;
                            }
                            // Proteger lagos y océanos contra inundación
                            if (base_h <= 38.0f + 2.0f && wy >= base_h - 4.0f) {
                                continue;
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

} // namespace Caves