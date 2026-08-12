#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

namespace Config {
    constexpr int GRID_X = 64;
    constexpr int GRID_Y = 64;
    constexpr int GRID_Z = 64;
    constexpr int CHUNK_SIZE = 16;
    constexpr float ISO_SURFACE = 0.0f;

    inline const std::string WORLD_NAME = "world_1";
    constexpr int WORLD_SEED = 1234512;

    constexpr float WATER_LEVEL = 38.0f;

    struct BlockType {
        std::string name;
        int tex_x;
        int tex_y;
        bool transparent;
        bool is_waving; // Propiedad añadida para movimiento de hojas
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
        AIR = 255
    };

    inline const std::unordered_map<uint8_t, BlockType> BLOCKS = {
        {GRASS, {"Pasto", 6, 8, false, false}},  // Pasto tiene animación de viento
        {STONE, {"Piedra", 3, 5, false, false}},
        {DIRT,  {"Tierra", 7, 4, false, false}},
        {TORCH, {"Antorcha", 2, 6, false, false}},
        {WOOD,  {"Madera", 1, 9, false, false}},
        {LEAVES,{"Hojas", 4, 1, false, true}},  // Hojas tienen animación de viento
        {SAND,  {"Arena", 3, 6, false, false}},
        {WATER, {"Agua", 0, 3, true, false}}
    };
}
