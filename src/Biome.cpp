#include "Biome.hpp"
#include "Noise.hpp"
#include <cmath>
#include <algorithm>

namespace Biome {

static inline Color lerp_color(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{
        (unsigned char)(a.r + t * (b.r - a.r)),
        (unsigned char)(a.g + t * (b.g - a.g)),
        (unsigned char)(a.b + t * (b.b - a.b)),
        255
    };
}

static inline BiomeConfig get_pure_biome_config(BiomeType type, float temp, float hum, float cont) {
    BiomeConfig cfg;
    cfg.type = type;
    cfg.temperature = temp;
    cfg.humidity = hum;
    cfg.continentalness = cont;
    cfg.step_weight = 0.0f; // 100% suave
    cfg.surface_block = Config::GRASS;
    cfg.subsurface_block = Config::DIRT;
    cfg.tree_min_h = 5;
    cfg.tree_max_h = 8;
    cfg.birch_tree_ratio = 0.0f;
    cfg.tall_grass_chance = 0;
    cfg.red_mushroom_chance = 0;
    cfg.brown_mushroom_chance = 0;
    cfg.dead_bush_chance = 0;
    
    switch (type) {
        case BIOME_OCEAN:
            cfg.name = "Oceano";
            cfg.base_height = 26.0f;
            cfg.mountain_scale = 3.0f;
            cfg.valley_neg_scale = 14.0f;
            cfg.valley_pos_scale = 8.0f;
            cfg.detail_scale = 2.0f;
            cfg.surface_block = Config::SAND;
            cfg.subsurface_block = Config::GRAVEL;
            cfg.tree_chance = 0;
            cfg.grass_tint = {69, 140, 115, 255};
            cfg.foliage_tint = {69, 140, 115, 255};
            break;
            
        case BIOME_BEACH:
            cfg.name = "Playa";
            cfg.base_height = 38.0f;
            cfg.mountain_scale = 2.0f;
            cfg.valley_neg_scale = 8.0f;
            cfg.valley_pos_scale = 8.0f;
            cfg.detail_scale = 2.0f;
            cfg.surface_block = Config::SAND;
            cfg.subsurface_block = Config::GRAVEL;
            cfg.tree_chance = 0;
            cfg.dead_bush_chance = 5;
            cfg.grass_tint = {154, 191, 80, 255};
            cfg.foliage_tint = {145, 185, 75, 255};
            break;
            
        case BIOME_MOUNTAINS:
            cfg.name = "Montanas Altas";
            cfg.base_height = 42.0f;
            cfg.mountain_scale = 65.0f;
            cfg.valley_neg_scale = 12.0f;
            cfg.valley_pos_scale = 14.0f;
            cfg.detail_scale = 3.5f;
            cfg.surface_block = Config::GRASS;
            cfg.subsurface_block = Config::DIRT;
            cfg.tree_chance = 8;
            cfg.tree_min_h = 4;
            cfg.tree_max_h = 6;
            cfg.birch_tree_ratio = 0.20f;
            cfg.tall_grass_chance = 15;
            cfg.red_mushroom_chance = 0;
            cfg.brown_mushroom_chance = 0;
            cfg.grass_tint = {138, 182, 120, 255};
            cfg.foliage_tint = {115, 165, 110, 255};
            break;
            
        case BIOME_DESERT:
            cfg.name = "Desierto";
            cfg.base_height = 42.0f;
            cfg.mountain_scale = 4.0f;
            cfg.valley_neg_scale = 8.0f;
            cfg.valley_pos_scale = 8.0f;
            cfg.detail_scale = 2.5f;
            cfg.surface_block = Config::SAND;
            cfg.subsurface_block = Config::RED_CLAY;
            cfg.tree_chance = 0;
            cfg.dead_bush_chance = 15;
            cfg.grass_tint = {174, 178, 72, 255};
            cfg.foliage_tint = {160, 165, 65, 255};
            break;
            
        case BIOME_TAIGA:
            cfg.name = "Taiga";
            cfg.base_height = 42.0f;
            cfg.mountain_scale = 8.0f;
            cfg.valley_neg_scale = 14.0f;
            cfg.valley_pos_scale = 10.0f;
            cfg.detail_scale = 3.0f;
            cfg.surface_block = Config::GRASS;
            cfg.subsurface_block = Config::DIRT;
            cfg.tree_chance = 35;
            cfg.tree_min_h = 6;
            cfg.tree_max_h = 10;
            cfg.birch_tree_ratio = 0.0f;
            cfg.tall_grass_chance = 25;
            cfg.red_mushroom_chance = 4;
            cfg.brown_mushroom_chance = 8;
            cfg.grass_tint = {72, 133, 98, 255};
            cfg.foliage_tint = {60, 120, 85, 255};
            break;
            
        case BIOME_JUNGLE:
            cfg.name = "Selva Tropical";
            cfg.base_height = 42.0f;
            cfg.mountain_scale = 8.0f;
            cfg.valley_neg_scale = 14.0f;
            cfg.valley_pos_scale = 10.0f;
            cfg.detail_scale = 3.5f;
            cfg.surface_block = Config::GRASS;
            cfg.subsurface_block = Config::DIRT;
            cfg.tree_chance = 70;
            cfg.tree_min_h = 7;
            cfg.tree_max_h = 12;
            cfg.birch_tree_ratio = 0.0f;
            cfg.tall_grass_chance = 60;
            cfg.red_mushroom_chance = 10;
            cfg.brown_mushroom_chance = 6;
            cfg.grass_tint = {50, 200, 34, 255};
            cfg.foliage_tint = {40, 185, 30, 255};
            break;
            
        case BIOME_FOREST:
            cfg.name = "Bosque Templado";
            cfg.base_height = 42.0f;
            cfg.mountain_scale = 6.0f;
            cfg.valley_neg_scale = 14.0f;
            cfg.valley_pos_scale = 10.0f;
            cfg.detail_scale = 3.0f;
            cfg.surface_block = Config::GRASS;
            cfg.subsurface_block = Config::DIRT;
            cfg.tree_chance = 45;
            cfg.tree_min_h = 5;
            cfg.tree_max_h = 8;
            cfg.birch_tree_ratio = 0.40f;
            cfg.tall_grass_chance = 40;
            cfg.red_mushroom_chance = 4;
            cfg.brown_mushroom_chance = 6;
            cfg.grass_tint = {88, 156, 40, 255};
            cfg.foliage_tint = {75, 145, 35, 255};
            break;
            
        case BIOME_PLAINS:
        default:
            cfg.name = "Llanura";
            cfg.base_height = 42.0f;
            cfg.mountain_scale = 4.0f;
            cfg.valley_neg_scale = 14.0f;
            cfg.valley_pos_scale = 10.0f;
            cfg.detail_scale = 3.0f;
            cfg.surface_block = Config::GRASS;
            cfg.subsurface_block = Config::DIRT;
            cfg.tree_chance = 8;
            cfg.tree_min_h = 5;
            cfg.tree_max_h = 7;
            cfg.birch_tree_ratio = 0.20f;
            cfg.tall_grass_chance = 35;
            cfg.red_mushroom_chance = 0;
            cfg.brown_mushroom_chance = 0;
            cfg.grass_tint = {124, 189, 45, 255};
            cfg.foliage_tint = {110, 175, 40, 255};
            break;
    }
    
    return cfg;
}

BiomeConfig get_discrete_biome(double wx, double wz, int seed_offset) {
    double sx = wx + seed_offset;
    double sz = wz + seed_offset;
    
    double n_cont  = pnoise3(sx * 0.0006, 100.0, sz * 0.0006, 3, 0.5);
    double n_temp  = pnoise3(sx * 0.0009, 250.0, sz * 0.0009, 3, 0.5);
    double n_hum   = pnoise3(sx * 0.0009, 450.0, sz * 0.0009, 3, 0.5);
    double n_mount = pnoise3(sx * 0.0011, 750.0, sz * 0.0011, 3, 0.5);
    
    float cont = (float)n_cont;
    float temp = std::clamp((float)((n_temp + 0.8) * 0.625), 0.0f, 1.0f);
    float hum  = std::clamp((float)((n_hum + 0.8) * 0.625), 0.0f, 1.0f);
    
    if (cont < -0.06f) {
        return get_pure_biome_config(BIOME_OCEAN, temp, hum, cont);
    }
    if (cont < -0.01f) {
        return get_pure_biome_config(BIOME_BEACH, temp, hum, cont);
    }
    if (n_mount > 0.30) {
        return get_pure_biome_config(BIOME_MOUNTAINS, temp, hum, cont);
    }
    if (temp > 0.68f && hum < 0.32f) {
        return get_pure_biome_config(BIOME_DESERT, temp, hum, cont);
    }
    if (temp < 0.35f) {
        return get_pure_biome_config(BIOME_TAIGA, temp, hum, cont);
    }
    if (temp > 0.65f && hum > 0.65f) {
        return get_pure_biome_config(BIOME_JUNGLE, temp, hum, cont);
    }
    if (hum > 0.52f) {
        return get_pure_biome_config(BIOME_FOREST, temp, hum, cont);
    }
    
    return get_pure_biome_config(BIOME_PLAINS, temp, hum, cont);
}


struct BiomeTarget {
    BiomeType type;
    float c, t, h, m; // Ideal coordinates
};

static const BiomeTarget BIOME_TARGETS[] = {
    {BIOME_OCEAN,     -0.2f, 0.5f, 0.5f, 0.0f},
    {BIOME_BEACH,     -0.03f,0.5f, 0.5f, 0.0f},
    {BIOME_MOUNTAINS,  0.5f, 0.3f, 0.5f, 0.8f},
    {BIOME_DESERT,     0.5f, 0.85f,0.15f,0.0f},
    {BIOME_TAIGA,      0.5f, 0.15f,0.6f, 0.0f},
    {BIOME_JUNGLE,     0.5f, 0.85f,0.85f,0.0f},
    {BIOME_FOREST,     0.5f, 0.5f, 0.8f, 0.0f},
    {BIOME_PLAINS,     0.5f, 0.5f, 0.3f, 0.0f}
};

BiomeConfig get_blended_biome(double wx, double wz, int seed_offset, int radius) {
    // La identidad central (nombre, bloques, árboles) es 100% estricta
    BiomeConfig center = get_discrete_biome(wx, wz, seed_offset);
    
    // Mezcla de alturas y estructura en un radio físico (máx 2 chunks = 32 bloques)
    int r = 24; 
    
    BiomeConfig blended = center;
    float total_h = 0;
    float total_m = 0;
    float total_v_neg = 0;
    float total_v_pos = 0;
    float total_d = 0;
    
    float w_total = 0;
    
    // Muestreo circular físico en lugar de paramétrico
    for (int dx = -r; dx <= r; dx += 8) {
        for (int dz = -r; dz <= r; dz += 8) {
            float dist = std::sqrt(dx*dx + dz*dz);
            if (dist > r) continue;
            
            // Función de peso por distancia al punto exacto
            float weight = 1.0f / (dist + 4.0f);
            BiomeConfig c = get_discrete_biome(wx + dx, wz + dz, seed_offset);
            
            total_h += c.base_height * weight;
            total_m += c.mountain_scale * weight;
            total_v_neg += c.valley_neg_scale * weight;
            total_v_pos += c.valley_pos_scale * weight;
            total_d += c.detail_scale * weight;
            w_total += weight;
        }
    }
    
    blended.base_height = total_h / w_total;
    blended.mountain_scale = total_m / w_total;
    blended.valley_neg_scale = total_v_neg / w_total;
    blended.valley_pos_scale = total_v_pos / w_total;
    blended.detail_scale = total_d / w_total;
    
    return blended;
}

Color get_blended_grass_tint(double wx, double wz, int seed_offset, int /*radius*/) {
    double sx = wx + seed_offset;
    double sz = wz + seed_offset;
    
    double n_temp  = pnoise3(sx * 0.0009, 250.0, sz * 0.0009, 3, 0.5);
    double n_hum   = pnoise3(sx * 0.0009, 450.0, sz * 0.0009, 3, 0.5);
    double n_mount = pnoise3(sx * 0.0011, 750.0, sz * 0.0011, 3, 0.5);
    
    float temp = std::clamp((float)((n_temp + 0.8) * 0.625), 0.0f, 1.0f);
    float hum  = std::clamp((float)((n_hum + 0.8) * 0.625), 0.0f, 1.0f);
    float mount_t = std::clamp((float)((n_mount - 0.15) / 0.35), 0.0f, 1.0f);
    float mount_f = mount_t * mount_t * (3.0f - 2.0f * mount_t);
    
    Color col_taiga   = {72, 133, 98, 255};   // Frio
    Color col_plains  = {124, 189, 45, 255};  // Templado medio
    Color col_forest  = {88, 156, 40, 255};   // Templado humedo
    Color col_desert  = {174, 178, 72, 255};  // Calido seco
    Color col_jungle  = {50, 200, 34, 255};   // Calido humedo
    Color col_alpine  = {138, 182, 120, 255}; // Montana
    
    Color col_dry, col_wet;
    if (temp < 0.5f) {
        float t = temp / 0.5f;
        col_dry = lerp_color(col_taiga, col_plains, t);
        col_wet = lerp_color(col_taiga, col_forest, t);
    } else {
        float t = (temp - 0.5f) / 0.5f;
        col_dry = lerp_color(col_plains, col_desert, t);
        col_wet = lerp_color(col_forest, col_jungle, t);
    }
    
    Color climate_col = lerp_color(col_dry, col_wet, hum);
    return lerp_color(climate_col, col_alpine, mount_f * 0.7f);
}

Color get_blended_foliage_tint(double wx, double wz, int seed_offset, int /*radius*/) {
    double sx = wx + seed_offset;
    double sz = wz + seed_offset;
    
    double n_temp  = pnoise3(sx * 0.0009, 250.0, sz * 0.0009, 3, 0.5);
    double n_hum   = pnoise3(sx * 0.0009, 450.0, sz * 0.0009, 3, 0.5);
    double n_mount = pnoise3(sx * 0.0011, 750.0, sz * 0.0011, 3, 0.5);
    
    float temp = std::clamp((float)((n_temp + 0.8) * 0.625), 0.0f, 1.0f);
    float hum  = std::clamp((float)((n_hum + 0.8) * 0.625), 0.0f, 1.0f);
    float mount_t = std::clamp((float)((n_mount - 0.15) / 0.35), 0.0f, 1.0f);
    float mount_f = mount_t * mount_t * (3.0f - 2.0f * mount_t);
    
    Color col_taiga   = {60, 120, 85, 255};
    Color col_plains  = {110, 175, 40, 255};
    Color col_forest  = {75, 145, 35, 255};
    Color col_desert  = {160, 165, 65, 255};
    Color col_jungle  = {40, 185, 30, 255};
    Color col_alpine  = {115, 165, 110, 255};
    
    Color col_dry, col_wet;
    if (temp < 0.5f) {
        float t = temp / 0.5f;
        col_dry = lerp_color(col_taiga, col_plains, t);
        col_wet = lerp_color(col_taiga, col_forest, t);
    } else {
        float t = (temp - 0.5f) / 0.5f;
        col_dry = lerp_color(col_plains, col_desert, t);
        col_wet = lerp_color(col_forest, col_jungle, t);
    }
    
    Color climate_col = lerp_color(col_dry, col_wet, hum);
    return lerp_color(climate_col, col_alpine, mount_f * 0.7f);
}

const char* get_biome_name_at(double wx, double wz, int seed_offset) {
    static std::string current_name;
    BiomeConfig cfg = get_discrete_biome(wx, wz, seed_offset);
    current_name = cfg.name;
    return current_name.c_str();
}

}
