#include "VoxelLighting.hpp"
#include <queue>
#include <cmath>
#include <algorithm>

namespace VoxelLighting {

struct LightNode {
    int16_t x, y, z;
    uint8_t val;
};


void compute_chunk_lighting(const LightCache& cache, uint8_t* out_light_grid) {
    if (!out_light_grid) return;
    
    const int PAD = 15;
    const int p_size = Config::CHUNK_SIZE + 1 + 2 * PAD; // 47
    const int total_cells = p_size * Config::GRID_Y * p_size;
    std::vector<uint8_t> p_grid(total_cells, 0);

    auto get_idx = [&](int px, int py, int pz) -> int {
        return py * (p_size * p_size) + pz * p_size + px;
    };

    auto get_light_filter = [&](int px, int py, int pz) -> uint8_t {
        if (px < 0 || px >= p_size || py < 0 || py >= Config::GRID_Y || pz < 0 || pz >= p_size) return 15;
        
        int wx = cache.base_cx * Config::CHUNK_SIZE + px - PAD;
        int wy = py;
        int wz = cache.base_cz * Config::CHUNK_SIZE + pz - PAD;
        
        uint8_t b = cache.get_block(wx, wy, wz);
        float d = cache.get_density(wx, wy, wz);
        
        if (b == Config::AIR && d < Config::ISO_SURFACE) return 0;
        if (b == Config::WATER) return 1;
        if (b == Config::TALL_GRASS || b == Config::TORCH || b == Config::RED_MUSHROOM || b == Config::BROWN_MUSHROOM || b == Config::DEAD_BUSH) return 0;

        if (Config::BLOCKS.count(b)) {
            const auto& bt = Config::BLOCKS.at(b);
            if (bt.shape == Config::SHAPE_TERRAIN && d >= Config::ISO_SURFACE) {
                return bt.light_filter;
            }
            return bt.light_filter;
        }

        return (d >= Config::ISO_SURFACE) ? 15 : 0;
    };

    std::queue<LightNode> sun_queue;
    std::queue<LightNode> block_queue;

    // 1. Barrido vertical de luz solar directa
    for (int pz = 0; pz < p_size; ++pz) {
        for (int px = 0; px < p_size; ++px) {
            uint8_t cur_sun = 15;
            for (int py = Config::GRID_Y - 1; py >= 0; --py) {
                int idx = get_idx(px, py, pz);
                uint8_t filter = get_light_filter(px, py, pz);
                if (cur_sun > 0) {
                    p_grid[idx] = pack_light(cur_sun, 0);
                    cur_sun = (cur_sun > filter) ? (cur_sun - filter) : 0;
                }
            }
        }
    }

    static const int DX[6] = { 1, -1, 0,  0, 0,  0 };
    static const int DY[6] = { 0,  0, 1, -1, 0,  0 };
    static const int DZ[6] = { 0,  0, 0,  0, 1, -1 };

    // 1.1 Encolar UNICAMENTE nodos frontera de luz solar que realmente puedan propagarse horizontalmente
    for (int py = 0; py < Config::GRID_Y; ++py) {
        for (int pz = 0; pz < p_size; ++pz) {
            for (int px = 0; px < p_size; ++px) {
                int idx = get_idx(px, py, pz);
                uint8_t cur_sun = get_sunlight(p_grid[idx]);
                if (cur_sun <= 1) continue;

                bool can_spread = false;
                for (int d = 0; d < 6; ++d) {
                    int nx = px + DX[d];
                    int ny = py + DY[d];
                    int nz = pz + DZ[d];
                    if (nx >= 0 && nx < p_size && ny >= 0 && ny < Config::GRID_Y && nz >= 0 && nz < p_size) {
                        uint8_t n_sun = get_sunlight(p_grid[get_idx(nx, ny, nz)]);
                        if (n_sun < cur_sun - 1) {
                            can_spread = true;
                            break;
                        }
                    }
                }
                if (can_spread) {
                    sun_queue.push({ (int16_t)px, (int16_t)py, (int16_t)pz, cur_sun });
                }
            }
        }
    }

    // 2. Fuentes de luz
    for (int py = 0; py < Config::GRID_Y; ++py) {
        for (int pz = 0; pz < p_size; ++pz) {
            for (int px = 0; px < p_size; ++px) {
                int wx = cache.base_cx * Config::CHUNK_SIZE + px - PAD;
                int wy = py;
                int wz = cache.base_cz * Config::CHUNK_SIZE + pz - PAD;
                
                uint8_t b = cache.get_block(wx, wy, wz);
                uint8_t emit = 0;
                if (Config::BLOCKS.count(b)) {
                    emit = Config::BLOCKS.at(b).light_emission;
                }
                if (b == Config::TORCH && emit == 0) emit = 14;

                if (emit > 0) {
                    int idx = get_idx(px, py, pz);
                    uint8_t cur_sun = get_sunlight(p_grid[idx]);
                    p_grid[idx] = pack_light(cur_sun, emit);
                    block_queue.push({ (int16_t)px, (int16_t)py, (int16_t)pz, emit });
                }
            }
        }
    }

    // 3. Propagación BFS Sol

    while (!sun_queue.empty()) {
        LightNode node = sun_queue.front();
        sun_queue.pop();
        if (node.val <= 1) continue;

        for (int d = 0; d < 6; ++d) {
            int nx = node.x + DX[d];
            int ny = node.y + DY[d];
            int nz = node.z + DZ[d];

            if (nx >= 0 && nx < p_size && ny >= 0 && ny < Config::GRID_Y && nz >= 0 && nz < p_size) {
                uint8_t filter = get_light_filter(nx, ny, nz);
                if (filter >= 15) continue;
                uint8_t step_loss = 1 + filter;
                if (node.val <= step_loss) continue;
                uint8_t next_val = node.val - step_loss;

                int nidx = get_idx(nx, ny, nz);
                uint8_t cur_sun = get_sunlight(p_grid[nidx]);
                if (cur_sun < next_val) {
                    uint8_t cur_block = get_blocklight(p_grid[nidx]);
                    p_grid[nidx] = pack_light(next_val, cur_block);
                    sun_queue.push({ (int16_t)nx, (int16_t)ny, (int16_t)nz, next_val });
                }
            }
        }
    }

    // 4. Propagación BFS Bloque
    while (!block_queue.empty()) {
        LightNode node = block_queue.front();
        block_queue.pop();
        if (node.val <= 1) continue;

        for (int d = 0; d < 6; ++d) {
            int nx = node.x + DX[d];
            int ny = node.y + DY[d];
            int nz = node.z + DZ[d];

            if (nx >= 0 && nx < p_size && ny >= 0 && ny < Config::GRID_Y && nz >= 0 && nz < p_size) {
                uint8_t filter = get_light_filter(nx, ny, nz);
                if (filter >= 15) continue;
                uint8_t step_loss = 1 + filter;
                if (node.val <= step_loss) continue;
                uint8_t next_val = node.val - step_loss;

                int nidx = get_idx(nx, ny, nz);
                uint8_t cur_block = get_blocklight(p_grid[nidx]);
                if (cur_block < next_val) {
                    uint8_t cur_sun = get_sunlight(p_grid[nidx]);
                    p_grid[nidx] = pack_light(cur_sun, next_val);
                    block_queue.push({ (int16_t)nx, (int16_t)ny, (int16_t)nz, next_val });
                }
            }
        }
    }

    // 5. Copiar resultado central al chunk
    int c_size = Config::CHUNK_SIZE + 1;
    for (int z = 0; z < c_size; ++z) {
        for (int x = 0; x < c_size; ++x) {
            for (int y = 0; y < Config::GRID_Y; ++y) {
                int p_idx = get_idx(x + PAD, y, z + PAD);
                int out_idx = y * (c_size * c_size) + z * c_size + x;
                out_light_grid[out_idx] = p_grid[p_idx];
            }
        }
    }
}

LightSample sample_smooth_light(const uint8_t* light_grid, int size_x, int size_y, int size_z, float x, float y, float z, Vector3 normal) {
    if (!light_grid) return { 1.0f, 0.0f };

    // Desplazar el punto de muestreo a lo largo de la normal hacia el aire
    // para evitar interpolar con el interior negro de la tierra en laderas
    float sx = std::clamp(x + normal.x * 0.45f, 0.0f, (float)(size_x - 1.001f));
    float sy = std::clamp(y + normal.y * 0.45f, 0.0f, (float)(size_y - 1.001f));
    float sz = std::clamp(z + normal.z * 0.45f, 0.0f, (float)(size_z - 1.001f));

    int ix = std::clamp((int)std::floor(sx), 0, size_x - 2);
    int iy = std::clamp((int)std::floor(sy), 0, size_y - 2);
    int iz = std::clamp((int)std::floor(sz), 0, size_z - 2);
    float fx = sx - (float)ix;
    float fy = sy - (float)iy;
    float fz = sz - (float)iz;

    int slice = size_x * size_z;
    auto get_l = [&](int cx, int cy, int cz) -> std::pair<float, float> {
        uint8_t packed = light_grid[cy * slice + cz * size_x + cx];
        return { (float)get_sunlight(packed) / 15.0f, (float)get_blocklight(packed) / 15.0f };
    };

    auto l000 = get_l(ix,   iy,   iz);
    auto l100 = get_l(ix+1, iy,   iz);
    auto l010 = get_l(ix,   iy+1, iz);
    auto l110 = get_l(ix+1, iy+1, iz);
    auto l001 = get_l(ix,   iy,   iz+1);
    auto l101 = get_l(ix+1, iy,   iz+1);
    auto l011 = get_l(ix,   iy+1, iz+1);
    auto l111 = get_l(ix+1, iy+1, iz+1);

    auto interp = [&](float a000, float a100, float a010, float a110,
                      float a001, float a101, float a011, float a111) -> float {
        float c00 = a000 * (1.0f - fx) + a100 * fx;
        float c10 = a010 * (1.0f - fx) + a110 * fx;
        float c01 = a001 * (1.0f - fx) + a101 * fx;
        float c11 = a011 * (1.0f - fx) + a111 * fx;
        float c0  = c00  * (1.0f - fz) + c01  * fz;
        float c1  = c10  * (1.0f - fz) + c11  * fz;
        return c0 * (1.0f - fy) + c1 * fy;
    };

    float sun = interp(l000.first, l100.first, l010.first, l110.first, l001.first, l101.first, l011.first, l111.first);
    float block = interp(l000.second, l100.second, l010.second, l110.second, l001.second, l101.second, l011.second, l111.second);

    return { std::clamp(sun, 0.0f, 1.0f), std::clamp(block, 0.0f, 1.0f) };
}

LightSample sample_block_face_light(const uint8_t* light_grid, int size_x, int size_y, int size_z, int x, int y, int z) {
    if (!light_grid) return { 1.0f, 0.0f };
    int cx = std::clamp(x, 0, size_x - 1);
    int cy = std::clamp(y, 0, size_y - 1);
    int cz = std::clamp(z, 0, size_z - 1);
    int idx = cy * (size_x * size_z) + cz * size_x + cx;
    uint8_t packed = light_grid[idx];
    return { (float)get_sunlight(packed) / 15.0f, (float)get_blocklight(packed) / 15.0f };
}

} // namespace VoxelLighting
