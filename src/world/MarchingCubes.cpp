#include "world/MarchingCubes.hpp"
#include "core/Config.hpp"
#include "generation/Biome.hpp"
#include "world/VoxelLighting.hpp"
#include <raymath.h>
#include <cmath>
#include <algorithm>

namespace mc {

static const int edge_table[256] = 
{
    0x000, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c, 0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x099, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c, 0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x033, 0x13a, 0x636, 0x73f, 0x435, 0x53c, 0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0x0aa, 0x7a6, 0x6af, 0x5a5, 0x4ac, 0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x066, 0x16f, 0x265, 0x36c, 0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0x0ff, 0x3f5, 0x2fc, 0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x055, 0x15c, 0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0x0cc, 0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc, 0x0cc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c, 0x15c, 0x055, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc, 0x2fc, 0x3f5, 0x0ff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c, 0x36c, 0x265, 0x16f, 0x066, 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac, 0x4ac, 0x5a5, 0x6af, 0x7a6, 0x0aa, 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c, 0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x033, 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c, 0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x099, 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c, 0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x000
};

static const int triangle_table[256][16] = 
{
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
    {3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
    {1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
    {4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
    {9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
    {2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
    {10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
    {5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
    {5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
    {8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
    {2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
    {7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
    {11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
    {5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
    {11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
    {11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
    {9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
    {6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
    {6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
    {6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
    {8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
    {7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
    {3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
    {5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
    {0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
    {9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
    {8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
    {5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
    {0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
    {6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
    {10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
    {10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
    {0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
    {3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
    {6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
    {9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
    {8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
    {3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
    {6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
    {10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
    {10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1},
    {7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
    {7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
    {2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
    {1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
    {11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
    {8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
    {0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
    {7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
    {7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
    {1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
    {10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
    {0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
    {7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
    {6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
    {9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
    {6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
    {4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
    {10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
    {8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
    {1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
    {8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
    {10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
    {4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
    {10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
    {9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
    {6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
    {7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
    {3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
    {7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
    {3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
    {6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
    {9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
    {1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
    {4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
    {7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
    {6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
    {0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
    {6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
    {0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
    {11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
    {6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
    {5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
    {9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
    {1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
    {1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
    {10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
    {0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
    {5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
    {10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
    {11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
    {9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
    {7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
    {2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
    {9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
    {9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
    {1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
    {9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
    {9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
    {0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
    {10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
    {2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
    {0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
    {0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
    {9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
    {5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
    {3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
    {5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
    {0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
    {9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
    {1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
    {3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
    {4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
    {9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
    {11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
    {11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
    {2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
    {9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
    {3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
    {1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
    {4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
    {4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
    {0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
    {3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
    {3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
    {0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
    {9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
    {1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
};

// Interpolate the position where the isosurface crosses the edge between v1 and v2
static Vector3 interpolate(float isovalue, Vector3 p1, Vector3 p2, float valp1, float valp2) {
    if (std::abs(isovalue - valp1) < 0.00001f) return p1;
    if (std::abs(isovalue - valp2) < 0.00001f) return p2;
    if (std::abs(valp1 - valp2) < 0.00001f) return p1;
    
    float mu = (isovalue - valp1) / (valp2 - valp1);
    return {
        p1.x + mu * (p2.x - p1.x),
        p1.y + mu * (p2.y - p1.y),
        p1.z + mu * (p2.z - p1.z)
    };
}

void generate(const Config::VoxelData* voxels, const float* custom_density, int size_x, int size_y, int size_z, 
              float isovalue, uint8_t default_block,
              std::vector<Vector3>& vertices, 
              std::vector<Vector3>& normals,
              std::vector<Vector2>& uvs,
              std::vector<Vector2>& uvs2,
              std::vector<Color>& colors,
              std::vector<Vector3>& t_vertices,
              std::vector<Vector3>& t_normals,
              std::vector<Vector2>& t_uvs,
              std::vector<Vector2>& t_uvs2,
              std::vector<Color>& t_colors,
              float origin_x, float origin_z, int seed_offset, int lod,
              const Color* grass_tint_cache, const Color* foliage_tint_cache,
              const uint8_t* light_grid,
              int min_y, int max_y) 
{
    vertices.reserve(vertices.size() + 2048);
    normals.reserve(normals.size() + 2048);
    uvs.reserve(uvs.size() + 2048);
    uvs2.reserve(uvs2.size() + 2048);
    colors.reserve(colors.size() + 2048);
    t_vertices.reserve(t_vertices.size() + 512);
    t_normals.reserve(t_normals.size() + 512);
    t_uvs.reserve(t_uvs.size() + 512);
    t_uvs2.reserve(t_uvs2.size() + 512);
    t_colors.reserve(t_colors.size() + 512);

    int slice = size_x * size_z;

    static bool is_trans_lut[256] = { false };
    static bool is_terrain_lut[256] = { false };
    static bool luts_initialized = false;
    if (!luts_initialized) {
        is_trans_lut[Config::LEAVES] = true;
        for (const auto& [id, bt] : Config::BLOCKS) {
            if (id < 256) {
                if (id != Config::WATER && bt.shape == Config::SHAPE_TERRAIN) {
                    is_terrain_lut[id] = true;
                    if (bt.transparent || bt.is_foliage) {
                        is_trans_lut[id] = true;
                    }
                }
            }
        }
        is_terrain_lut[Config::WATER] = false;
        is_trans_lut[Config::WATER] = false;
        luts_initialized = true;
    }

    auto is_trans_terrain_block = [](uint8_t blk) -> bool {
        return is_trans_lut[blk];
    };

    bool has_trans_blocks = false;
    if (voxels && !custom_density) {
        int start_y_chk = std::max(0, min_y);
        int end_y_chk = (max_y > 0) ? std::min(size_y, max_y + lod) : size_y;
        for (int y = start_y_chk; y < end_y_chk && !has_trans_blocks; y++) {
            for (int z = 0; z < size_z && !has_trans_blocks; z++) {
                for (int x = 0; x < size_x && !has_trans_blocks; x++) {
                    int idx = y * slice + z * size_x + x;
                    if (voxels[idx].density >= isovalue && is_trans_lut[voxels[idx].block]) {
                        has_trans_blocks = true;
                    }
                }
            }
        }
    }

    int start_y = std::max(0, min_y);
    int end_y = (max_y > 0) ? std::min(size_y - lod, max_y) : (size_y - lod);

    Vector3 light_dir = {0.5f, 1.0f, 0.5f};
    float light_len = std::sqrt(light_dir.x*light_dir.x + light_dir.y*light_dir.y + light_dir.z*light_dir.z);
    light_dir.x /= light_len; light_dir.y /= light_len; light_dir.z /= light_len;

    int num_passes = has_trans_blocks ? 2 : 1;

    for (int pass = 0; pass < num_passes; ++pass) {
        bool is_trans_pass = (pass == 1);

        auto get_val = [&](int x, int y, int z) -> float {
            int idx = y * slice + z * size_x + x;
            if (custom_density) return custom_density[idx];
            if (!voxels) return -1.0f;
            uint8_t b = voxels[idx].block;
            if (is_trans_pass) {
                return voxels[idx].density;
            } else {
                if (is_trans_lut[b]) {
                    return -1.0f;
                }
                return voxels[idx].density;
            }
        };

        auto get_raw_block = [&](int bx, int by, int bz) -> uint8_t {
            if (!voxels) return default_block;
            if (bx < 0) bx = 0; if (bx >= size_x) bx = size_x - 1;
            if (by < 0) by = 0; if (by >= size_y) by = size_y - 1;
            if (bz < 0) bz = 0; if (bz >= size_z) bz = size_z - 1;
            return voxels[by * slice + bz * size_x + bx].block;
        };

        auto is_valid_terrain_block = [&](uint8_t blk) -> bool {
            return is_terrain_lut[blk];
        };

        auto get_v_block = [&](Vector3 v) -> uint8_t {
            int ix_f = std::floor(v.x); int iy_f = std::floor(v.y); int iz_f = std::floor(v.z);
            int ix_c = std::ceil(v.x);  int iy_c = std::ceil(v.y);  int iz_c = std::ceil(v.z);
            uint8_t b = get_raw_block(ix_f, iy_f, iz_f);
            if (!is_terrain_lut[b]) b = get_raw_block(ix_c, iy_c, iz_c);
            if (!is_terrain_lut[b]) b = get_raw_block(ix_f, iy_f - 1, iz_f);
            if (!is_terrain_lut[b]) b = get_raw_block(ix_c, iy_c - 1, iz_c);
            if (!is_terrain_lut[b]) b = get_raw_block(ix_f, iy_f - 2, iz_f);
            if (!is_terrain_lut[b]) b = get_raw_block(ix_f, iy_f + 1, iz_f);
            if (!is_terrain_lut[b]) b = default_block;
            return b;
        };

        auto sample_smooth_density = [&](float sx, float sy, float sz) -> float {
            int ix = std::clamp((int)std::floor(sx), 0, size_x - 2);
            int iy = std::clamp((int)std::floor(sy), 0, size_y - 2);
            int iz = std::clamp((int)std::floor(sz), 0, size_z - 2);
            float fx = sx - (float)ix;
            float fy = sy - (float)iy;
            float fz = sz - (float)iz;

            float d000 = get_val(ix,   iy,   iz);
            float d100 = get_val(ix+1, iy,   iz);
            float d010 = get_val(ix,   iy+1, iz);
            float d110 = get_val(ix+1, iy+1, iz);
            float d001 = get_val(ix,   iy,   iz+1);
            float d101 = get_val(ix+1, iy,   iz+1);
            float d011 = get_val(ix,   iy+1, iz+1);
            float d111 = get_val(ix+1, iy+1, iz+1);

            float c00 = d000 * (1.0f - fx) + d100 * fx;
            float c10 = d010 * (1.0f - fx) + d110 * fx;
            float c01 = d001 * (1.0f - fx) + d101 * fx;
            float c11 = d011 * (1.0f - fx) + d111 * fx;
            float c0  = c00  * (1.0f - fz) + c01  * fz;
            float c1  = c10  * (1.0f - fz) + c11  * fz;
            return c0 * (1.0f - fy) + c1 * fy;
        };

        auto calc_vertex_ao = [&](Vector3 v_pt, Vector3 n_vec) -> float {
            Vector3 tangent = (std::abs(n_vec.y) < 0.9f) ? Vector3Normalize(Vector3CrossProduct(n_vec, {0, 1, 0})) : Vector3Normalize(Vector3CrossProduct(n_vec, {1, 0, 0}));
            Vector3 bitangent = Vector3CrossProduct(n_vec, tangent);

            float r = 0.75f;
            float h = 0.35f;
            Vector3 p1 = Vector3Add(v_pt, Vector3Add(Vector3Scale(n_vec, h), Vector3Scale(tangent, r)));
            Vector3 p2 = Vector3Add(v_pt, Vector3Add(Vector3Scale(n_vec, h), Vector3Scale(tangent, -r)));
            Vector3 p3 = Vector3Add(v_pt, Vector3Add(Vector3Scale(n_vec, h), Vector3Scale(bitangent, r)));
            Vector3 p4 = Vector3Add(v_pt, Vector3Add(Vector3Scale(n_vec, h), Vector3Scale(bitangent, -r)));

            float occ = 0.0f;
            float d1 = sample_smooth_density(p1.x, p1.y, p1.z);
            float d2 = sample_smooth_density(p2.x, p2.y, p2.z);
            float d3 = sample_smooth_density(p3.x, p3.y, p3.z);
            float d4 = sample_smooth_density(p4.x, p4.y, p4.z);

            if (d1 >= isovalue) occ += std::clamp((d1 - isovalue) * 2.0f, 0.1f, 1.0f);
            if (d2 >= isovalue) occ += std::clamp((d2 - isovalue) * 2.0f, 0.1f, 1.0f);
            if (d3 >= isovalue) occ += std::clamp((d3 - isovalue) * 2.0f, 0.1f, 1.0f);
            if (d4 >= isovalue) occ += std::clamp((d4 - isovalue) * 2.0f, 0.1f, 1.0f);

            return std::clamp(1.0f - (occ / 4.0f) * 0.25f, 0.75f, 1.0f);
        };

        auto sample_bilinear_tint = [&](float px, float pz, const Color* cache) -> Color {
            float cx = std::clamp(px, 0.0f, (float)(size_x - 1));
            float cz = std::clamp(pz, 0.0f, (float)(size_z - 1));
            int x0 = (int)cx;
            int z0 = (int)cz;
            int x1 = std::min(x0 + 1, size_x - 1);
            int z1 = std::min(z0 + 1, size_z - 1);
            float tx = cx - (float)x0;
            float tz = cz - (float)z0;

            Color c00 = cache[z0 * size_x + x0];
            Color c10 = cache[z0 * size_x + x1];
            Color c01 = cache[z1 * size_x + x0];
            Color c11 = cache[z1 * size_x + x1];

            float r0 = (float)c00.r * (1.0f - tx) + (float)c10.r * tx;
            float r1 = (float)c01.r * (1.0f - tx) + (float)c11.r * tx;
            float r  = r0 * (1.0f - tz) + r1 * tz;

            float g0 = (float)c00.g * (1.0f - tx) + (float)c10.g * tx;
            float g1 = (float)c01.g * (1.0f - tx) + (float)c11.g * tx;
            float g  = g0 * (1.0f - tz) + g1 * tz;

            float b0 = (float)c00.b * (1.0f - tx) + (float)c10.b * tx;
            float b1 = (float)c01.b * (1.0f - tx) + (float)c11.b * tx;
            float b_val = b0 * (1.0f - tz) + b1 * tz;

            return Color{ (unsigned char)r, (unsigned char)g, (unsigned char)b_val, 255 };
        };

        for (int y = start_y; y < end_y; y += lod) {
            for (int z = 0; z < size_z - lod; z += lod) {
                for (int x = 0; x < size_x - lod; x += lod) {
                    
                    // EARLY-EXIT: En la pasada de hojas, si ninguno de los 8 vértices del cubo toca follaje, saltar celda de inmediato
                    if (is_trans_pass && voxels) {
                        bool cell_touches_trans = is_trans_lut[get_raw_block(x, y, z)] ||
                                                  is_trans_lut[get_raw_block(x + lod, y, z)] ||
                                                  is_trans_lut[get_raw_block(x + lod, y, z + lod)] ||
                                                  is_trans_lut[get_raw_block(x, y, z + lod)] ||
                                                  is_trans_lut[get_raw_block(x, y + lod, z)] ||
                                                  is_trans_lut[get_raw_block(x + lod, y + lod, z)] ||
                                                  is_trans_lut[get_raw_block(x + lod, y + lod, z + lod)] ||
                                                  is_trans_lut[get_raw_block(x, y + lod, z + lod)];
                        if (!cell_touches_trans) continue;
                    }

                    float val[8];
                    val[0] = get_val(x, y, z);
                    val[1] = get_val(x + lod, y, z);
                    val[2] = get_val(x + lod, y, z + lod);
                    val[3] = get_val(x, y, z + lod);
                    val[4] = get_val(x, y + lod, z);
                    val[5] = get_val(x + lod, y + lod, z);
                    val[6] = get_val(x + lod, y + lod, z + lod);
                    val[7] = get_val(x, y + lod, z + lod);

                    int cubeindex = 0;
                    if (val[0] < isovalue) cubeindex |= 1;
                    if (val[1] < isovalue) cubeindex |= 2;
                    if (val[2] < isovalue) cubeindex |= 4;
                    if (val[3] < isovalue) cubeindex |= 8;
                    if (val[4] < isovalue) cubeindex |= 16;
                    if (val[5] < isovalue) cubeindex |= 32;
                    if (val[6] < isovalue) cubeindex |= 64;
                    if (val[7] < isovalue) cubeindex |= 128;

                    if (edge_table[cubeindex] == 0)
                        continue;

                    Vector3 p[8] = {
                        {(float)x, (float)y, (float)z},
                        {(float)x + lod, (float)y, (float)z},
                        {(float)x + lod, (float)y, (float)z + lod},
                        {(float)x, (float)y, (float)z + lod},
                        {(float)x, (float)y + lod, (float)z},
                        {(float)x + lod, (float)y + lod, (float)z},
                        {(float)x + lod, (float)y + lod, (float)z + lod},
                        {(float)x, (float)y + lod, (float)z + lod}
                    };

                    Vector3 vertlist[12];
                    if (edge_table[cubeindex] & 1) vertlist[0] = interpolate(isovalue, p[0], p[1], val[0], val[1]);
                    if (edge_table[cubeindex] & 2) vertlist[1] = interpolate(isovalue, p[1], p[2], val[1], val[2]);
                    if (edge_table[cubeindex] & 4) vertlist[2] = interpolate(isovalue, p[2], p[3], val[2], val[3]);
                    if (edge_table[cubeindex] & 8) vertlist[3] = interpolate(isovalue, p[3], p[0], val[3], val[0]);
                    if (edge_table[cubeindex] & 16) vertlist[4] = interpolate(isovalue, p[4], p[5], val[4], val[5]);
                    if (edge_table[cubeindex] & 32) vertlist[5] = interpolate(isovalue, p[5], p[6], val[5], val[6]);
                    if (edge_table[cubeindex] & 64) vertlist[6] = interpolate(isovalue, p[6], p[7], val[6], val[7]);
                    if (edge_table[cubeindex] & 128) vertlist[7] = interpolate(isovalue, p[7], p[4], val[7], val[4]);
                    if (edge_table[cubeindex] & 256) vertlist[8] = interpolate(isovalue, p[0], p[4], val[0], val[4]);
                    if (edge_table[cubeindex] & 512) vertlist[9] = interpolate(isovalue, p[1], p[5], val[1], val[5]);
                    if (edge_table[cubeindex] & 1024) vertlist[10] = interpolate(isovalue, p[2], p[6], val[2], val[6]);
                    if (edge_table[cubeindex] & 2048) vertlist[11] = interpolate(isovalue, p[3], p[7], val[3], val[7]);

                    for (int i = 0; triangle_table[cubeindex][i] != -1; i += 3) {
                        Vector3 v1 = vertlist[triangle_table[cubeindex][i]];
                        Vector3 v2 = vertlist[triangle_table[cubeindex][i + 1]];
                        Vector3 v3 = vertlist[triangle_table[cubeindex][i + 2]];

                        // Compute normal (winding order flipped later)
                        Vector3 u = {v2.x - v3.x, v2.y - v3.y, v2.z - v3.z};
                        Vector3 v = {v1.x - v3.x, v1.y - v3.y, v1.z - v3.z};
                        Vector3 n = {
                            u.y * v.z - u.z * v.y,
                            u.z * v.x - u.x * v.z,
                            u.x * v.y - u.y * v.x
                        };
                        float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
                        if (len > 0.0f) { n.x /= len; n.y /= len; n.z /= len; }

                        float ao1 = calc_vertex_ao(v1, n);
                        float ao2 = calc_vertex_ao(v2, n);
                        float ao3 = calc_vertex_ao(v3, n);

                        uint8_t b1 = get_v_block(v1);
                        uint8_t b2 = get_v_block(v2);
                        uint8_t b3 = get_v_block(v3);

                        bool has_trans = is_trans_terrain_block(b1) || is_trans_terrain_block(b2) || is_trans_terrain_block(b3);

                        if (is_trans_pass) {
                            if (!has_trans) continue;
                            if (!is_trans_terrain_block(b1)) {
                                if (is_trans_terrain_block(b2)) b1 = b2;
                                else if (is_trans_terrain_block(b3)) b1 = b3;
                                else b1 = Config::LEAVES;
                            }
                            if (!is_trans_terrain_block(b2)) b2 = b1;
                            if (!is_trans_terrain_block(b3)) b3 = b1;
                        }
                        
                        uint8_t b_primary = b1;
                        uint8_t b_secondary = b1;
                        if (b2 != b1) b_secondary = b2;
                        else if (b3 != b1) b_secondary = b3;

                        if (default_block == Config::WATER) {
                            b_primary = Config::WATER;
                            b_secondary = Config::WATER;
                        } else {
                            auto is_terrain = [](uint8_t b) {
                                return b != Config::TORCH && b != Config::WATER && b != Config::AIR;
                            };
                            
                            if (!is_terrain(b_primary) || !is_terrain(b_secondary)) {
                                b_secondary = b_primary;
                            } else if (b_primary > b_secondary) {
                                std::swap(b_primary, b_secondary);
                            }
                        }

                        bool pri_is_foliage = (b_primary == Config::GRASS || b_primary == Config::LEAVES || (Config::BLOCKS.count(b_primary) && (Config::BLOCKS.at(b_primary).is_foliage || Config::BLOCKS.at(b_primary).is_grass)));
                        bool sec_is_foliage = (b_secondary == Config::GRASS || b_secondary == Config::LEAVES || (Config::BLOCKS.count(b_secondary) && (Config::BLOCKS.at(b_secondary).is_foliage || Config::BLOCKS.at(b_secondary).is_grass)));
                        float foliage_pri_offset = pri_is_foliage ? 10.0f : 0.0f;
                        float foliage_sec_offset = sec_is_foliage ? 10.0f : 0.0f;

                        Color tri_tint = { 255, 255, 255, 255 };
                        if ((pri_is_foliage || sec_is_foliage) && grass_tint_cache && foliage_tint_cache) {
                            float tri_x = (v1.x + v2.x + v3.x) / 3.0f;
                            float tri_z = (v1.z + v2.z + v3.z) / 3.0f;
                            const Color* target_cache = (b_primary == Config::LEAVES || b_secondary == Config::LEAVES) ? foliage_tint_cache : grass_tint_cache;
                            tri_tint = sample_bilinear_tint(tri_x, tri_z, target_cache);
                        }

                        VoxelLighting::LightSample ls1 = VoxelLighting::sample_smooth_light(light_grid, size_x, size_y, size_z, v1.x, v1.y, v1.z, n);
                        VoxelLighting::LightSample ls2 = VoxelLighting::sample_smooth_light(light_grid, size_x, size_y, size_z, v2.x, v2.y, v2.z, n);
                        VoxelLighting::LightSample ls3 = VoxelLighting::sample_smooth_light(light_grid, size_x, size_y, size_z, v3.x, v3.y, v3.z, n);

                        Config::BlockType b_info_pri = Config::BLOCKS.at(Config::GRASS);
                        if (Config::BLOCKS.find(b_primary) != Config::BLOCKS.end()) {
                            b_info_pri = Config::BLOCKS.at(b_primary);
                        } else {
                            b_info_pri.is_waving = false;
                        }
                        
                        Config::BlockType b_info_sec = Config::BLOCKS.at(Config::GRASS);
                        if (Config::BLOCKS.find(b_secondary) != Config::BLOCKS.end()) {
                            b_info_sec = Config::BLOCKS.at(b_secondary);
                        } else {
                            b_info_sec.is_waving = false;
                        }

                        auto& v_vec = is_trans_pass ? t_vertices : vertices;
                        auto& n_vec = is_trans_pass ? t_normals : normals;
                        auto& u_vec = is_trans_pass ? t_uvs : uvs;
                        auto& u2_vec = is_trans_pass ? t_uvs2 : uvs2;
                        auto& c_vec = is_trans_pass ? t_colors : colors;

                        // El orden de vertices es v3, v2, v1: x = AO, y = Luz Solar, z = Luz de Bloque
                        v_vec.push_back(v3); v_vec.push_back(v2); v_vec.push_back(v1);
                        n_vec.push_back({ ao3, ls3.sunlight, ls3.blocklight });
                        n_vec.push_back({ ao2, ls2.sunlight, ls2.blocklight });
                        n_vec.push_back({ ao1, ls1.sunlight, ls1.blocklight });

                        Color col1 = tri_tint;
                        Color col2 = tri_tint;
                        Color col3 = tri_tint;
                        col1.a = (b1 == b_primary) ? 255 : 0;
                        col2.a = (b2 == b_primary) ? 255 : 0;
                        col3.a = (b3 == b_primary) ? 255 : 0;

                        c_vec.push_back(col3);
                        c_vec.push_back(col2);
                        c_vec.push_back(col1);

                        float tex_w = 1.0f / (float)Config::TILES_ATLAS_COLS;
                        float tex_h = 1.0f / (float)Config::TILES_ATLAS_ROWS;
                        float offset_u_pri = b_info_pri.tex_x * tex_w;
                        float offset_v_pri = (Config::TILES_ATLAS_ROWS - 1 - b_info_pri.tex_y) * tex_h;
                        float offset_u_sec = b_info_sec.tex_x * tex_w;
                        float offset_v_sec = (Config::TILES_ATLAS_ROWS - 1 - b_info_sec.tex_y) * tex_h;

                        float min_x = std::min(v1.x, std::min(v2.x, v3.x));
                        float min_y = std::min(v1.y, std::min(v2.y, v3.y));
                        float min_z = std::min(v1.z, std::min(v2.z, v3.z));

                        float abs_x = std::abs(n.x);
                        float abs_y = std::abs(n.y);
                        float abs_z = std::abs(n.z);
                        
                        auto check_sway = [&](Vector3 v) -> bool {
                            bool has_waving = false;
                            int base_x = std::floor(v.x);
                            int base_y = std::floor(v.y);
                            int base_z = std::floor(v.z);
                            
                            for (int dx = 0; dx <= 1; dx++) {
                                for (int dy = 0; dy <= 1; dy++) {
                                    for (int dz = 0; dz <= 1; dz++) {
                                        uint8_t b = get_raw_block(base_x + dx, base_y + dy, base_z + dz);
                                        if (b == 255 || b == 7 || b == Config::TALL_GRASS || b == Config::AIR) continue;
                                        if (Config::BLOCKS.count(b)) {
                                            if (!Config::BLOCKS.at(b).is_waving) return false;
                                            has_waving = true;
                                        }
                                    }
                                }
                            }
                            return has_waving;
                        };

                        for(int j=0; j<3; ++j) {
                            Vector2 uv = {0, 0};
                            Vector3 vert = (j == 0) ? v3 : (j == 1) ? v2 : v1;
                            
                            if (abs_y >= abs_x && abs_y >= abs_z) { // Dominante Y (Techos y suelos)
                                uv.x = (vert.x - min_x) / (float)lod; 
                                uv.y = (vert.z - min_z) / (float)lod; 
                            }
                            else if (abs_x >= abs_y && abs_x >= abs_z) { // Dominante X (Paredes)
                                uv.x = (vert.z - min_z) / (float)lod; 
                                uv.y = 1.0f - ((vert.y - min_y) / (float)lod);
                            }
                            else { // Dominante Z (Paredes frontales)
                                uv.x = (vert.x - min_x) / (float)lod; 
                                uv.y = 1.0f - ((vert.y - min_y) / (float)lod);
                            }
                            
                            float sway = check_sway(vert) ? 10.0f : 0.0f;
                            
                            u_vec.push_back({uv.x * tex_w + offset_u_pri + sway, uv.y * tex_h + offset_v_pri + foliage_pri_offset});
                            u2_vec.push_back({uv.x * tex_w + offset_u_sec + sway, uv.y * tex_h + offset_v_sec + foliage_sec_offset});
                        }
                    }
                }
            }
        }
    }
}
}
