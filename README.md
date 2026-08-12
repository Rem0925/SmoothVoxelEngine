# SmoothVoxelEngine

A fast and efficient smooth voxel engine (Marching Cubes) written in C++ with [raylib](https://github.com/raysan5/raylib). 
This project was completely rewritten from Python to C++ to achieve massive performance gains, multithreading, and advanced visual effects like Toon Water shading.

## Features
- **Marching Cubes Terrain Generation**: Smooth, organic terrain generation using 3D Perlin noise.
- **Multithreaded Chunk Generation**: Seamless infinite world exploration with background chunk generation.
- **Custom Shaders**:
  - `terrain.fs`: Geometric low-poly projection, removing artifacts and flat shading for a clean voxel look.
  - `water.fs`: Beautiful "Toon Water" with morphing domain-warped surface foam, depth-based cyan gradient, and organic blending.
- **C++ Performance**: Runs incredibly fast compared to the previous Python implementation.

## Requirements
- CMake (3.15+)
- A C++17 compatible compiler (GCC, Clang, or MSVC)
- *Optional but recommended*: Install `raylib-devel` globally on your system (e.g. `sudo dnf install raylib-devel` on Fedora). If not found, CMake will automatically download it via FetchContent.

## Building and Running

1. Clone the repository:
   ```bash
   git clone https://github.com/Rem0925/SmoothVoxelEngine.git
   cd SmoothVoxelEngine
   ```

2. Generate the build files using CMake:
   ```bash
   mkdir build && cd build
   cmake ..
   ```

3. Compile the project:
   ```bash
   make
   ```

4. Run the engine (make sure you are in the root directory so it can load `assets/`):
   ```bash
   cd ..
   ./build/SmoothVoxelEngine_CPP
   ```

## Controls
- **W, A, S, D**: Move around
- **Space**: Ascend (Spectator mode) / Jump
- **Shift**: Descend (Spectator mode)
- **Mouse**: Look around
- **Right Click**: Place block
- **Left Click**: Break block

## Legacy Python Version
The old Python version of this engine has been completely replaced by this C++ version for performance reasons.
