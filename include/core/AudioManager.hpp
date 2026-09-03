#pragma once
#include <raylib.h>
#include <string>
#include <unordered_map>
#include <cstdint>
#include "core/Config.hpp"

enum class SoundType {
    STEP_GRASS,
    STEP_DIRT,
    STEP_STONE,
    STEP_WOOD,
    STEP_WATER,
    HIT_DIRT,
    HIT_STONE,
    HIT_WOOD,
    BREAK_DIRT,
    BREAK_STONE,
    BREAK_WOOD,
    PLACE_BLOCK,
    PICKUP_ITEM,
    DOOR_OPEN,
    DOOR_CLOSE,
    CHEST_OPEN,
    CHEST_CLOSE
};

enum class MaterialCategory {
    DIRT,
    STONE,
    WOOD,
    PLANT,
    WATER,
    OTHER
};

class AudioManager {
public:
    static AudioManager& get();

    void init();
    void cleanup();

    void play(SoundType type, float volume = 1.0f, float pitch_var = 0.12f);

    // Helpers contextuales
    void play_step(uint8_t block_id, bool in_water);
    void play_hit(uint8_t block_id);
    void play_break(uint8_t block_id);
    void play_place(uint8_t block_id);

    // Actualización de pasos del jugador
    void update_footsteps(float dt, bool is_grounded, bool in_water, float speed, uint8_t block_below);

    // Volumen maestro (0.0f a 1.0f)
    void set_master_volume(float vol);
    float get_master_volume() const { return master_volume; }

private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool initialized = false;
    float master_volume = 1.0f;
    float step_accumulator = 0.0f;

    std::unordered_map<SoundType, Sound> sounds;

    void load_sound(SoundType type, const std::string& filepath);
    MaterialCategory get_block_material(uint8_t block_id) const;
};
