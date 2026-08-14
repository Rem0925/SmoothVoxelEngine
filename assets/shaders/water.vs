#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 cameraPos;
uniform float time;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;
out float viewDist;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    
    vec3 animatedPos = vertexPosition;
    
    // Posición mundial base
    vec3 baseWorldPos = vec3(matModel * vec4(animatedPos, 1.0));
    
    // Onda base simétrica: oscila entre -1 y 1 alrededor del nivel del agua
    float wave = sin(time * 1.5 + baseWorldPos.x * 2.0) * cos(time * 1.2 + baseWorldPos.z * 2.0);
    
    // Aplicar máscara para que la orilla no se despegue de la tierra
    float shoreMask = 1.0 - vertexColor.a; 
    
    // Desplazamiento final (sube y baja, sin inflarse por encima del nivel)
    animatedPos.y += wave * 0.1 * shoreMask;

    fragPosition = vec3(matModel * vec4(animatedPos, 1.0));
    viewDist = length(fragPosition - cameraPos);
    gl_Position = mvp * vec4(animatedPos, 1.0);
}