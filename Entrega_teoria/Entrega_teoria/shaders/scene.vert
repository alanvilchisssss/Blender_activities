#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
uniform float uTime;

// Animacion procedural por codigo: ondeo de banderas (vertex shader)
uniform float uWaveAmp;      // 0 = sin ondeo
uniform vec3  uWaveLong;     // eje largo de la malla (espacio local)
uniform vec3  uWaveThin;     // eje delgado (direccion del desplazamiento)
uniform float uWaveStart, uWaveLen;

out vec3 vPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec3 p = aPos;
    if (uWaveAmp > 0.0) {
        float s  = clamp((dot(p, uWaveLong) - uWaveStart) / max(uWaveLen, 1e-4), 0.0, 1.0);
        float ph = s * 9.0 - uTime * 4.0;
        p += uWaveThin * uWaveAmp * s * (sin(ph) + 0.35 * sin(2.3 * ph + 1.7));
    }
    vec4 w = uModel * vec4(p, 1.0);
    vPos = w.xyz;
    vNormal = uNormalMat * aNormal;
    vUV = aUV;
    gl_Position = uProj * uView * w;
}
