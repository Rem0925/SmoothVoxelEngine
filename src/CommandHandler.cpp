#include "CommandHandler.hpp"

namespace CommandHandler {
    void process(Chat& chat, float& day_time, Camera3D& camera, bool& spectator_mode, float& player_vel_y, UI& ui, bool& show_chunks) {
        std::string cmd = chat.get_pending_command();
        if (cmd.empty()) return;

        if (cmd.find("/time set ") == 0) {
            std::string arg = cmd.substr(10);
            if (arg == "day") day_time = 1.57f;
            else if (arg == "night") day_time = 4.71f;
            else if (arg == "sunrise") day_time = 0.0f;
            else if (arg == "sunset") day_time = 3.14f;
            else {
                try { day_time = std::stof(arg); } catch (...) {}
            }
            chat.add_message("Tiempo cambiado a " + arg);
        }
        else if (cmd.find("/tp ") == 0) {
            float tx, ty, tz;
            if (sscanf(cmd.c_str(), "/tp %f %f %f", &tx, &ty, &tz) == 3) {
                camera.position = {tx, ty, tz};
                camera.target = {tx, ty, tz + 1.0f};
                chat.add_message("Teletransportado a " + std::to_string(tx) + ", " + std::to_string(ty) + ", " + std::to_string(tz));
            } else {
                chat.add_message("Uso: /tp <x> <y> <z>");
            }
        }
        else if (cmd.find("/give ") == 0) {
            std::string arg = cmd.substr(6);
            // /give tool <type> <tier> — dar herramienta
            if (arg.find("tool ") == 0) {
                int ttype, ttier;
                if (sscanf(arg.c_str(), "tool %d %d", &ttype, &ttier) == 2) {
                    if (ttype >= 0 && ttype < (int)Config::TOOL_COUNT && ttier >= 0 && ttier < (int)Config::TIER_COUNT) {
                        ui.add_tool((Config::ToolType)ttype, (Config::ToolTier)ttier);
                        chat.add_message("Herramienta anadida");
                    } else {
                        chat.add_message("Valores fuera de rango");
                    }
                } else {
                    chat.add_message("Uso: /give tool <tipo> <tier>  (tipos: 0-5, tiers: 0-5)");
                }
            }
            // /give item <id> [cantidad] — dar item (stick=0, coal=1, planks=2, etc.)
            else if (arg.find("item ") == 0) {
                int item_id = 0, count = 1;
                sscanf(arg.c_str(), "item %d %d", &item_id, &count);
                if (item_id >= 0 && item_id < (int)Config::ITEM_COUNT) {
                    ui.add_item((uint8_t)item_id, count);
                    chat.add_message("Item anadido x" + std::to_string(count));
                } else {
                    chat.add_message("ID de item invalido (0-7)");
                }
            }
            // /give <block_id> — dar bloque (compatibilidad anterior)
            else {
                int block_id;
                if (sscanf(arg.c_str(), "%d", &block_id) == 1) {
                    ui.add_resource((uint8_t)block_id);
                    chat.add_message("Bloque " + std::to_string(block_id) + " anadido");
                } else {
                    chat.add_message("Uso: /give <id> | /give item <id> [cant] | /give tool <tipo> <tier>");
                }
            }
        }
        else if (cmd == "/spectator") {
            spectator_mode = !spectator_mode;
            player_vel_y = 0.0f; // Resetear gravedad al cambiar de modo
            chat.add_message(spectator_mode ? "Modo espectador activado" : "Modo espectador desactivado");
        }
        else if (cmd == "/chunks") {
            show_chunks = !show_chunks;
            chat.add_message(show_chunks ? "Limites de chunks visibles" : "Limites de chunks ocultos");
        }
        else if (cmd == "/help") {
            chat.add_message("Comandos: /time set <day|night>, /tp <x> <y> <z>, /give <id>, /give item <id> [cant], /give tool <tipo> <tier>, /spectator, /chunks");
        }
        else {
            chat.add_message("Comando desconocido: " + cmd);
        }
    }
}
