#include "world/FluidSimulation.hpp"
#include "world/World.hpp"
#include "world/Chunk.hpp"
#include "core/Config.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>

static inline uint64_t cell_key(int wx, int wy, int wz) {
    // wx: 20 bits (0-19), wy: 10 bits (20-29, offset 64 cubre 0..959), wz: 20 bits (30-49)
    return (uint64_t)(wx + (1 << 19)) | ((uint64_t)(wy + 64) << 20) | ((uint64_t)(wz + (1 << 19)) << 30);
}

static inline void cell_decode(uint64_t k, int& wx, int& wy, int& wz) {
    wx = (int)(k & 0xFFFFF) - (1 << 19);
    wy = (int)((k >> 20) & 0x3FF) - 64;
    wz = (int)((k >> 30) & 0xFFFFF) - (1 << 19);
}

FluidSimulator::FluidSimulator(World* w) : world(w) {
    if (world) {
        start();
    }
}

FluidSimulator::~FluidSimulator() {
    stop();
}

void FluidSimulator::set_world(World* w) {
    stop();
    world = w;
    if (world) {
        start();
    }
}

void FluidSimulator::start() {
    if (!sim_running) {
        sim_running = true;
        sim_thread = std::thread([this] { sim_loop(); });
    }
}

void FluidSimulator::stop() {
    sim_running = false;
    if (sim_thread.joinable()) {
        sim_thread.join();
    }
}

void FluidSimulator::sim_loop() {
    while (sim_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        simulate_water_pass();
    }
}

void FluidSimulator::activate(int wx, int wy, int wz) {
    if (wx < -(1 << 19) || wx >= (1 << 19) || wz < -(1 << 19) || wz >= (1 << 19)) return;
    if (wy < 0 || wy >= Config::GRID_Y) return;
    std::lock_guard<std::mutex> lock(active_mutex);
    if (active_cells.size() < 5000) {
        active_cells.push_back(cell_key(wx, wy, wz));
    }
}

void FluidSimulator::activate_neighbors(int wx, int wy, int wz) {
    activate(wx, wy, wz);
    activate(wx + 1, wy, wz);
    activate(wx - 1, wy, wz);
    activate(wx, wy, wz + 1);
    activate(wx, wy, wz - 1);
    if (wy + 1 < Config::GRID_Y) activate(wx, wy + 1, wz);
    if (wy > 0) activate(wx, wy - 1, wz);
}

void FluidSimulator::step() {
    simulate_water_pass();
}

void FluidSimulator::simulate_water_pass() {
    if (!world) return;

    std::vector<std::pair<std::pair<int,int>, std::shared_ptr<Chunk>>> snap;
    {
        std::lock_guard<std::mutex> lock(world->chunks_mutex);
        snap.reserve(world->chunks.size());
        for (auto& kv : world->chunks) {
            snap.emplace_back(kv.first, kv.second);
        }
    }
    if (snap.empty()) return;

    std::sort(snap.begin(), snap.end(), [](const auto& a, const auto& b) {
        if (a.first.first != b.first.first) return a.first.first < b.first.first;
        return a.first.second < b.first.second;
    });

    auto snap_find = [&](int cx, int cz) -> std::shared_ptr<Chunk>* {
        auto key = std::make_pair(cx, cz);
        auto it = std::lower_bound(snap.begin(), snap.end(), key, [](const auto& a, const auto& b_key) {
            if (a.first.first != b_key.first) return a.first.first < b_key.first;
            return a.first.second < b_key.second;
        });
        if (it != snap.end() && it->first == key && it->second->is_ready) return &it->second;
        return nullptr;
    };

    std::vector<uint64_t> to_process;
    {
        std::lock_guard<std::mutex> lock(active_mutex);
        to_process.swap(active_cells);
    }
    if (to_process.empty()) return;

    std::vector<FluidEdit> edits;
    edits.reserve(512);
    std::vector<uint64_t> next_active;
    next_active.reserve(512);

    const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    auto add_neighborhood = [&](int wx, int wy, int wz) {
        next_active.push_back(cell_key(wx, wy, wz));
        for (int d = 0; d < 4; ++d) {
            next_active.push_back(cell_key(wx + dirs[d][0], wy, wz + dirs[d][1]));
        }
        if (wy > 0) next_active.push_back(cell_key(wx, wy - 1, wz));
        if (wy < Config::GRID_Y - 1) next_active.push_back(cell_key(wx, wy + 1, wz));
    };

    for (uint64_t k : to_process) {
        if (edits.size() >= 2000) { next_active.push_back(k); continue; }
        int wx, wy, wz;
        cell_decode(k, wx, wy, wz);
        if (wy <= 0 || wy >= Config::GRID_Y) continue;

        int ccx = std::floor((float)wx / Config::CHUNK_SIZE);
        int ccz = std::floor((float)wz / Config::CHUNK_SIZE);
        std::shared_ptr<Chunk>* found = snap_find(ccx, ccz);
        if (!found) continue;
        Chunk* c = found->get();
        int lx = wx - ccx * Config::CHUNK_SIZE;
        int lz = wz - ccz * Config::CHUNK_SIZE;

        uint8_t L = c->get_water_level(lx, wy, lz);
        if (L == 0) continue;

        auto get_target = [&](int wwx, int wwz) -> Chunk* {
            int ncx = std::floor((float)wwx / Config::CHUNK_SIZE);
            int ncz = std::floor((float)wwz / Config::CHUNK_SIZE);
            std::shared_ptr<Chunk>* nb = snap_find(ncx, ncz);
            return nb ? nb->get() : nullptr;
        };

        auto read_block = [&](int wx2, int wy2, int wz2) -> uint8_t {
            if (wy2 < 0 || wy2 >= Config::GRID_Y) return Config::AIR;
            int tl = wx2 - ccx * Config::CHUNK_SIZE;
            int tlz = wz2 - ccz * Config::CHUNK_SIZE;
            Chunk* target = c;
            if (tl < 0) { target = get_target(wx2, wz2); tl += Config::CHUNK_SIZE; }
            else if (tl > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tl -= Config::CHUNK_SIZE; }
            if (tlz < 0) { target = get_target(wx2, wz2); tlz += Config::CHUNK_SIZE; }
            else if (tlz > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tlz -= Config::CHUNK_SIZE; }
            if (!target) return Config::AIR;
            return target->get_block(tl, wy2, tlz);
        };

        auto read_level = [&](int wx2, int wy2, int wz2) -> uint8_t {
            if (wy2 < 0 || wy2 >= Config::GRID_Y) return 0;
            int tl = wx2 - ccx * Config::CHUNK_SIZE;
            int tlz = wz2 - ccz * Config::CHUNK_SIZE;
            Chunk* target = c;
            if (tl < 0) { target = get_target(wx2, wz2); tl += Config::CHUNK_SIZE; }
            else if (tl > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tl -= Config::CHUNK_SIZE; }
            if (tlz < 0) { target = get_target(wx2, wz2); tlz += Config::CHUNK_SIZE; }
            else if (tlz > Config::CHUNK_SIZE) { target = get_target(wx2, wz2); tlz -= Config::CHUNK_SIZE; }
            if (!target) return 0;
            return target->get_water_level(tl, wy2, tlz);
        };

        bool is_source = (L == 8);
        uint8_t expected = 0;

        if (is_source) {
            expected = 8;
        } else {
            // Check for infinite water source generation (2 adjacent sources + solid below)
            int source_count = 0;
            for (int d = 0; d < 4; ++d) {
                if (read_level(wx + dirs[d][0], wy, wz + dirs[d][1]) == 8) source_count++;
            }
            
            bool has_solid_below = false;
            if (wy > 0) {
                uint8_t below = read_block(wx, wy - 1, wz);
                if (below != Config::AIR && below != Config::WATER) has_solid_below = true;
            } else {
                has_solid_below = true;
            }

            if (source_count >= 2 && has_solid_below) {
                expected = 8;
            } else {
                uint8_t above = read_level(wx, wy + 1, wz);
                if (above > 0) {
                    expected = 7;
                } else {
                    for (int d = 0; d < 4; ++d) {
                        uint8_t nL = read_level(wx + dirs[d][0], wy, wz + dirs[d][1]);
                        if (nL == 8) expected = std::max(expected, (uint8_t)7);
                        else if (nL > 1 && nL <= 7) expected = std::max(expected, (uint8_t)(nL - 1));
                    }
                }
            }
        }

        if (L != expected) {
            edits.push_back({wx, wy, wz, expected, FluidType::WATER});
            add_neighborhood(wx, wy, wz);
        }

        if (expected > 0) {
            bool can_fall = (wy > 0 && (read_block(wx, wy - 1, wz) == Config::AIR || read_block(wx, wy - 1, wz) == Config::WATER));
            
            if (can_fall) {
                uint8_t below = read_level(wx, wy - 1, wz);
                if (below != 7 && below != 8) {
                    edits.push_back({wx, wy - 1, wz, 7, FluidType::WATER});
                    add_neighborhood(wx, wy - 1, wz);
                }
            } else if (expected > 1) {
                for (int d = 0; d < 4; ++d) {
                    int nwx = wx + dirs[d][0];
                    int nwz = wz + dirs[d][1];
                    uint8_t nb = read_block(nwx, wy, nwz);
                    if (nb == Config::AIR || nb == Config::WATER) {
                        uint8_t nl = read_level(nwx, wy, nwz);
                        if (nl < expected - 1) {
                            edits.push_back({nwx, wy, nwz, (uint8_t)(expected - 1), FluidType::WATER});
                            add_neighborhood(nwx, wy, nwz);
                        }
                    }
                }
            }
        }
    }

    for (const auto& e : edits) {
        set_water_node(snap, e.x, e.y, e.z, e.level);
    }

    {
        std::lock_guard<std::mutex> lock(active_mutex);
        std::sort(next_active.begin(), next_active.end());
        next_active.erase(std::unique(next_active.begin(), next_active.end()), next_active.end());
        for (uint64_t k : next_active) {
            if (active_cells.size() < 5000) active_cells.push_back(k);
        }
    }
}

void FluidSimulator::set_water_node(const std::vector<std::pair<std::pair<int,int>, std::shared_ptr<Chunk>>>& snap, int wx, int wy, int wz, uint8_t level) {
    if (wy < 0 || wy >= Config::GRID_Y) return;
    int cx = std::floor((float)wx / Config::CHUNK_SIZE);
    int cz = std::floor((float)wz / Config::CHUNK_SIZE);
    int lx = wx - cx * Config::CHUNK_SIZE;
    int lz = wz - cz * Config::CHUNK_SIZE;

    auto apply = [&](int ccx, int ccz, int llx, int llz) {
        auto key = std::make_pair(ccx, ccz);
        auto it = std::lower_bound(snap.begin(), snap.end(), key, [](const auto& a, const auto& b_key) {
            if (a.first.first != b_key.first) return a.first.first < b_key.first;
            return a.first.second < b_key.second;
        });
        if (it != snap.end() && it->first == key && it->second->is_ready) {
            it->second->set_water_node(llx, wy, llz, level);
        }
    };

    apply(cx, cz, lx, lz);
    if (lx == 0) apply(cx - 1, cz, Config::CHUNK_SIZE, lz);
    if (lz == 0) apply(cx, cz - 1, lx, Config::CHUNK_SIZE);
    if (lx == 0 && lz == 0) apply(cx - 1, cz - 1, Config::CHUNK_SIZE, Config::CHUNK_SIZE);
}
