#include "Noise.hpp"
#include "Config.hpp"
#include <cmath>
#include <numeric>
#include <random>
#include <algorithm>
#include <vector>

PerlinNoise3D g_perlin_gen(Config::WORLD_SEED);

PerlinNoise3D::PerlinNoise3D(int seed) {
    std::vector<int> p_temp(256);
    std::iota(p_temp.begin(), p_temp.end(), 0);
    
    std::mt19937 engine(seed);
    std::shuffle(p_temp.begin(), p_temp.end(), engine);
    
    for (int i = 0; i < 256; ++i) {
        p[i] = p_temp[i];
        p[256 + i] = p_temp[i];
    }
}

double PerlinNoise3D::fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double PerlinNoise3D::lerp(double t, double a, double b) {
    return a + t * (b - a);
}

double PerlinNoise3D::grad(int hash_val, double x, double y, double z) {
    int h = hash_val & 15;
    double u = (h < 8) ? x : y;
    double v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
    return (((h & 1) == 0) ? u : -u) + (((h & 2) == 0) ? v : -v);
}

static inline int fast_floor(double x) {
    int xi = (int)x;
    return x < xi ? xi - 1 : xi;
}

double PerlinNoise3D::noise(double x, double y, double z) const {
    int X_floor = fast_floor(x);
    int Y_floor = fast_floor(y);
    int Z_floor = fast_floor(z);

    int X = X_floor & 255;
    int Y = Y_floor & 255;
    int Z = Z_floor & 255;

    x -= X_floor;
    y -= Y_floor;
    z -= Z_floor;

    double u = fade(x);
    double v = fade(y);
    double w = fade(z);

    int A = p[X] + Y;
    int AA = p[A] + Z;
    int AB = p[A + 1] + Z;
    int B = p[X + 1] + Y;
    int BA = p[B] + Z;
    int BB = p[B + 1] + Z;

    return lerp(w,
        lerp(v,
            lerp(u, grad(p[AA], x, y, z), grad(p[BA], x - 1.0, y, z)),
            lerp(u, grad(p[AB], x, y - 1.0, z), grad(p[BB], x - 1.0, y - 1.0, z))
        ),
        lerp(v,
            lerp(u, grad(p[AA + 1], x, y, z - 1.0), grad(p[BA + 1], x - 1.0, y, z - 1.0)),
            lerp(u, grad(p[AB + 1], x, y - 1.0, z - 1.0), grad(p[BB + 1], x - 1.0, y - 1.0, z - 1.0))
        )
    );
}

double pnoise3(double x, double y, double z, int octaves, double persistence) {
    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double max_value = 0.0;

    for (int i = 0; i < octaves; ++i) {
        total += g_perlin_gen.noise(x * frequency, y * frequency, z * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total / max_value;
}
