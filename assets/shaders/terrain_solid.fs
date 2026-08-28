#version 330

// Atributos de los vértices
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec4 fragColor;
in vec3 fragPosition;
in float viewDist;
in vec2 outFoliageFlags;
in float fragAO;
in float fragSunLight;
in float fragBlockLight;

// Uniforms
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 sunDir;
uniform vec3 cameraPos;

// Color de salida
out vec4 finalColor;

// 1. Hash Ajustado (Para evitar picos extraños y salvajes)
float hash(vec2 p) {
    vec2 p2 = fract(p * vec2(0.3183099, 0.3678794));
    p2 += dot(p2, p2.yx + 19.19);
    return mix(0.2, 0.8, fract((p2.x + p2.y) * p2.x));
}

// 2. Interpolación lineal estricta para formar triángulos geométricos
float geometricNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    if (f.x > f.y) {
        return a + (b - a) * f.x + (d - b) * f.y;
    } else {
        return a + (d - c) * f.x + (c - a) * f.y;
    }
}

void main()
{
    vec4 texPrimary = texture(texture0, fragTexCoord);
    vec4 texSecondary = texture(texture0, fragTexCoord2);
    float baseWeight = fragColor.a;
    
    // 6. Proyección para el ruido geométrico
    vec3 normal = abs(normalize(cross(dFdx(fragPosition), dFdy(fragPosition))));
    vec2 projCoord;
    if (normal.x >= normal.y && normal.x >= normal.z) projCoord = fragPosition.yz;
    else if (normal.y >= normal.x && normal.y >= normal.z) projCoord = fragPosition.xz;
    else projCoord = fragPosition.xy;
    
    // Evaluamos los puntos interconectados
    float geoScale = 3.5;
    float noise = geometricNoise(projCoord * geoScale);
    
    // 5. EL SECRETO PARA ELIMINAR ISLAS Y HUECOS:
    float noiseOffset = (noise - 0.5) * 0.6; 
    
    // 6. Corte duro para separación nítida y asignación exacta de tinte (sin halos)
    vec4 blendedTex;
    vec3 blockColor;
    if (baseWeight + noiseOffset > 0.5) {
        blendedTex = texPrimary;
        blockColor = (outFoliageFlags.x > 0.5) ? fragColor.rgb : vec3(1.0);
    } else {
        blendedTex = texSecondary;
        blockColor = (outFoliageFlags.y > 0.5) ? fragColor.rgb : vec3(1.0);
    }

    // --- ILUMINACIÓN FLAT SHADING CON AO Y LUZ VOXEL ---
    vec3 dpdx = dFdx(fragPosition);
    vec3 dpdy = dFdy(fragPosition);
    vec3 surfNormal = cross(dpdx, dpdy);
    if (dot(surfNormal, surfNormal) > 0.0001) {
        surfNormal = normalize(surfNormal);
    } else {
        surfNormal = vec3(0.0, 1.0, 0.0);
    }
    
    // Luz Direccional (Sol)
    vec3 lightDir = normalize(vec3(0.6, 0.8, -0.4));
    float diff = max(dot(surfNormal, lightDir), 0.0);
    
    // 1. Luz Solar modulada por el ciclo día/noche con respuesta perceptual suave
    float sunAltitude = clamp(sunDir.y * 1.2 + 0.3, 0.05, 1.0);
    float sunLight = (fragSunLight * (0.85 + 0.15 * fragSunLight)) * sunAltitude;

    // 2. Luz de Antorchas / Bloques (brillante y cálida)
    float blockLight = (fragBlockLight * (0.85 + 0.15 * fragBlockLight)) * 1.3;

    // 3. Luz Total Combinada (luz ambiental mínima en cuevas suaves de 0.07)
    float ambientCave = 0.07;
    float voxelLight = clamp(max(max(sunLight, blockLight), ambientCave), 0.0, 1.0);

    // 4. Oclusión Ambiental
    float ao = (fragAO > 0.01) ? fragAO : 1.0;

    // 5. Iluminación final modulada
    float lighting = (0.75 + 0.25 * diff) * ao * voxelLight;

    // Tinte cálido suave para la luz de antorchas
    vec3 torchWarmth = vec3(1.0, 0.95, 0.85);
    vec3 finalTint = mix(blockColor, blockColor * torchWarmth, clamp(blockLight, 0.0, 0.5));

    // Apply vertex color, lighting, and diffuse
    vec4 colorNoAlpha = vec4(finalTint * lighting, 1.0);
    finalColor = blendedTex * colorNoAlpha * colDiffuse;
    
    // --- NIEBLA DE PROFUNDIDAD Y ATARDECER DIRECCIONAL ---
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
