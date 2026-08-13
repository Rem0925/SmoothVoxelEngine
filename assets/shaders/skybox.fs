#version 330
in vec3 fragPosition;
in vec2 fragTexCoord;
out vec4 finalColor;
uniform vec3 sunDir;
uniform sampler2D texture0;

void main() {
    vec4 texColor = texture(texture0, fragTexCoord);
    vec3 viewDir = normalize(fragPosition);
    vec3 sun = normalize(sunDir);
    
    float sunHeight = sun.y;
    
    // 1. Oscurecimiento del cielo original (Día a Noche)
    float dayFactor = clamp(sunHeight * 2.0 + 0.2, 0.0, 1.0);
    vec3 nightSky = vec3(0.01, 0.02, 0.05);
    vec3 baseSky = mix(nightSky, texColor.rgb, dayFactor);
    
    // 2. Resplandor del atardecer (Dura menos y solo en el horizonte)
    // Cuando sunHeight está entre -0.1 y 0.2, sunsetFactor es > 0.
    // Usamos abs(sunHeight - 0.05) para que el pico sea justo en el horizonte.
    float sunsetFactor = clamp(1.0 - abs(sunHeight - 0.05) * 6.0, 0.0, 1.0);
    
    // 3. El atardecer SOLO debe verse hacia donde está el sol.
    float sunDot = dot(viewDir, sun);
    float sunsetDirectional = smoothstep(0.5, 1.0, sunDot) * sunsetFactor;
    
    vec3 sunsetColor = vec3(1.0, 0.4, 0.1) * sunsetDirectional;
    
    finalColor = vec4(baseSky + sunsetColor, 1.0);
}
