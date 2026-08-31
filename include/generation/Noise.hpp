#pragma once

class PerlinNoise3D {
private:
    int p[512];

    static double fade(double t);
    static double lerp(double t, double a, double b);
    static double grad(int hash_val, double x, double y, double z);

public:
    PerlinNoise3D(int seed = 42);
    double noise(double x, double y, double z) const;
};

// Global noise generator
extern PerlinNoise3D g_perlin_gen;

// Helper function for fractal noise (FBM)
double pnoise3(double x, double y, double z, int octaves = 3, double persistence = 0.5);
