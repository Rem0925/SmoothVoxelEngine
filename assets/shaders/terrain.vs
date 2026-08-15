#version 330

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec4 vertexColor;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform float time;
uniform vec3 cameraPos;

// Output vertex attributes (to fragment shader)
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec4 fragColor;
out vec3 fragPosition;
out float viewDist; // Para la niebla
out vec2 outFoliageFlags; // x: primario es follaje (pasto/hojas), y: secundario es follaje

void main()
{
    // Decode wind sway flag (> 5.0 on x)
    bool waves_pri = vertexTexCoord.x > 5.0;
    bool waves_sec = vertexTexCoord2.x > 5.0;
    
    // Decode foliage tint flag (> 5.0 on y)
    bool foliage_pri = vertexTexCoord.y > 5.0;
    bool foliage_sec = vertexTexCoord2.y > 5.0;
    
    vec2 realTexCoord = vertexTexCoord;
    if (waves_pri) realTexCoord.x -= 10.0;
    if (foliage_pri) realTexCoord.y -= 10.0;
    
    vec2 realTexCoord2 = vertexTexCoord2;
    if (waves_sec) realTexCoord2.x -= 10.0;
    if (foliage_sec) realTexCoord2.y -= 10.0;

    // Send vertex attributes to fragment shader
    fragTexCoord = realTexCoord;
    fragTexCoord2 = realTexCoord2;
    fragColor = vertexColor;
    outFoliageFlags = vec2(foliage_pri ? 1.0 : 0.0, foliage_sec ? 1.0 : 0.0);
    
    // Apply wind sway if needed
    vec3 animatedPos = vertexPosition;
    if (waves_pri || waves_sec) {
        animatedPos.x += sin(time * 0.8 + vertexPosition.y + vertexPosition.z) * 0.15;
        animatedPos.z += cos(time * 0.6 + vertexPosition.x) * 0.15;
    }
    
    // Calculate fragment position based on model matrix
    fragPosition = vec3(matModel * vec4(animatedPos, 1.0));
    
    // Distancia para la niebla
    viewDist = length(fragPosition - cameraPos);

    // Calculate final vertex position
    gl_Position = mvp * vec4(animatedPos, 1.0);
}
