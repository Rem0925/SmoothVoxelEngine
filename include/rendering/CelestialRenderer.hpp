#pragma once
#include <raylib.h>

void DrawCelestialScene(Camera3D camera, float day_time, float light_intensity,
                        Texture2D tex_sun, Texture2D tex_moon, Texture2D tex_clouds,
                        Texture2D sky_side, Texture2D sky_top, Texture2D sky_bottom,
                        Shader skyboxShader, int skyboxSunDirLoc, int skyboxViewRotLoc,
                        Material mat_solid, int solidSunDirLoc,
                        Material mat_plants, int plantsSunDirLoc,
                        Material mat_water, int waterSunDirLoc);
