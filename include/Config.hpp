#pragma once
#include <raylib.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <thread>

namespace Config {
    constexpr int GRID_X = 64;
    constexpr int GRID_Y = 128;
    constexpr int GRID_Z = 64;
    constexpr int CHUNK_SIZE = 16;
    constexpr int SUBCHUNK_SIZE = 16;
    constexpr int NUM_SUBCHUNKS = GRID_Y / SUBCHUNK_SIZE; // 8 secciones verticales de 16x16x16
    constexpr float ISO_SURFACE = 0.0f;

    inline const std::string WORLD_NAME = "world_1";
    constexpr int WORLD_SEED = 414432;
    
    constexpr int MAX_FPS = 120;
    constexpr int RENDER_DISTANCE = 6;
    constexpr float FOG_END = RENDER_DISTANCE * CHUNK_SIZE;
    constexpr float FOG_START = FOG_END * 0.7f;
    inline const int MAX_WORKER_THREADS = std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1;

    constexpr float WATER_LEVEL = 38.0f;

    // ===================== JUGADOR (Dimensiones estilo Minecraft) =====================
    constexpr float PLAYER_HEIGHT = 1.8f;
    constexpr float PLAYER_EYE_HEIGHT = 1.62f;
    constexpr float PLAYER_HEAD_OFFSET = PLAYER_HEIGHT - PLAYER_EYE_HEIGHT; // 0.18f
    constexpr float PLAYER_RADIUS = 0.30f;

    // ===================== ITEMS (no bloque) =====================

    enum ItemID : uint8_t {
        ITEM_STICK = 0,
        ITEM_COAL = 1,
        ITEM_PLANKS = 2,
        ITEM_COPPER_INGOT = 3,
        ITEM_IRON_INGOT = 4,
        ITEM_SILVER_INGOT = 5,
        ITEM_GOLD_INGOT = 6,
        ITEM_DIAMOND_INGOT = 7,
        ITEM_COUNT
    };

    // ===================== BLOCKS =====================

    enum BlockShape : uint8_t {
        SHAPE_TERRAIN = 0,        // Terreno suave generado con Marching Cubes
        SHAPE_CUBE = 1,           // Cubo solido ortogonal 6 caras
        SHAPE_STAIRS = 2,         // Escalera (peldaños en L con colision fluida)
        SHAPE_FENCE = 3,          // Valla (poste central + travesaños autoconectables)
        SHAPE_TORCH = 4,          // Antorcha 3D
        SHAPE_DOOR = 5,           // Puerta de 2 de altura (interactiva)
        SHAPE_CHEST = 6,          // Cofre (interactivo con almacenamiento)
        SHAPE_FURNACE = 7,        // Horno de piedra con boca frontal
        SHAPE_CRAFTING_TABLE = 8, // Mesa de crafteo con tablero
        SHAPE_GLASS = 9           // Cubo de cristal
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
        COAL_ORE = 16,
        IRON_ORE = 17,
        SILVER_ORE = 18,
        GOLD_ORE = 19,
        DIAMOND_ORE = 20,
        COBBLESTONE = 21,
        // Bloques de construccion y mobiliario
        PLANKS_CUBE = 22,
        STONE_BRICK = 23,
        STAIRS_WOOD = 24,
        STAIRS_STONE = 25,
        FENCE_WOOD = 26,
        DOOR_WOOD = 27,
        CHEST = 28,
        FURNACE = 29,
        CRAFTING_TABLE = 30,
        GLASS = 31,
        AIR = 255
    };

    // ===================== HERRAMIENTAS (enums antes de BLOCKS) =====================

    enum ToolType : uint8_t {
        TOOL_PICKAXE = 0,
        TOOL_AXE,
        TOOL_SHOVEL,
        TOOL_HAMMER,
        TOOL_FLAIL,
        TOOL_SWORD,
        TOOL_COUNT
    };

    enum ToolTier : uint8_t {
        TIER_WOOD = 0,
        TIER_STONE,
        TIER_IRON,
        TIER_SILVER,
        TIER_GOLD,
        TIER_DIAMOND,
        TIER_COUNT
    };

    struct CuboidElement {
        std::string name = "box";
        Vector3 from = {-0.5f, -0.5f, -0.5f};
        Vector3 to   = { 0.5f,  0.5f,  0.5f};
        int tex_top_x = -1, tex_top_y = -1;
        int tex_bottom_x = -1, tex_bottom_y = -1;
        int tex_front_x = -1, tex_front_y = -1;
        int tex_back_x = -1, tex_back_y = -1;
        int tex_left_x = -1, tex_left_y = -1;
        int tex_right_x = -1, tex_right_y = -1;
    };

    struct BlockType {
        std::string name;
        int tex_x;
        int tex_y;
        bool transparent;
        bool is_waving;
        uint8_t drop_id;        // que bloque/item suelta al minar (255 = ninguno)
        bool drop_is_item;      // true = drop_id es ItemID, false = drop_id es BlockID
        uint8_t ideal_tool;     // ToolType mas eficiente (255 = cualquiera)
        uint8_t require_tool;   // ToolType minimo para romper (255 = no requiere)
        uint8_t require_tier;   // ToolTier minimo (255 = inrompible)
        float hardness;         // tiempo base para romper
        uint8_t light_emission = 0; // Nivel de luz emitida (0 a 15, configurable por JSON)
        uint8_t light_filter = 15;  // Cuanta luz atenua al pasar (0 = transparente como aire/cristal, 1 = hojas, 15 = solido opaco)
        BlockShape shape = SHAPE_TERRAIN;
        // Texturas especificas por cara (si son -1, usan tex_x y tex_y)
        int tex_top_x = -1, tex_top_y = -1;
        int tex_bottom_x = -1, tex_bottom_y = -1;
        int tex_front_x = -1, tex_front_y = -1;
        int tex_latch_x = -1, tex_latch_y = -1; // Para el pomo/cerrojo del cofre
        int tex_icon_x = -1, tex_icon_y = -1;   // Portada / Icono de inventario (si es -1, usa fallback)
        std::vector<CuboidElement> elements;     // Modelos 3D por cuboides/partes
    };

    struct VoxelData {
        float density;
        uint8_t block;
        uint8_t water;
        uint8_t rotation = 0;
    };

    inline std::unordered_map<uint8_t, BlockType> BLOCKS;

    struct ItemType {
        std::string name;
        int item_tex_x;   // coordenada en spritesheet_items (columna)
        int item_tex_y;   // coordenada en spritesheet_items (fila)
    };

    inline std::unordered_map<uint8_t, ItemType> ITEMS;

    inline const char* TIER_NAMES[] = {
        "Madera", "Piedra", "Hierro", "Plata", "Oro", "Diamante"
    };

    inline const char* TOOL_TYPE_NAMES[] = {
        "Pico", "Hacha", "Pala", "Martillo", "Mangual", "Espada"
    };

    struct ToolInfo {
        ToolType type;
        ToolTier tier;
        std::string name;
        int durability;
        float mining_speed;    // multiplicador de velocidad de minado
        int item_tex_x;        // columna en spritesheet_items.png
        int item_tex_y;        // fila en spritesheet_items.png
    };

    inline std::vector<ToolInfo> TOOLS;

    // ===================== RECETAS DE CRAFTEO =====================

    struct RecipeIngredient {
        uint8_t id;      // BlockID o ItemID (depende de is_item)
        int count;
        bool is_item;    // true = ItemID, false = BlockID
    };

    struct CraftingRecipe {
        std::string result_name;
        int result_count;
        bool result_is_tool;           // true = crea ToolInfo
        ToolType result_tool_type;     // si result_is_tool
        ToolTier result_tool_tier;     // si result_is_tool
        bool result_is_block;          // true = BlockID, false = ItemID
        uint8_t result_id;             // BlockID o ItemID
        bool requires_table = false;   // true = solo disponible en Mesa de Crafteo 3x3
        std::vector<RecipeIngredient> ingredients;
    };

    inline std::vector<CraftingRecipe> RECIPES;
}
