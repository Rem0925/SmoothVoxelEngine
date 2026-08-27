# Smooth Voxel Engine - Contexto y Arquitectura para IA (AI Context)

## 📌 Propósito de este archivo
Este documento explica la arquitectura y el funcionamiento interno del **Smooth Voxel Engine**. Sirve como un mapa mental detallado para que cualquier asistente de IA o desarrollador entienda cómo interactúan los sistemas principales del código, facilitando la adición de nuevas características, la depuración y el mantenimiento. Describe el estado actual del motor tras sus optimizaciones de físicas continuas, modularización data-driven y renderizado.

---

## 🏗️ Arquitectura General
El motor está escrito en **C++17** y utiliza **Raylib** para la gestión de ventana, inputs y renderizado base. Implementa:
- Generación de terreno procedural continuo mediante **Marching Cubes** y ruido multicapa.
- Espacio tridimensional dividido en **Chunks**.
- Persistencia de datos concurrente con **SQLite (WAL)**.
- Simulación celular de fluidos y autómatas de agua.
- **Sistema Data-Driven (JSON)** para bloques, ítems, herramientas y recetas sin recompilación.
- Físicas de colisión continua por **interpolación trilineal y bisección de densidad**.
- Efectos visuales y sombreado con **Shaders GLSL personalizados**.

---

## ⚙️ Sistemas Principales y Flujo de Datos

### 1. Sistema Data-Driven y Registros (`BlockRegistry.hpp/cpp`, `assets/data/`)
El motor sigue un enfoque **Data-Driven (Orientado a Datos)** estilo Minecraft/Minetest:
*   **Estructura de Carpetas:**
    *   `assets/data/blocks/`: Archivos `.json` individuales por bloque con ID, nombre, forma geométrica (`shape`), resistencia (`hardness`), transparencia, flags de viento (`is_waving`), requerimientos de herramientas (`ideal_tool`, `required_tier`), drops, icono de inventario (`icon`) y texturas por cara (`top`, `bottom`, `front`, `latch`, `sides`, `default`).
    *   `assets/data/items/`: Archivos `.json` para ítems básicos no colocables (palos, carbón, lingotes, tablas).
    *   `assets/data/tools/`: Archivos `.json` para herramientas con tipo, tier, nombre, durabilidad, multiplicador de minado y coordenadas de atlas.
    *   `assets/data/recipes/`: Archivos `.json` para todas las recetas de crafteo (bloques, items y herramientas).
*   **Modelado 3D por Cuboides (`CuboidElement` / `elements`):** Cualquier bloque puede definir una lista de cajas 3D compuestas con coordenadas `from [x, y, z]`, `to [x, y, z]` y mapeo de texturas independiente por cara (por ejemplo, el cofre con cuerpo y cerrojo, vallas con postes, antorchas, etc.).
*   **Carga en Arranque ($O(1)$ Runtime):** Al iniciar, `BlockRegistry::load_all("assets/data")` lee todos los JSONs en milisegundos usando `nlohmann/json` y llena los contenedores en RAM (`Config::BLOCKS`, `Config::ITEMS`, `Config::TOOLS`, `Config::RECIPES`). Durante el juego, las consultas son accesos inmediatos a memoria sin sobrecarga de CPU ni lectura de disco.

---

### 2. Sistema de Mundo y Chunks (`World.hpp/cpp`, `Chunk.hpp/cpp`)
*   **Gestión Espacial y Vóxeles:** El mundo infinito se almacena en `std::unordered_map<std::pair<int,int>, std::shared_ptr<Chunk>>`. Los datos de cada celda se condensan en `Config::VoxelData` `{float density; uint8_t block; uint8_t water; uint8_t rotation;}`.
*   **Separación de Mallas:** Cada chunk (`CHUNK_SIZE = 16`, altura `GRID_Y = 128`) genera mallas asíncronas independientes en un `ThreadPool`:
    *   `solid_mesh`: Terreno opaco y bloques de construcción con colisión.
    *   `water_mesh`: Malla volumétrica de fluidos.
    *   `plants_mesh`: Vegetación y follaje no sólido con sombreado de viento.
*   **Accesos Ultra-rápidos (Inlining):** `get_block`, `get_density` y `get_water_level` operan de forma `inline` en `Chunk.hpp`.
*   **Frustum Culling y Culling AABB:** El renderizado descarta chunks fuera del campo de visión y el sistema de físicas descarta bloques lejanos con comprobación AABB por chunk.
*   **Liberación Segura OpenGL:** Al destruir chunks en hilos secundarios, los buffers (VBO/VAO) se encolan y destruyen en el hilo principal vía `Chunk::flush_gl_delete_queue()`.

---

### 3. Físicas y Colisiones Continuas con Marching Cubes (`main.cpp`)
El sistema de colisiones del jugador resuelve el contacto directo con la isosuperficie suave del terreno:
*   **Interpolación Trilineal (`sample_density`):** Muestrea continuamente la densidad en coordenadas flotantes continuas `(x, y, z)` interpolando los 8 nodos enteros circundantes del campo escalar.
*   **Detección Híbrida (`is_solid`):**
    1.  Para bloques de construcción (`SHAPE_CUBE`, `SHAPE_STAIRS`, `SHAPE_DOOR`, `SHAPE_CHEST`, etc.): evalúa colisión ortogonal discreta basada en vóxeles.
    2.  Para terreno natural (`SHAPE_TERRAIN`): evalúa `sample_density(x, y, z) >= Config::ISO_SURFACE`.
*   **Resolución de Superficie por Bisección (`get_surface_height`):** Escanea verticalmente el terreno alrededor de los pies del jugador y localiza la transición sólido-aire con 8 a 10 iteraciones de bisección, garantizando precisión milimétrica en la altura del suelo.
*   **Deslizamiento de Rampas y Desniveles (`smooth_step_offset`):**
    *   Las paredes se comprueban por encima de la altura de paso (`step_height = 0.50m`), evitando que pequeñas piedras o pendientes frenen al jugador.
    *   Soporte para *Wall Sliding* independiente en los ejes X y Z.
    *   Al subir desniveles u obstáculos bajos, la altura física se actualiza y `smooth_step_offset` absorbe la diferencia aplicando `Lerp` visual en la cámara, logrando un deslizamiento suave sin saltos bruscos.

---

### 4. Control de Cámara y Primera Persona (`main.cpp`)
*   **Desacople con `UpdateCameraPro`:** Se eliminó la captura rígida por defecto de Raylib (`CAMERA_FIRST_PERSON`) para liberar las teclas **`Q`** y **`E`** (evitando rotaciones y ladeos involuntarios de la cámara).
*   **Controles:**
    *   **Ratón:** Rotación pura de Yaw y Pitch con sensibilidad balanceada (`0.15f`) y bloqueo de Roll.
    *   **Teclado:** Desplazamiento cardinal `W, A, S, D`.
    *   **Sprint:** Correr a 8.5 m/s activado con `Control Izquierdo` o `Shift Izquierdo`.
    *   **Espacio:** Salto vertical con aceleración gravitatoria de 30.0 m/s².
*   **Viewmodel en Mano (`DrawFirstPersonViewmodel`):**
    *   Bloques de construcción y herramientas renderizados con escala proporcional (0.72x).
    *   Bloques de terreno natural renderizados como rocas facetadas de 8 lados con texturizado continuo y mapeo UV radial.

---

### 5. Base de Datos Asíncrona y Guardado (`DatabaseIO.hpp/cpp`)
*   **SQLite WAL:** Emplea SQLite nativo optimizado con `PRAGMA journal_mode=WAL` y `synchronous=NORMAL`.
*   **Worker Thread Dedicado:** La clase estática `DatabaseIO` procesa en segundo plano las peticiones de guardado (`enqueue_with_future`) agrupadas en lotes (`BEGIN TRANSACTION` / `COMMIT`), evitando congelamientos de fotogramas al guardar chunks o inventario.

---

### 6. Simulación de Fluidos (`World::simulate_water`)
*   **Autómata Celular:** Simulación concurrente en segundo plano con coordenadas empaquetadas en `uint64_t cell_key`.
*   **Búsqueda Logarítmica:** Emplea instantáneas ordenadas y búsqueda binaria (`std::lower_bound`, $O(\log N)$) para localizar chunks durante la propagación.
*   **Comportamiento Dinámico:** Presión lateral, cascadas verticales aceleradas y regeneración de fuentes infinitas al converger corrientes adyacentes.

---

### 7. Generador de Terreno y Cuevas (`Caves.hpp/cpp`, `Biome.hpp/cpp`, `Noise.hpp/cpp`)
*   **Ruido Perlin 2D Multicapa:** Continentalidad, temperatura y humedad generan biomas continuos (océanos, llanuras, bosques, selvas, montañas).
*   **Caché de Tinte Bilineal:** `compute_chunk_tint_cache()` suaviza los colores biómicos en grillas $25 \times 25$ para que Marching Cubes interpole colores rápidamente.
*   **Gusanos de Cueva 3D (`CaveMap`):** Sistema de gusanos Perlin 3D conectados mediante hash grid espacial que forman cuevas y galerías subterráneas continuas.

---

### 8. Shaders y Efectos (`assets/shaders/`)
*   **`terrain_solid.fs`:** Flat-shading calculado por derivadas en GPU (`dFdx/dFdy`), proyección triplanar y niebla atmosférica (Fog).
*   **`terrain.fs`:** Viento ondulante para plantas y hojas.
*   **`water.fs`:** Animación paramétrica con textura de ruido Perlin y espuma costera en bordes de contacto.
*   **`skybox.vs/fs`:** Ciclo día/noche con sol y luna volumétricos que sincronizan la iluminación global y el color de la niebla.

---

### 9. Interfaz de Usuario y HUD (`UI.hpp/cpp`)
*   HUD nativo 2D con Hotbar, Inventario y Sistema de Crafteo.
*   Pestañas de crafteo filtradas automáticamente entre herramientas y bloques/ítems leyendo `Config::RECIPES` dinámicamente.
*   Previsualización holográfica 3D (Ghost voxel) del bloque seleccionado para guiar la construcción.
