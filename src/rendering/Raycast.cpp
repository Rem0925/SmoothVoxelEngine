#include "rendering/Raycast.hpp"
#include "world/World.hpp"
#include "core/Config.hpp"
#include <raymath.h>
#include <cmath>
#include <mutex>
#include <vector>
#include <memory>

bool VoxelRaycastSmooth(World& world, Vector3 origin, Vector3 dir, float max_dist,
                        Vector3& out_hit, Vector3& out_solid, Vector3& out_empty) {
    Ray ray = { origin, dir };

    RayCollision closest_hit = { 0 };
    closest_hit.distance = max_dist;
    closest_hit.hit = false;

    bool hit_is_build = false;

    // Thread-safe snapshot to avoid data race with chunk loading/unloading
    std::vector<std::shared_ptr<Chunk>> snapshot;
    {
        std::lock_guard<std::mutex> lock(world.chunks_mutex);
        auto& chunks_map = world.get_chunks();
        snapshot.reserve(chunks_map.size());
        for (auto& [key, chunk] : chunks_map) {
            snapshot.push_back(chunk);
        }
    }

    for (auto& c : snapshot) {
        if (!c->is_ready) continue;

        float cx_center = c->cx * Config::CHUNK_SIZE + Config::CHUNK_SIZE / 2.0f;
        float cz_center = c->cz * Config::CHUNK_SIZE + Config::CHUNK_SIZE / 2.0f;
        float dist_sq = (origin.x - cx_center)*(origin.x - cx_center) + (origin.z - cz_center)*(origin.z - cz_center);

        float max_r = max_dist + (Config::CHUNK_SIZE * 1.5f);
        if (dist_sq > max_r * max_r) continue;

        BoundingBox box = {
            { (float)(c->cx * Config::CHUNK_SIZE) - 1.0f, 0.0f, (float)(c->cz * Config::CHUNK_SIZE) - 1.0f },
            { (float)((c->cx + 1) * Config::CHUNK_SIZE) + 1.0f, (float)Config::GRID_Y, (float)((c->cz + 1) * Config::CHUNK_SIZE) + 1.0f }
        };
        if (!GetRayCollisionBox(ray, box).hit) continue;

        for (int s = 0; s < Config::NUM_SUBCHUNKS; ++s) {
            auto& sc = c->subchunks[s];
            BoundingBox sbox = {
                { (float)(c->cx * Config::CHUNK_SIZE) - 0.5f, (float)(s * Config::SUBCHUNK_SIZE) - 0.5f, (float)(c->cz * Config::CHUNK_SIZE) - 0.5f },
                { (float)((c->cx + 1) * Config::CHUNK_SIZE) + 0.5f, (float)((s + 1) * Config::SUBCHUNK_SIZE) + 0.5f, (float)((c->cz + 1) * Config::CHUNK_SIZE) + 0.5f }
            };
            if (!GetRayCollisionBox(ray, sbox).hit) continue;

            if (sc.solid_mesh.vertexCount > 0) {
                RayCollision mesh_hit = GetRayCollisionMesh(ray, sc.solid_mesh, MatrixIdentity());
                if (mesh_hit.hit && mesh_hit.distance < closest_hit.distance) {
                    closest_hit = mesh_hit;
                    hit_is_build = false;
                }
            }

            if (sc.build_mesh.vertexCount > 0) {
                RayCollision build_hit = GetRayCollisionMesh(ray, sc.build_mesh, MatrixIdentity());
                if (build_hit.hit && build_hit.distance < closest_hit.distance) {
                    closest_hit = build_hit;
                    hit_is_build = true;
                }
            }

            if (sc.trans_mesh.vertexCount > 0) {
                RayCollision trans_hit = GetRayCollisionMesh(ray, sc.trans_mesh, MatrixIdentity());
                if (trans_hit.hit && trans_hit.distance < closest_hit.distance) {
                    closest_hit = trans_hit;

                    int bx = std::round(trans_hit.point.x - trans_hit.normal.x * 0.02f);
                    int by = std::round(trans_hit.point.y - trans_hit.normal.y * 0.02f);
                    int bz = std::round(trans_hit.point.z - trans_hit.normal.z * 0.02f);
                    uint8_t b = c->get_block(bx - c->cx * Config::CHUNK_SIZE, by, bz - c->cz * Config::CHUNK_SIZE);
                    if (b == Config::AIR) {
                        bx = std::floor(trans_hit.point.x);
                        by = std::floor(trans_hit.point.y);
                        bz = std::floor(trans_hit.point.z);
                        b = c->get_block(bx - c->cx * Config::CHUNK_SIZE, by, bz - c->cz * Config::CHUNK_SIZE);
                    }
                    if (Config::BLOCKS.count(b) && Config::BLOCKS.at(b).shape != Config::SHAPE_TERRAIN) {
                        hit_is_build = true;
                    } else {
                        hit_is_build = false;
                    }
                }
            }
        }
    }

    if (closest_hit.hit) {
        out_hit = closest_hit.point;

        if (hit_is_build) {
            int bx = std::round(out_hit.x - closest_hit.normal.x * 0.02f);
            int by = std::round(out_hit.y - closest_hit.normal.y * 0.02f);
            int bz = std::round(out_hit.z - closest_hit.normal.z * 0.02f);
            out_solid = { (float)bx, (float)by, (float)bz };

            Vector3 n = closest_hit.normal;
            Vector3 card = {0, 0, 0};
            if (std::abs(n.y) >= std::abs(n.x) && std::abs(n.y) >= std::abs(n.z)) {
                card.y = (n.y > 0) ? 1.0f : -1.0f;
            } else if (std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z)) {
                card.x = (n.x > 0) ? 1.0f : -1.0f;
            } else {
                card.z = (n.z > 0) ? 1.0f : -1.0f;
            }
            out_empty = Vector3Add(out_solid, card);
            return true;
        }

        int ix = std::floor(out_hit.x);
        int iy = std::floor(out_hit.y);
        int iz = std::floor(out_hit.z);

        float best_dist_solid = 9999.0f;
        Vector3 best_solid = { (float)ix, (float)iy, (float)iz };

        float best_dist_empty = 9999.0f;
        Vector3 best_empty = { (float)ix, (float)iy, (float)iz };

        for (int dx = 0; dx <= 1; dx++) {
            for (int dy = 0; dy <= 1; dy++) {
                for (int dz = 0; dz <= 1; dz++) {
                    int nx = ix + dx;
                    int ny = iy + dy;
                    int nz = iz + dz;

                    if (ny >= 0 && ny < Config::GRID_Y) {
                        float den = world.get_density(nx, ny, nz);
                        Vector3 node_pos = { (float)nx, (float)ny, (float)nz };
                        float dist = Vector3Distance(out_hit, node_pos);

                        if (den >= Config::ISO_SURFACE) {
                            if (dist < best_dist_solid) {
                                best_dist_solid = dist;
                                best_solid = node_pos;
                            }
                        } else {
                            if (dist < best_dist_empty) {
                                best_dist_empty = dist;
                                best_empty = node_pos;
                            }
                        }
                    }
                }
            }
        }

        out_solid = best_solid;
        out_empty = best_empty;
        return true;
    }

    return false;
}
