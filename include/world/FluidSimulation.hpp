#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <utility>

class World;
class Chunk;

enum class FluidType : uint8_t {
    NONE = 0,
    WATER = 1,
    LAVA = 2
};

struct FluidProperties {
    int tick_rate_ms = 150;                  // Velocidad de propagación en ms (150ms agua, 500ms lava)
    uint8_t max_level = 8;                   // 8 = bloque fuente, 1..7 = flujo decreciente
    uint8_t min_flow_level = 1;              // Nivel mínimo antes de evaporarse/desaparecer
    int flow_distance = 7;                   // Distancia horizontal máxima (7 agua, 3 lava)
    bool can_create_infinite_sources = true; // Si 2 fuentes crean una nueva (agua sí, lava no)
};

struct FluidEdit {
    int x, y, z;
    uint8_t level;
    FluidType type = FluidType::WATER;
};

class FluidSimulator {
public:
    FluidSimulator(World* world = nullptr);
    ~FluidSimulator();

    void set_world(World* w);
    void start();
    void stop();

    // Activa una celda o sus vecinos para despertar la simulación
    void activate(int wx, int wy, int wz);
    void activate_neighbors(int wx, int wy, int wz);

    // Paso de simulación celular
    void step();

    inline bool is_running() const { return sim_running; }

private:
    World* world = nullptr;
    std::thread sim_thread;
    std::atomic<bool> sim_running{false};
    std::vector<uint64_t> active_cells;
    std::mutex active_mutex;

    void sim_loop();
    void simulate_water_pass();
    void set_water_node(const std::vector<std::pair<std::pair<int,int>, std::shared_ptr<Chunk>>>& snap, int wx, int wy, int wz, uint8_t level);
};
