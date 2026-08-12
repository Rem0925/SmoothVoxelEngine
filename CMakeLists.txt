cmake_minimum_required(VERSION 3.15)
project(SmoothVoxelEngine_CPP VERSION 1.0 LANGUAGES C CXX)

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


# Recopilar los archivos fuente
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c")

# Si main.cpp no existe aún, creamos un archivo temporal para que CMake no falle en el primer pase
if(NOT SOURCES)
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" "int main() { return 0; }\n")
    set(SOURCES "src/main.cpp")
endif()

# Definir el ejecutable principal
add_executable(${PROJECT_NAME} ${SOURCES})

# Enlazar librerías (Raylib ya vincula dependencias del SO internamente o vía cmake)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# Copiar carpeta assets al directorio de salida
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets"
)
