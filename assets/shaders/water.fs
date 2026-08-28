#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;
in float viewDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 sunDir;
uniform vec3 cameraPos;

out vec4 finalColor;

void main()
{
    // Profundidad simulada (0.0 = orilla, 1.0 = aguas profundas)
    float depth = 1.0 - fragColor.a;
    float smoothDepth = smoothstep(0.0, 1.0, depth);

    // 1. Color Vibrante del Agua (Afectado por sunAltitude para ciclo día/noche)
    float sunAltitude = clamp(sunDir.y * 1.2 + 0.3, 0.05, 1.0);
    vec4 texColor = texture(texture0, fragTexCoord);
    vec3 shallowColor = vec3(0.2, 0.8, 0.9); // Cyan vibrante
    vec3 deepColor = vec3(0.05, 0.3, 0.8);   // Azul profundo
    
    // Mezclamos el cyan en lugar de multiplicar, así el agua bajita no desaparece
    vec3 waterColorMix = mix(shallowColor, deepColor, smoothDepth);
    vec3 vibrantWater = mix(texColor.rgb, waterColorMix, 0.65) * sunAltitude; // Se oscurece naturalmente de noche
    vec4 waterColor = vec4(vibrantWater, 0.78); // Opacidad reducida un poco para más transparencia

    finalColor = waterColor;

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