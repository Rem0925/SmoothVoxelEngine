#include "rendering/CelestialRenderer.hpp"
#include "rendering/Skybox.hpp"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <cmath>
#include <algorithm>

void DrawCelestialScene(Camera3D camera, float day_time, float light_intensity,
                        Texture2D tex_sun, Texture2D tex_moon, Texture2D tex_clouds,
                        Texture2D sky_side, Texture2D sky_top, Texture2D sky_bottom,
                        Shader skyboxShader, int skyboxSunDirLoc, int skyboxViewRotLoc,
                        Material mat_solid, int solidSunDirLoc,
                        Material mat_plants, int plantsSunDirLoc,
                        Material mat_water, int waterSunDirLoc)
{
    Vector3 sun_center = { camera.position.x + 400.0f * std::cos(day_time), camera.position.y + 400.0f * std::sin(day_time), camera.position.z };
    Vector3 sun_dir_vec = Vector3Normalize(Vector3Subtract(sun_center, camera.position));

    SetShaderValue(skyboxShader, skyboxSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);

    Vector3 cam_forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 cam_right = Vector3Normalize(Vector3CrossProduct(cam_forward, camera.up));
    Vector3 cam_up = Vector3CrossProduct(cam_right, cam_forward);
    Matrix viewRotMat = { 0 };
    viewRotMat.m0 = cam_right.x;     viewRotMat.m1 = cam_right.y;     viewRotMat.m2 = cam_right.z;
    viewRotMat.m4 = cam_up.x;        viewRotMat.m5 = cam_up.y;        viewRotMat.m6 = cam_up.z;
    viewRotMat.m8 = -cam_forward.x;  viewRotMat.m9 = -cam_forward.y;  viewRotMat.m10 = -cam_forward.z;
    viewRotMat.m15 = 1.0f;
    SetShaderValueMatrix(skyboxShader, skyboxViewRotLoc, viewRotMat);
    SetShaderValue(mat_solid.shader, solidSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);
    SetShaderValue(mat_plants.shader, plantsSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);
    SetShaderValue(mat_water.shader, waterSunDirLoc, &sun_dir_vec, SHADER_UNIFORM_VEC3);

    BeginShaderMode(skyboxShader);
    DrawSkybox(camera, sky_side, sky_top, sky_bottom, light_intensity);
    EndShaderMode();

    rlDisableDepthMask();
    rlDisableDepthTest();
    rlDisableBackfaceCulling();

    float sun_y = std::sin(day_time);
    float sun_alpha = std::clamp((sun_y + 0.15f) / 0.05f, 0.0f, 1.0f);
    float moon_alpha = std::clamp((-sun_y + 0.15f) / 0.05f, 0.0f, 1.0f);

    rlEnableColorBlend();

    rlPushMatrix();
        rlTranslatef(camera.position.x, camera.position.y, camera.position.z);

        if (sun_alpha > 0) {
            rlPushMatrix();
                rlRotatef(day_time * RAD2DEG, 0, 0, 1);
                rlTranslatef(400.0f, 0.0f, 0.0f);
                rlRotatef(-90, 0, 1, 0);

                rlSetTexture(tex_sun.id);
                rlBegin(RL_QUADS);
                    rlColor4ub(255, 255, 255, (unsigned char)(sun_alpha * 255.0f));
                    rlTexCoord2f(0, 0); rlVertex3f(-40.0f, 40.0f, 0.0f);
                    rlTexCoord2f(0, 1); rlVertex3f(-40.0f, -40.0f, 0.0f);
                    rlTexCoord2f(1, 1); rlVertex3f(40.0f, -40.0f, 0.0f);
                    rlTexCoord2f(1, 0); rlVertex3f(40.0f, 40.0f, 0.0f);
                rlEnd();
                rlSetTexture(0);
            rlPopMatrix();
        }

        if (moon_alpha > 0) {
            rlPushMatrix();
                rlRotatef(day_time * RAD2DEG + 180.0f, 0, 0, 1);
                rlTranslatef(400.0f, 0.0f, 0.0f);
                rlRotatef(-90, 0, 1, 0);

                rlSetTexture(tex_moon.id);
                rlBegin(RL_QUADS);
                    rlColor4ub(255, 255, 255, (unsigned char)(moon_alpha * 255.0f));
                    rlTexCoord2f(0, 0); rlVertex3f(-30.0f, 30.0f, 0.0f);
                    rlTexCoord2f(0, 1); rlVertex3f(-30.0f, -30.0f, 0.0f);
                    rlTexCoord2f(1, 1); rlVertex3f(30.0f, -30.0f, 0.0f);
                    rlTexCoord2f(1, 0); rlVertex3f(30.0f, 30.0f, 0.0f);
                rlEnd();
                rlSetTexture(0);
            rlPopMatrix();
        }
    rlPopMatrix();

    float cloud_offset = GetTime() * 0.02f;
    rlPushMatrix();
        rlTranslatef(camera.position.x, 250.0f, camera.position.z);
        rlRotatef(90, 1, 0, 0);
        rlSetTexture(tex_clouds.id);
        rlBegin(RL_QUADS);
            int grid = 16;
            float size = 480.0f;
            float step = size * 2.0f / grid;
            float uv_c = 4.0f;
            for (int x = 0; x < grid; x++) {
                for (int y = 0; y < grid; y++) {
                    float x1 = -size + x * step;
                    float y1 = -size + y * step;
                    float x2 = x1 + step;
                    float y2 = y1 + step;

                    float d11 = std::sqrt(x1*x1 + y1*y1);
                    float d12 = std::sqrt(x1*x1 + y2*y2);
                    float d21 = std::sqrt(x2*x2 + y1*y1);
                    float d22 = std::sqrt(x2*x2 + y2*y2);

                    float max_d = 450.0f;
                    float fade_dist = 150.0f;

                    float f11 = std::clamp((max_d - d11) / fade_dist, 0.0f, 1.0f);
                    float f12 = std::clamp((max_d - d12) / fade_dist, 0.0f, 1.0f);
                    float f21 = std::clamp((max_d - d21) / fade_dist, 0.0f, 1.0f);
                    float f22 = std::clamp((max_d - d22) / fade_dist, 0.0f, 1.0f);

                    float tex_u1 = ((y1 + size) / (size*2)) * uv_c;
                    float tex_u2 = ((y2 + size) / (size*2)) * uv_c;
                    float tex_v1 = cloud_offset + ((x1 + size) / (size*2)) * uv_c;
                    float tex_v2 = cloud_offset + ((x2 + size) / (size*2)) * uv_c;

                    rlColor4ub(255, 255, 255, (unsigned char)(f11 * 220));
                    rlTexCoord2f(tex_u1, tex_v1); rlVertex3f(x1, y1, 0.0f);
                    rlColor4ub(255, 255, 255, (unsigned char)(f21 * 220));
                    rlTexCoord2f(tex_u1, tex_v2); rlVertex3f(x2, y1, 0.0f);
                    rlColor4ub(255, 255, 255, (unsigned char)(f22 * 220));
                    rlTexCoord2f(tex_u2, tex_v2); rlVertex3f(x2, y2, 0.0f);
                    rlColor4ub(255, 255, 255, (unsigned char)(f12 * 220));
                    rlTexCoord2f(tex_u2, tex_v1); rlVertex3f(x1, y2, 0.0f);
                }
            }
        rlEnd();
        rlSetTexture(0);
    rlPopMatrix();

    rlEnableBackfaceCulling();
    rlEnableDepthTest();
    rlEnableDepthMask();
}
