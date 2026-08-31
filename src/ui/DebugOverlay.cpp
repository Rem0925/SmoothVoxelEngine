#include "ui/DebugOverlay.hpp"
#include "world/World.hpp"
#include "core/Config.hpp"
#include "world/VoxelLighting.hpp"
#include "generation/Biome.hpp"
#include "ui/UI.hpp"
#include "ui/Chat.hpp"
#include <raylib.h>
#include <raymath.h>
#include <cmath>
#include <string>
#include <vector>

using namespace Config;

void DrawDebugOverlay(const DebugOverlayParams& p) {
    int fps = GetFPS();
    Color fps_color = (fps >= 55) ? Color{80, 250, 100, 255} : ((fps >= 30) ? Color{250, 210, 60, 255} : Color{250, 70, 70, 255});

    Vector3 look_dir = Vector3Normalize(Vector3Subtract(p.camera.target, p.camera.position));
    const char* facing = "Desconocido";
    if (std::abs(look_dir.x) > std::abs(look_dir.z)) {
        facing = (look_dir.x > 0) ? "Este (+X)" : "Oeste (-X)";
    } else {
        facing = (look_dir.z > 0) ? "Sur (+Z)" : "Norte (-Z)";
    }

    int p_cx = (int)std::floor(p.camera.position.x / (float)Config::CHUNK_SIZE);
    int p_cz = (int)std::floor(p.camera.position.z / (float)Config::CHUNK_SIZE);
    int in_cx = ((p.cam_x % Config::CHUNK_SIZE) + Config::CHUNK_SIZE) % Config::CHUNK_SIZE;
    int in_cz = ((p.cam_z % Config::CHUNK_SIZE) + Config::CHUNK_SIZE) % Config::CHUNK_SIZE;

    int seed_offset = static_cast<int>(static_cast<uint32_t>(Config::WORLD_SEED) * 1000U);
    const char* biome_name = Biome::get_biome_name_at(p.camera.position.x, p.camera.position.z, seed_offset);

    uint8_t look_block = AIR;
    std::string block_name = "Aire";
    if (!p.ui.is_open && !p.chat.is_open && p.ray_hit_valid) {
        look_block = p.world.get_block(p.target_solid.x, p.target_solid.y, p.target_solid.z);
        if (look_block != AIR && BLOCKS.count(look_block)) {
            block_name = BLOCKS.at(look_block).name;
        }
    }

    std::string time_str;
    float norm_time = std::fmod(p.day_time, 6.28318f);
    if (norm_time < 0) norm_time += 6.28318f;
    if (norm_time >= 0.78f && norm_time < 2.35f) time_str = "Dia";
    else if (norm_time >= 2.35f && norm_time < 3.92f) time_str = "Atardecer";
    else if (norm_time >= 3.92f && norm_time < 5.49f) time_str = "Noche";
    else time_str = "Amanecer";

    int feet_y = (int)std::floor(p.camera.position.y - Config::PLAYER_EYE_HEIGHT);
    std::vector<std::pair<std::string, Color>> lines;
    lines.push_back({ TextFormat("SmoothVoxelEngine C++ | %d FPS", fps), fps_color });
    lines.push_back({ TextFormat("XYZ: %.2f / %.2f / %.2f", p.camera.position.x, p.camera.position.y, p.camera.position.z), WHITE });
    int sub_y_idx = std::clamp((int)std::floor(p.camera.position.y / (float)Config::SUBCHUNK_SIZE), 0, Config::NUM_SUBCHUNKS - 1);
    int in_sub_y = ((feet_y % Config::SUBCHUNK_SIZE) + Config::SUBCHUNK_SIZE) % Config::SUBCHUNK_SIZE;
    lines.push_back({ TextFormat("Chunk: %d, %d (Sub-chunk %d/%d)  [En sub-chunk: %d, %d, %d]", p_cx, p_cz, sub_y_idx, Config::NUM_SUBCHUNKS - 1, in_cx, in_sub_y, in_cz), Color{220, 225, 235, 255} });
    lines.push_back({ TextFormat("Orientacion: %s", facing), Color{200, 215, 235, 255} });
    lines.push_back({ TextFormat("Bioma: %s", biome_name), Color{140, 230, 160, 255} });

    if (look_block != AIR) {
        lines.push_back({ TextFormat("Mirando a: %s (ID %d en %d, %d, %d)", block_name.c_str(), (int)look_block, (int)p.target_solid.x, (int)p.target_solid.y, (int)p.target_solid.z), Color{255, 220, 100, 255} });

        uint8_t raw_l = p.world.get_light(p.target_empty.x, p.target_empty.y, p.target_empty.z);
        if (raw_l == 0) raw_l = p.world.get_light(p.target_solid.x, p.target_solid.y, p.target_solid.z);
        uint8_t sun_l = VoxelLighting::get_sunlight(raw_l);
        uint8_t blk_l = VoxelLighting::get_blocklight(raw_l);
        uint8_t max_l = std::max(sun_l, blk_l);
        lines.push_back({ TextFormat("Luz del bloque: %d (Sol: %d, Bloque: %d)", (int)max_l, (int)sun_l, (int)blk_l), Color{255, 235, 120, 255} });
    } else {
        lines.push_back({ "Mirando a: Aire", Color{170, 180, 195, 255} });
        uint8_t raw_l = p.world.get_light(p.cam_x, feet_y, p.cam_z);
        uint8_t sun_l = VoxelLighting::get_sunlight(raw_l);
        uint8_t blk_l = VoxelLighting::get_blocklight(raw_l);
        uint8_t max_l = std::max(sun_l, blk_l);
        lines.push_back({ TextFormat("Luz en jugador: %d (Sol: %d, Bloque: %d)", (int)max_l, (int)sun_l, (int)blk_l), Color{255, 235, 120, 255} });
    }

    lines.push_back({ TextFormat("Hora: %.2f (%s)", norm_time, time_str.c_str()), Color{240, 240, 200, 255} });

    if (p.spectator_mode) {
        lines.push_back({ "[ MODO ESPECTADOR ]", Color{100, 210, 255, 255} });
    }
    if (p.show_chunks) {
        lines.push_back({ "[ LIMITES DE CHUNK ACTIVOS (F3+G) ]", Color{255, 215, 80, 255} });
    }

    int cur_y = 10;
    int font_size = 14;
    int line_h = 20;
    for (const auto& [txt, col] : lines) {
        int tw = MeasureText(txt.c_str(), font_size);
        DrawRectangle(8, cur_y - 2, tw + 10, line_h, Fade(Color{14, 18, 24, 255}, 0.75f));
        DrawText(txt.c_str(), 12, cur_y, font_size, col);
        cur_y += line_h + 2;
    }
}
