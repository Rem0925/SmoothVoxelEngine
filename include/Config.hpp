#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
#include <thread>

namespace Config {
    constexpr int GRID_X = 64;
    constexpr int GRID_Y = 128;
    constexpr int GRID_Z = 64;
    constexpr int CHUNK_SIZE = 16;
    constexpr float ISO_SURFACE = 0.0f;

    inline const std::string WORLD_NAME = "world_1";
    constexpr int WORLD_SEED = 414432;
    
    constexpr int MAX_FPS = 120;
    constexpr int RENDER_DISTANCE = 6;
    constexpr float FOG_END = RENDER_DISTANCE * CHUNK_SIZE;
    constexpr float FOG_START = FOG_END * 0.7f;
    inline const int MAX_WORKER_THREADS = std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1;

    constexpr float WATER_LEVEL = 38.0f;

    struct BlockType {
        std::string name;
        int tex_x;
        int tex_y;
        bool transparent;
        bool is_waving; 
    };

    struct VoxelData {
        float density;
        uint8_t block;
        uint8_t water;
    };

    enum BlockID : uint8_t {
        GRASS = 0,
        STONE = 1,
        DIRT = 2,
        TORCH = 3,
        WOOD = 4,
        LEAVES = 5,
        SAND = 6,
        WATER = 7,
        TALL_GRASS = 8,
        RED_MUSHROOM = 9,
        BROWN_MUSHROOM = 10,
        DEAD_BUSH = 11,
        GRAVEL = 12,
        RED_CLAY = 13,
        BIRCH_WOOD = 14,
        CACTUS = 15,
        AIR = 255
    };

    inline const std::unordered_map<uint8_t, BlockType> BLOCKS = {
        {GRASS, {"Pasto", 6, 8, false, false}},
        {STONE, {"Piedra", 3, 5, false, false}},
        {DIRT,  {"Tierra", 7, 4, false, false}},
        {TORCH, {"Antorcha", 2, 6, false, false}},
        {WOOD,  {"Madera Roble", 1, 9, false, false}},
        {LEAVES,{"Hojas", 4, 1, false, true}},
        {SAND,  {"Arena", 3, 3, false, false}},
        {WATER, {"Agua", 0, 3, true, false}},
        {TALL_GRASS, {"Pasto Alto", 6, 5, true, true}},
        {RED_MUSHROOM, {"Seta Roja", 4, 4, true, false}},
        {BROWN_MUSHROOM, {"Seta Marron", 4, 3, true, false}},
        {DEAD_BUSH, {"Arbusto Seco", 6, 7, true, true}},
        {GRAVEL, {"Grava", 6, 9, false, false}},
        {RED_CLAY, {"Arcilla Roja", 3, 0, false, false}},
        {BIRCH_WOOD, {"Madera Abedul", 0, 1, false, false}},
        {CACTUS, {"Cactus", 8, 7, false, false}}
    };
}
