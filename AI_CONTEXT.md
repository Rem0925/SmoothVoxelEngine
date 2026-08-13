# Smooth Voxel Engine - Contexto y Arquitectura para IA (AI Context)

## 📌 Propósito de este archivo
Este documento explica la arquitectura y el funcionamiento interno del **Smooth Voxel Engine**. Sirve como un mapa mental detallado para que cualquier asistente de IA entienda cómo interactúan los sistemas principales del código actual, facilitando la adición de nuevas características, la depuración y el mantenimiento. No contiene problemas a solucionar, sino la descripción de cómo funciona el motor en su estado actual.

## 🏗️ Arquitectura General
El motor está escrito en **C++17** y utiliza **Raylib** para la gestión de la ventana, inputs y renderizado base. Implementa generación de terreno procedural mediante **Marching Cubes**, un sistema de mundos infinitos basado en **Chunks**, persistencia de datos local con **SQLite** (amalgamation) y efectos visuales avanzados a través de **Shaders GLSL personalizados**.

---

## ⚙️ Sistemas Principales y Flujo de Datos

### 1. Sistema de Mundo y Chunks (`World.hpp/cpp`, `Chunk.hpp/cpp`)
*   **Gestión Espacial:** El mundo (`World`) es un entorno infinito que maneja la carga y descarga de segmentos a través de un `std::unordered_map`. Las coordenadas `(x, z)` actúan como claves usando una estructura `std::pair<int, int>` combinada con una función hash personalizada (`pair_hash`).
*   **Estructura del Chunk:** Cada objeto `Chunk` representa un volumen 3D (definido por `CHUNK_SIZE` en la configuración). Internamente almacena:
    *   `float* density_grid`: Un arreglo tridimensional que guarda los valores continuos del ruido. Define la *forma geométrica* del terreno.
    *   `uint8_t* block_grid`: Un arreglo tridimensional de IDs (Vóxeles) que define el *material* de ese punto en el espacio.
*   **Separación de Mallas:** Un único chunk genera y dibuja múltiples mallas independientes para permitir distintos shaders de Raylib y comportamientos:
    *   `solid_mesh`: Terreno general opaco.
    *   `water_mesh`: Líquidos con transparencia y shader de deformación.
    *   `plants_mesh`: Vegetación (como hierba alta) que no tiene colisión sólida.
*   **Multithreading Centralizado (ThreadPool):** La generación procedural y la construcción de la malla ocurren en segundo plano. Existe un `ThreadPool` global instanciado en `World.cpp` calibrado por `MAX_WORKER_THREADS` (definido en `Config.hpp`). Los objetos `Chunk` ya no gestionan hilos individuales, sino que envían sus tareas pesadas (`generate_thread()`, `rebuild_thread()`) a la cola concurrente de este pool global usando lambdas y son sincronizadas con `std::future`.
*   **Gestión de Memoria (Render Distance y Unloading):** El mundo controla un área dinámica delimitada por `RENDER_DISTANCE` en `Config.hpp`. Cuando un Chunk queda fuera del rango `RENDER_DISTANCE + 1`, el sistema ejecuta una rutina de descarga (Unloading). Si el chunk tiene datos nuevos (`is_dirty`), invoca asíncronamente `save_to_disk()` para almacenar el estado exacto en la base de datos (SQLite), previniendo pérdida de mundo antes de liberar su memoria RAM al destruirse de `std::unordered_map chunks`.
*   **Carga Deslimitada y Frustum Culling:** El mundo evalúa e inyecta la carga de nuevos chunks al pool sin retrasos artificiales dentro de `RENDER_DISTANCE`. En el renderizado (`World::draw`), los chunks calculan matemáticamente su posición frente a la cámara (mediante intersección por distancia y ángulo del cono) para aplicar **Frustum Culling**. Si están fuera de visión, sus llamadas de dibujado se omiten totalmente.

### 2. Generación Procedural y Marching Cubes (`Noise.hpp/cpp`, `MarchingCubes.hpp/cpp`)
*   **Ruido Perlin 3D:** En `Noise.cpp` reside la implementación matemática (`PerlinNoise3D`). Se utiliza Movimiento Browniano Fraccionario (FBM) mediante la función `pnoise3`, sumando múltiples *octavas* de ruido para dar una forma orgánica y natural a montañas, valles y cuevas.
*   **Evaluación de Isosuperficie:** En `MarchingCubes.cpp`, el algoritmo recorre cada "cubo" imaginario formado por 8 puntos adyacentes del `density_grid`. Compara estos puntos contra un valor umbral (`ISO_SURFACE`). Basándose en esto, interpola las posiciones de los vértices a lo largo de las aristas del cubo para generar mallas suaves y contorneadas en lugar de formas cúbicas.

### 3. Persistencia y Base de Datos (`sqlite3.h/c`)
*   El motor integra SQLite directamente en el árbol de dependencias del código fuente (amalgamation de C).
*   Existe un puntero global `extern sqlite3* db` y un mutex concurrente `extern std::mutex sqlite_mutex` (definidos en `World.hpp`). Esto permite que múltiples hilos de chunks puedan leer o persistir en disco el estado de los bloques (cuando el usuario construye/destruye) de manera segura (Thread-safe) en la base de datos local.
*   **Optimización de Disco:** Para maximizar la eficiencia y prevenir bloqueos I/O del SO durante guardados asíncronos concurrentes, se utiliza `PRAGMA journal_mode=WAL;` y `PRAGMA synchronous=NORMAL;`. Además, las operaciones de guardado se envuelven en transacciones (`BEGIN TRANSACTION` y `COMMIT`).

### 4. Shaders y Efectos Visuales (`assets/shaders/`)
*   **Terreno (`terrain.vs` / `terrain.fs`):**
    *   *Flat Shading Dinámico:* Como Marching Cubes genera vértices compartidos (smooth normals), el *fragment shader* reconstruye las normales planas (estilo low-poly) en tiempo real usando derivadas espaciales (`dFdx`, `dFdy` -> `cross(dpdx, dpdy)`), lo que le da su aspecto de polígonos afilados característico.
    *   *Texturizado Triplanar & Blending:* Las texturas se proyectan basándose en la normal de la superficie (proyección triplanar). Para evitar islas flotantes, el shader fusiona las texturas (`texPrimary`, `texSecondary`) utilizando un factor de ruido matemático para transiciones duras.
    *   *Ondulación (Viento):* El *vertex shader* intercepta coordenadas UV específicas (ej. `TexCoord.x > 5.0`) para identificar hojas y pasto, aplicando un desplazamiento trigonométrico animado por la variable de tiempo (`time`).
*   **Agua Estilo Toon (`water.vs` / `water.fs`):**
    *   *Olas Base:* El *vertex shader* altera la posición `Y` usando seno/coseno del tiempo. Utiliza el canal Alpha (`vertexColor.a`) como una máscara (`shoreMask`) para anclar el agua a las orillas y evitar que se separe de la tierra.
    *   *Profundidad y Espuma Procedural:* El *fragment shader* simula profundidad y mezcla colores (cyan/azul profundo). Renderiza espuma ("Foam") animada en la superficie evaluando funciones de ruido en tiempo real (`valueNoise`), controlando el corte (cutoff) dependiendo de la cercanía a la costa.

*   **Centralización de Uniforms:** Los parámetros estéticos ambientales de ambos shaders, como `fogStart` y `fogEnd`, están controlados globalmente desde constantes en `Config.hpp`. Estos valores se envían dinámicamente cada fotograma a la GPU mediante `SetShaderValue()` en C++.

### 5. Configuración y UI (`Config.hpp`, `UI.hpp/cpp`)
*   **El diccionario `BLOCKS`:** En `Config.hpp`, un `std::unordered_map` unifica todos los datos de los bloques (ID, Nombre de visualización, coordenadas de textura `tex_x/tex_y`, si es transparente y si tiene flag `is_waving` para ondular con el viento).
*   **Vegetación y Decoraciones:** El motor incluye bloques decorativos no sólidos como el `TALL_GRASS`. Este bloque mantiene la lógica de vegetación pero se guarda en el arreglo `block_grid`. Al construir la malla (`build_mesh_data()`), el motor ignora estos IDs para no formar cubos sólidos (a través del algoritmo Marching Cubes), y en su lugar emite cuádruples transversales (cross-quads) que se envían directamente a la `plants_mesh`, manteniendo su animación de viento mediante el shader.
*   **Inventario (UI):** La clase `UI` abstrae las lógicas de interfaz 2D de Raylib. Genera visualmente la barra de acceso rápido (Hotbar), lee el sprite principal (`spritesheet_tiles.png`) y recorta los iconos 2D correspondientes de la cuadrícula a partir de la configuración del bloque seleccionado en 3D.
