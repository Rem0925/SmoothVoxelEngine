#include "rendering/ChunkDebugDraw.hpp"
#include "core/Config.hpp"
#include <rlgl.h>
#include <cmath>
#include <algorithm>

void DrawChunkDebug(Camera3D camera) {
    float CS = (float)Config::CHUNK_SIZE;
    float SCS = (float)Config::SUBCHUNK_SIZE;
    float GY = (float)Config::GRID_Y;

    int p_cx = (int)std::floor(camera.position.x / CS);
    int p_cz = (int)std::floor(camera.position.z / CS);
    int p_sub_idx = std::clamp((int)std::floor(camera.position.y / SCS), 0, Config::NUM_SUBCHUNKS - 1);

    rlDisableDepthTest();
    rlDisableBackfaceCulling();
    rlBegin(RL_LINES);

    Color neighbor_col = Fade(BLUE, 0.35f);
    Color neighbor_sub_col = Fade(BLUE, 0.18f);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            float nx0 = (p_cx + dx) * CS;
            float nz0 = (p_cz + dz) * CS;
            float nx1 = nx0 + CS;
            float nz1 = nz0 + CS;

            rlColor4ub(neighbor_col.r, neighbor_col.g, neighbor_col.b, neighbor_col.a);
            rlVertex3f(nx0, 0, nz0); rlVertex3f(nx0, GY, nz0);
            rlVertex3f(nx1, 0, nz0); rlVertex3f(nx1, GY, nz0);
            rlVertex3f(nx1, 0, nz1); rlVertex3f(nx1, GY, nz1);
            rlVertex3f(nx0, 0, nz1); rlVertex3f(nx0, GY, nz1);

            rlColor4ub(neighbor_sub_col.r, neighbor_sub_col.g, neighbor_sub_col.b, neighbor_sub_col.a);
            for (int s = 0; s <= Config::NUM_SUBCHUNKS; ++s) {
                float sy = s * SCS;
                rlVertex3f(nx0, sy, nz0); rlVertex3f(nx1, sy, nz0);
                rlVertex3f(nx1, sy, nz0); rlVertex3f(nx1, sy, nz1);
                rlVertex3f(nx1, sy, nz1); rlVertex3f(nx0, sy, nz1);
                rlVertex3f(nx0, sy, nz1); rlVertex3f(nx0, sy, nz0);
            }
        }
    }

    float x0 = p_cx * CS;
    float z0 = p_cz * CS;
    float x1 = x0 + CS;
    float z1 = z0 + CS;

    Color wall_grid_col = Color{ 255, 220, 0, 100 };
    rlColor4ub(wall_grid_col.r, wall_grid_col.g, wall_grid_col.b, wall_grid_col.a);
    for (int i = 1; i < Config::CHUNK_SIZE; ++i) {
        float ox = x0 + (float)i;
        float oz = z0 + (float)i;
        rlVertex3f(ox, 0, z0); rlVertex3f(ox, GY, z0);
        rlVertex3f(ox, 0, z1); rlVertex3f(ox, GY, z1);
        rlVertex3f(x0, 0, oz); rlVertex3f(x0, GY, oz);
        rlVertex3f(x1, 0, oz); rlVertex3f(x1, GY, oz);
    }

    Color section_col = Color{ 255, 230, 0, 240 };
    rlColor4ub(section_col.r, section_col.g, section_col.b, section_col.a);
    for (int s = 0; s <= Config::NUM_SUBCHUNKS; ++s) {
        float fy = (float)(s * SCS);
        rlVertex3f(x0, fy, z0); rlVertex3f(x1, fy, z0);
        rlVertex3f(x1, fy, z0); rlVertex3f(x1, fy, z1);
        rlVertex3f(x1, fy, z1); rlVertex3f(x0, fy, z1);
        rlVertex3f(x0, fy, z1); rlVertex3f(x0, fy, z0);
    }

    float sub_y0 = p_sub_idx * SCS;
    float sub_y1 = (p_sub_idx + 1) * SCS;
    Color active_sub_col = Color{ 0, 240, 255, 255 };
    rlColor4ub(active_sub_col.r, active_sub_col.g, active_sub_col.b, active_sub_col.a);

    rlVertex3f(x0, sub_y0, z0); rlVertex3f(x1, sub_y0, z0);
    rlVertex3f(x1, sub_y0, z0); rlVertex3f(x1, sub_y0, z1);
    rlVertex3f(x1, sub_y0, z1); rlVertex3f(x0, sub_y0, z1);
    rlVertex3f(x0, sub_y0, z1); rlVertex3f(x0, sub_y0, z0);
    rlVertex3f(x0, sub_y1, z0); rlVertex3f(x1, sub_y1, z0);
    rlVertex3f(x1, sub_y1, z0); rlVertex3f(x1, sub_y1, z1);
    rlVertex3f(x1, sub_y1, z1); rlVertex3f(x0, sub_y1, z1);
    rlVertex3f(x0, sub_y1, z1); rlVertex3f(x0, sub_y1, z0);
    rlVertex3f(x0, sub_y0, z0); rlVertex3f(x0, sub_y1, z0);
    rlVertex3f(x1, sub_y0, z0); rlVertex3f(x1, sub_y1, z0);
    rlVertex3f(x1, sub_y0, z1); rlVertex3f(x1, sub_y1, z1);
    rlVertex3f(x0, sub_y0, z1); rlVertex3f(x0, sub_y1, z1);

    Color sub_detail_col = Color{ 0, 210, 255, 120 };
    rlColor4ub(sub_detail_col.r, sub_detail_col.g, sub_detail_col.b, sub_detail_col.a);
    for (int y = (int)sub_y0 + 2; y < (int)sub_y1; y += 2) {
        float fy = (float)y;
        rlVertex3f(x0, fy, z0); rlVertex3f(x1, fy, z0);
        rlVertex3f(x1, fy, z0); rlVertex3f(x1, fy, z1);
        rlVertex3f(x1, fy, z1); rlVertex3f(x0, fy, z1);
        rlVertex3f(x0, fy, z1); rlVertex3f(x0, fy, z0);
    }

    Color corner_col = Color{ 255, 30, 30, 255 };
    rlColor4ub(corner_col.r, corner_col.g, corner_col.b, corner_col.a);
    rlVertex3f(x0, 0, z0); rlVertex3f(x0, GY, z0);
    rlVertex3f(x1, 0, z0); rlVertex3f(x1, GY, z0);
    rlVertex3f(x1, 0, z1); rlVertex3f(x1, GY, z1);
    rlVertex3f(x0, 0, z1); rlVertex3f(x0, GY, z1);

    rlEnd();
    rlEnableDepthTest();
    rlEnableBackfaceCulling();
}
