#pragma once
#include <raylib.h>
#include <vector>
#include <string>
#include "Config.hpp"

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

class UI {
public:
    // Hotbar: slot 0 = herramienta activa, slots 1-9 = bloques/items
    std::vector<InventorySlot> slots;        // 10 slots: [0]=tool, [1-9]=blocks
    std::vector<InventorySlot> storage;      // 18 slots: almacenamiento extra (6x3)
    std::vector<ToolSlot> tool_inventory;    // herramientas poseídas (prioridad)
    int selected_slot = 0;
    int selected_tool_idx = 0;               // indice en tool_inventory para slot 0
    bool is_open = false;
    Texture2D spritesheet;
    Texture2D spritesheet_items;             // para iconos de herramientas

    // Drag & drop
    bool is_dragging = false;
    InventorySlot drag_item;                 // item/bloque siendo arrastrado
    ToolSlot drag_tool;                      // herramienta siendo arrastrada
    bool dragging_tool = false;              // true si arrastramos herramienta
    int drag_source_type = -1;               // 0=hotbar, 1=tool_inv, 2=storage

    UI(Texture2D sheet, Texture2D items_sheet);
    ~UI();

    void update();
    void draw();
    
    void toggle_inventory();
    void select_slot(int index);

    // Herramientas
    void add_tool(Config::ToolType type, Config::ToolTier tier);
    void remove_active_tool();
    ToolSlot* get_active_tool();
    void cycle_tool(int direction);     // rueda del mouse en slot 0
    void cycle_block(int direction);    // rueda del mouse en slots 1-9

    // Bloques/Items
    void add_resource(uint8_t block_type);
    void add_item(uint8_t item_id, int count = 1);
    bool has_item(uint8_t item_id, int count = 1) const;
    bool remove_item(uint8_t item_id, int count = 1);

    // Crafteo
    bool can_craft(int recipe_index) const;
    void craft(int recipe_index);
    int craft_scroll_offset = 0;  // scroll para panel de crafting

    // Guardar/cargar inventario
    void save_inventory();
    void load_inventory();

    // Herramientas: logs -> tablas, tablas -> palitos, etc.
    void add_log();  //para testing, agregar tronco

private:
    void draw_hotbar();
    void draw_tool_hud();
    void draw_inventory_panel();
    void draw_block_icon(uint8_t block_id, int x, int y, int size);
    void draw_tool_icon(Config::ToolType type, Config::ToolTier tier, int x, int y, int size);
    void draw_item_icon(uint8_t item_id, int x, int y, int size);
};
