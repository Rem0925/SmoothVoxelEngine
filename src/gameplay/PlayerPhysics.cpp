#include "gameplay/PlayerPhysics.hpp"
#include "world/World.hpp"
#include "core/Config.hpp"
#include <raylib.h>
#include <raymath.h>
#include <cmath>
#include <algorithm>

void UpdatePlayerPhysics(World& world, Camera3D& camera, PlayerPhysicsState& state, Vector3 pre_move_pos, Vector3 post_move_pos) {
    if (!state.spectator_mode) {
        int cx = std::floor(camera.position.x / Config::CHUNK_SIZE);
        int cz = std::floor(camera.position.z / Config::CHUNK_SIZE);
        Chunk* current_chunk = world.get_chunk(cx, cz);

        if (current_chunk && current_chunk->is_ready) {
            float r = 0.28f;

            auto sample_density = [&](float x, float y, float z) -> float {
                int ix = (int)std::floor(x);
                int iy = (int)std::floor(y);
                int iz = (int)std::floor(z);
                float fx = x - ix;
                float fy = y - iy;
                float fz = z - iz;

                float d000 = world.get_density(ix,   iy,   iz);
                float d100 = world.get_density(ix+1, iy,   iz);
                float d010 = world.get_density(ix,   iy+1, iz);
                float d110 = world.get_density(ix+1, iy+1, iz);
                float d001 = world.get_density(ix,   iy,   iz+1);
                float d101 = world.get_density(ix+1, iy,   iz+1);
                float d011 = world.get_density(ix,   iy+1, iz+1);
                float d111 = world.get_density(ix+1, iy+1, iz+1);

                float c00 = d000 * (1.0f - fx) + d100 * fx;
                float c10 = d010 * (1.0f - fx) + d110 * fx;
                float c01 = d001 * (1.0f - fx) + d101 * fx;
                float c11 = d011 * (1.0f - fx) + d111 * fx;
                float c0  = c00  * (1.0f - fz) + c01  * fz;
                float c1  = c10  * (1.0f - fz) + c11  * fz;
                return c0 * (1.0f - fy) + c1 * fy;
            };

            auto is_solid = [&](float x, float y, float z) -> bool {
                int bx = (int)std::floor(x + 0.5f);
                int by = (int)std::floor(y + 0.5f);
                int bz = (int)std::floor(z + 0.5f);
                uint8_t b = world.get_block(bx, by, bz);
                if (b != Config::AIR && b != Config::WATER && Config::BLOCKS.count(b)) {
                    const auto& bt = Config::BLOCKS.at(b);
                    if (bt.shape != Config::SHAPE_TERRAIN) {
                        if (bt.shape == Config::SHAPE_DOOR) {
                            uint8_t rot = world.get_rotation(bx, by, bz);
                            if (rot & 4) return false;
                        }
                        return true;
                    }
                }
                return sample_density(x, y, z) >= Config::ISO_SURFACE;
            };

            auto check_wall = [&](float vx, float vz, float y) {
                return is_solid(vx - r, y, vz - r) || is_solid(vx + r, y, vz - r) ||
                       is_solid(vx - r, y, vz + r) || is_solid(vx + r, y, vz + r) ||
                       is_solid(vx - r, y, vz)     || is_solid(vx + r, y, vz)     ||
                       is_solid(vx, y, vz - r)     || is_solid(vx, y, vz + r);
            };

            auto sample_surface_at = [&](float px, float pz, float ref_feet_y) -> float {
                float ground_y = -999.0f;
                float scan_top = ref_feet_y + 0.80f;
                float scan_bot = ref_feet_y - 1.20f;

                float prev_y = scan_bot;
                float prev_d = sample_density(px, prev_y, pz);

                for (float y = scan_bot + 0.12f; y <= scan_top + 0.05f; y += 0.12f) {
                    float d = sample_density(px, y, pz);
                    if (prev_d >= Config::ISO_SURFACE && d < Config::ISO_SURFACE) {
                        float lo = prev_y;
                        float hi = y;
                        for (int i = 0; i < 8; i++) {
                            float mid = (lo + hi) * 0.5f;
                            if (sample_density(px, mid, pz) >= Config::ISO_SURFACE) lo = mid;
                            else hi = mid;
                        }
                        ground_y = (lo + hi) * 0.5f;
                        break;
                    }
                    prev_y = y;
                    prev_d = d;
                }

                if (ground_y < -900.0f) {
                    float d_bot = sample_density(px, scan_bot, pz);
                    float d_top = sample_density(px, scan_top, pz);
                    if (d_bot >= Config::ISO_SURFACE && d_top < Config::ISO_SURFACE) {
                        float lo = scan_bot;
                        float hi = scan_top;
                        for (int i = 0; i < 10; i++) {
                            float mid = (lo + hi) * 0.5f;
                            if (sample_density(px, mid, pz) >= Config::ISO_SURFACE) lo = mid;
                            else hi = mid;
                        }
                        ground_y = (lo + hi) * 0.5f;
                    }
                }

                int bx = (int)std::floor(px + 0.5f);
                int bz = (int)std::floor(pz + 0.5f);
                for (int dy = -1; dy <= 1; dy++) {
                    int by = (int)std::floor(ref_feet_y + 0.5f) + dy;
                    uint8_t b = world.get_block(bx, by, bz);
                    if (b != Config::AIR && b != Config::WATER && Config::BLOCKS.count(b) && Config::BLOCKS.at(b).shape != Config::SHAPE_TERRAIN) {
                        float block_top = (float)by + 0.5f;
                        if (block_top > ground_y && block_top <= ref_feet_y + 0.80f) {
                            ground_y = block_top;
                        }
                    }
                }

                return ground_y;
            };

            auto get_surface_height = [&](float px, float pz, float ref_feet_y) -> float {
                float max_ground = sample_surface_at(px, pz, ref_feet_y);
                float off = 0.18f;
                float g1 = sample_surface_at(px - off, pz - off, ref_feet_y);
                float g2 = sample_surface_at(px + off, pz - off, ref_feet_y);
                float g3 = sample_surface_at(px - off, pz + off, ref_feet_y);
                float g4 = sample_surface_at(px + off, pz + off, ref_feet_y);
                if (g1 > max_ground) max_ground = g1;
                if (g2 > max_ground) max_ground = g2;
                if (g3 > max_ground) max_ground = g3;
                if (g4 > max_ground) max_ground = g4;
                return max_ground;
            };

            float eye_y = camera.position.y;
            float feet_bottom = eye_y - Config::PLAYER_EYE_HEIGHT;
            float step_height = 0.55f;
            float feet_test_y = feet_bottom + step_height;
            float waist_y = eye_y - 0.75f;
            float head_y = eye_y + Config::PLAYER_HEAD_OFFSET - 0.06f;

            float new_x = camera.position.x;
            float old_x = pre_move_pos.x;
            float new_z = camera.position.z;
            float old_z = pre_move_pos.z;

            bool blocked_x = check_wall(new_x, old_z, feet_test_y) || check_wall(new_x, old_z, waist_y) || check_wall(new_x, old_z, head_y);
            if (blocked_x) {
                camera.position.x = old_x;
                new_x = old_x;
            }

            bool blocked_z = check_wall(new_x, new_z, feet_test_y) || check_wall(new_x, new_z, waist_y) || check_wall(new_x, new_z, head_y);
            if (blocked_z) {
                camera.position.z = old_z;
            }

            float dt = std::min(GetFrameTime(), 0.033f);
            state.player_vel_y -= 30.0f * dt;
            camera.position.y += state.player_vel_y * dt;

            float head_top = camera.position.y + Config::PLAYER_HEAD_OFFSET;
            if (state.player_vel_y > 0 && check_wall(camera.position.x, camera.position.z, head_top)) {
                camera.position.y = pre_move_pos.y;
                state.player_vel_y = 0.0f;
            }

            state.is_grounded = false;
            feet_bottom = camera.position.y - Config::PLAYER_EYE_HEIGHT;
            float ground_y = get_surface_height(camera.position.x, camera.position.z, feet_bottom);

            if (ground_y > -900.0f && state.player_vel_y <= 0.0f) {
                float diff = ground_y - feet_bottom;
                if (diff >= -0.40f && diff <= 0.85f) {
                    float target_cam_y = ground_y + Config::PLAYER_EYE_HEIGHT;
                    float dy = target_cam_y - camera.position.y;

                    camera.position.y = target_cam_y;
                    if (dy > 0.02f) {
                        state.smooth_step_offset -= dy;
                    }
                    state.player_vel_y = 0.0f;
                    state.is_grounded = true;
                }
            }

            if (state.is_grounded && IsKeyPressed(KEY_SPACE)) {
                state.player_vel_y = 10.5f;
            }

            if (camera.position.y < -20.0f) {
                camera.position.y = 80.0f;
                state.player_vel_y = 0.0f;
            }
        }

        camera.target.x += (camera.position.x - post_move_pos.x);
        camera.target.y += (camera.position.y - post_move_pos.y);
        camera.target.z += (camera.position.z - post_move_pos.z);
    } else {
        if (IsKeyDown(KEY_LEFT_SHIFT)) {
            Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));

            float ds = 40.0f * GetFrameTime();
            Vector3 move = {0,0,0};
            if (IsKeyDown(KEY_W)) move = Vector3Add(move, Vector3Scale(forward, ds));
            if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, Vector3Scale(forward, ds));
            if (IsKeyDown(KEY_D)) move = Vector3Add(move, Vector3Scale(right, ds));
            if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, Vector3Scale(right, ds));

            camera.position = Vector3Add(camera.position, move);
            camera.target = Vector3Add(camera.target, move);
        }
    }
}
