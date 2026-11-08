#include <iostream>
#include <cmath>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <algorithm>

// GLEW
#include <GL/glew.h>
// GLFW
#include <GLFW/glfw3.h>
// Other Libs
#include "stb_image.h"
// GLM Mathematics
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
//Load Models
#include "SOIL2/SOIL2.h"
// Other includes
#include "Shader.h"
#include "Camera.h"
#include "Model.h"
#include "Ruta.h"          // ruta cerrada de los coches (x,z en OpenGL)

const std::string CARPETA_MODELOS = "Models/calle/";
const float DIST_DIBUJO = 350.0f;                       
const glm::vec3 CARRO_ADELANTE_LOCAL(0.0f, 0.0f, 1.0f); 
const glm::vec3 EJE_RUEDA(1.0f, 0.0f, 0.0f);           
const float RADIO_RUEDA = 0.35f;                        
const float DIST_ENTRE_EJES = 2.4f;                    
const float V_MAX = 11.0f;                        
const float V_MIN = 3.5f;                         
const float A_LATERAL = 2.8f;                       
const float SEPARACION_COCHE2 = 28.0f;              
const int   INDICE_INICIO = 4;                      
const float DIST_TREN = 80.0f;                          

void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mode);
void MouseCallback(GLFWwindow *window, double xPos, double yPos);
void DoMovement();

const GLuint WIDTH = 1280, HEIGHT = 720;
int SCREEN_WIDTH, SCREEN_HEIGHT;

Camera  camera(glm::vec3(0.0f, 3.0f, 10.0f));
GLfloat lastX = WIDTH / 2.0;
GLfloat lastY = HEIGHT / 2.0;
bool keys[1024];
bool firstMouse = true;

GLfloat deltaTime = 0.0f;
GLfloat lastFrame = 0.0f;

enum Categoria { ESTATICO, CARRO, RUEDA_CARRO, HIEDRA, DRAGON, TREN, PUERTA_TREN };

struct Objeto {
	std::string nombre, padre, raiz;
	Model* modelo = nullptr;
	bool transp = false;
	glm::vec3 bmin, bmax;      
	glm::mat4 M;                
	glm::vec3 centro;          
	float radio = 1.0f;
	Categoria cat = ESTATICO;
	int tren = -1;              
	int carro = -1;             
	int estilo = 0;             
	bool frontal = false;       
	float fase = 0.0f;
	bool siempreVisible = false;
};
struct Luz { std::string nombre, tipo; glm::vec3 color; float energia; glm::vec3 pos, dir; };
struct CamBlender { std::string nombre; glm::vec3 pos, fwd; float fov; };
struct Tren { std::string raiz; glm::vec3 eje; float fase; };

std::vector<Objeto> objetos;
std::vector<Luz> luces;
std::vector<CamBlender> camaras;
std::vector<Tren> trenes;
std::map<std::string, Model*> cacheModelos;

// ----------------------------------------------------------------------------
//  Carga del layout exportado desde Blender
// ----------------------------------------------------------------------------
bool CargarEscena(const std::string &ruta)
{
	std::ifstream f(ruta);
	if (!f.is_open()) { std::cout << "No se pudo abrir " << ruta << std::endl; return false; }
	std::string linea;
	while (std::getline(f, linea))
	{
		std::istringstream ss(linea);
		std::string tag; ss >> tag;
		if (tag == "O")
		{
			Objeto o; std::string archivo; int tr;
			ss >> o.nombre >> o.padre >> o.raiz >> archivo >> tr
			   >> o.bmin.x >> o.bmin.y >> o.bmin.z >> o.bmax.x >> o.bmax.y >> o.bmax.z;
			float m[16]; for (int i = 0; i < 16; i++) ss >> m[i];
			o.M = glm::make_mat4(m);
			o.transp = (tr != 0);
			if (!cacheModelos.count(archivo))
			{
				std::string p = CARPETA_MODELOS + archivo + ".obj";
				cacheModelos[archivo] = new Model((GLchar*)p.c_str());
			}
			o.modelo = cacheModelos[archivo];
		
			glm::vec3 mn(1e30f), mx(-1e30f);
			for (int i = 0; i < 8; i++)
			{
				glm::vec3 c((i & 1) ? o.bmax.x : o.bmin.x, (i & 2) ? o.bmax.y : o.bmin.y, (i & 4) ? o.bmax.z : o.bmin.z);
				glm::vec3 w = glm::vec3(o.M * glm::vec4(c, 1.0f));
				mn = glm::min(mn, w); mx = glm::max(mx, w);
			}
			o.centro = (mn + mx) * 0.5f;
			o.radio = glm::length(mx - mn) * 0.5f;
			objetos.push_back(o);
		}
		else if (tag == "L")
		{
			Luz l; ss >> l.nombre >> l.tipo >> l.color.x >> l.color.y >> l.color.z >> l.energia
			          >> l.pos.x >> l.pos.y >> l.pos.z >> l.dir.x >> l.dir.y >> l.dir.z;
			luces.push_back(l);
		}
		else if (tag == "C")
		{
			CamBlender c; ss >> c.nombre >> c.pos.x >> c.pos.y >> c.pos.z >> c.fwd.x >> c.fwd.y >> c.fwd.z >> c.fov;
			camaras.push_back(c);
		}
	}
	std::cout << objetos.size() << " objetos, " << cacheModelos.size() << " modelos, "
	          << luces.size() << " luces, " << camaras.size() << " camaras" << std::endl;
	return true;
}

bool EmpiezaCon(const std::string &s, const char *pre) { return s.rfind(pre, 0) == 0; }


void ClasificarObjetos()
{
	std::map<std::string, int> indiceTren;
	for (auto &o : objetos)
	{
		
		if (o.nombre == "Penguin_Cylinder") o.estilo = 1;
		if (EmpiezaCon(o.nombre, "BlackDragon") || EmpiezaCon(o.nombre, "GreenDragon")) o.estilo = 2;

		if (o.raiz == "Car_Rig.001" || o.raiz == "Car_Rig.002")
		{
			o.carro = (o.raiz == "Car_Rig.001") ? 0 : 1;
			if (EmpiezaCon(o.nombre, "Wheel.Ft") || EmpiezaCon(o.nombre, "Wheel.Bk"))
			{ o.cat = RUEDA_CARRO; o.frontal = EmpiezaCon(o.nombre, "Wheel.Ft"); }
			else o.cat = CARRO;
			o.siempreVisible = true;
		}
		else if (EmpiezaCon(o.raiz, "metro_mover"))
		{
			if (!indiceTren.count(o.raiz))
			{
				indiceTren[o.raiz] = (int)trenes.size();
				Tren t; t.raiz = o.raiz; t.eje = glm::vec3(1, 0, 0); t.fase = 7.0f * trenes.size();
				trenes.push_back(t);
			}
			o.tren = indiceTren[o.raiz];
			o.cat = EmpiezaCon(o.nombre, "door") ? PUERTA_TREN : TREN;
			o.siempreVisible = true;
		}
		else if (EmpiezaCon(o.nombre, "BagaPie_Ivy_1")) { o.cat = HIEDRA; o.fase = (float)(std::hash<std::string>()(o.nombre) % 628) / 100.0f; }
		else if (EmpiezaCon(o.nombre, "BlackDragon") || EmpiezaCon(o.nombre, "GreenDragon"))
		{ o.cat = DRAGON; o.fase = EmpiezaCon(o.nombre, "Green") ? 1.5f : 0.0f; }
	}

	for (size_t t = 0; t < trenes.size(); t++)
	{
		glm::vec3 mn(1e30f), mx(-1e30f);
		for (auto &o : objetos) if (o.tren == (int)t) { mn = glm::min(mn, o.centro); mx = glm::max(mx, o.centro); }
		trenes[t].eje = ((mx.x - mn.x) >= (mx.z - mn.z)) ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
	}
}

// ----------------------------------------------------------------------------
//  Animacion 5: METRO  (maquina de estados con suavizado)
// ----------------------------------------------------------------------------
float easeOut(float u) { u = glm::clamp(u, 0.0f, 1.0f); return 1.0f - (1.0f - u)*(1.0f - u)*(1.0f - u); }
float easeIn(float u)  { u = glm::clamp(u, 0.0f, 1.0f); return u*u*u; }
float smooth(float u)  { u = glm::clamp(u, 0.0f, 1.0f); return u*u*(3.0f - 2.0f*u); }

enum EstadoTren { LLEGANDO, ABRIENDO, ESPERA, CERRANDO, SALIENDO, FUERA };
void EstadoMetro(float t, float &desp, float &puerta, EstadoTren &est)
{
	const float T_LLEGA = 9, T_ABRE = 2.5f, T_ESPERA = 5, T_CIERRA = 2.5f, T_SALE = 9, T_FUERA = 3;
	const float ciclo = T_LLEGA + T_ABRE + T_ESPERA + T_CIERRA + T_SALE + T_FUERA;
	t = fmod(t, ciclo);
	puerta = 0.0f; desp = 0.0f;
	if (t < T_LLEGA)                         { est = LLEGANDO; desp = -DIST_TREN * (1.0f - easeOut(t / T_LLEGA)); return; }
	t -= T_LLEGA;
	if (t < T_ABRE)                          { est = ABRIENDO; puerta = smooth(t / T_ABRE); return; }
	t -= T_ABRE;
	if (t < T_ESPERA)                        { est = ESPERA; puerta = 1.0f; return; }
	t -= T_ESPERA;
	if (t < T_CIERRA)                        { est = CERRANDO; puerta = 1.0f - smooth(t / T_CIERRA); return; }
	t -= T_CIERRA;
	if (t < T_SALE)                          { est = SALIENDO; desp = DIST_TREN * easeIn(t / T_SALE); return; }
	est = FUERA; desp = DIST_TREN * 2.0f;
}

// ----------------------------------------------------------------------------
//  Animacion 4: FERRARIS sobre una ruta cerrada (ida - glorieta - regreso)
// ----------------------------------------------------------------------------
struct RutaCoches
{
	std::vector<glm::vec2> p;          
	std::vector<float> cum, phi, kap, vel;
	float L = 0.0f;

	void construir()
	{
		int N = RUTA_N;
		p.resize(N); cum.assign(N + 1, 0.0f); phi.resize(N); kap.resize(N); vel.resize(N);
		for (int i = 0; i < N; i++) p[i] = glm::vec2(RUTA_XZ[i][0], RUTA_XZ[i][1]);
		for (int i = 0; i < N; i++) cum[i + 1] = cum[i] + glm::length(p[(i + 1) % N] - p[i]);
		L = cum[N];
		auto at = [&](int i) { return p[((i % N) + N) % N]; };
		for (int i = 0; i < N; i++)                       
		{
			glm::vec2 d = at(i + 2) - at(i - 2);
			phi[i] = atan2f(d.x, d.y);
		}
		for (int i = 0; i < N; i++)                      
		{
			float d = phi[(i + 2) % N] - phi[((i - 2) % N + N) % N];
			while (d >  3.14159265f) d -= 2.0f * 3.14159265f;
			while (d < -3.14159265f) d += 2.0f * 3.14159265f;
			kap[i] = d / 4.0f;
		}
		std::vector<float> k2(N);
		for (int i = 0; i < N; i++) { float a = 0; for (int j = -4; j <= 4; j++) a += kap[((i + j) % N + N) % N]; k2[i] = a / 9.0f; }
		kap = k2;
		std::vector<float> v(N);                          
		for (int i = 0; i < N; i++)
			v[i] = glm::clamp(sqrtf(A_LATERAL / (fabsf(kap[i]) + 1e-3f)), V_MIN, V_MAX);
		std::vector<float> v2(N);                         
		for (int i = 0; i < N; i++) { float m = v[i]; for (int j = 0; j < 14; j++) m = std::min(m, v[(i + j) % N]); v2[i] = m; }
		for (int i = 0; i < N; i++) { float a = 0; for (int j = -5; j <= 5; j++) a += v2[((i + j) % N + N) % N]; vel[i] = a / 11.0f; }
	}
	
	void ubicar(float s, int &i, float &u) const
	{
		s = fmodf(s, L); if (s < 0) s += L;
		int lo = 0, hi = (int)p.size();
		while (hi - lo > 1) { int mid = (lo + hi) / 2; if (cum[mid] <= s) lo = mid; else hi = mid; }
		i = lo; u = (s - cum[lo]) / std::max(1e-6f, cum[lo + 1] - cum[lo]);
	}
	glm::vec2 posicion(float s) const { int i; float u; ubicar(s, i, u); return glm::mix(p[i], p[(i + 1) % p.size()], u); }
	float rumbo(float s) const
	{
		int i; float u; ubicar(s, i, u); int j = (i + 1) % p.size();
		glm::vec2 a(sinf(phi[i]), cosf(phi[i])), b(sinf(phi[j]), cosf(phi[j]));
		glm::vec2 t = glm::mix(a, b, u); return atan2f(t.x, t.y);
	}
	float curvatura(float s) const { int i; float u; ubicar(s, i, u); return glm::mix(kap[i], kap[(i + 1) % p.size()], u); }
	float velocidad(float s) const { int i; float u; ubicar(s, i, u); return glm::mix(vel[i], vel[(i + 1) % p.size()], u); }
};
RutaCoches ruta;

struct Coche
{
	bool valido = false;
	glm::vec3 p0;              
	float phi0 = 0.0f;          
	float s = 0.0f;             
	float giroRuedas = 0.0f;    
	float volante = 0.0f;       
	glm::mat4 T = glm::mat4(1.0f);   
};
std::vector<Coche> coches;
float tAnim = 0.0f;            

void PrepararCoches()
{
	ruta.construir();
	const char* cuerpos[2] = { "Body.001", "Body.002" };
	coches.assign(2, Coche());
	for (int c = 0; c < 2; c++)
	{
		glm::mat4 Mb(1.0f); bool hayCuerpo = false;
		glm::vec3 suma(0.0f); int nr = 0;
		for (auto &o : objetos)
		{
			if (o.nombre == cuerpos[c]) { Mb = o.M; hayCuerpo = true; }
			if (o.carro == c && o.cat == RUEDA_CARRO) { suma += glm::vec3(o.M[3]); nr++; }
		}
		if (!hayCuerpo) continue;
		Coche &k = coches[c];
		k.p0 = (nr > 0) ? suma / (float)nr : glm::vec3(Mb[3]);  
		glm::vec3 f = glm::mat3(Mb) * CARRO_ADELANTE_LOCAL; f.y = 0.0f; f = glm::normalize(f);
		k.phi0 = atan2f(f.x, f.z);
		int i0 = std::min(INDICE_INICIO, (int)ruta.p.size() - 1);
		k.s = ruta.cum[i0] + (c == 0 ? 0.0f : SEPARACION_COCHE2);
		k.valido = true;
	}
	for (auto &o : objetos)
	{
		if (o.cat != ESTATICO || o.radio > 3.0f) continue;
		for (int c = 0; c < 2; c++)
			if (coches[c].valido && glm::distance(o.centro, coches[c].p0) < 3.5f)
			{ o.cat = CARRO; o.carro = c; o.siempreVisible = true; std::cout << "Pieza pegada al coche " << c + 1 << ": " << o.nombre << std::endl; break; }
	}
}

void ActualizarCoches(float dt)
{
	tAnim += dt;
	float arranque = smooth(tAnim / 5.0f);                
	for (auto &k : coches)
	{
		if (!k.valido) continue;
		float v = ruta.velocidad(k.s) * arranque;           
		k.s = fmodf(k.s + v * dt, ruta.L);
		k.giroRuedas += (v * dt) / RADIO_RUEDA;
		float objetivo = glm::clamp(atanf(DIST_ENTRE_EJES * ruta.curvatura(k.s)), -0.75f, 0.75f);
		k.volante += (objetivo - k.volante) * std::min(1.0f, dt * 6.0f);   
		glm::vec2 q = ruta.posicion(k.s);
		k.T = glm::translate(glm::mat4(1.0f), glm::vec3(q.x, k.p0.y, q.y))
		    * glm::rotate(glm::mat4(1.0f), ruta.rumbo(k.s) - k.phi0, glm::vec3(0, 1, 0))
		    * glm::translate(glm::mat4(1.0f), -k.p0);
	}
}

// ----------------------------------------------------------------------------
//  Dia / noche (animacion 1)
// ----------------------------------------------------------------------------
float anguloSol = 1.0f;
bool cicloActivo = true;
bool linterna = false;


int main()
{
	glfwInit();
	GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Proyecto Final - Calle", nullptr, nullptr);
	if (nullptr == window)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return EXIT_FAILURE;
	}
	glfwMakeContextCurrent(window);
	glfwGetFramebufferSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);
	glfwSetKeyCallback(window, KeyCallback);
	glfwSetCursorPosCallback(window, MouseCallback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	glewExperimental = GL_TRUE;
	if (GLEW_OK != glewInit())
	{
		std::cout << "Failed to initialize GLEW" << std::endl;
		return EXIT_FAILURE;
	}
	glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

	Shader lightingShader("Shader/lighting.vs", "Shader/lighting.frag");

	if (!CargarEscena(CARPETA_MODELOS + "layout.txt")) return EXIT_FAILURE;
	ClasificarObjetos();

	PrepararCoches();     

	for (auto &c : camaras) if (c.nombre == "Camera")
	{
		float yaw = glm::degrees(atan2f(c.fwd.z, c.fwd.x));
		float pitch = glm::degrees(asinf(glm::clamp(c.fwd.y, -1.0f, 1.0f)));
		camera = Camera(c.pos, glm::vec3(0, 1, 0), yaw, pitch);
		break;
	}

	lightingShader.Use();
	glUniform1i(glGetUniformLocation(lightingShader.Program, "material.diffuse"), 0);
	glUniform1i(glGetUniformLocation(lightingShader.Program, "material.specular"), 1);

	glm::mat4 projection = glm::perspective(camera.GetZoom(), (GLfloat)SCREEN_WIDTH / (GLfloat)SCREEN_HEIGHT, 0.1f, 2000.0f);

	glEnable(GL_DEPTH_TEST);

	lastFrame = glfwGetTime(); 
	while (!glfwWindowShouldClose(window))
	{
		GLfloat currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;
		float t = currentFrame;

		glfwPollEvents();
		DoMovement();

		
		if (cicloActivo) anguloSol += deltaTime * 0.12f;
		ActualizarCoches(std::min(deltaTime, 0.05f));   

		
		float altura = sinf(anguloSol);                      
		float luzDia = glm::clamp(altura * 2.0f + 0.2f, 0.0f, 1.0f);
		glm::vec3 cielo = glm::mix(glm::vec3(0.02f, 0.03f, 0.08f), glm::vec3(0.55f, 0.75f, 0.95f), luzDia);
		glClearColor(cielo.x, cielo.y, cielo.z, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		lightingShader.Use();
		GLint P = lightingShader.Program;
		glUniform3f(glGetUniformLocation(P, "viewPos"), camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);

		glm::vec3 dirSol = glm::normalize(glm::vec3(cosf(anguloSol) * 0.5f, -altura, 0.3f));
		glm::vec3 amb = glm::mix(glm::vec3(0.06f, 0.07f, 0.12f), glm::vec3(0.45f), luzDia);
		glm::vec3 dif = glm::mix(glm::vec3(0.03f, 0.03f, 0.06f), glm::vec3(0.85f, 0.82f, 0.75f), luzDia);
		glUniform3f(glGetUniformLocation(P, "dirLight.direction"), dirSol.x, dirSol.y, dirSol.z);
		glUniform3f(glGetUniformLocation(P, "dirLight.ambient"), amb.x, amb.y, amb.z);
		glUniform3f(glGetUniformLocation(P, "dirLight.diffuse"), dif.x, dif.y, dif.z);
		glUniform3f(glGetUniformLocation(P, "dirLight.specular"), 0.3f * luzDia, 0.3f * luzDia, 0.3f * luzDia);

		int np = 0;
		for (auto &l : luces)
		{
			if (l.tipo == "SUN" || np >= 4) continue;
			std::string b = "pointLights[" + std::to_string(np) + "].";
			float k = glm::clamp(l.energia / 300.0f, 0.2f, 1.5f) * (1.0f - 0.7f * luzDia);
			glm::vec3 c = l.color * k;
			glUniform3f(glGetUniformLocation(P, (b + "position").c_str()), l.pos.x, l.pos.y, l.pos.z);
			glUniform3f(glGetUniformLocation(P, (b + "ambient").c_str()), c.x * 0.3f, c.y * 0.3f, c.z * 0.3f);
			glUniform3f(glGetUniformLocation(P, (b + "diffuse").c_str()), c.x, c.y, c.z);
			glUniform3f(glGetUniformLocation(P, (b + "specular").c_str()), c.x, c.y, c.z);
			glUniform1f(glGetUniformLocation(P, (b + "constant").c_str()), 1.0f);
			glUniform1f(glGetUniformLocation(P, (b + "linear").c_str()), 0.045f);
			glUniform1f(glGetUniformLocation(P, (b + "quadratic").c_str()), 0.0075f);
			np++;
		}
		for (; np < 4; np++)   
		{
			std::string b = "pointLights[" + std::to_string(np) + "].";
			glUniform3f(glGetUniformLocation(P, (b + "position").c_str()), 0, 0, 0);
			glUniform3f(glGetUniformLocation(P, (b + "ambient").c_str()), 0, 0, 0);
			glUniform3f(glGetUniformLocation(P, (b + "diffuse").c_str()), 0, 0, 0);
			glUniform3f(glGetUniformLocation(P, (b + "specular").c_str()), 0, 0, 0);
			glUniform1f(glGetUniformLocation(P, (b + "constant").c_str()), 1.0f);
			glUniform1f(glGetUniformLocation(P, (b + "linear").c_str()), 1.0f);
			glUniform1f(glGetUniformLocation(P, (b + "quadratic").c_str()), 1.0f);
		}

		float s = linterna ? 1.0f : 0.0f;
		glUniform3f(glGetUniformLocation(P, "spotLight.position"), camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
		glUniform3f(glGetUniformLocation(P, "spotLight.direction"), camera.GetFront().x, camera.GetFront().y, camera.GetFront().z);
		glUniform3f(glGetUniformLocation(P, "spotLight.ambient"), 0.0f, 0.0f, 0.0f);
		glUniform3f(glGetUniformLocation(P, "spotLight.diffuse"), 0.9f * s, 0.9f * s, 0.8f * s);
		glUniform3f(glGetUniformLocation(P, "spotLight.specular"), 0.0f, 0.0f, 0.0f);
		glUniform1f(glGetUniformLocation(P, "spotLight.constant"), 1.0f);
		glUniform1f(glGetUniformLocation(P, "spotLight.linear"), 0.09f);
		glUniform1f(glGetUniformLocation(P, "spotLight.quadratic"), 0.032f);
		glUniform1f(glGetUniformLocation(P, "spotLight.cutOff"), glm::cos(glm::radians(12.0f)));
		glUniform1f(glGetUniformLocation(P, "spotLight.outerCutOff"), glm::cos(glm::radians(18.0f)));

		glUniform1f(glGetUniformLocation(P, "material.shininess"), 16.0f);

		glm::mat4 view = camera.GetViewMatrix();
		GLint modelLoc = glGetUniformLocation(P, "model");
		GLint estiloLoc = glGetUniformLocation(P, "estilo");
		glUniformMatrix4fv(glGetUniformLocation(P, "view"), 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(glGetUniformLocation(P, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

		std::vector<float> desTren(trenes.size()), abrePuerta(trenes.size());
		std::vector<EstadoTren> estTren(trenes.size());
		for (size_t i = 0; i < trenes.size(); i++)
			EstadoMetro(t + trenes[i].fase, desTren[i], abrePuerta[i], estTren[i]);

		glm::vec3 camPos = camera.GetPosition();
		for (int pasada = 0; pasada < 2; pasada++)
		{
			if (pasada == 1) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
			glUniform1i(glGetUniformLocation(P, "transparency"), pasada);

			for (auto &o : objetos)
			{
				if (o.transp != (pasada == 1)) continue;
				if (!o.siempreVisible && glm::distance(camPos, o.centro) - o.radio > DIST_DIBUJO) continue;

				glm::mat4 model = o.M;
				switch (o.cat)
				{
				case CARRO:
					if (o.carro >= 0 && coches[o.carro].valido) model = coches[o.carro].T * o.M;
					break;
				case RUEDA_CARRO:
					if (o.carro >= 0 && coches[o.carro].valido)
					{
						model = coches[o.carro].T * o.M;
						if (o.frontal) model = glm::rotate(model, coches[o.carro].volante, glm::vec3(0, 1, 0));   
						model = glm::rotate(model, coches[o.carro].giroRuedas, EJE_RUEDA);                         
					}
					break;
				case TREN: case PUERTA_TREN:
				{
					int i = o.tren;
					if (estTren[i] == FUERA) continue;
					glm::mat4 Tt = glm::translate(glm::mat4(1.0f), trenes[i].eje * desTren[i]);
					model = Tt * o.M;
					if (o.cat == PUERTA_TREN)
					{
						glm::vec3 ext = o.bmax - o.bmin;
						glm::vec3 eje = (ext.x >= ext.z) ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
						float desliza = std::max(ext.x, ext.z) * 0.9f * abrePuerta[i];
						model = glm::translate(model, eje * desliza);
					}
					break;
				}
				case HIEDRA:
				{
					float a = glm::radians(2.5f) * sinf(t * 1.3f + o.fase);
					float b = glm::radians(2.0f) * sinf(t * 0.9f + o.fase * 1.7f);
					model = glm::rotate(model, a, glm::vec3(1, 0, 0));
					model = glm::rotate(model, b, glm::vec3(0, 0, 1));
					break;
				}
				case DRAGON:
					model = glm::rotate(model, glm::radians(20.0f) * sinf(t * 0.6f + o.fase), glm::vec3(0, 1, 0));
					break;
				default: break;
				}
				glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
				glUniform1i(estiloLoc, o.estilo);
				o.modelo->Draw(lightingShader);
			}
			if (pasada == 1) glDisable(GL_BLEND);
		}

		glBindVertexArray(0);
		glfwSwapBuffers(window);
	}

	glfwTerminate();
	return 0;
}

void DoMovement()
{
	float dt = deltaTime * ((keys[GLFW_KEY_LEFT_SHIFT]) ? 4.0f : 1.0f);
	if (keys[GLFW_KEY_W] || keys[GLFW_KEY_UP])    camera.ProcessKeyboard(FORWARD, dt);
	if (keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN])  camera.ProcessKeyboard(BACKWARD, dt);
	if (keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT])  camera.ProcessKeyboard(LEFT, dt);
	if (keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]) camera.ProcessKeyboard(RIGHT, dt);
}

void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mode)
{
	if (GLFW_KEY_ESCAPE == key && GLFW_PRESS == action)
		glfwSetWindowShouldClose(window, GL_TRUE);

	if (key >= 0 && key < 1024)
	{
		if (action == GLFW_PRESS)        keys[key] = true;
		else if (action == GLFW_RELEASE) keys[key] = false;
	}

	if (action == GLFW_PRESS)    
	{
		if (key == GLFW_KEY_N) cicloActivo = !cicloActivo;
		if (key == GLFW_KEY_F) linterna = !linterna;
		if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
		{
			size_t i = key - GLFW_KEY_1;
			if (i < camaras.size())
			{
				float yaw = glm::degrees(atan2f(camaras[i].fwd.z, camaras[i].fwd.x));
				float pitch = glm::degrees(asinf(glm::clamp(camaras[i].fwd.y, -1.0f, 1.0f)));
				camera = Camera(camaras[i].pos, glm::vec3(0, 1, 0), yaw, pitch);
			}
		}
	}
}

void MouseCallback(GLFWwindow *window, double xPos, double yPos)
{
	if (firstMouse)
	{
		lastX = xPos;
		lastY = yPos;
		firstMouse = false;
	}
	GLfloat xOffset = xPos - lastX;
	GLfloat yOffset = lastY - yPos;  
	lastX = xPos;
	lastY = yPos;
	camera.ProcessMouseMovement(xOffset, yOffset);
}
