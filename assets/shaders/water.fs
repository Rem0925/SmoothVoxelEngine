#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;
in float viewDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 fogColor;
uniform float time;
uniform float fogStart;
uniform float fogEnd;

out vec4 finalColor;

// Función hash 2D para generar ruido pseudoaleatorio
float hash(vec2 p) {
    vec2 p2 = fract(p * vec2(0.3183099, 0.3678794));
    p2 += dot(p2, p2.yx + 19.19);
    return fract((p2.x + p2.y) * p2.x);
}

// Función valueNoise para generar ruido procedimental suave
float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

// Mezcla Alpha (Normal Blending) como Photoshop / Roystan
vec4 alphaBlend(vec4 top, vec4 bottom) {
    vec3 color = (top.rgb * top.a) + (bottom.rgb * (1.0 - top.a));
    float alpha = top.a + bottom.a * (1.0 - top.a);
    return vec4(color, alpha);
}

void main()
{
    // Profundidad simulada (0.0 = orilla, 1.0 = aguas profundas)
    float depth = 1.0 - fragColor.a;
    float smoothDepth = smoothstep(0.0, 1.0, depth);

    // 1. Color Vibrante del Agua (Afectado por colDiffuse para día/noche)
    vec4 texColor = texture(texture0, fragTexCoord) * colDiffuse;
    vec3 shallowColor = vec3(0.2, 0.8, 0.9); // Cyan vibrante
    vec3 deepColor = vec3(0.05, 0.3, 0.8);   // Azul profundo
    
    // Mezclamos el cyan en lugar de multiplicar, así el agua bajita no desaparece
    vec3 waterColorMix = mix(shallowColor, deepColor, smoothDepth) * colDiffuse.rgb;
    vec3 vibrantWater = mix(texColor.rgb, waterColorMix, 0.65); // Mezclamos la textura con el color del agua
    vec4 waterColor = vec4(vibrantWater, 0.78); // Opacidad reducida un poco para más transparencia

    // 2. Ruido Evolutivo (Morphing Noise - Fuerte y Rápido)
    // Aumentamos la velocidad de desfase (time * 0.4) para que hierva y cambie de forma más rápido
    vec2 offset1 = vec2(time * 0.4, -time * 0.6);
    vec2 offset2 = vec2(-time * 0.1, time * 0.05); // El movimiento general sigue lento
    
    // Aumentamos la escala (de 1.5 y 2.5 a 3.0 y 5.0) para hacer la espuma mucho más pequeña
    float n1 = valueNoise(fragPosition.xz * 3.0 + offset1);
    float evolvingNoise = valueNoise(fragPosition.xz * 5.0 + offset2 + (n1 * 2.0));

    // 3. Espuma de Superficie Evolutiva
    // Subimos el cutoff base a 0.85 para que la espuma sea escasa y finita
    float surfaceNoiseCutoff = mix(0.85, 0.95, depth); 
    float surfaceFoam = smoothstep(surfaceNoiseCutoff, surfaceNoiseCutoff + 0.05, evolvingNoise) * (1.0 - fragColor.a);
    
    float finalFoam = clamp(surfaceFoam, 0.0, 1.0);

    // 5. Color de la Espuma (Afectada por la iluminación colDiffuse)
    vec4 foamColor = vec4(colDiffuse.rgb, 1.0); // La espuma se oscurece de noche
    foamColor.a *= finalFoam;

    // 5. Mezcla final (Alpha Blending)
    finalColor = alphaBlend(foamColor, waterColor);

    // 6. Niebla de profundidad (Mantenida del original)
    float fogFactor = clamp((viewDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    
    finalColor.rgb = mix(finalColor.rgb, fogColor, fogFactor);
}