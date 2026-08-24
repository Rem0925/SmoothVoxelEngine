#pragma once
#include <raylib.h>
#include <cstdint>
#include <string>
#include "Config.hpp"

enum BiomeType : uint8_t {
    BIOME_OCEAN = 0,
    BIOME_BEACH = 1,
    BIOME_PLAINS = 2,
    BIOME_FOREST = 3,
    BIOME_MOUNTAINS = 4,
    BIOME_DESERT = 5,
    BIOME_TAIGA = 6,
    BIOME_JUNGLE = 7
};

struct BiomeConfig {
    BiomeType type;
    std::string name;
    
    // Parametros que modulan la formula de generacion
    float base_height;
    float valley_neg_scale;
    float valley_pos_scale;
    float detail_scale;
    float mountain_scale;
    float step_weight;
    
    // Bloques del terreno
    uint8_t surface_block;
    uint8_t subsurface_block;
    
    // Arboles
    int tree_chance;        // Probabilidad (sobre 1000)
    int tree_min_h;
    int tree_max_h;
    float birch_tree_ratio; // Proporcion de abedul vs roble (0.0 a 1.0)
    
    // Decoraciones de plantas (cross-quads)
    int tall_grass_chance;    // Probabilidad (sobre 1000)
    int red_mushroom_chance;  // Probabilidad (sobre 1000)
    int brown_mushroom_chance;// Probabilidad (sobre 1000)
    int dead_bush_chance;     // Probabilidad (sobre 1000)
    
    // Tintes de color puros
    Color grass_tint;
    Color foliage_tint;
    
    float temperature;
    float humidity;
    float continentalness;
};

namespace Biome {
    // Bioma discreto puro
    BiomeConfig get_discrete_biome(double wx, double wz, int seed_offset);
    
    // Bioma mezclado suavemente en las fronteras (Minecraft-style Blend Kernel)
    BiomeConfig get_blended_biome(double wx, double wz, int seed_offset, int radius = 10);
    
    // Tintes con degradado suave en fronteras entre biomas
    Color get_blended_grass_tint(double wx, double wz, int seed_offset, int radius = 6);
    Color get_blended_foliage_tint(double wx, double wz, int seed_offset, int radius = 6);
    
    // Precalculo ultrarrapido de cache de tintes para un chunk completo (17x17 columnas)
    void compute_chunk_tint_cache(int cx, int cz, int seed_offset, Color* out_grass, Color* out_foliage);
    
    // Alias directos
    inline BiomeConfig get_biome_at(double wx, double wz, int seed_offset) {
        return get_blended_biome(wx, wz, seed_offset, 10);
    }
    inline Color get_grass_tint_at(double wx, double wz, int seed_offset) {
        return get_blended_grass_tint(wx, wz, seed_offset, 6);
    }
    inline Color get_foliage_tint_at(double wx, double wz, int seed_offset) {
        return get_blended_foliage_tint(wx, wz, seed_offset, 6);
    }
    
    const char* get_biome_name_at(double wx, double wz, int seed_offset);
}
