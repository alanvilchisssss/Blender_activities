#version 330 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uSky;
uniform vec3 uSkyTint;
void main() { FragColor = vec4(texture(uSky, vDir).rgb * uSkyTint, 1.0); }
