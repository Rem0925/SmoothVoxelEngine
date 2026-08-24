#include "CommandHandler.hpp"

namespace CommandHandler {
    void process(Chat& chat, float& day_time, Camera3D& camera, bool& spectator_mode, float& player_vel_y, UI& ui) {
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
            int block_id;
            if (sscanf(cmd.c_str(), "/give %d", &block_id) == 1) {
                ui.add_resource((uint8_t)block_id);
                chat.add_message("Bloque " + std::to_string(block_id) + " anadido al inventario");
            } else {
                chat.add_message("Uso: /give <block_id>");
            }
        }
        else if (cmd == "/spectator") {
            spectator_mode = !spectator_mode;
            player_vel_y = 0.0f; // Resetear gravedad al cambiar de modo
            chat.add_message(spectator_mode ? "Modo espectador activado" : "Modo espectador desactivado");
        }
        else if (cmd == "/help") {
            chat.add_message("Comandos: /time set <day|night>, /tp <x> <y> <z>, /give <id>, /spectator");
        }
        else {
            chat.add_message("Comando desconocido: " + cmd);
        }
    }
}
