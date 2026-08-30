# Smooth Voxel Engine - Contexto y Arquitectura para IA (AI Context)

## Proposito de este archivo
Este documento explica la arquitectura y el funcionamiento interno del **Smooth Voxel Engine**. Sirve como un mapa mental detallado para que cualquier asistente de IA o desarrollador entienda como interactuan los sistemas principales del codigo, facilitando la adicion de nuevas características, la depuracion y el mantenimiento.

---

## Estructura de Archivos

```
SmoothVoxelEngine_CPP/
├── include/
│   ├── Config.hpp              # Constantes, tipos de bloque/herramienta, RNG, defines globales
│   ├── World.hpp               # Clase World: chunks, terreno, iluminacion, simulacion agua
│   ├── Chunk.hpp               # Clase Chunk: voxels, mashing, mesh data, OpenGL
│   ├── BlockRegistry.hpp       # Registro data-driven: carga JSON de blocks/items/tools/recipes
│   ├── UI.hpp                  # Interfaz de usuario: hotbar, inventario, crafting, cofres, hornos
│   ├── Chat.hpp                # Sistema de chat/consola
│   ├── CommandHandler.hpp      # Procesamiento de comandos de consola
│   ├── ItemDrop.hpp            # Entidades de items flotantes en el mundo
│   ├── ItemModel3D.hpp         # Modelos 3D para items (rocas facetadas, modelos compuestos)
│   ├── VoxelLighting.hpp       # Motor de iluminacion voxel dual (sol + bloques)
│   ├── MarchingCubes.hpp       # Generacion de malla isosuperficie
│   ├── Biome.hpp               # Sistema de biomas procedural
│   ├── Caves.hpp               # Generador de cuevas 3D (gusanos Perlin)
│   ├── Noise.hpp               # Ruido Perlin/Simplex multicapa
│   ├── DatabaseIO.hpp          # SQLite WAL async, worker thread dedicado
│   ├── ThreadPool.hpp          # Pool de hilos con cola de tareas
│   ├── ResourcePackManager.hpp # Gestion de resource packs (atlas, texturas)
│   ├── MenuManager.hpp         # Manager de menus (pausa, opciones, resource packs)
│   ├── MenuPause.hpp           # Menu de pausa
│   ├── MenuOptions.hpp         # Menu de opciones/video/audio
│   ├── MenuResourcePacks.hpp   # Selector de resource packs
│   ├── MenuTheme.hpp           # Colores y utilidades compartidas entre menus
│   ├── rendering/
│   │   ├── Raycast.hpp         # Raycast voxel suave (VoxelRaycastSmooth)
│   │   ├── Skybox.hpp          # DrawSkybox: cubo de cielo 6 caras
│   │   ├── Viewmodel.hpp       # DrawFirstPersonViewmodel: mano y objeto en primera persona
│   │   ├── BlockHighlight.hpp  # DrawBlockHighlight: preview holografico de colocacion/minado
│   │   ├── CelestialRenderer.hpp # DrawCelestialScene: sol, luna, nubes, cielo shader
│   │   └── ChunkDebugDraw.hpp  # DrawChunkDebug: limites de chunks/sub-chunks (F3+G)
│   ├── UI/
│   │   └── DebugOverlay.hpp    # DrawDebugOverlay: pantalla F3 estilo Minecraft
│   └── gameplay/
│       ├── PlayerPhysics.hpp   # UpdatePlayerPhysics: colisiones, gravedad, escalado suave
│       └── PlayerSaveData.hpp  # load/save_player_data: persistencia JSON del jugador
├── src/
│   ├── main.cpp                # Game loop principal (~730 lineas)
│   ├── World.cpp               # Mundo, chunks, agua, iluminacion
│   ├── Chunk.cpp               # Terrain meshing, meshing de construccion, DB I/O
│   ├── UI.cpp                  # HUD, inventario, crafting, drag-and-drop
│   ├── ItemDrop.cpp            # Fisica de drops, stacking, magnetismo
│   ├── ItemModel3D.cpp         # Modelos 3D de items
│   ├── VoxelLighting.cpp       # Iluminacion dual canal, BFS, muestreo trilineal
│   ├── MarchingCubes.cpp       # Generacion de vertices/normales/UVs
│   ├── Biome.cpp               # Evaluacion de biomas, tintes biomicos
│   ├── Caves.cpp               # Gusanos de cueva 3D
│   ├── Noise.cpp               # Perlin/Simplex, fractal
│   ├── DatabaseIO.cpp          # SQLite, batch writes, WAL
│   ├── BlockRegistry.cpp       # Parser JSON de blocks/items/tools/recipes
│   ├── Chat.cpp                # Input/output de chat
│   ├── CommandHandler.cpp      # Interpreter de comandos
│   ├── ResourcePackManager.cpp # Carga de packs, atlas, override
│   ├── MenuManager.cpp         # Navegacion entre menus
│   ├── MenuPause.cpp           # Botones de pausa
│   ├── MenuOptions.cpp         # Opciones de video/audio
│   ├── MenuResourcePacks.cpp   # Selector de packs
│   ├── rendering/
│   │   ├── Raycast.cpp         # Raycast voxel con suavizado
│   │   ├── Skybox.cpp          # Renderizado de cielo cubemap
│   │   ├── Viewmodel.cpp       # Mano en primera persona con vista
│   │   ├── BlockHighlight.cpp  # Preview holografico con Marching Cubes en vivo
│   │   ├── CelestialRenderer.cpp # Sol, luna, nubes, shader de cielo
│   │   └── ChunkDebugDraw.cpp  # Wireframe de chunks/sub-chunks
│   ├── UI/
│   │   └── DebugOverlay.cpp    # HUD de debug F3
│   ├── gameplay/
│   │   ├── PlayerPhysics.cpp   # Fisicas completas del jugador
│   │   └── PlayerSaveData.cpp  # Carga/guardado JSON del jugador
│   └── sqlite3.c               # SQLite amalgamation
├── assets/
│   ├── data/                   # JSON data-driven (blocks/, items/, tools/, recipes/)
│   └── shaders/                # GLSL vertex/fragment shaders
└── CMakeLists.txt              # Build system
```

---

## Sistemas Principales y Flujo de Datos

### 1. Sistema Data-Driven y Registros (`BlockRegistry.hpp/cpp`, `assets/data/`)
El motor sigue un enfoque **Data-Driven (Orientado a Datos)** estilo Minecraft/Minetest:
*   **Estructura de Carpetas:**
    *   `assets/data/blocks/`: Archivos `.json` individuales por bloque con ID, nombre, forma geometrica (`shape`), resistencia (`hardness`), transparencia, flags de viento (`is_waving`), requerimientos de herramientas (`ideal_tool`, `required_tier`), drops, icono de inventario (`icon`) y texturas por cara (`top`, `bottom`, `front`, `latch`, `sides`, `default`).
    *   `assets/data/items/`: Archivos `.json` para items basicos no colocables (palos, carbon, lingotes, tablas).
    *   `assets/data/tools/`: Archivos `.json` para herramientas con tipo, tier, nombre, durabilidad, multiplicador de minado y coordenadas de atlas.
    *   `assets/data/recipes/`: Archivos `.json` para todas las recetas de crafteo (bloques, items y herramientas).
*   **Modelado 3D por Cuboides (`CuboidElement` / `elements`):** Cualquier bloque puede definir una lista de cajas 3D compuestas con coordenadas `from [x, y, z]`, `to [x, y, z]` y mapeo de texturas independiente por cara.
*   **Carga en Arranque:** Al iniciar, `BlockRegistry::load_all("assets/data")` lee todos los JSONs usando `nlohmann/json` y llena los contenedores en RAM (`Config::BLOCKS`, `Config::ITEMS`, `Config::TOOLS`, `Config::RECIPES`).

---

### 2. Sistema de Mundo y Chunks (`World.hpp/cpp`, `Chunk.hpp/cpp`)
*   **Gestion Espacial y Voxeles:** El mundo infinito se almacena en `std::unordered_map<std::pair<int,int>, std::shared_ptr<Chunk>>`. Los datos de cada celda se condensan en `Config::VoxelData` `{float density; uint8_t block; uint8_t water; uint8_t rotation;}`.
*   **Separacion de Mallas:** Cada chunk (`CHUNK_SIZE = 16`, altura `GRID_Y = 128`) genera mallas asincronas independientes en un `ThreadPool`:
    *   `solid_mesh`: Terreno opaco y bloques de construccion con colision.
    *   `water_mesh`: Malla volumetrica de fluidos.
    *   `plants_mesh`: Vegetacion y follaje no solido con sombreado de viento.
*   **Accesos Ultra-rapidos (Inlining):** `get_block`, `get_density` y `get_water_level` operan de forma `inline` en `Chunk.hpp`.
*   **Frustum Culling y Culling AABB:** El renderizado descarta chunks fuera del campo de vision y el sistema de fisicas descarta bloques lejanos con comprobacion AABB por chunk.
*   **Liberacion Segura OpenGL:** Al destruir chunks en hilos secundarios, los buffers (VBO/VAO) se encolan y destruyen en el hilo principal via `Chunk::flush_gl_delete_queue()`.

---

### 3. Fisicas y Colisiones Continuas (`gameplay/PlayerPhysics.hpp/cpp`)
El sistema de colisiones del jugador resuelve el contacto directo con la isosuperficie suave del terreno:
*   **Interpolacion Trilineal (`sample_density`):** Muestrea continuamente la densidad en coordenadas flotantes continuas `(x, y, z)` interpolando los 8 nodos enteros circundantes del campo escalar.
*   **Deteccion Hibrida (`is_solid`):**
    1.  Para bloques de construccion (`SHAPE_CUBE`, `SHAPE_STAIRS`, `SHAPE_DOOR`, `SHAPE_CHEST`, etc.): evalua colision ortogonal discreta basada en voxeles.
    2.  Para terreno natural (`SHAPE_TERRAIN`): evalua `sample_density(x, y, z) >= Config::ISO_SURFACE`.
*   **Resolucion de Superficie por Biseccion (`get_surface_height`):** Escanea verticalmente el terreno alrededor de los pies del jugador y localiza la transicion solido-aire con 8 a 10 iteraciones de biseccion.
*   **Deslizamiento de Rampas y Desniveles (`smooth_step_offset`):** Wall sliding independiente en ejes X/Z, escalado suave con Lerp visual en la camara.

---

### 4. Sistema de Items Flotantes en el Mundo (`ItemDrop.hpp/cpp`)
*   **Fisica de Drops y Apilado:**
    *   Al minar o soltar con **Q**, los bloques, items y herramientas se generan como entidades 3D con impulso y gravedad.
    *   **Fusion Dinamica (Stacking):** Drops cercanos (< 1.2m) del mismo tipo se combinan.
    *   **Magnetismo:** Al acercarse a < 2.5m, los drops son atraidos y se recolectan automaticamente al contacto (< 0.6m).

---

### 5. Renderizado y Efectos Visuales (`rendering/`)
El renderizado esta modularizado en archivos dedicados bajo `src/rendering/`:
*   **`Raycast.hpp/cpp`:** Raycast voxel suave con `VoxelRaycastSmooth` que devuelve punto de impacto, bloque solido y bloque vacio adyacente.
*   **`Skybox.hpp/cpp`:** Renderizado de cielo cubemap de 6 caras.
*   **`CelestialRenderer.hpp/cpp`:** Escena celeste completa (sol, luna, nubes, shader de cielo, uniformes de iluminacion para terrain/water).
*   **`Viewmodel.hpp/cpp`:** Mano en primera persona con `DrawFirstPersonViewmodel` (bloques, herramientas, rocas facetadas). Usa `ViewmodelState` struct para estado persistente.
*   **`BlockHighlight.hpp/cpp`:** Preview holografico 3D con Marching Cubes en vivo para colocacion, minado y martillo. Soporta todos los tipos de bloque (puertas, escaleras, cofres, etc.).
*   **`ChunkDebugDraw.hpp/cpp`:** Wireframe de limites de chunks/sub-chunks (F3+G), con colores por tipo de linea.

---

### 6. Base de Datos Asincrona y Guardado (`DatabaseIO.hpp/cpp`)
*   **SQLite WAL:** Emplea SQLite nativo optimizado con `PRAGMA journal_mode=WAL` y `synchronous=NORMAL`.
*   **Worker Thread Dedicado:** La clase estatica `DatabaseIO` procesa en segundo plano las peticiones de guardado (`enqueue_with_future`) agrupadas en lotes.

---

### 7. Simulación de Fluidos (`world/FluidSimulation.hpp/cpp`)
*   **Subsistema Modular `FluidSimulator`:** Totalmente desacoplado de `World.cpp`, preparado para múltiples tipos de fluidos (`FluidType::WATER`, `FluidType::LAVA`).
*   **Autómata Celular:** Simulación concurrente en segundo plano con coordenadas empaquetadas en `uint64_t cell_key`.
*   **Búsqueda Logarítmica:** Emplea instantáneas ordenadas y búsqueda binaria (`std::lower_bound`, O(log N)) para localizar chunks.
*   **Comportamiento Dinámico:** Presión lateral, cascadas verticales aceleradas y regeneración de fuentes infinitas.
*   **Parámetros de Fluidos (`FluidProperties`):** Soporta configuración de `tick_rate_ms`, `max_level` (8), `flow_distance` y reglas de fuentes infinitas por cada tipo de fluido.

---

### 8. Generador de Terreno y Cuevas (`Caves.hpp/cpp`, `Biome.hpp/cpp`, `Noise.hpp/cpp`)
*   **Ruido Perlin 2D Multicapa:** Continentalidad, temperatura y humedad generan biomas continuos.
*   **Cache de Tinte Bilineal:** `compute_chunk_tint_cache()` suaviza los colores biomicos en grillas 25x25.
*   **Gusanos de Cueva 3D (`CaveMap`):** Sistema de gusanos Perlin 3D conectados mediante hash grid espacial.

---

### 9. Shaders, Oclusion Ambiental e Iluminacion (`VoxelLighting.hpp/cpp`, `MarchingCubes.cpp`, `Chunk.cpp`, `assets/shaders/`)
*   **Motor Modular de Iluminacion Voxel (`VoxelLighting.hpp/cpp`):**
    *   **Canal Dual de Luz (Luz Solar y Luz de Bloques):** Empaquetado compacto en 1 byte por voxel (4 bits sol, 4 bits bloque, rango 0-15).
    *   **Transparencia y Filtrado de Luz por JSON (`light_filter`):** Configurable en cada bloque.
    *   **Emision de Luz por JSON (`light_emission`):** Potencia luminica configurable (0-15).
    *   **Barrido Vertical y Propagacion BFS:** Luz solar vertical + Flood-fill BFS 3D en cuevas.
    *   **Muestreo Trilineal Continuo con Offset Normal (`sample_smooth_light`):** Elimina bandas oscuras.
*   **Pipeline de Atributos de Vertice en Shaders:**
    *   `vertexColor`: Tinte biomico y blend weight uniforme por triangulo.
    *   `vertexNormal.x`: Factor de Oclusion Ambiental continua (fragAO, 0.75-1.0).
    *   `vertexNormal.y`: Luz Solar continua (fragSunLight, 0.0-1.0).
    *   `vertexNormal.z`: Luz de Bloque / Antorchas (fragBlockLight, 0.0-1.0).
*   **Shaders (`terrain_solid.fs` / `terrain.fs`):** Respuesta luminica perceptual, flat-shading, proyeccion triplanar, niebla.
*   **`water.fs`:** Animacion con ruido Perlin y espuma costera.
*   **`skybox.vs/fs`:** Ciclo dia/noche con sol/luna que sincronizan iluminacion global.

---

### 10. Interfaz de Usuario y HUD (`UI/`)
El sistema de UI está modularizado en archivos especializados bajo `src/ui/`:
*   **`UI.hpp` / `UI.cpp` (~95 líneas):** Ciclo de vida principal, inicialización de slots, `update()` de teclas y listeners de apertura/cierre.
*   **`UIInventory.cpp` (~275 líneas):** Lógica de slots, manejo de herramientas, adición/remoción de bloques e ítems con apilado automático (hasta 64) y cancelación de arrastre (`cancel_drag`).
*   **`UICrafting.cpp` (~115 líneas):** Validación de recetas (`can_craft`), consumo y fabricación (`craft`), y simulación a 20 TPS de fundición en hornos (`tick_furnaces`).
*   **`UIDrawing.cpp` (~1225 líneas):** Renderizado 2D completo de hotbar, HUD de herramientas, inventario del jugador con arrastre de ítems, paneles de cofres, hornos, mesa de crafteo y tooltips flotantes.
*   **`DebugOverlay.hpp/cpp`:** Pantalla de depuración F3 con coordenadas, bioma, bloque objetivo, desglose de luz (Sol vs Bloque) y hora.

---

### 11. Persistencia del Jugador (`gameplay/PlayerSaveData.hpp/cpp`)
*   **Carga/Guardado JSON:** Posición, orientación, hotbar, herramientas, almacenamiento, hora del día.
*   **Autoguardado:** Cada 5 segundos vía `save_player_data()`.

---

### 12. Menús (`MenuManager.hpp/cpp`, `MenuPause.hpp/cpp`, `MenuOptions.hpp/cpp`, `MenuResourcePacks.hpp/cpp`)
*   **Tema Compartido (`MenuTheme.hpp`):** Colores, utilidades de dibujo de botones/paneles usados por todos los menús.
*   **Menú de Pausa:** Continuar, Opciones, Resource Packs, Guardar y Salir.
*   **Menú de Opciones:** Brillo, Volumen, sensibilidad, fullscreen.
*   **Selector de Resource Packs:** Aplica un pack a la vez (override del atlas por defecto).

---

### 13. Game Loop Principal (`main.cpp`, ~730 líneas)
El `main.cpp` actúa como orquestador del game loop:
*   **`GameState` Struct:** Encapsula todo el estado mutable de la partida (`player_vel_y`, `is_grounded`, `day_time`, `spectator_mode`, `show_chunks`, `show_debug_info`, `smooth_step_offset`, `mining_progress`, `autosave_timer`, `tick_accumulator`, etc.).
*   Inicialización de ventana, shaders, materiales, world, UI, chat.
*   Bucle principal: input -> físicas -> minado -> tick (20 TPS) -> render -> UI -> draw.
*   Coordina todos los sistemas modulares vía llamadas a funciones dedicadas.
*   Maneja el ciclo día/noche, shader uniforms, y la transición de resource packs.
