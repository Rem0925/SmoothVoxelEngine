#version 330

// Atributos de los vértices
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec4 fragColor;
in vec3 fragPosition;
in float viewDist;

// Uniforms
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 fogColor;

// Color de salida
out vec4 finalColor;

// 1. Hash Ajustado (Para evitar picos extraños y salvajes)
float hash(vec2 p) {
    vec2 p2 = fract(p * vec2(0.3183099, 0.3678794));
    p2 += dot(p2, p2.yx + 19.19);
    
    // En lugar de devolver un valor de 0.0 a 1.0, lo comprimimos (ej. 0.2 a 0.8).
    // Esto hace que la diferencia de altura entre los puntos aleatorios sea menor,
    // resultando en líneas rectas mucho más suaves y sin dientes de sierra agresivos.
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
    
    // 3. Escala de los polígonos: Controla qué tan largos son los cortes rectos
    float geoScale = 3.5; 
    
    // 4. Proyección Triplanar (Mantiene la cuadrícula sin estirarse en pendientes)
    vec3 normal = abs(normalize(cross(dFdx(fragPosition), dFdy(fragPosition))));
    
    vec2 projCoord;
    if (normal.x >= normal.y && normal.x >= normal.z) projCoord = fragPosition.yz;
    else if (normal.y >= normal.x && normal.y >= normal.z) projCoord = fragPosition.xz;
    else projCoord = fragPosition.xy;
    
    // Evaluamos los puntos interconectados
    float noise = geometricNoise(projCoord * geoScale);
    
    // 5. EL SECRETO PARA ELIMINAR ISLAS Y HUECOS:
    // Reducimos el multiplicador de 1.3 a 0.6. 
    // Ahora el ruido solo tiene fuerza para mover el borde unos pocos píxeles, 
    // por lo que es matemáticamente imposible que genere textura flotante en áreas puras.
    float noiseOffset = (noise - 0.5) * 0.6; 
    
    // 6. Corte duro para separación nítida
    vec4 blendedTex;
    if (baseWeight + noiseOffset > 0.5) {
        blendedTex = texPrimary;
    } else {
        blendedTex = texSecondary;
    }

    // --- ILUMINACIÓN FLAT SHADING ---
    // Reconstruimos la normal del polígono a partir de sus derivadas en pantalla
    vec3 dpdx = dFdx(fragPosition);
    vec3 dpdy = dFdy(fragPosition);
    vec3 surfNormal = cross(dpdx, dpdy);
    if (dot(surfNormal, surfNormal) > 0.0001) {
        surfNormal = normalize(surfNormal);
    } else {
        surfNormal = vec3(0.0, 1.0, 0.0);
    }
    
    // Luz Direccional (Sol) y Luz Ambiental
    vec3 lightDir = normalize(vec3(0.6, 0.8, -0.4));
    float diff = max(dot(surfNormal, lightDir), 0.0);
    
    // Sombras más suaves estilo voxel (0.5 a 1.0)
    float lighting = 0.5 + 0.5 * diff;

    // Apply vertex color, lighting, and diffuse
    vec4 colorNoAlpha = vec4(fragColor.rgb * lighting, 1.0);
    finalColor = blendedTex * colorNoAlpha * colDiffuse;
    
    // --- NIEBLA DE PROFUNDIDAD ---
    float fogStart = 30.0;
    float fogEnd = 80.0;
    float fogFactor = clamp((viewDist - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    
    finalColor.rgb = mix(finalColor.rgb, fogColor, fogFactor);
    
    // Discard transparent pixels
    if (finalColor.a < 0.1) discard;
}