#pragma once
#include <raylib.h>
#include <vector>
#include <cstdint>
#include "Config.hpp"
#include "UI.hpp"

class World;

struct ItemDrop {
    Vector3 position;
    Vector3 prev_position;
    Vector3 velocity;
    uint8_t id = 0;
    bool is_item = false;
    bool is_tool = false;
    Config::ToolType tool_type = Config::TOOL_PICKAXE;
    Config::ToolTier tool_tier = Config::TIER_WOOD;
    int durability = 100;
    int count = 1;
    float lifetime = 0.0f;
    float pickup_delay = 0.2f;
    float rot_y = 0.0f;
};

class ItemDropManager {
public:
    std::vector<ItemDrop> drops;

    void spawn(Vector3 pos, uint8_t id, bool is_item, int count = 1, Vector3 initial_vel = {0, 0, 0}, float pickup_delay = 0.2f);
    void spawn_tool(Vector3 pos, Config::ToolType type, Config::ToolTier tier, int durability, Vector3 initial_vel = {0, 0, 0}, float pickup_delay = 0.8f);
    void update(float dt, World& world, Vector3 player_pos, UI& ui);
    void draw(Texture2D spritesheet_tiles, Texture2D spritesheet_items, float light, float alpha = 1.0f);
    void clear();
};
