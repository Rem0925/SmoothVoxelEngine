#pragma once
#include <raylib.h>
#include <vector>
#include <string>
#include <map>
#include <tuple>
#include "core/Config.hpp"

struct InventorySlot {
    uint8_t id = Config::AIR;
    std::string name = "";
    int count = 0;
};

struct ToolSlot {
    Config::ToolType type = Config::TOOL_PICKAXE;
    Config::ToolTier tier = Config::TIER_WOOD;
    int durability_current = 0;
    int durability_max = 0;
    bool active = false;
};

struct ChestSlot {
    bool is_tool = false;
    ToolSlot tool;
    InventorySlot item;
};

struct ChestData {
    std::vector<ChestSlot> slots;
    ChestData() { slots.resize(27); }
};

struct FurnaceData {
    InventorySlot input;
    InventorySlot fuel;
    InventorySlot output;
    int burn_ticks = 0;
    int max_burn_ticks = 0;
    int cook_ticks = 0;
};

enum UIMode {
    UI_MODE_CLOSED,
    UI_MODE_INVENTORY,
    UI_MODE_CRAFTING_TABLE,
    UI_MODE_CHEST,
    UI_MODE_FURNACE
};

class UI {
public:
    // Hotbar: slot 0 = herramienta activa, slots 1-9 = bloques/items
    std::vector<InventorySlot> slots;        // 10 slots: [0]=tool, [1-9]=blocks
    std::vector<InventorySlot> storage;      // 27 slots: almacenamiento extra (9x3)
    std::vector<ToolSlot> tool_inventory;    // herramientas poseídas (prioridad)
    int selected_slot = 0;
    int selected_tool_idx = 0;               // indice en tool_inventory para slot 0
    bool is_open = false;
    UIMode mode = UI_MODE_CLOSED;
    Vector3 active_container_pos = {0, 0, 0};
    
    // Contenedores del mundo (Persistencia)
    std::map<std::tuple<int, int, int>, ChestData> world_chests;
    std::map<std::tuple<int, int, int>, FurnaceData> world_furnaces;

    Texture2D spritesheet;
    Texture2D spritesheet_items;             // para iconos de herramientas

    // Drag & drop seguro
    bool is_dragging = false;
    InventorySlot drag_item;                 // item/bloque siendo arrastrado
    ToolSlot drag_tool;                      // herramienta siendo arrastrada
    bool dragging_tool = false;              // true si arrastramos herramienta
    int drag_source_type = -1;               // 0=hotbar, 1=tool_inv, 2=storage, 3=chest, 4=furnace_input, 5=furnace_fuel, 6=furnace_output
    int drag_source_idx = -1;                // indice del slot origen

    // Crafteo & Filtros
    int craft_category = 0;                  // 0=Todos, 1=Herramientas, 2=Materiales, 3=Disponibles
    int craft_scroll_offset = 0;             // scroll para panel de crafting
    std::string tooltip_text = "";

    UI(Texture2D sheet, Texture2D items_sheet);
    ~UI();

    void update();
    void tick_furnaces();                    // Ejecutado en cada tick de 20 TPS
    void draw();
    
    void toggle_inventory();
    void open_crafting_table(Vector3 pos);
    void open_chest(Vector3 pos);
    void open_furnace(Vector3 pos);
    void close_ui();
    void select_slot(int index);

    // Herramientas
    void add_tool(Config::ToolType type, Config::ToolTier tier, int durability = -1);
    void remove_active_tool();
    ToolSlot* get_active_tool();
    void cycle_tool(int direction);     // rueda del mouse en slot 0
    void cycle_block(int direction);    // rueda del mouse en slots 1-9

    // Bloques/Items
    void add_resource(uint8_t block_type, int count = 1);
    void add_item(uint8_t item_id, int count = 1);
    bool has_item(uint8_t item_id, int count = 1) const;
    bool remove_item(uint8_t item_id, int count = 1);
    int count_block(uint8_t block_id) const;
    bool remove_block(uint8_t block_id, int count = 1);
    void consume_held_item();

    // Crafteo
    bool can_craft(int recipe_index) const;
    void craft(int recipe_index, int times = 1);

    // Cancelar arrastre de forma segura
    void cancel_drag();

private:
    void draw_hotbar();
    void draw_tool_hud();
    void draw_inventory_panel();
    void draw_crafting_table_panel();
    void draw_chest_panel();
    void draw_furnace_panel();
    void draw_player_inventory_section(int px, int py, int mouse_x, int mouse_y, bool is_l_click, bool is_r_click, bool is_shift, bool show_tools = true);

    void draw_block_icon(uint8_t block_id, int x, int y, int size);
    void draw_tool_icon(Config::ToolType type, Config::ToolTier tier, int x, int y, int size);
    void draw_item_icon(uint8_t item_id, int x, int y, int size);
};
