#version 330 core
in vec3 vPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 FragColor;

uniform vec3 uCamPos;
uniform samplerCube uSky;
uniform sampler2D uTex;
uniform bool uHasTex;
uniform bool uTexAlpha;

uniform vec3  uKd, uKs, uEmissive;
uniform float uShin, uAlpha;
uniform int   uMode;      // 0 = Phong (opacos) | 1 = Fresnel + environment mapping
uniform float uMetal;

// Luces (interactuables en tiempo real)
uniform vec3 uSunDir;     // direccion HACIA el sol
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uSkyTint;
#define MAXP 8
uniform int   uNP;
uniform vec3  uPPos[MAXP];
uniform vec3  uPCol[MAXP];
uniform float uPRange;
uniform int   uNS;
uniform vec3  uSPos[2];
uniform vec3  uSDir[2];
uniform vec3  uSCol[2];

vec3 blinn(vec3 N, vec3 V, vec3 L, vec3 col, vec3 kd, vec3 ks, float shin) {
    float ndl = max(dot(N, L), 0.0);
    vec3  H   = normalize(L + V);
    float spec = ndl > 0.0 ? pow(max(dot(N, H), 0.0), shin) : 0.0;
    return col * (kd * ndl + ks * spec);
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vPos);
    if (dot(N, V) < 0.0) N = -N;                    // doble cara

    vec3 kd = uKd; float a = uAlpha;
    if (uHasTex) {
        vec4 t = texture(uTex, vUV);
        kd *= t.rgb;
        if (uTexAlpha) { if (t.a < 0.4) discard; }
        else a *= t.a;
    }

    // ---- Modelo 1: Phong / Blinn-Phong (sol + puntuales + reflectores) ----
    vec3 c = uAmbient * kd;
    c += blinn(N, V, normalize(uSunDir), uSunColor, kd, uKs, uShin);
    for (int i = 0; i < uNP; ++i) {
        vec3 d = uPPos[i] - vPos; float r = length(d);
        float att = 1.0 / (1.0 + (r / uPRange) * (r / uPRange));
        c += blinn(N, V, d / r, uPCol[i] * att, kd, uKs, uShin);
    }
    for (int i = 0; i < uNS; ++i) {
        vec3 d = uSPos[i] - vPos; float r = length(d);
        float cosA = dot(normalize(-d), normalize(uSDir[i]));
        float cone = smoothstep(0.80, 0.93, cosA);
        float att  = 1.0 / (1.0 + (r / (uPRange * 2.0)) * (r / (uPRange * 2.0)));
        c += blinn(N, V, d / r, uSCol[i] * att * cone, kd, uKs, uShin);
    }

    // ---- Modelo 2: Fresnel (Schlick) + environment mapping (skybox) ----
    if (uMode == 1) {
        vec3 R = reflect(-V, N);
        vec3 env = texture(uSky, R).rgb * uSkyTint;
        float F0 = mix(0.04, 0.85, uMetal);
        float F  = F0 + (1.0 - F0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        vec3 tintEnv = env * mix(vec3(1.0), kd, uMetal);
        c = mix(c, tintEnv, clamp(F, 0.0, 1.0));
    }

    c += uEmissive;
    FragColor = vec4(c, a);
}
