#pragma once
#include <vector>
#include <raylib.h>
#include <cstdint>

#include "Config.hpp"

namespace mc {
    void generate(const Config::VoxelData* voxels, const float* custom_density, int size_x, int size_y, int size_z, 
                  float isovalue, uint8_t default_block,
                  std::vector<Vector3>& vertices, 
                  std::vector<Vector3>& normals,
                  std::vector<Vector2>& uvs,
                  std::vector<Vector2>& uvs2,
                  std::vector<Color>& colors,
                  float origin_x = 0.0f, float origin_z = 0.0f, int seed_offset = 0, int lod = 1);
}
