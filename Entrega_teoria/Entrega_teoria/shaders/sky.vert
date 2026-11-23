#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uView, uProj;
out vec3 vDir;
void main() {
    vDir = aPos;
    vec4 p = uProj * mat4(mat3(uView)) * vec4(aPos, 1.0);
    gl_Position = p.xyww;
}
