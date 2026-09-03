#include "core/AudioManager.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>

AudioManager& AudioManager::get() {
    static AudioManager instance;
    return instance;
}

void AudioManager::load_sound(SoundType type, const std::string& filepath) {
    if (std::filesystem::exists(filepath)) {
        Sound snd = LoadSound(filepath.c_str());
        if (snd.frameCount > 0) {
            sounds[type] = snd;
            return;
        }
    }
    std::cout << "[Audio] Aviso: No se pudo cargar el audio: " << filepath << std::endl;
}

void AudioManager::init() {
    if (initialized) return;

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        std::cerr << "[Audio] Error: No se pudo inicializar el dispositivo de audio." << std::endl;
        return;
    }

    initialized = true;
    SetMasterVolume(master_volume);

    // Cargar sonidos placeholder sintetizados
    load_sound(SoundType::STEP_GRASS,  "assets/audio/step_grass.wav");
    load_sound(SoundType::STEP_DIRT,   "assets/audio/step_dirt.wav");
    load_sound(SoundType::STEP_STONE,  "assets/audio/step_stone.wav");
    load_sound(SoundType::STEP_WOOD,   "assets/audio/step_wood.wav");
    load_sound(SoundType::STEP_WATER,  "assets/audio/step_water.wav");

    load_sound(SoundType::HIT_DIRT,    "assets/audio/hit_dirt.wav");
    load_sound(SoundType::HIT_STONE,   "assets/audio/hit_stone.wav");
    load_sound(SoundType::HIT_WOOD,    "assets/audio/hit_wood.wav");

    load_sound(SoundType::BREAK_DIRT,  "assets/audio/break_dirt.wav");
    load_sound(SoundType::BREAK_STONE, "assets/audio/break_stone.wav");
    load_sound(SoundType::BREAK_WOOD,  "assets/audio/break_wood.wav");

    load_sound(SoundType::PLACE_BLOCK, "assets/audio/place.wav");
    load_sound(SoundType::PICKUP_ITEM, "assets/audio/pop.wav");

    load_sound(SoundType::DOOR_OPEN,   "assets/audio/door_open.wav");
    load_sound(SoundType::DOOR_CLOSE,  "assets/audio/door_close.wav");
    load_sound(SoundType::CHEST_OPEN,  "assets/audio/chest_open.wav");
    load_sound(SoundType::CHEST_CLOSE, "assets/audio/chest_close.wav");

    std::cout << "[Audio] Sistema de audio inicializado con exito (" << sounds.size() << " sonidos cargados)." << std::endl;
}

void AudioManager::cleanup() {
    if (!initialized) return;

    for (auto& [type, snd] : sounds) {
        if (snd.frameCount > 0) {
            UnloadSound(snd);
        }
    }
    sounds.clear();

    CloseAudioDevice();
    initialized = false;
    std::cout << "[Audio] Dispositivo de audio cerrado." << std::endl;
}

void AudioManager::set_master_volume(float vol) {
    master_volume = std::clamp(vol, 0.0f, 1.0f);
    if (initialized) {
        SetMasterVolume(master_volume);
    }
}

void AudioManager::play(SoundType type, float volume, float pitch_var) {
    if (!initialized) return;
    auto it = sounds.find(type);
    if (it == sounds.end() || it->second.frameCount == 0) return;

    Sound& snd = it->second;
    float pitch = 1.0f + Config::rand_float(-pitch_var, pitch_var);
    SetSoundPitch(snd, pitch);
    SetSoundVolume(snd, std::clamp(volume, 0.0f, 1.0f));
    PlaySound(snd);
}

MaterialCategory AudioManager::get_block_material(uint8_t block_id) const {
    using namespace Config;
    switch (block_id) {
        case GRASS:
        case TALL_GRASS:
        case LEAVES:
        case RED_MUSHROOM:
        case BROWN_MUSHROOM:
        case DEAD_BUSH:
        case CACTUS:
            return MaterialCategory::PLANT;

        case DIRT:
        case SAND:
        case GRAVEL:
        case RED_CLAY:
            return MaterialCategory::DIRT;

        case STONE:
        case COBBLESTONE:
        case STONE_BRICK:
        case COAL_ORE:
        case IRON_ORE:
        case SILVER_ORE:
        case GOLD_ORE:
        case DIAMOND_ORE:
        case FURNACE:
        case STAIRS_STONE:
        case GLASS:
            return MaterialCategory::STONE;

        case WOOD:
        case BIRCH_WOOD:
        case PLANKS_CUBE:
        case STAIRS_WOOD:
        case FENCE_WOOD:
        case DOOR_WOOD:
        case CHEST:
        case CRAFTING_TABLE:
            return MaterialCategory::WOOD;

        case WATER:
            return MaterialCategory::WATER;

        default:
            return MaterialCategory::OTHER;
    }
}

void AudioManager::play_step(uint8_t block_id, bool in_water) {
    if (in_water) {
        play(SoundType::STEP_WATER, 0.65f, 0.15f);
        return;
    }

    MaterialCategory mat = get_block_material(block_id);
    switch (mat) {
        case MaterialCategory::PLANT:
            play(SoundType::STEP_GRASS, 0.45f, 0.15f);
            break;
        case MaterialCategory::STONE:
            play(SoundType::STEP_STONE, 0.48f, 0.12f);
            break;
        case MaterialCategory::WOOD:
            play(SoundType::STEP_WOOD, 0.48f, 0.12f);
            break;
        case MaterialCategory::DIRT:
        default:
            play(SoundType::STEP_DIRT, 0.50f, 0.15f);
            break;
    }
}

void AudioManager::play_hit(uint8_t block_id) {
    MaterialCategory mat = get_block_material(block_id);
    switch (mat) {
        case MaterialCategory::STONE:
            play(SoundType::HIT_STONE, 0.75f, 0.12f);
            break;
        case MaterialCategory::WOOD:
            play(SoundType::HIT_WOOD, 0.70f, 0.12f);
            break;
        case MaterialCategory::PLANT:
        case MaterialCategory::DIRT:
        default:
            play(SoundType::HIT_DIRT, 0.65f, 0.14f);
            break;
    }
}

void AudioManager::play_break(uint8_t block_id) {
    MaterialCategory mat = get_block_material(block_id);
    switch (mat) {
        case MaterialCategory::STONE:
            play(SoundType::BREAK_STONE, 0.85f, 0.12f);
            break;
        case MaterialCategory::WOOD:
            play(SoundType::BREAK_WOOD, 0.85f, 0.12f);
            break;
        case MaterialCategory::PLANT:
        case MaterialCategory::DIRT:
        default:
            play(SoundType::BREAK_DIRT, 0.80f, 0.14f);
            break;
    }
}

void AudioManager::play_place(uint8_t block_id) {
    (void)block_id;
    play(SoundType::PLACE_BLOCK, 0.75f, 0.15f);
}

void AudioManager::update_footsteps(float dt, bool is_grounded, bool in_water, float speed, uint8_t block_below) {
    if ((!is_grounded && !in_water) || speed < 0.35f) {
        // Reducir acumulador para que el primer paso no se dispare de inmediato al aterrizar
        step_accumulator = std::min(step_accumulator, 0.3f);
        return;
    }

    step_accumulator += speed * dt;
    // Frecuencia de pasos ajustada según si camina o corre
    float step_distance = in_water ? 1.6f : (speed > 5.5f ? 2.3f : 1.9f);

    if (step_accumulator >= step_distance) {
        step_accumulator -= step_distance;
        play_step(block_below, in_water);
    }
}
