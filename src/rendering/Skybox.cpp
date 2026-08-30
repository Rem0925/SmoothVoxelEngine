#include "rendering/Skybox.hpp"
#include "rlgl.h"

void DrawSkybox(Camera3D camera, Texture2D side, Texture2D top,
                Texture2D bottom, float sky_intensity) {
    rlDisableDepthMask();
    rlDisableDepthTest();

    unsigned char c = (unsigned char)(255.0f * sky_intensity);
    rlPushMatrix();
        rlTranslatef(camera.position.x, camera.position.y, camera.position.z);
        float s = 500.0f * 1.01f;
        float u0 = 0.005f;
        float u1 = 0.995f;

        rlSetTexture(top.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, s, -s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, s, -s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, s, s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, s, s);
        rlEnd();

        rlSetTexture(bottom.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, -s, s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, -s, s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, -s, -s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, -s, -s);
        rlEnd();

        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, s, -s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, -s, -s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, -s, -s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, s, -s);
        rlEnd();

        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(s, s, -s);
            rlTexCoord2f(u0, u1); rlVertex3f(s, -s, -s);
            rlTexCoord2f(u1, u1); rlVertex3f(s, -s, s);
            rlTexCoord2f(u1, u0); rlVertex3f(s, s, s);
        rlEnd();

        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(s, s, s);
            rlTexCoord2f(u0, u1); rlVertex3f(s, -s, s);
            rlTexCoord2f(u1, u1); rlVertex3f(-s, -s, s);
            rlTexCoord2f(u1, u0); rlVertex3f(-s, s, s);
        rlEnd();

        rlSetTexture(side.id);
        rlBegin(RL_QUADS);
            rlColor4ub(c, c, c, 255);
            rlTexCoord2f(u0, u0); rlVertex3f(-s, s, s);
            rlTexCoord2f(u0, u1); rlVertex3f(-s, -s, s);
            rlTexCoord2f(u1, u1); rlVertex3f(-s, -s, -s);
            rlTexCoord2f(u1, u0); rlVertex3f(-s, s, -s);
        rlEnd();

        rlSetTexture(0);
    rlPopMatrix();

    rlEnableDepthTest();
    rlEnableDepthMask();
}
