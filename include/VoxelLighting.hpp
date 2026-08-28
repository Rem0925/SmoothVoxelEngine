#include <cmath>
#pragma once
#include <cstdint>
#include <vector>
#include <raylib.h>
#include "Config.hpp"

namespace VoxelLighting {

// Empaquetado y desempaquetado de luz (4 bits sol [0-15], 4 bits bloques [0-15] en 1 byte)
inline uint8_t get_sunlight(uint8_t val) { return val & 0x0F; }
inline uint8_t get_blocklight(uint8_t val) { return (val >> 4) & 0x0F; }
inline uint8_t pack_light(uint8_t sun, uint8_t block) {
    return (sun & 0x0F) | ((block & 0x0F) << 4);
}

struct LightSample {
    float sunlight = 1.0f;   // Normalizado: 0.0 a 1.0
    float blocklight = 0.0f; // Normalizado: 0.0 a 1.0
};

struct LightCache {
    const Config::VoxelData* chunks[3][3];
    int base_cx, base_cz;

    uint8_t get_block(int wx, int wy, int wz) const {
        if (wy < 0 || wy >= Config::GRID_Y) return Config::AIR;
        
        int cx = (int)std::floor((float)wx / (float)Config::CHUNK_SIZE);
        int cz = (int)std::floor((float)wz / (float)Config::CHUNK_SIZE);
        int dx = cx - base_cx;
        int dz = cz - base_cz;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) return Config::AIR;
        
        int lx = wx - cx * Config::CHUNK_SIZE;
        int lz = wz - cz * Config::CHUNK_SIZE;
        
        const Config::VoxelData* c = chunks[dx + 1][dz + 1];
        if (c) {
            return c[wy * (Config::CHUNK_SIZE + 1) * (Config::CHUNK_SIZE + 1) + lz * (Config::CHUNK_SIZE + 1) + lx].block;
        }
        return Config::AIR;
    }

    float get_density(int wx, int wy, int wz) const {
        if (wy < 0 || wy >= Config::GRID_Y) return -1.0f;
        
        int cx = (int)std::floor((float)wx / (float)Config::CHUNK_SIZE);
        int cz = (int)std::floor((float)wz / (float)Config::CHUNK_SIZE);
        int dx = cx - base_cx;
        int dz = cz - base_cz;
        if (dx < -1 || dx > 1 || dz < -1 || dz > 1) return -1.0f;
        
        int lx = wx - cx * Config::CHUNK_SIZE;
        int lz = wz - cz * Config::CHUNK_SIZE;
        
        const Config::VoxelData* c = chunks[dx + 1][dz + 1];
        if (c) {
            return c[wy * (Config::CHUNK_SIZE + 1) * (Config::CHUNK_SIZE + 1) + lz * (Config::CHUNK_SIZE + 1) + lx].density;
        }
        return -1.0f;
    }
};

// Calcula la propagación completa de luz solar y fuentes de bloques con padding de 3x3 chunks
void compute_chunk_lighting(const LightCache& cache, uint8_t* light_grid);

// Muestreo trilineal continuo de luz para vértices de Marching Cubes (con offset de normal hacia el aire)
LightSample sample_smooth_light(const uint8_t* light_grid, int size_x, int size_y, int size_z, float x, float y, float z, Vector3 normal = {0, 0, 0});

// Muestreo de luz discreta para caras de bloques de construcción
LightSample sample_block_face_light(const uint8_t* light_grid, int size_x, int size_y, int size_z, int x, int y, int z);

} // namespace VoxelLighting
