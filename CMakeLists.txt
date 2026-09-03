cmake_minimum_required(VERSION 3.15)
project(SmoothVoxelEngine_CPP VERSION 1.0 LANGUAGES C CXX)

# Build optimizado por defecto (evita el modo debug sin -O2/-O3)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Rutas de inclusión
include_directories(include)

# Buscar Raylib (asumiendo que está instalado en el sistema, ej. via apt, brew o vcpkg)
find_package(raylib QUIET)

if (NOT raylib_FOUND)
    message(STATUS "Raylib no encontrado en el sistema. Descargando mediante FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        master
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
endif()


set(SOURCES
    src/core/sqlite3.c
    src/core/AudioManager.cpp
    src/world/Chunk.cpp
    src/world/World.cpp
    src/world/FluidSimulation.cpp
    src/world/VoxelLighting.cpp
    src/world/MarchingCubes.cpp
    src/generation/Biome.cpp
    src/generation/Caves.cpp
    src/generation/Noise.cpp
    src/data/BlockRegistry.cpp
    src/data/ResourcePackManager.cpp
    src/data/DatabaseIO.cpp
    src/gameplay/ItemDrop.cpp
    src/gameplay/ItemModel3D.cpp
    src/gameplay/CommandHandler.cpp
    src/gameplay/PlayerPhysics.cpp
    src/gameplay/PlayerSaveData.cpp
    src/ui/UI.cpp
    src/ui/UIInventory.cpp
    src/ui/UICrafting.cpp
    src/ui/UIDrawing.cpp
    src/ui/Chat.cpp
    src/ui/MenuManager.cpp
    src/ui/MenuPause.cpp
    src/ui/MenuOptions.cpp
    src/ui/MenuResourcePacks.cpp
    src/ui/DebugOverlay.cpp
    src/rendering/Raycast.cpp
    src/rendering/Skybox.cpp
    src/rendering/Viewmodel.cpp
    src/rendering/BlockHighlight.cpp
    src/rendering/CelestialRenderer.cpp
    src/rendering/ChunkDebugDraw.cpp
    src/main.cpp
)

# Definir el ejecutable principal
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(OpenGL REQUIRED)

# Enlazar librerías (Raylib ya vincula dependencias del SO internamente o vía cmake)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib OpenGL::GL)

# Copiar carpeta assets al directorio de salida
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
)
