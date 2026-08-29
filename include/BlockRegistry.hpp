#pragma once
#include <string>
#include <vector>
#include "Config.hpp"

namespace BlockRegistry {
    // Carga todos los bloques, items y herramientas desde data_dir (default: "assets/data")
    bool load_all(const std::string& data_dir = "assets/data");
    
    bool load_atlas_config(const std::string& config_file);
    bool load_blocks(const std::string& blocks_dir);
    bool load_items(const std::string& items_dir);
    bool load_tools(const std::string& tools_dir);
    bool load_recipes(const std::string& recipes_dir);
}
