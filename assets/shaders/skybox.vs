#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec3 fragPosition;
out vec2 fragTexCoord;
void main() {
    fragPosition = vertexPosition;
    fragTexCoord = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
