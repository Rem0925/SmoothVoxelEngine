#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;
in float viewDist;

uniform sampler2D texture0;
uniform sampler2D noiseTex;
uniform vec4 colDiffuse;
uniform vec3 fogColor;
uniform float time;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 sunDir;
uniform vec3 cameraPos;

out vec4 finalColor;

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
    vec2 offset1 = vec2(time * 0.04, -time * 0.06);
    vec2 offset2 = vec2(-time * 0.01, time * 0.005);
    
    // Muestreo de textura precalculada en lugar de valueNoise procedural
    float n1 = texture(noiseTex, fragPosition.xz * 0.15 + offset1).r;
    float evolvingNoise = texture(noiseTex, fragPosition.xz * 0.25 + offset2 + (n1 * 0.2)).r;

    // 3. Espuma de Superficie Evolutiva
    // Ajustamos el cutoff base para adaptarse a la distribución de la textura de Perlin
    float surfaceNoiseCutoff = mix(0.55, 0.70, depth); 
    float surfaceFoam = smoothstep(surfaceNoiseCutoff, surfaceNoiseCutoff + 0.1, evolvingNoise) * (1.0 - fragColor.a);
    
    float finalFoam = clamp(surfaceFoam, 0.0, 1.0);

    // 5. Color de la Espuma (Afectada por la iluminación colDiffuse)
    vec4 foamColor = vec4(colDiffuse.rgb, 1.0); // La espuma se oscurece de noche
    foamColor.a *= finalFoam;

    // 5. Mezcla final (Alpha Blending)
    finalColor = alphaBlend(foamColor, waterColor);

    // 6. Niebla de profundidad (Mantenida del original)
    float fogFactor = clamp((viewDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    
    vec3 viewDir = normalize(fragPosition - cameraPos);
    vec3 sun = normalize(sunDir);
    float sunHeight = sun.y;
    
    // Matemática del atardecer idéntica al skybox
    float sunsetFactor = clamp(1.0 - abs(sunHeight - 0.05) * 6.0, 0.0, 1.0);
    float sunDot = dot(viewDir, sun);
    float sunsetDirectional = smoothstep(0.5, 1.0, sunDot) * sunsetFactor;
    
    vec3 sunsetColor = vec3(1.0, 0.4, 0.1) * sunsetDirectional;
    vec3 dynamicFogColor = fogColor + sunsetColor;
    
    finalColor.rgb = mix(finalColor.rgb, dynamicFogColor, fogFactor);
}