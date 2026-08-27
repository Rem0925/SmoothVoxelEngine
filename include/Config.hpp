#pragma once
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
        BlockShape shape = SHAPE_TERRAIN;
        // Texturas especificas por cara (si son -1, usan tex_x y tex_y)
        int tex_top_x = -1, tex_top_y = -1;
        int tex_bottom_x = -1, tex_bottom_y = -1;
        int tex_front_x = -1, tex_front_y = -1;
        int tex_latch_x = -1, tex_latch_y = -1; // Para el pomo/cerrojo del cofre
        int tex_icon_x = -1, tex_icon_y = -1;   // Portada / Icono de inventario (si es -1, usa fallback)
    };

    struct VoxelData {
        float density;
        uint8_t block;
        uint8_t water;
        uint8_t rotation = 0;
    };

    inline const std::unordered_map<uint8_t, BlockType> BLOCKS = {
        //                            name             tex_x tex_y trans wave  drop     is_item ideal_tool  req_tool  req_tier hard  shape
        {GRASS,       {"Pasto",              6,  8, false, false, DIRT,     false,  TOOL_SHOVEL, 255,  0,  0.5f, SHAPE_TERRAIN}},
        {STONE,       {"Piedra",             3,  5, false, false, COBBLESTONE,false, TOOL_PICKAXE, 255,  0,  1.5f, SHAPE_TERRAIN}},
        {DIRT,        {"Tierra",             7,  4, false, false, DIRT,     false,  TOOL_SHOVEL, 255,  0,  0.5f, SHAPE_TERRAIN}},
        {TORCH,       {"Antorcha",           2,  6, false, false, TORCH,    false,  255,  255,  0,  0.1f, SHAPE_TORCH}},
        {WOOD,        {"Madera Roble",       1,  9, false, false, WOOD,     false,  TOOL_AXE,  255,  0,  2.0f, SHAPE_TERRAIN}},
        {LEAVES,      {"Hojas",              4,  1, false, true,  LEAVES,   false,  255,  255,  0,  0.2f, SHAPE_TERRAIN}},
        {SAND,        {"Arena",              3,  3, false, false, SAND,     false,  TOOL_SHOVEL, 255,  0,  0.5f, SHAPE_TERRAIN}},
        {WATER,       {"Agua",               0,  3, true,  false, WATER,    false,  255,  255,  255, 999.f, SHAPE_TERRAIN}},
        {TALL_GRASS,  {"Pasto Alto",         6,  5, true,  true,  255,      false,  255,  255,  0,  0.1f, SHAPE_TERRAIN}},
        {RED_MUSHROOM,{"Seta Roja",          4,  4, true,  false, RED_MUSHROOM,false, 255, 255,  0,  0.1f, SHAPE_TERRAIN}},
        {BROWN_MUSHROOM,{"Seta Marron",      4,  3, true,  false, BROWN_MUSHROOM,false, 255, 255, 0,  0.1f, SHAPE_TERRAIN}},
        {DEAD_BUSH,   {"Arbusto Seco",       6,  7, true,  true,  DEAD_BUSH,false,  255,  255,  0,  0.1f, SHAPE_TERRAIN}},
        {GRAVEL,      {"Grava",              6,  9, false, false, GRAVEL,   false,  TOOL_SHOVEL, 255,  0,  0.6f, SHAPE_TERRAIN}},
        {RED_CLAY,    {"Arcilla Roja",       3,  0, false, false, RED_CLAY, false,  TOOL_SHOVEL, 255,  0,  0.6f, SHAPE_TERRAIN}},
        {BIRCH_WOOD,  {"Madera Abedul",      0,  1, false, false, BIRCH_WOOD,false, TOOL_AXE,  255,  0,  2.0f, SHAPE_TERRAIN}},
        {CACTUS,      {"Cactus",             8,  8, false, false, CACTUS,   false,  TOOL_AXE,  255,  0,  0.4f, SHAPE_TERRAIN}},
        // Minerales
        {COAL_ORE,    {"Mineral de Carbon",  3,  8, false, false, ITEM_COAL,true,   TOOL_PICKAXE, 255,  0,  3.0f, SHAPE_TERRAIN}},
        {IRON_ORE,    {"Mineral de Hierro",  3,  6, false, false, ITEM_IRON_INGOT,true, TOOL_PICKAXE, TOOL_PICKAXE, 0,  3.0f, SHAPE_TERRAIN}},
        {SILVER_ORE,  {"Mineral de Plata",   1,  0, false, false, ITEM_SILVER_INGOT,true, TOOL_PICKAXE, TOOL_PICKAXE, 1,  3.0f, SHAPE_TERRAIN}},
        {GOLD_ORE,    {"Mineral de Oro",     2,  3, false, false, ITEM_GOLD_INGOT,true, TOOL_PICKAXE, TOOL_PICKAXE, 2,  3.0f, SHAPE_TERRAIN}},
        {DIAMOND_ORE, {"Mineral de Diamante",2,  0, false, false, ITEM_DIAMOND_INGOT,true, TOOL_PICKAXE, TOOL_PICKAXE, 3,  3.0f, SHAPE_TERRAIN}},
        {COBBLESTONE, {"Adoquin",            3,  5, false, false, COBBLESTONE,false,  TOOL_PICKAXE, 255,  0,  2.0f, SHAPE_TERRAIN}},
        
        // Bloques de Construccion y Mobiliario
        {PLANKS_CUBE,    {"Tablas de Madera",   0,  8, false, false, PLANKS_CUBE, false, TOOL_AXE,  255,  0,  1.2f, SHAPE_CUBE}},
        {STONE_BRICK,    {"Ladrillos de Piedra",4,  7, false, false, STONE_BRICK, false, TOOL_PICKAXE, 255, 0, 1.8f, SHAPE_CUBE}},
        {STAIRS_WOOD,    {"Escalera de Madera", 0,  8, false, false, STAIRS_WOOD, false, TOOL_AXE,  255,  0,  1.2f, SHAPE_STAIRS}},
        {STAIRS_STONE,   {"Escalera de Piedra", 4,  7, false, false, STAIRS_STONE,false, TOOL_PICKAXE, 255, 0, 1.8f, SHAPE_STAIRS}},
        {FENCE_WOOD,     {"Valla de Madera",    0,  8, false, false, FENCE_WOOD,  false, TOOL_AXE,  255,  0,  1.0f, SHAPE_FENCE}},
        {DOOR_WOOD,      {"Puerta de Madera",   0,  8, false, false, DOOR_WOOD,   false, TOOL_AXE,  255,  0,  1.5f, SHAPE_DOOR, 0, 8, 0, 8, 0, 8}},
        {CHEST,          {"Cofre",              1,  9, false, false, CHEST,       false, TOOL_AXE,  255,  0,  1.5f, SHAPE_CHEST, -1, -1, -1, -1, -1, -1, 0, 8, 1, 9}},
        {FURNACE,        {"Horno",              3,  5, false, false, FURNACE,     false, TOOL_PICKAXE, 255, 0, 2.0f, SHAPE_FURNACE, 3, 5, 3, 5, 4, 5, -1, -1, 4, 5}},
        {CRAFTING_TABLE, {"Mesa de Crafteo",    0,  8, false, false, CRAFTING_TABLE,false,TOOL_AXE, 255,  0,  1.5f, SHAPE_CRAFTING_TABLE, 1, 2, 0, 8, 0, 8, -1, -1, 1, 2}},
        {GLASS,          {"Vidrio",             5,  6, true,  false, 255,         false, TOOL_PICKAXE, 255, 0, 0.3f, SHAPE_GLASS}},
    };

    struct ItemType {
        std::string name;
        int item_tex_x;   // coordenada en spritesheet_items (columna)
        int item_tex_y;   // coordenada en spritesheet_items (fila)
    };

    // Coordenadas en spritesheet_items.png (128x128 tiles, 7 cols x 8 rows)
    // Organizado: fila por tier, items al final
    // tex_y va de abajo hacia arriba (como el block atlas)
    // tex_y=0: Vacio (fila inferior del PNG)
    // tex_y=1: Items (stick, coal, planks, ingots)
    // tex_y=2: Diamond tools
    // tex_y=3: Gold tools
    // tex_y=4: Silver tools
    // tex_y=5: Iron tools
    // tex_y=6: Stone tools
    // tex_y=7: Wood tools (fila superior del PNG)
    // Columnas: 0=pick, 1=axe, 2=shovel, 3=hammer, 4=flail, 5=sword, 6=item extra
    inline const std::unordered_map<uint8_t, ItemType> ITEMS = {
        {ITEM_STICK,          {"Palo",              0, 1}},
        {ITEM_COAL,           {"Carbon",            1, 1}},
        {ITEM_PLANKS,         {"Tablas",            2, 1}},
        {ITEM_COPPER_INGOT,   {"Ingoto de Cobre",   3, 1}},
        {ITEM_IRON_INGOT,     {"Ingoto de Hierro",  3, 1}},
        {ITEM_SILVER_INGOT,   {"Ingoto de Plata",   4, 1}},
        {ITEM_GOLD_INGOT,     {"Ingoto de Oro",     5, 1}},
        {ITEM_DIAMOND_INGOT,  {"Ingoto de Diamante",6, 1}},
    };

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

    // Coordenadas spritesheet_items.png (128px tiles, 7 columnas)
    // tex_y va de abajo hacia arriba:
    // tex_y=7: Wood tools (fila superior del PNG, copias de bronze)
    // tex_y=6: Stone tools (copias de iron)
    // tex_y=5: Iron tools
    // tex_y=4: Silver tools
    // tex_y=3: Gold tools
    // tex_y=2: Diamond tools
    // tex_y=1: Items
    // tex_y=0: Vacio
    // Columnas: 0=pick, 1=axe, 2=shovel, 3=hammer, 4=flail, 5=sword
    inline const std::vector<ToolInfo> TOOLS = {
        // WOOD (tex_y=7, fila superior del PNG)
        {TOOL_PICKAXE, TIER_WOOD,     "Pico de Madera",        100, 1.0f,  0, 7},
        {TOOL_AXE,     TIER_WOOD,     "Hacha de Madera",       100, 1.5f,  1, 7},
        {TOOL_SHOVEL,  TIER_WOOD,     "Pala de Madera",        100, 2.0f,  2, 7},
        {TOOL_HAMMER,  TIER_WOOD,     "Martillo de Madera",    150, 1.0f,  3, 7},
        {TOOL_FLAIL,   TIER_WOOD,     "Mangual de Madera",      80, 1.0f,  4, 7},
        {TOOL_SWORD,   TIER_WOOD,     "Espada de Madera",       90, 1.0f,  5, 7},
        // STONE (tex_y=6)
        {TOOL_PICKAXE, TIER_STONE,    "Pico de Piedra",        200, 1.5f,  0, 6},
        {TOOL_AXE,     TIER_STONE,    "Hacha de Piedra",       200, 2.0f,  1, 6},
        {TOOL_SHOVEL,  TIER_STONE,    "Pala de Piedra",        200, 2.5f,  2, 6},
        {TOOL_HAMMER,  TIER_STONE,    "Martillo de Piedra",    300, 1.2f,  3, 6},
        {TOOL_FLAIL,   TIER_STONE,    "Mangual de Piedra",     160, 1.3f,  4, 6},
        {TOOL_SWORD,   TIER_STONE,    "Espada de Piedra",      180, 1.2f,  5, 6},
        // IRON (tex_y=5)
        {TOOL_PICKAXE, TIER_IRON,     "Pico de Hierro",        300, 2.0f,  0, 5},
        {TOOL_AXE,     TIER_IRON,     "Hacha de Hierro",       300, 2.5f,  1, 5},
        {TOOL_SHOVEL,  TIER_IRON,     "Pala de Hierro",        300, 3.0f,  2, 5},
        {TOOL_HAMMER,  TIER_IRON,     "Martillo de Hierro",    400, 1.4f,  3, 5},
        {TOOL_FLAIL,   TIER_IRON,     "Mangual de Hierro",     250, 1.5f,  4, 5},
        {TOOL_SWORD,   TIER_IRON,     "Espada de Hierro",      260, 1.3f,  5, 5},
        // SILVER (tex_y=4)
        {TOOL_PICKAXE, TIER_SILVER,   "Pico de Plata",         200, 2.5f,  0, 4},
        {TOOL_AXE,     TIER_SILVER,   "Hacha de Plata",        200, 3.0f,  1, 4},
        {TOOL_SHOVEL,  TIER_SILVER,   "Pala de Plata",         200, 3.5f,  2, 4},
        {TOOL_HAMMER,  TIER_SILVER,   "Martillo de Plata",     300, 1.6f,  3, 4},
        {TOOL_FLAIL,   TIER_SILVER,   "Mangual de Plata",      180, 2.0f,  4, 4},
        {TOOL_SWORD,   TIER_SILVER,   "Espada de Plata",       190, 1.6f,  5, 4},
        // GOLD (tex_y=3)
        {TOOL_PICKAXE, TIER_GOLD,     "Pico de Oro",           100, 3.5f,  0, 3},
        {TOOL_AXE,     TIER_GOLD,     "Hacha de Oro",          100, 4.0f,  1, 3},
        {TOOL_SHOVEL,  TIER_GOLD,     "Pala de Oro",           100, 4.5f,  2, 3},
        {TOOL_HAMMER,  TIER_GOLD,     "Martillo de Oro",       150, 1.8f,  3, 3},
        {TOOL_FLAIL,   TIER_GOLD,     "Mangual de Oro",         80, 2.5f,  4, 3},
        {TOOL_SWORD,   TIER_GOLD,     "Espada de Oro",          90, 2.0f,  5, 3},
        // DIAMOND (tex_y=2)
        {TOOL_PICKAXE, TIER_DIAMOND,  "Pico de Diamante",      600, 3.0f,  0, 2},
        {TOOL_AXE,     TIER_DIAMOND,  "Hacha de Diamante",     600, 3.5f,  1, 2},
        {TOOL_SHOVEL,  TIER_DIAMOND,  "Pala de Diamante",      600, 4.0f,  2, 2},
        {TOOL_HAMMER,  TIER_DIAMOND,  "Martillo de Diamante",  800, 2.0f,  3, 2},
        {TOOL_FLAIL,   TIER_DIAMOND,  "Mangual de Diamante",   500, 3.0f,  4, 2},
        {TOOL_SWORD,   TIER_DIAMOND,  "Espada de Diamante",    550, 2.5f,  5, 2},
    };

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
        std::vector<RecipeIngredient> ingredients;
    };

    inline const std::vector<CraftingRecipe> RECIPES = {
        // === Materiales y Bloques de Construccion ===
        // 1 Tronco -> 4 Tablas (Item)
        {"Tablas (Item)", 4, false, TOOL_COUNT, TIER_COUNT, false, ITEM_PLANKS,
         {{WOOD, 1, false}}},
        // 1 Tronco -> 4 Tablas de Madera (Bloque solido)
        {"Tablas de Madera", 4, false, TOOL_COUNT, TIER_COUNT, true, PLANKS_CUBE,
         {{WOOD, 1, false}}},
        // 2 Tablas -> 4 Palitos
        {"Palitos", 4, false, TOOL_COUNT, TIER_COUNT, false, ITEM_STICK,
         {{ITEM_PLANKS, 2, true}}},
        // 4 Adoquin -> 4 Ladrillos de Piedra
        {"Ladrillos de Piedra", 4, false, TOOL_COUNT, TIER_COUNT, true, STONE_BRICK,
         {{COBBLESTONE, 4, false}}},
        // 6 Tablas -> 4 Escaleras de Madera
        {"Escalera de Madera", 4, false, TOOL_COUNT, TIER_COUNT, true, STAIRS_WOOD,
         {{ITEM_PLANKS, 6, true}}},
        // 6 Adoquin -> 4 Escaleras de Piedra
        {"Escalera de Piedra", 4, false, TOOL_COUNT, TIER_COUNT, true, STAIRS_STONE,
         {{COBBLESTONE, 6, false}}},
        // 4 Tablas + 2 Palitos -> 3 Vallas de Madera
        {"Valla de Madera", 3, false, TOOL_COUNT, TIER_COUNT, true, FENCE_WOOD,
         {{ITEM_PLANKS, 4, true}, {ITEM_STICK, 2, true}}},
        // 1 Palo + 1 Carbon -> 4 Antorchas
        {"Antorcha", 4, false, TOOL_COUNT, TIER_COUNT, true, TORCH,
         {{ITEM_STICK, 1, true}, {ITEM_COAL, 1, true}}},
        // 6 Tablas -> 3 Puertas de Madera
        {"Puerta de Madera", 3, false, TOOL_COUNT, TIER_COUNT, true, DOOR_WOOD,
         {{ITEM_PLANKS, 6, true}}},
        // 8 Tablas -> 1 Cofre
        {"Cofre", 1, false, TOOL_COUNT, TIER_COUNT, true, CHEST,
         {{ITEM_PLANKS, 8, true}}},
        // 8 Adoquin -> 1 Horno
        {"Horno", 1, false, TOOL_COUNT, TIER_COUNT, true, FURNACE,
         {{COBBLESTONE, 8, false}}},
        // 4 Tablas -> 1 Mesa de Crafteo
        {"Mesa de Crafteo", 1, false, TOOL_COUNT, TIER_COUNT, true, CRAFTING_TABLE,
         {{ITEM_PLANKS, 4, true}}},

        // === Herramientas de MADERA ===
        // Pico: 3 Tablas + 2 Palitos
        {"Pico de Madera", 1, true, TOOL_PICKAXE, TIER_WOOD, false, 0,
         {{ITEM_PLANKS, 3, true}, {ITEM_STICK, 2, true}}},
        // Hacha: 3 Tablas + 2 Palitos
        {"Hacha de Madera", 1, true, TOOL_AXE, TIER_WOOD, false, 0,
         {{ITEM_PLANKS, 3, true}, {ITEM_STICK, 2, true}}},
        // Pala: 1 Tabla + 2 Palitos
        {"Pala de Madera", 1, true, TOOL_SHOVEL, TIER_WOOD, false, 0,
         {{ITEM_PLANKS, 1, true}, {ITEM_STICK, 2, true}}},
        // Martillo: 4 Tablas + 2 Palitos
        {"Martillo de Madera", 1, true, TOOL_HAMMER, TIER_WOOD, false, 0,
         {{ITEM_PLANKS, 4, true}, {ITEM_STICK, 2, true}}},
        // Mangual: 1 Tabla + 3 Palitos
        {"Mangual de Madera", 1, true, TOOL_FLAIL, TIER_WOOD, false, 0,
         {{ITEM_PLANKS, 1, true}, {ITEM_STICK, 3, true}}},
        // Espada: 2 Tablas + 1 Palo
        {"Espada de Madera", 1, true, TOOL_SWORD, TIER_WOOD, false, 0,
         {{ITEM_PLANKS, 2, true}, {ITEM_STICK, 1, true}}},

        // === Herramientas de PIEDRA ===
        {"Pico de Piedra", 1, true, TOOL_PICKAXE, TIER_STONE, false, 0,
         {{COBBLESTONE, 3, false}, {ITEM_STICK, 2, true}}},
        {"Hacha de Piedra", 1, true, TOOL_AXE, TIER_STONE, false, 0,
         {{COBBLESTONE, 3, false}, {ITEM_STICK, 2, true}}},
        {"Pala de Piedra", 1, true, TOOL_SHOVEL, TIER_STONE, false, 0,
         {{COBBLESTONE, 1, false}, {ITEM_STICK, 2, true}}},
        {"Martillo de Piedra", 1, true, TOOL_HAMMER, TIER_STONE, false, 0,
         {{COBBLESTONE, 4, false}, {ITEM_STICK, 2, true}}},
        {"Mangual de Piedra", 1, true, TOOL_FLAIL, TIER_STONE, false, 0,
         {{COBBLESTONE, 1, false}, {ITEM_STICK, 3, true}}},
        {"Espada de Piedra", 1, true, TOOL_SWORD, TIER_STONE, false, 0,
         {{COBBLESTONE, 2, false}, {ITEM_STICK, 1, true}}},

        // === Herramientas de HIERRO ===
        {"Pico de Hierro", 1, true, TOOL_PICKAXE, TIER_IRON, false, 0,
         {{ITEM_IRON_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Hacha de Hierro", 1, true, TOOL_AXE, TIER_IRON, false, 0,
         {{ITEM_IRON_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Pala de Hierro", 1, true, TOOL_SHOVEL, TIER_IRON, false, 0,
         {{ITEM_IRON_INGOT, 2, true}, {ITEM_STICK, 2, true}}},
        {"Martillo de Hierro", 1, true, TOOL_HAMMER, TIER_IRON, false, 0,
         {{ITEM_IRON_INGOT, 4, true}, {ITEM_STICK, 2, true}}},
        {"Mangual de Hierro", 1, true, TOOL_FLAIL, TIER_IRON, false, 0,
         {{ITEM_IRON_INGOT, 2, true}, {ITEM_STICK, 3, true}}},
        {"Espada de Hierro", 1, true, TOOL_SWORD, TIER_IRON, false, 0,
         {{ITEM_IRON_INGOT, 2, true}, {ITEM_STICK, 1, true}}},

        // === Herramientas de PLATA ===
        {"Pico de Plata", 1, true, TOOL_PICKAXE, TIER_SILVER, false, 0,
         {{ITEM_SILVER_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Hacha de Plata", 1, true, TOOL_AXE, TIER_SILVER, false, 0,
         {{ITEM_SILVER_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Pala de Plata", 1, true, TOOL_SHOVEL, TIER_SILVER, false, 0,
         {{ITEM_SILVER_INGOT, 2, true}, {ITEM_STICK, 2, true}}},
        {"Martillo de Plata", 1, true, TOOL_HAMMER, TIER_SILVER, false, 0,
         {{ITEM_SILVER_INGOT, 4, true}, {ITEM_STICK, 2, true}}},
        {"Mangual de Plata", 1, true, TOOL_FLAIL, TIER_SILVER, false, 0,
         {{ITEM_SILVER_INGOT, 2, true}, {ITEM_STICK, 3, true}}},
        {"Espada de Plata", 1, true, TOOL_SWORD, TIER_SILVER, false, 0,
         {{ITEM_SILVER_INGOT, 2, true}, {ITEM_STICK, 1, true}}},

        // === Herramientas de ORO ===
        {"Pico de Oro", 1, true, TOOL_PICKAXE, TIER_GOLD, false, 0,
         {{ITEM_GOLD_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Hacha de Oro", 1, true, TOOL_AXE, TIER_GOLD, false, 0,
         {{ITEM_GOLD_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Pala de Oro", 1, true, TOOL_SHOVEL, TIER_GOLD, false, 0,
         {{ITEM_GOLD_INGOT, 2, true}, {ITEM_STICK, 2, true}}},
        {"Martillo de Oro", 1, true, TOOL_HAMMER, TIER_GOLD, false, 0,
         {{ITEM_GOLD_INGOT, 4, true}, {ITEM_STICK, 2, true}}},
        {"Mangual de Oro", 1, true, TOOL_FLAIL, TIER_GOLD, false, 0,
         {{ITEM_GOLD_INGOT, 2, true}, {ITEM_STICK, 3, true}}},
        {"Espada de Oro", 1, true, TOOL_SWORD, TIER_GOLD, false, 0,
         {{ITEM_GOLD_INGOT, 2, true}, {ITEM_STICK, 1, true}}},

        // === Herramientas de DIAMANTE ===
        {"Pico de Diamante", 1, true, TOOL_PICKAXE, TIER_DIAMOND, false, 0,
         {{ITEM_DIAMOND_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Hacha de Diamante", 1, true, TOOL_AXE, TIER_DIAMOND, false, 0,
         {{ITEM_DIAMOND_INGOT, 3, true}, {ITEM_STICK, 2, true}}},
        {"Pala de Diamante", 1, true, TOOL_SHOVEL, TIER_DIAMOND, false, 0,
         {{ITEM_DIAMOND_INGOT, 2, true}, {ITEM_STICK, 2, true}}},
        {"Martillo de Diamante", 1, true, TOOL_HAMMER, TIER_DIAMOND, false, 0,
         {{ITEM_DIAMOND_INGOT, 4, true}, {ITEM_STICK, 2, true}}},
        {"Mangual de Diamante", 1, true, TOOL_FLAIL, TIER_DIAMOND, false, 0,
         {{ITEM_DIAMOND_INGOT, 2, true}, {ITEM_STICK, 3, true}}},
        {"Espada de Diamante", 1, true, TOOL_SWORD, TIER_DIAMOND, false, 0,
         {{ITEM_DIAMOND_INGOT, 2, true}, {ITEM_STICK, 1, true}}},
    };
}
