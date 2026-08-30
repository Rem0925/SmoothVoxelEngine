#pragma once
#include <raylib.h>

void DrawSkybox(Camera3D camera, Texture2D side, Texture2D top,
                Texture2D bottom, float sky_intensity);
