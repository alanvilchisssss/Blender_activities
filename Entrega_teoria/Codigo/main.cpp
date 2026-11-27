// ============================================================================
// ============================================================================
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <nlohmann/json.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using glm::mat3;
using glm::mat4;
using glm::vec3;
using std::string;
using std::vector;

// ---------------------------------------------------------------------------
// Configuracion (ajustable)
// ---------------------------------------------------------------------------
static string  gAssets = "assets";
static string  gShaders = "shaders";
static bool    gDemo = false;
static const char* kPathF1    = "Trayectoria_NURBS";      // trayectoria del F1
static const char* kPathTsuru = "Trayectoria_NURBS.001";  // trayectoria del Tsuru
static const float kLapTimeF1    = 30.f;                  // segundos por vuelta (vel. maxima)
static const float kLapTimeTsuru = 48.f;
static const float kPitDistance = 7.0f;   // recorrido de los autos de pits (en largos de auto)
static const bool  kPitFlip = false;      // true si los autos de pits salen en reversa (invierte su frente)

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------
static string readFile(const string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("No se pudo abrir: " + p);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
static bool fileExists(const string& p) { std::ifstream f(p, std::ios::binary); return (bool)f; }

static string baseName(const string& n) {              // "Wheel.Ft.L.001" -> "Wheel.Ft.L"
    size_t d = n.rfind('.');
    if (d == string::npos || d + 1 >= n.size()) return n;
    for (size_t i = d + 1; i < n.size(); ++i) if (!isdigit((unsigned char)n[i])) return n;
    return n.substr(0, d);
}
static bool hasPrefix(const string& s, const char* p) { return s.rfind(p, 0) == 0; }
static float smooth01(float x) { x = glm::clamp(x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }
static float easeInOutCubic(float x) { x = glm::clamp(x, 0.f, 1.f); return x < 0.5f ? 4*x*x*x : 1.f - powf(-2*x + 2, 3) / 2.f; }
static mat4 aboutPivot(const vec3& c, const mat4& R) { return glm::translate(mat4(1), c) * R * glm::translate(mat4(1), -c); }
static vec3 flat(vec3 v) { v.y = 0; float l = glm::length(v); return l > 1e-6f ? v / l : vec3(0, 0, -1); }
static float signedYaw(const vec3& a, const vec3& b) {   // angulo que gira a -> b alrededor de +Y
    return atan2f(glm::cross(a, b).y, glm::dot(a, b));
}

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------
struct Shader {
    GLuint id = 0;
    std::unordered_map<string, GLint> loc;
    static GLuint compile(GLenum type, const string& src, const string& name) {
        GLuint s = glCreateShader(type);
        const char* c = src.c_str();
        glShaderSource(s, 1, &c, nullptr); glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[2048]; glGetShaderInfoLog(s, 2048, nullptr, log);
                   throw std::runtime_error("Shader " + name + ": " + log); }
        return s;
    }
    void load(const string& vs, const string& fs) {
        GLuint v = compile(GL_VERTEX_SHADER, readFile(vs), vs);
        GLuint f = compile(GL_FRAGMENT_SHADER, readFile(fs), fs);
        id = glCreateProgram(); glAttachShader(id, v); glAttachShader(id, f); glLinkProgram(id);
        GLint ok; glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) { char log[2048]; glGetProgramInfoLog(id, 2048, nullptr, log); throw std::runtime_error(log); }
        glDeleteShader(v); glDeleteShader(f);
    }
    void use() const { glUseProgram(id); }
    GLint L(const string& n) { auto it = loc.find(n); if (it != loc.end()) return it->second;
                               GLint l = glGetUniformLocation(id, n.c_str()); loc[n] = l; return l; }
    void set(const string& n, float v) { glUniform1f(L(n), v); }
    void set(const string& n, int v)   { glUniform1i(L(n), v); }
    void set(const string& n, const vec3& v) { glUniform3fv(L(n), 1, glm::value_ptr(v)); }
    void set(const string& n, const mat4& m) { glUniformMatrix4fv(L(n), 1, GL_FALSE, glm::value_ptr(m)); }
    void set(const string& n, const mat3& m) { glUniformMatrix3fv(L(n), 1, GL_FALSE, glm::value_ptr(m)); }
    void setIdx(const char* n, int i, const vec3& v) { char b[64]; snprintf(b, 64, "%s[%d]", n, i); set(b, v); }
};

// ---------------------------------------------------------------------------
// Texturas y skybox
// ---------------------------------------------------------------------------
struct TexData { GLuint id = 0; bool alpha = false; };

static TexData makeTexture(unsigned char* px, int w, int h) {
    TexData t;
    for (int i = 0; i < w * h; ++i) if (px[i * 4 + 3] < 250) { t.alpha = true; break; }
    glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return t;
}

static GLuint loadCubemap(const string& dir) {
    GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    const char* names[6] = {"px", "nx", "py", "ny", "pz", "nz"};
    stbi_set_flip_vertically_on_load(false);
    for (int i = 0; i < 6; ++i) {
        int w, h, n; string p = dir + "/" + names[i] + ".png";
        unsigned char* d = stbi_load(p.c_str(), &w, &h, &n, 3);
        if (d) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, d);
            stbi_image_free(d);
        } else {                                        // cielo procedural de respaldo
            if (i == 0) std::cerr << "[aviso] Skybox no encontrado en " << dir << " -> cielo procedural\n";
            const int S = 128; vector<unsigned char> buf(S * S * 3);
            for (int y = 0; y < S; ++y) for (int x = 0; x < S; ++x) {
                float t = (float)y / (S - 1);               // 0 arriba, 1 abajo en la imagen
                vec3 top(0.25f, 0.45f, 0.85f), hor(0.85f, 0.75f, 0.65f), gnd(0.25f, 0.22f, 0.2f);
                vec3 c = (i == 2) ? top : (i == 3) ? gnd : (t < 0.5f ? glm::mix(top, hor, t * 2) : glm::mix(hor, gnd, (t - 0.5f) * 2));
                for (int k = 0; k < 3; ++k) buf[(y * S + x) * 3 + k] = (unsigned char)(255.f * c[k]);
            }
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8, S, S, 0, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return id;
}

// ---------------------------------------------------------------------------
// Modelo
// ---------------------------------------------------------------------------
struct Material {
    vec3 kd{0.8f}, ks{0.15f};
    float shin = 32, alpha = 1, metal = 0;
    GLuint tex = 0; bool texAlpha = false;
};
struct GLMesh {
    GLuint vao = 0, vbo = 0, ebo = 0; GLsizei n = 0; int mat = 0;
    vec3 bmin{1e30f}, bmax{-1e30f};
};
struct Obj {
    string name, base;
    mat4 world{1}, anim{1};              // anim: transformacion extra en espacio MUNDO (animaciones)
    vector<int> meshes;
    vec3 wmin{1e30f}, wmax{-1e30f};
    vec3 emissive{0};
    float wave = 0;                      // ondeo (banderas): fraccion de la longitud LOCAL de la malla
    bool visible = true;                 // false = plantilla oculta (no se dibuja)
    int mode = 0; float metal = -1;      // 0 Phong, 1 Fresnel
    vec3 center() const { return 0.5f * (wmin + wmax); }
    vec3 ext() const { return wmax - wmin; }
};

static vector<Material> gMats;
static vector<GLMesh>   gMeshes;
static vector<Obj>      gObjs;
static std::unordered_map<string, int> gByName;
static vec3 gSceneMin{1e30f}, gSceneMax{-1e30f};

static void computeBounds(Obj& o) {
    o.wmin = vec3(1e30f); o.wmax = vec3(-1e30f);
    for (int mi : o.meshes) {
        const GLMesh& m = gMeshes[mi];
        for (int c = 0; c < 8; ++c) {
            vec3 corner((c & 1) ? m.bmax.x : m.bmin.x, (c & 2) ? m.bmax.y : m.bmin.y, (c & 4) ? m.bmax.z : m.bmin.z);
            vec3 p = vec3(o.world * glm::vec4(corner, 1));
            o.wmin = glm::min(o.wmin, p); o.wmax = glm::max(o.wmax, p);
        }
    }
}
static mat4 toGlm(const aiMatrix4x4& m) { return glm::transpose(glm::make_mat4(&m.a1)); }

static void visitNode(const aiNode* n, const mat4& parent) {
    mat4 w = parent * toGlm(n->mTransformation);
    if (n->mNumMeshes) {
        Obj o; o.name = n->mName.C_Str(); o.base = baseName(o.name); o.world = w;
        for (unsigned i = 0; i < n->mNumMeshes; ++i) {
            int mi = (int)n->mMeshes[i]; o.meshes.push_back(mi);
            const GLMesh& m = gMeshes[mi];
            for (int c = 0; c < 8; ++c) {
                vec3 corner((c & 1) ? m.bmax.x : m.bmin.x, (c & 2) ? m.bmax.y : m.bmin.y, (c & 4) ? m.bmax.z : m.bmin.z);
                vec3 p = vec3(w * glm::vec4(corner, 1));
                o.wmin = glm::min(o.wmin, p); o.wmax = glm::max(o.wmax, p);
            }
        }
        gSceneMin = glm::min(gSceneMin, o.wmin); gSceneMax = glm::max(gSceneMax, o.wmax);
        gByName[o.name] = (int)gObjs.size();
        gObjs.push_back(o);
    }
    for (unsigned i = 0; i < n->mNumChildren; ++i) visitNode(n->mChildren[i], w);
}

static void loadModel(const string& file) {
    Assimp::Importer imp;
    const aiScene* sc = imp.ReadFile(file, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                           aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality);
    if (!sc || !sc->mRootNode) throw std::runtime_error(string("Assimp: ") + imp.GetErrorString());
    string dir = file.substr(0, file.find_last_of("/\\") + 1);
    stbi_set_flip_vertically_on_load(true);              // Assimp invierte V al importar glTF
    std::map<string, TexData> cache;

    for (unsigned i = 0; i < sc->mNumMaterials; ++i) {
        const aiMaterial* m = sc->mMaterials[i]; Material mt;
        aiColor4D c;
        if (aiGetMaterialColor(m, AI_MATKEY_BASE_COLOR, &c) == AI_SUCCESS ||
            aiGetMaterialColor(m, AI_MATKEY_COLOR_DIFFUSE, &c) == AI_SUCCESS) { mt.kd = vec3(c.r, c.g, c.b); mt.alpha = c.a; }
        float op = 1; if (m->Get(AI_MATKEY_OPACITY, op) == AI_SUCCESS) mt.alpha = std::min(mt.alpha, op);
        float metal = 0, rough = 0.6f;
        m->Get(AI_MATKEY_METALLIC_FACTOR, metal); m->Get(AI_MATKEY_ROUGHNESS_FACTOR, rough);
        mt.metal = metal; mt.shin = glm::mix(200.f, 8.f, rough);
        mt.ks = glm::mix(vec3(0.12f), mt.kd, metal) * (1.f - 0.7f * rough);
        aiString tp;
        if (m->GetTexture(aiTextureType_BASE_COLOR, 0, &tp) == AI_SUCCESS || m->GetTexture(aiTextureType_DIFFUSE, 0, &tp) == AI_SUCCESS) {
            string key = tp.C_Str();
            if (!cache.count(key)) {
                int w = 0, h = 0, n = 0; unsigned char* d = nullptr;
                if (const aiTexture* et = sc->GetEmbeddedTexture(key.c_str()))
                    d = stbi_load_from_memory((const stbi_uc*)et->pcData, (int)et->mWidth, &w, &h, &n, 4);
                else d = stbi_load((dir + key).c_str(), &w, &h, &n, 4);
                if (d) { cache[key] = makeTexture(d, w, h); stbi_image_free(d); } else cache[key] = TexData{};
            }
            mt.tex = cache[key].id; mt.texAlpha = cache[key].alpha;
        }
        gMats.push_back(mt);
    }

    gMeshes.resize(sc->mNumMeshes);
    for (unsigned i = 0; i < sc->mNumMeshes; ++i) {
        const aiMesh* am = sc->mMeshes[i]; GLMesh& gm = gMeshes[i];
        vector<float> vb; vector<unsigned> ib;
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            aiVector3D p = am->mVertices[v], n = am->HasNormals() ? am->mNormals[v] : aiVector3D(0, 1, 0);
            aiVector3D t = am->HasTextureCoords(0) ? am->mTextureCoords[0][v] : aiVector3D(0, 0, 0);
            vb.insert(vb.end(), {p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y});
            gm.bmin = glm::min(gm.bmin, vec3(p.x, p.y, p.z)); gm.bmax = glm::max(gm.bmax, vec3(p.x, p.y, p.z));
        }
        for (unsigned f = 0; f < am->mNumFaces; ++f)
            if (am->mFaces[f].mNumIndices == 3) ib.insert(ib.end(), {am->mFaces[f].mIndices[0], am->mFaces[f].mIndices[1], am->mFaces[f].mIndices[2]});
        gm.n = (GLsizei)ib.size(); gm.mat = (int)am->mMaterialIndex;
        glGenVertexArrays(1, &gm.vao); glGenBuffers(1, &gm.vbo); glGenBuffers(1, &gm.ebo);
        glBindVertexArray(gm.vao);
        glBindBuffer(GL_ARRAY_BUFFER, gm.vbo); glBufferData(GL_ARRAY_BUFFER, vb.size() * 4, vb.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib.size() * 4, ib.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    }
    visitNode(sc->mRootNode, mat4(1));
    std::cout << "Modelo: " << gObjs.size() << " objetos, " << gMeshes.size() << " mallas, " << gMats.size() << " materiales\n";
}

// ---------------------------------------------------------------------------
// Trayectorias (exportadas de Blender o fallback ovalado)
// ---------------------------------------------------------------------------
struct Path {
    vector<vec3> p; vector<float> cum; float len = 0;
    void build(const vector<vec3>& pts) {
        p.clear(); cum.clear();
        for (auto& v : pts) if (p.empty() || glm::distance(v, p.back()) > 1e-5f) p.push_back(v);
        if (p.size() > 2 && glm::distance(p.front(), p.back()) < 1e-5f) p.pop_back();
        float acc = 0; cum.push_back(0);
        for (size_t i = 1; i < p.size(); ++i) { acc += glm::distance(p[i], p[i - 1]); cum.push_back(acc); }
        len = acc + glm::distance(p.back(), p.front());
    }
    bool ok() const { return p.size() >= 3 && len > 1e-4f; }
    vec3 at(float s) const {
        s = fmodf(s, len); if (s < 0) s += len;
        size_t i = std::upper_bound(cum.begin(), cum.end(), s) - cum.begin() - 1;
        size_t j = (i + 1) % p.size();
        float segEnd = (i + 1 < p.size()) ? cum[i + 1] : len;
        float t = (s - cum[i]) / std::max(segEnd - cum[i], 1e-6f);
        return glm::mix(p[i], p[j], t);
    }
    vec3 tangent(float s) const { float d = std::max(len / 500.f, 1e-3f); return flat(at(s + d) - at(s - d)); }
};
static Path gPathF1, gPathTsuru;

static Path ovalPath(vec3 c, float rx, float rz) {
    vector<vec3> pts; for (int i = 0; i < 240; ++i) { float a = (float)i / 240 * 6.2831853f; pts.push_back(c + vec3(rx * cosf(a), 0, rz * sinf(a))); }
    Path p; p.build(pts); return p;
}

// ---------------------------------------------------------------------------
// Autos (jerarquia: carroceria -> 4 llantas)
// ---------------------------------------------------------------------------
struct Car {
    string tag;
    int body = -1, wh[4] = {-1, -1, -1, -1};          // FL, FR, BL, BR
    vec3 wc[4];                                        // centro de cada llanta (pose inicial)
    vec3 b0, fwd0, spinAxis; float wheelR = 0.3f, bodyLen = 1;
    const Path* path = nullptr; float s = 0, v = 0, vmax = 1; bool on = false;
    float spin = 0, steer = 0; mat4 M{1};
    bool hasCam = false; vec3 camP, camF, camU;
    bool valid() const { return body >= 0; }

    void setup(const string& suffix) {
        auto f = [&](const char* n) { auto it = gByName.find(string(n) + suffix); return it == gByName.end() ? -1 : it->second; };
        body = f("Body");
        wh[0] = f("Wheel.Ft.L"); wh[1] = f("Wheel.Ft.R"); wh[2] = f("Wheel.Bk.L"); wh[3] = f("Wheel.Bk.R");
        if (body < 0) return;
        bool all = true; for (int i = 0; i < 4; ++i) { if (wh[i] < 0) all = false; else wc[i] = gObjs[wh[i]].center(); }
        vec3 up(0, 1, 0);
        if (all) {
            vec3 front = 0.5f * (wc[0] + wc[1]), back = 0.5f * (wc[2] + wc[3]);
            fwd0 = flat(front - back); b0 = 0.5f * (front + back);
            wheelR = 0.5f * gObjs[wh[0]].ext().y;
        } else { fwd0 = vec3(0, 0, -1); b0 = gObjs[body].center(); }
        spinAxis = glm::cross(up, fwd0);
        vec3 e = gObjs[body].ext(); bodyLen = glm::dot(glm::abs(fwd0), e);
    }
    void update(float dt, float t) {
        (void)t;
        float target = on ? vmax : 0.f;
        float k = on ? 1.2f : 2.5f;                       // arranque suave / frenado mas rapido
        v += (target - v) * (1.f - expf(-k * dt));
        if (v < 1e-3f && !on) v = 0;
        s += v * dt;
        vec3 p = path->at(s), tg = path->tangent(s), tg2 = path->tangent(s + bodyLen * 1.5f);
        float yaw = signedYaw(fwd0, tg);
        float turn = glm::clamp(signedYaw(tg, tg2) * 2.5f, -0.6f, 0.6f);
        steer += (turn - steer) * (1.f - expf(-8.f * dt));
        spin += (wheelR > 1e-4f ? v / wheelR : 0.f) * dt;
        M = glm::translate(mat4(1), vec3(p.x, 0, p.z)) * glm::rotate(mat4(1), yaw, vec3(0, 1, 0)) *
            glm::translate(mat4(1), vec3(-b0.x, 0, -b0.z));
        gObjs[body].anim = M;
        for (int i = 0; i < 4; ++i) if (wh[i] >= 0) {
            mat4 st = (i < 2) ? glm::rotate(mat4(1), steer, vec3(0, 1, 0)) : mat4(1);       // direccion (delanteras)
            mat4 rl = glm::rotate(mat4(1), spin, spinAxis);                      // rodamiento
            gObjs[wh[i]].anim = M * aboutPivot(wc[i], st * rl);
        }
    }
    mat4 frame() const { return M; }
};

// ---------------------------------------------------------------------------
// Estado de animaciones / entrada
// ---------------------------------------------------------------------------
struct State {
    // Camara
    vec3 camPos{0, 50, 100}; float yaw = -90, pitch = -20; int camMode = 0;
    double lastX = 0, lastY = 0; bool firstMouse = true, mouseOn = true;
    // luces
    float dayTarget = 1, dayLevel = 1; bool headlights = true;
    // animaciones por input
    float flagStrong = 0, flagStrongTarget = 0;     // F
    float radarOmega = 0, radarTarget = 0, radarAng = 0;   // R
    float billT0 = -100;                            // B
    float pitT0 = -100;                             // P
    float gustT0 = -100;                            // V
    float startT0 = -100;                           // L
    bool startSeqActive = false;
    // util
    string screenshot; int shotFrames = 0; bool wantShot = false;
} S;

static Car gF1, gTsuru;
static vec3 gSunDir0{0.4f, 0.8f, 0.3f}, gSunCol0{1.0f, 0.95f, 0.85f};

static void keyCb(GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    double now = glfwGetTime();
    switch (key) {
        case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, 1); break;
        case GLFW_KEY_TAB:
            S.mouseOn = !S.mouseOn; S.firstMouse = true;
            glfwSetInputMode(w, GLFW_CURSOR, S.mouseOn ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL); break;
        case GLFW_KEY_C: S.camMode = (S.camMode + 1) % 4; break;
        // ---- 8 animaciones por input ----
        case GLFW_KEY_SPACE: if (gF1.valid()) gF1.on = !gF1.on; break;                       // I1 F1 arranca/frena
        case GLFW_KEY_T:     if (gTsuru.valid()) gTsuru.on = !gTsuru.on; break;              // I2 Tsuru arranca/frena
        case GLFW_KEY_F:     S.flagStrongTarget = S.flagStrongTarget > 0.5f ? 0.f : 1.f; break; // I3 viento fuerte en banderas
        case GLFW_KEY_R:     S.radarTarget = S.radarTarget > 0.f ? 0.f : 2.2f; break;       // I4 radar gira
        case GLFW_KEY_B:     S.billT0 = (float)now; break;                                  // I5 anuncios giran 360
        case GLFW_KEY_P:     S.pitT0 = (float)now; break;                                   // I6 autos de pits salen y regresan
        case GLFW_KEY_V:     S.gustT0 = (float)now; break;                                  // I7 rafaga en arboles
        case GLFW_KEY_L:     S.startT0 = (float)now; S.startSeqActive = true; break;        // I8 semaforo de salida
        // ---- 2 interacciones con luces ----
        case GLFW_KEY_N:     S.dayTarget = S.dayTarget > 0.5f ? 0.f : 1.f; break;          // dia/noche
        case GLFW_KEY_H:     S.headlights = !S.headlights; break;                           // faros/postes
        case GLFW_KEY_F12:   S.wantShot = true; break;
        default: break;
    }
}
static void mouseCb(GLFWwindow*, double x, double y) {
    if (!S.mouseOn) return;
    if (S.firstMouse) { S.lastX = x; S.lastY = y; S.firstMouse = false; }
    S.yaw += (float)(x - S.lastX) * 0.1f; S.pitch = glm::clamp(S.pitch + (float)(S.lastY - y) * 0.1f, -89.f, 89.f);
    S.lastX = x; S.lastY = y;
}

// ---------------------------------------------------------------------------
// Dibujo
// ---------------------------------------------------------------------------
static GLuint gCubeVAO = 0;
static void initCube() {
    float v[] = {-1,1,-1,-1,-1,-1,1,-1,-1,1,-1,-1,1,1,-1,-1,1,-1, -1,-1,1,-1,-1,-1,-1,1,-1,-1,1,-1,-1,1,1,-1,-1,1,
                 1,-1,-1,1,-1,1,1,1,1,1,1,1,1,1,-1,1,-1,-1, -1,-1,1,-1,1,1,1,1,1,1,1,1,1,-1,1,-1,-1,1,
                 -1,1,-1,1,1,-1,1,1,1,1,1,1,-1,1,1,-1,1,-1, -1,-1,-1,-1,-1,1,1,-1,-1,1,-1,-1,-1,-1,1,1,-1,1};
    GLuint vbo; glGenVertexArrays(1, &gCubeVAO); glGenBuffers(1, &vbo);
    glBindVertexArray(gCubeVAO); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
}

static void drawObjects(Shader& sh, bool transparentPass, float time) {
    for (const Obj& o : gObjs) {
        if (!o.visible) continue;
        mat4 model = o.anim * o.world;
        sh.set("uModel", model); sh.set("uNormalMat", mat3(glm::transpose(glm::inverse(model))));
        sh.set("uEmissive", o.emissive);
        for (int mi : o.meshes) {
            const GLMesh& m = gMeshes[mi]; const Material& mt = gMats[m.mat];
            bool trans = mt.alpha < 0.99f;
            if (trans != transparentPass) continue;
            sh.set("uKd", mt.kd); sh.set("uKs", mt.ks); sh.set("uShin", mt.shin); sh.set("uAlpha", mt.alpha);
            bool fres = o.mode == 1 || mt.metal > 0.5f || trans;
            sh.set("uMode", fres ? 1 : 0);
            sh.set("uMetal", o.metal >= 0 ? o.metal : mt.metal);
            sh.set("uHasTex", mt.tex ? 1 : 0); sh.set("uTexAlpha", mt.texAlpha ? 1 : 0);
            if (mt.tex) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, mt.tex); }
            if (o.wave > 0) {
                vec3 e = m.bmax - m.bmin; int li = 0, ti = 0;
                for (int k = 1; k < 3; ++k) { if (e[k] > e[li]) li = k; if (e[k] < e[ti]) ti = k; }
                vec3 L(0), T(0); L[li] = 1; T[ti] = 1;
                sh.set("uWaveAmp", o.wave * e[li]); sh.set("uWaveLong", L); sh.set("uWaveThin", T);
                sh.set("uWaveStart", m.bmin[li]); sh.set("uWaveLen", e[li]);
            } else sh.set("uWaveAmp", 0.f);
            sh.set("uTime", time);
            glBindVertexArray(m.vao); glDrawElements(GL_TRIANGLES, m.n, GL_UNSIGNED_INT, nullptr);
        }
    }
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--assets") && i + 1 < argc) gAssets = argv[++i];
        else if (!strcmp(argv[i], "--shaders") && i + 1 < argc) gShaders = argv[++i];
        else if (!strcmp(argv[i], "--demo")) gDemo = true;
        else if (!strcmp(argv[i], "--cam") && i + 1 < argc) S.camMode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 2 < argc) { S.screenshot = argv[++i]; S.shotFrames = atoi(argv[++i]); }
    }
    if (!fileExists(gAssets + "/foro_sol.glb") && argc > 0) {          // buscar junto al ejecutable
        auto dir = std::filesystem::absolute(argv[0]).parent_path();
        if (fileExists((dir / "assets/foro_sol.glb").string())) { gAssets = (dir / "assets").string(); gShaders = (dir / "shaders").string(); }
    }
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWwindow* win = glfwCreateWindow(1280, 720, "Foro Sol - OpenGL", nullptr, nullptr);
    if (!win) { std::cerr << "No se pudo crear la ventana OpenGL 3.3\n"; return 1; }
    glfwMakeContextCurrent(win); glfwSwapInterval(1);
    glewExperimental = GL_TRUE; if (glewInit() != GLEW_OK) { std::cerr << "GLEW fallo\n"; return 1; }
    glfwSetKeyCallback(win, keyCb); glfwSetCursorPosCallback(win, mouseCb);
    if (S.screenshot.empty()) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED); else S.mouseOn = false;

    try {
        Shader scene, sky;
        scene.load(gShaders + "/scene.vert", gShaders + "/scene.frag");
        sky.load(gShaders + "/sky.vert", gShaders + "/sky.frag");
        initCube();
        GLuint cube = loadCubemap(gAssets + "/skybox");
        loadModel(gAssets + "/foro_sol.glb");

        // ---------- meta (trayectorias, sol, camaras de Blender) ----------
        vec3 size = gSceneMax - gSceneMin, center = 0.5f * (gSceneMin + gSceneMax);
        float sceneSize = std::max({size.x, size.y, size.z});
        try {
            auto J = nlohmann::json::parse(readFile(gAssets + "/scene_meta.json"));
            auto rd = [&](const char* key, Path& out) {
                if (J.contains("paths") && J["paths"].contains(key)) {
                    vector<vec3> pts; for (auto& q : J["paths"][key]["points"]) pts.push_back(vec3(q[0], q[1], q[2]));
                    out.build(pts);
                }
            };
            rd(kPathF1, gPathF1); rd(kPathTsuru, gPathTsuru);
            if (J.contains("sun")) {
                auto& s = J["sun"]; gSunDir0 = -vec3(s["dir"][0], s["dir"][1], s["dir"][2]);
                gSunCol0 = vec3(s["color"][0], s["color"][1], s["color"][2]);
            }
            if (J.contains("cameras")) for (auto& [k, c] : J["cameras"].items()) {
                Car* car = (k.find("f1") != string::npos) ? &gF1 : (k.find("tsuru") != string::npos) ? &gTsuru : nullptr;
                if (!car) continue;
                car->hasCam = true; car->camP = vec3(c["pos"][0], c["pos"][1], c["pos"][2]);
                car->camF = vec3(c["fwd"][0], c["fwd"][1], c["fwd"][2]); car->camU = vec3(c["up"][0], c["up"][1], c["up"][2]);
            }
        } catch (const std::exception& e) { std::cerr << "[aviso] scene_meta.json no disponible (" << e.what() << ")\n"; }
        if (!gPathF1.ok())    { std::cerr << "[aviso] sin trayectoria F1 -> oval de respaldo\n";    gPathF1 = ovalPath(center, size.x * 0.30f, size.z * 0.30f); }
        if (!gPathTsuru.ok()) { std::cerr << "[aviso] sin trayectoria Tsuru -> oval de respaldo\n"; gPathTsuru = ovalPath(center, size.x * 0.24f, size.z * 0.24f); }

        gF1.tag = "F1"; gF1.setup(".001"); gTsuru.tag = "Tsuru"; gTsuru.setup("");
        gF1.path = &gPathF1; gF1.vmax = gPathF1.len / kLapTimeF1; gF1.on = false;
        gTsuru.path = &gPathTsuru; gTsuru.vmax = gPathTsuru.len / kLapTimeTsuru;
        gTsuru.s = gPathTsuru.len * 0.5f;                  // salen en puntos distintos
        if (!gF1.valid())    std::cerr << "[aviso] No se encontro 'Body.001' (F1)\n";
        if (!gTsuru.valid()) std::cerr << "[aviso] No se encontro 'Body' (Tsuru)\n";

        // ---------- plantillas ocultas (arboles, radar): se colocan instancias alrededor de la pista ----------
        {
            float refLen = gF1.valid() ? gF1.bodyLen : (gTsuru.valid() ? gTsuru.bodyLen : sceneSize * 0.01f);
            float groundY = gF1.valid() && gF1.wh[0] >= 0 ? gObjs[gF1.wh[0]].wmin.y : gSceneMin.y;
            vector<int> tplTrees, tplRadar;
            size_t nOrig = gObjs.size();
            for (size_t i = 0; i < nOrig; ++i) {
                Obj& o = gObjs[i];
                bool tiny = std::max({o.ext().x, o.ext().y, o.ext().z}) < sceneSize * 0.02f && glm::length(o.center() - center) > 0.f;
                if (!tiny) continue;
                if (hasPrefix(o.base, "tree")) { tplTrees.push_back((int)i); o.visible = false; }
                else if (hasPrefix(o.base, "radarEquipment")) { tplRadar.push_back((int)i); o.visible = false; }
                else if (hasPrefix(o.base, "flagCheckersSmall")) o.visible = false;
            }
            auto spawn = [&](int tplIdx, vec3 pos, float height, float yaw, const string& name) {
                Obj o = gObjs[tplIdx]; o.visible = true; o.name = name; o.anim = mat4(1); o.emissive = vec3(0);
                float k = height / std::max(gObjs[tplIdx].ext().y, 1e-6f);
                vec3 bottom(gObjs[tplIdx].center().x, gObjs[tplIdx].wmin.y, gObjs[tplIdx].center().z);
                o.world = glm::translate(mat4(1), pos) * glm::rotate(mat4(1), yaw, vec3(0, 1, 0)) *
                          glm::scale(mat4(1), vec3(k)) * glm::translate(mat4(1), -bottom) * gObjs[tplIdx].world;
                computeBounds(o); gByName[name] = (int)gObjs.size(); gObjs.push_back(o);
            };
            const float kSide = 3.0f;                       // distancia lateral a la pista (en largos de auto)
            int nT = 0;
            for (size_t k = 0; k < tplTrees.size() * 7 && !tplTrees.empty(); ++k) {   // 7 por plantilla
                float sdist = gPathF1.len * ((float)k + 0.5f) / (float)(tplTrees.size() * 7);
                vec3 p = gPathF1.at(sdist), tg = gPathF1.tangent(sdist), nrm = glm::cross(vec3(0, 1, 0), tg);
                float side = (k % 2 == 0) ? 1.f : -1.f;
                vec3 pos = vec3(p.x, groundY, p.z) + nrm * side * refLen * (kSide + (float)(k % 3));
                int ti = tplTrees[k % tplTrees.size()];
                spawn(ti, pos, refLen * (ti == tplTrees[0] ? 2.2f : 1.6f), (float)k * 0.9f, "tree_inst_" + std::to_string(k)); ++nT;
            }
            if (!tplRadar.empty()) {
                vec3 p = gPathF1.at(0), tg = gPathF1.tangent(0), nrm = glm::cross(vec3(0, 1, 0), tg);
                spawn(tplRadar[0], vec3(p.x, groundY, p.z) + nrm * refLen * 4.0f, refLen * 1.4f, 0.f, "radar_inst");
            }
            std::cout << "Plantillas ocultas: " << tplTrees.size() << " arboles, " << tplRadar.size() << " radar -> instancias: " << nT << " arboles, " << (tplRadar.empty() ? 0 : 1) << " radar\n";
        }

        // ---------- clasificacion de objetos ----------
        vector<int> flags, trees, radars, bills, pitCars, startLights, postLights;
        for (size_t i = 0; i < gObjs.size(); ++i) {
            Obj& o = gObjs[i]; const string& b = o.base;
            if (!o.visible) continue;
            if (hasPrefix(b, "flagCheckers")) flags.push_back((int)i);
            else if (hasPrefix(b, "tree")) trees.push_back((int)i);
            else if (hasPrefix(b, "radarEquipment")) radars.push_back((int)i);
            else if (hasPrefix(b, "billboard")) bills.push_back((int)i);
            else if (hasPrefix(b, "raceCar")) { pitCars.push_back((int)i); o.mode = 1; o.metal = 0.45f; }
            if (b == "Body") { o.mode = 1; o.metal = 0.55f; }
            if (hasPrefix(b, "lightPost") || hasPrefix(b, "overheadLights")) postLights.push_back((int)i);
        }
        for (const char* n : {"lightRed", "lightRedDouble", "lightColored", "overheadLights", "overheadRoundColored"}) {
            auto it = gByName.find(n); startLights.push_back(it == gByName.end() ? -1 : it->second);
        }
        vector<vec3> pitDir(pitCars.size(), vec3(0, 0, -1)); vector<float> pitLen(pitCars.size(), 1.f);
        for (size_t k = 0; k < pitCars.size(); ++k) {            // frente del auto = eje largo, en el sentido de la pista
            const Obj& o = gObjs[pitCars[k]]; const GLMesh& m = gMeshes[o.meshes[0]]; vec3 e = m.bmax - m.bmin;
            float best = -1; vec3 dir(0, 0, -1);
            for (int i = 0; i < 3; ++i) {
                vec3 col(o.world[i]); float cl = glm::length(col); if (cl < 1e-8f) continue;
                float horiz = glm::length(vec3(col.x, 0, col.z)) / cl, ext = e[i] * cl * horiz;
                if (ext > best) { best = ext; dir = flat(col); pitLen[k] = ext; }
            }
            vec3 c = o.center(); size_t bi = 0; float bd = 1e30f;
            for (size_t j = 0; j < gPathF1.p.size(); ++j) { float d = glm::distance(vec3(c.x, 0, c.z), vec3(gPathF1.p[j].x, 0, gPathF1.p[j].z)); if (d < bd) { bd = d; bi = j; } }
            if (glm::dot(dir, gPathF1.tangent(gPathF1.cum[bi])) < 0) dir = -dir;
            pitDir[k] = kPitFlip ? -dir : dir;
        }
        std::cout << "Banderas:" << flags.size() << " Arboles:" << trees.size() << " Radar:" << radars.size()
                  << " Anuncios:" << bills.size() << " AutosPits:" << pitCars.size() << " Postes:" << postLights.size() << "\n";

        S.camPos = center + vec3(0, sceneSize * 0.25f, sceneSize * 0.45f);
        S.yaw = -90; S.pitch = -25;
        const float camSpeed = sceneSize * 0.12f;

        glEnable(GL_DEPTH_TEST); glEnable(GL_MULTISAMPLE); glEnable(GL_FRAMEBUFFER_SRGB);
        std::cout << "Controles: WASD+mouse (camara libre, SHIFT rapido) | C camara | TAB soltar mouse\n"
                     "  SPACE F1 | T Tsuru | F viento banderas | R radar | B anuncios | P autos pits | V rafaga | L semaforo\n"
                     "  N dia/noche | H faros y postes | F12 captura | ESC salir\n";

        if (gDemo) { gF1.on = gTsuru.on = true; S.flagStrongTarget = 1; S.radarTarget = 2.2f; S.billT0 = 0.2f; S.pitT0 = 0.2f; S.gustT0 = 0.2f; }
        double prev = glfwGetTime(); int frame = 0;
        while (!glfwWindowShouldClose(win)) {
            double now = glfwGetTime(); float dt = (float)std::min(now - prev, 0.1); prev = now;
            if (!S.screenshot.empty()) { dt = 1.f / 60.f; now = frame / 60.0; }   // reloj fijo para capturas
            float t = (float)now; glfwPollEvents();
            int W, H; glfwGetFramebufferSize(win, &W, &H); if (W == 0 || H == 0) continue;

            // ------------------ ENTRADA: camara libre ------------------
            vec3 front(cosf(glm::radians(S.yaw)) * cosf(glm::radians(S.pitch)), sinf(glm::radians(S.pitch)), sinf(glm::radians(S.yaw)) * cosf(glm::radians(S.pitch)));
            front = glm::normalize(front); vec3 right = glm::normalize(glm::cross(front, vec3(0, 1, 0)));
            float sp = camSpeed * dt * (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 4.f : 1.f);
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) S.camPos += front * sp;
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) S.camPos -= front * sp;
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) S.camPos += right * sp;
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) S.camPos -= right * sp;
            if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) S.camPos.y += sp;
            if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) S.camPos.y -= sp;

            // ------------------ LUCES (interaccion en tiempo real) ------------------
            S.dayLevel += (S.dayTarget - S.dayLevel) * (1.f - expf(-1.5f * dt));
            float day = S.dayLevel;
            vec3 L0 = glm::normalize(gSunDir0);
            vec3 axis = glm::normalize(glm::cross(L0, vec3(0, 1, 0)) + vec3(1e-4f, 0, 0));
            vec3 sunL = glm::normalize(vec3(glm::rotate(mat4(1), (1.f - day) * 2.2f, axis) * glm::vec4(L0, 0)));
            float sunI = powf(smooth01(day), 1.5f);
            vec3 sunCol = gSunCol0 * sunI * 1.1f;
            vec3 ambient = glm::mix(vec3(0.02f, 0.03f, 0.07f), vec3(0.32f, 0.34f, 0.38f), day);
            vec3 skyTint = glm::mix(vec3(0.05f, 0.07f, 0.16f), vec3(1.f), smooth01(day));

            // ------------------ ANIMACIONES POR CODIGO ------------------
            // C1: autos siguiendo trayectoria (jerarquia + rodamiento + direccion)
            // I8: secuencia de semaforo -> al terminar arranca el F1
            if (S.startSeqActive) {
                float ts = t - S.startT0;
                for (size_t i = 0; i < startLights.size(); ++i) if (startLights[i] >= 0)
                    gObjs[startLights[i]].emissive = (ts >= 1.f + (float)i && ts < 6.5f) ? vec3(1.6f, 0.12f, 0.06f) : vec3(0);
                if (ts >= 6.5f) { S.startSeqActive = false; if (gF1.valid()) gF1.on = true; }
            }
            if (gF1.valid()) gF1.update(dt, t);
            if (gTsuru.valid()) gTsuru.update(dt, t);

            // I7 rafaga: oscilacion amortiguada
            float tg = t - S.gustT0; float gust = tg >= 0 ? expf(-tg / 2.5f) * sinf(6.2831853f * 1.1f * tg) : 0.f;
            float gustEnv = tg >= 0 ? expf(-tg / 2.5f) : 0.f;

            // C2 + I3: banderas (ondeo en shader). I3 aumenta la intensidad suavemente
            S.flagStrong += (S.flagStrongTarget - S.flagStrong) * (1.f - expf(-2.f * dt));
            for (int i : flags) {
                gObjs[i].wave = 0.04f + 0.12f * S.flagStrong + 0.10f * gustEnv;
            }
            // C3 + I7: arboles mecidos (pivote en la base)
            for (size_t k = 0; k < trees.size(); ++k) {
                Obj& o = gObjs[trees[k]]; float ph = (float)k * 1.7f; vec3 c = o.center(); vec3 pivot(c.x, o.wmin.y, c.z);
                float ax = 0.04f * sinf(t * 1.3f + ph) + 0.22f * gust, az = 0.03f * sinf(t * 1.7f + ph * 0.6f) + 0.16f * gust;
                o.anim = aboutPivot(pivot, glm::rotate(mat4(1), ax, vec3(1, 0, 0)) * glm::rotate(mat4(1), az, vec3(0, 0, 1)));
            }
            // I4 radar
            S.radarOmega += (S.radarTarget - S.radarOmega) * (1.f - expf(-1.5f * dt)); S.radarAng += S.radarOmega * dt;
            for (int i : radars) gObjs[i].anim = aboutPivot(gObjs[i].center(), glm::rotate(mat4(1), S.radarAng, vec3(0, 1, 0)));
            // I5 anuncios: giro de 360 con easing y retardo escalonado
            for (size_t k = 0; k < bills.size(); ++k) {
                float tt = (t - S.billT0 - 0.15f * (float)k) / 2.0f; float ang = (tt > 0 && tt < 1) ? 6.2831853f * easeInOutCubic(tt) : 0.f;
                gObjs[bills[k]].anim = aboutPivot(gObjs[bills[k]].center(), glm::rotate(mat4(1), ang, vec3(0, 1, 0)));
            }
            // I6 autos de pits: salen manejando por el carril, hacen una pausa y regresan (escalonados)
            for (size_t k = 0; k < pitCars.size(); ++k) {
                Obj& o = gObjs[pitCars[k]]; float tt = t - S.pitT0 - 0.7f * (float)k; float off = 0, bob = 0, pitch = 0;
                if (tt > 0 && tt < 10.f) {
                    float u = tt < 4.5f ? tt / 4.5f : (tt < 5.5f ? 1.f : 1.f - (tt - 5.5f) / 4.5f);
                    off = kPitDistance * pitLen[k] * easeInOutCubic(u);
                    bool moving = (u > 0.01f && u < 0.99f);
                    if (moving) { bob = 0.012f * pitLen[k] * sinf(tt * 22.f); pitch = 0.025f * sinf(tt * 9.f); }
                }
                vec3 axis = glm::normalize(glm::cross(vec3(0, 1, 0), pitDir[k]));       // eje lateral para el cabeceo
                o.anim = glm::translate(mat4(1), pitDir[k] * off + vec3(0, bob, 0)) * aboutPivot(o.center(), glm::rotate(mat4(1), pitch, axis));
            }

            // ------------------ CAMARA ------------------
            mat4 view; vec3 camPos = S.camPos;
            auto carCam = [&](Car& c) -> bool {
                if (!c.valid() || !c.hasCam) return false;
                vec3 p = vec3(c.M * glm::vec4(c.camP, 1)), f = glm::normalize(vec3(c.M * glm::vec4(c.camF, 0))), u = vec3(0, 1, 0);
                camPos = p; view = glm::lookAt(p, p + f, u); return true;
            };
            bool done = false;
            if (S.camMode == 1) done = carCam(gF1);
            else if (S.camMode == 2) done = carCam(gTsuru);
            else if (S.camMode == 3 && gF1.valid()) {                              // persecucion en 3a persona
                vec3 f = glm::normalize(vec3(gF1.M * glm::vec4(gF1.fwd0, 0))); vec3 c = vec3(gF1.M * glm::vec4(gF1.b0, 1));
                camPos = c - f * gF1.bodyLen * 3.0f + vec3(0, gF1.bodyLen * 0.9f, 0); view = glm::lookAt(camPos, c + vec3(0, gF1.bodyLen * 0.2f, 0), vec3(0, 1, 0)); done = true;
            }
            if (!done) view = glm::lookAt(S.camPos, S.camPos + front, vec3(0, 1, 0));
            mat4 proj = glm::perspective(glm::radians(60.f), (float)W / H, sceneSize * 0.0008f, sceneSize * 8.f);

            // ------------------ RENDER ------------------
            glViewport(0, 0, W, H);
            glClearColor(0.05f, 0.06f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            scene.use();
            scene.set("uView", view); scene.set("uProj", proj); scene.set("uCamPos", camPos);
            scene.set("uSunDir", sunL); scene.set("uSunColor", sunCol); scene.set("uAmbient", ambient); scene.set("uSkyTint", skyTint);
            // luces puntuales (postes) + reflectores (faros)
            float nightK = 1.f - day; vec3 warm(1.0f, 0.82f, 0.55f);
            int np = 0;
            if (S.headlights) for (int i : postLights) { if (np >= 8) break; vec3 c = gObjs[i].center();
                scene.setIdx("uPPos", np, vec3(c.x, gObjs[i].wmax.y, c.z)); scene.setIdx("uPCol", np, warm * (0.25f + 1.6f * nightK)); ++np; }
            scene.set("uNP", np); scene.set("uPRange", sceneSize * 0.03f);
            int ns = 0;
            if (S.headlights) for (Car* c : {&gF1, &gTsuru}) if (c->valid() && ns < 2) {
                vec3 pos = vec3(c->M * glm::vec4(c->b0 + c->fwd0 * (c->bodyLen * 0.5f) + vec3(0, c->bodyLen * 0.12f, 0), 1));
                vec3 dir = glm::normalize(vec3(c->M * glm::vec4(c->fwd0, 0)) - vec3(0, 0.08f, 0));
                scene.setIdx("uSPos", ns, pos); scene.setIdx("uSDir", ns, dir); scene.setIdx("uSCol", ns, vec3(1.0f, 0.95f, 0.8f) * (0.5f + 2.5f * nightK)); ++ns;
            }
            scene.set("uNS", ns);
            scene.set("uSky", 1); glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
            scene.set("uTex", 0);
            drawObjects(scene, false, t);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
            drawObjects(scene, true, t);
            glDepthMask(GL_TRUE); glDisable(GL_BLEND);

            glDepthFunc(GL_LEQUAL); sky.use(); sky.set("uView", view); sky.set("uProj", proj); sky.set("uSkyTint", skyTint);
            sky.set("uSky", 1); glBindVertexArray(gCubeVAO); glDrawArrays(GL_TRIANGLES, 0, 36); glDepthFunc(GL_LESS);

            // titulo con estado
            if (frame % 15 == 0) {
                char buf[256]; snprintf(buf, 256, "Foro Sol | F1:%s Tsuru:%s | Viento:%s | Radar:%s | Dia:%d%% | Faros:%s | Cam:%d",
                    gF1.on ? "ON" : "off", gTsuru.on ? "ON" : "off", S.flagStrongTarget > 0.5f ? "fuerte" : "brisa", S.radarTarget > 0 ? "ON" : "off",
                    (int)(day * 100), S.headlights ? "ON" : "off", S.camMode);
                glfwSetWindowTitle(win, buf);
            }
            if (S.wantShot || (!S.screenshot.empty() && frame == S.shotFrames)) {
                vector<unsigned char> px(W * H * 3); glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                stbi_flip_vertically_on_write(1);
                string out = S.screenshot.empty() ? "captura.png" : S.screenshot;
                stbi_write_png(out.c_str(), W, H, 3, px.data(), W * 3); std::cout << "Captura: " << out << "\n"; S.wantShot = false;
                if (!S.screenshot.empty()) break;
            }
            glfwSwapBuffers(win); ++frame;
        }
    } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << "\n"; glfwTerminate(); return 1; }
    glfwTerminate();
    return 0;
}
