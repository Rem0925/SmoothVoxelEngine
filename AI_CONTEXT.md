# Smooth Voxel Engine - Contexto y Arquitectura para IA (AI Context)

## 📌 Propósito de este archivo
Este documento explica la arquitectura y el funcionamiento interno del **Smooth Voxel Engine**. Sirve como un mapa mental detallado para que cualquier asistente de IA entienda cómo interactúan los sistemas principales del código actual, facilitando la adición de nuevas características, la depuración y el mantenimiento. No contiene problemas a solucionar, sino la descripción de cómo funciona el motor en su estado actual, tras un proceso intensivo de optimización.

## 🏗️ Arquitectura General
El motor está escrito en **C++17** y utiliza **Raylib** para la gestión de la ventana, inputs y renderizado base. Implementa generación de terreno procedural mediante **Marching Cubes** y sistemas de ruido continuos, un mundo tridimensional estructurado en **Chunks**, persistencia de datos concurrente con **SQLite**, simulación celular de fluidos y efectos visuales impulsados por **Shaders GLSL personalizados**.

---

## ⚙️ Sistemas Principales y Flujo de Datos

### 1. Sistema de Mundo y Chunks (`World.hpp/cpp`, `Chunk.hpp/cpp`)
*   **Gestión Espacial y Vóxeles:** El mundo es un espacio infinito gestionado por un mapa de segmentos (`std::unordered_map`). La estructura base del espacio está condensada en `Config::VoxelData`, la cual contiene `{float density; uint8_t block; uint8_t water;}` para unificar la forma geométrica, el material sólido y el estado del agua en una misma estructura continua y amigable con el caché de la CPU.
*   **Separación de Mallas:** Un único chunk (`CHUNK_SIZE`, altura `GRID_Y`) genera mallas independientes procesadas asíncronamente en un `ThreadPool`:
    *   `solid_mesh`: Terreno opaco con colisión.
    *   `water_mesh`: Renderizado volumétrico de agua y fluidos.
    *   `plants_mesh`: Vegetación no sólida que ondea (cross-quads).
*   **Accesos Ultra-rápidos (Inlining):** Los métodos `get_block`, `get_density` y `get_water_level` operan de forma `inline` directamente en la cabecera `Chunk.hpp`, lo que anula el coste de llamadas a función durante las miles de iteraciones que ocurren en físicas o simulaciones.
*   **Ciclo de Actualización Optimizado (`World::update`):** El patrón radial de carga de chunks dentro de `RENDER_DISTANCE` se precalcula (`static std::vector`) en memoria, eliminando asignaciones dinámicas y reordenamientos en cada frame. 
*   **Culling Activo:** Durante el renderizado, el mundo usa **Frustum Culling** basado en un cono visual, y el sistema de físicas (Raycasting) cuenta con un culling previo tipo **AABB** (Caja Delimitadora de Ejes) por chunk, que permite descartar miles de triángulos instantáneamente si el jugador no los está apuntando. 
*   **Liberación Segura OpenGL:** Al destruir los chunks, sus búferes (VBO/VAO) se envían al hilo principal a través de `Chunk::flush_gl_delete_queue()` para que Raylib los destruya sin violar el contexto de OpenGL.

### 2. Base de Datos Asíncrona y Guardado (`DatabaseIO.hpp/cpp`)
*   **Integración Local:** Emplea SQLite (amalgamation) para el guardado nativo, optimizado con `PRAGMA journal_mode=WAL` (Write-Ahead Logging) y `synchronous=NORMAL`.
*   **Worker Thread Dedicado:** La persistencia ocurre en una clase estática asíncrona (`DatabaseIO`). Los hilos del pool envían comandos de guardado (`enqueue` / `enqueue_with_future`) a esta clase, la cual agrupa las peticiones en lotes (Batches) y las envuelve en transacciones SQL (`BEGIN TRANSACTION` / `COMMIT`). Esto evita que el disco y las concurrencias congelen los fotogramas del juego.

### 3. Simulación de Fluidos (`World::simulate_water`)
*   **Autómata Celular Eficiente:** El agua y otros fluidos operan bajo un modelo de celdas ejecutado concurrentemente. Las celdas activas se procesan codificando sus posiciones absolutas 3D en índices de 64 bits (`uint64_t cell_key`).
*   **Búsqueda Logarítmica:** Para encontrar a qué chunk pertenece una gota de agua, el mundo realiza una copia (`snapshot`) del arreglo actual de chunks, lo ordena internamente y usa **Búsqueda Binaria** (`std::lower_bound`, completada en pasos $O(\log N)$) en lugar de búsquedas lineales.
*   **Características del fluido:** Las masas de agua soportan propagación lateral basada en presión, caídas verticales aceleradas (cascadas) y la fusión de corrientes vecinas para generar fuentes de agua infinitas.

### 4. Generador de Terreno y Cuevas (`Caves.hpp/cpp`, `Biome.hpp/cpp`, `Noise.hpp/cpp`)
*   **Evaluación Biómica Continua:** Un sistema paramétrico macro-escalar de Ruido Perlin 2D (Continentalidad, Temperatura, Humedad) define mapas continuos. Esto clasifica zonas fluidamente en océanos, playas, llanuras, bosques, montañas y selvas sin transiciones bruscas.
*   **Tinte Bilineal Inteligente:** El terreno de césped y hojas aplica `compute_chunk_tint_cache()` para convolucionar biomas en una grilla de puntos clave (suavizado $25 \times 25$). Marching Cubes luego evalúa colores en tiempo real con interpolación bilineal directa, previniendo bordes de color asimétricos o cuellos de botella en CPU.
*   **Red de Cuevas Subterráneas (`CaveMap`):** Usa un sistema de gusanos 3D conectados (Perlin worms) distribuidos por todo el mundo con un hash grid espacial. Controlan aperturas orgánicas hacia la superficie y dejan un fondo impenetrable en la coordenada base (Bedrock). Se optimizó su radio de sondeo para preservar continuidad lateral sin estresar la memoria.

### 5. Algoritmos de Renderizado (`MarchingCubes.hpp/cpp`)
*   El terreno es generado empleando **Marching Cubes** en una isosuperficie definida dinámicamente (`Config::ISO_SURFACE`), procesada con paso ajustable de nivel de detalle (`LOD`).
*   **Manejo Avanzado del Viento:** El sistema detecta materiales biológicos (`b_info_pri.is_waving`). Gracias a esto, no es necesario ejecutar pesadas comprobaciones adyacentes para el `sway`, reduciendo lecturas masivas.
*   El renderizado de fluidos utiliza una variante de Marching Cubes invertido para crear agua volumétrica que no requiere colisiones, rellenando espacios según un valor umbral denso.

### 6. Shaders y Efectos (`assets/shaders/`)
*   **Terreno Sólido (`terrain_solid.fs`):** Flat-shading computado enteramente por derivadas locales en la tarjeta gráfica (`dFdx/dFdy`). Proyección triplanar pura, difuminado atmosférico gradual (Fog) e iluminación base paramétrica.
*   **Vegetación (`terrain.fs`):** Shader dedicado con interpolación de viento ondulante desde las coordenadas base. 
*   **Agua (`water.fs`):** Aplica animación paramétrica en vértices. Interpola una textura de ruido dinámico renderizada vía `GenImagePerlinNoise()` y evalúa el canal Alfa en los vértices del Marching Cubes para generar efecto dinámico de espuma al golpear costas o terrenos bajos.
*   **Atmósfera (`skybox.vs/fs`):** Cielo procesado dinámicamente según la hora del día en el motor. Sol y luna volumétricos orbitando el jugador que controlan los colores y opacidades ambientales mediante interpolación temporal.

### 7. Control e Inventario (`UI.hpp/cpp`)
*   Motor HUD 2D nativo integrado. Lee el inventario (bloques mapeados por IDs planos para accesos veloces).
*   Muestra guías holográficas de colocación (Wireframe/Ghost voxel), minado sin trabas a pesar del retraso de sincronización y físicas "Auto-Step" compensadas que detectan paredes a nivel de tobillo para escalar bloques sin necesidad de saltos constantes. 
