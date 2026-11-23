/* ===== MACROS ===== */
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

/* ===== CABECERAS ===== */
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include "tiny_gltf.h"
#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_glfw.h"
#include "Imgui/imgui_impl_opengl3.h"

/* ===== SHADER DE VERTICES ===== */
const char* vsSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;
out vec2 TexCoord;
out vec3 FragPos;
out vec3 Normal;
uniform mat4 model, view, projection;
void main() {
    vec4 wp = model * vec4(aPos, 1.0);
    FragPos = vec3(wp);
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = projection * view * wp;
}
)";

/* ===== SHADER DE FRAGMENTOS (PHONG + SOL + NIEBLA) ===== */
const char* fsSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;
uniform sampler2D tex;
uniform bool useTexture, unlit;
uniform int alphaMode;
uniform float alphaCutoff;
uniform vec4 baseColor;
uniform vec3 lightPos, lightColor, viewPos, sunDir, fogColor;
uniform float ambientK, diffuseK, specularK, shininess, sunK, fogDensity;
void main() {
    vec4 c = baseColor;
    if (useTexture) c *= texture(tex, TexCoord);
    if (alphaMode == 1 && c.a < alphaCutoff) discard;
    if (alphaMode == 0) c.a = 1.0;
    if (unlit) { FragColor = c; return; }

    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    if (dot(N, V) < 0.0) N = -N;

    vec3 L = lightPos - FragPos;
    float d = length(L);
    L /= d;
    float att = 1.0 / (1.0 + 0.045 * d + 0.0075 * d * d);

    vec3 ambient = ambientK * vec3(0.7, 0.9, 1.0) * c.rgb;
    vec3 sun = sunK * max(dot(N, -sunDir), 0.0) * vec3(0.6, 0.85, 1.0) * c.rgb;
    vec3 diffuse = diffuseK * max(dot(N, L), 0.0) * lightColor * c.rgb;
    vec3 R = reflect(-L, N);
    vec3 specular = specularK * pow(max(dot(V, R), 0.0), shininess) * lightColor;

    vec3 col = ambient + sun + (diffuse + specular) * att;
    float f = exp(-fogDensity * length(viewPos - FragPos));
    col = mix(fogColor, col, f);
    FragColor = vec4(col, c.a);
}
)";

/* ===== CURVA DE BEZIER ONDULADA DE A A B ===== */
const float ENV_SCALE = 1.8f;
const int SEG = 4;
const glm::vec3 PA[SEG + 1] = {
    glm::vec3(-8.15f, -0.23f, 4.46f), glm::vec3(-4.55f, 1.57f, 5.18f), glm::vec3(-0.05f, -0.59f, 4.28f),
    glm::vec3(4.09f, 1.57f, 5.18f), glm::vec3(8.23f, -0.23f, 4.46f) };
const glm::vec3 PT[SEG + 1] = {
    glm::vec3(3.0f, 2.5f, 0.5f), glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(3.0f, -2.5f, 0.0f),
    glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(3.0f, 2.5f, -0.5f) };

void pathPoint(float s, glm::vec3& pos, glm::vec3& tangent) {
    float u = std::min(std::max(s, 0.0f), 1.0f) * SEG;
    int i = std::min((int)u, SEG - 1);
    float t = u - i, k = 1.0f - t;
    glm::vec3 a = PA[i], d = PA[i + 1];
    glm::vec3 b = a + PT[i] * 0.5f, c = d - PT[i + 1] * 0.5f;
    pos = k * k * k * a + 3.0f * k * k * t * b + 3.0f * k * t * t * c + t * t * t * d;
    tangent = 3.0f * k * k * (b - a) + 6.0f * k * t * (c - b) + 3.0f * t * t * (d - c);
}

/* ===== ESTRUCTURAS DEL MODELO ===== */
struct MatInfo {
    int tex = -1;
    float color[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
    int mode = 0;
    float cutoff = 0.5f;
};

struct MeshGL {
    unsigned vao;
    int count;
    bool indexed;
    glm::mat4 world;
    MatInfo mat;
};

struct Model3D {
    tinygltf::Model gltf;
    std::vector<MeshGL> meshes;
    std::vector<unsigned> textures; 
    glm::vec3 bmin = glm::vec3(1e30f), bmax = glm::vec3(-1e30f);
    glm::mat4 fit = glm::mat4(1.0f);
};

/* ===== LECTURA DE ATRIBUTOS ===== */
struct Attr { const unsigned char* base = nullptr; size_t stride = 0; int type = 0; bool norm = false; int size = 0; };

Attr getAttr(const tinygltf::Model& g, const tinygltf::Primitive& p, const char* name) {
    Attr r;
    auto it = p.attributes.find(name);
    if (it == p.attributes.end()) return r;
    const auto& a = g.accessors[it->second];
    if (a.bufferView < 0) return r;
    const auto& bv = g.bufferViews[a.bufferView];
    int s = a.ByteStride(bv);
    r.base = g.buffers[bv.buffer].data.data() + bv.byteOffset + a.byteOffset;
    r.stride = s > 0 ? (size_t)s : 0;
    r.type = a.componentType;
    r.norm = a.normalized;
    r.size = tinygltf::GetComponentSizeInBytes(a.componentType);
    return r;
}

float readComp(const Attr& a, size_t i, int k) {
    const unsigned char* p = a.base + i * a.stride + k * a.size;
    switch (a.type) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: return *(const float*)p;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { unsigned short v = *(const unsigned short*)p; return a.norm ? v / 65535.0f : (float)v; }
    case TINYGLTF_COMPONENT_TYPE_SHORT: { short v = *(const short*)p; return a.norm ? std::max(v / 32767.0f, -1.0f) : (float)v; }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return a.norm ? *p / 255.0f : (float)*p;
    case TINYGLTF_COMPONENT_TYPE_BYTE: { signed char v = *(const signed char*)p; return a.norm ? std::max(v / 127.0f, -1.0f) : (float)v; }
    }
    return 0.0f;
}

/* ===== MATERIAL ===== */
MatInfo materialInfo(const tinygltf::Model& g, int mi) {
    MatInfo r;
    if (mi < 0) return r;
    const auto& mat = g.materials[mi];
    const auto& pbr = mat.pbrMetallicRoughness;
    if (pbr.baseColorFactor.size() == 4)
        for (int c = 0; c < 4; c++) r.color[c] = (float)pbr.baseColorFactor[c];
    r.tex = pbr.baseColorTexture.index;
    auto it = mat.extensions.find("KHR_materials_pbrSpecularGlossiness");
    if (r.tex < 0 && it != mat.extensions.end()) {
        const auto& e = it->second;
        if (e.Has("diffuseTexture")) r.tex = e.Get("diffuseTexture").Get("index").GetNumberAsInt();
        if (e.Has("diffuseFactor"))
            for (int c = 0; c < 4; c++) r.color[c] = (float)e.Get("diffuseFactor").Get(c).GetNumberAsDouble();
    }
    r.mode = (mat.alphaMode == "BLEND") ? 2 : (mat.alphaMode == "MASK") ? 1 : 0;
    r.cutoff = (float)mat.alphaCutoff;
    return r;
}

/* ===== CREAR VAO (CON INDICES) ===== */
unsigned makeVAO(const std::vector<float>& v, const std::vector<unsigned>& idx) {
    unsigned vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    if (!idx.empty()) {
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    }
    const int comps[3] = { 3, 3, 2 }, offs[3] = { 0, 3, 6 };
    for (int i = 0; i < 3; i++) {
        glVertexAttribPointer(i, comps[i], GL_FLOAT, GL_FALSE, 32, (void*)(offs[i] * sizeof(float)));
        glEnableVertexAttribArray(i);
    }
    return vao;
}

/* ===== CREAR VAO (SOLO VERTICES) ===== */
unsigned makeVAO(const std::vector<float>& v) {
    return makeVAO(v, std::vector<unsigned>());
}

/* ===== AGREGAR PRIMITIVA ===== */
void addPrimitive(Model3D& M, const tinygltf::Primitive& p, const glm::mat4& w) {
    if (p.mode != TINYGLTF_MODE_TRIANGLES) return;
    const auto& g = M.gltf;
    Attr P = getAttr(g, p, "POSITION"), N = getAttr(g, p, "NORMAL"), T = getAttr(g, p, "TEXCOORD_0");
    if (!P.base) return;
    size_t n = g.accessors[p.attributes.at("POSITION")].count;

    std::vector<float> v;
    v.reserve(n * 8);
    for (size_t i = 0; i < n; i++) {
        float x = readComp(P, i, 0), y = readComp(P, i, 1), z = readComp(P, i, 2);
        v.insert(v.end(), { x, y, z,
            N.base ? readComp(N, i, 0) : 0.0f, N.base ? readComp(N, i, 1) : 1.0f, N.base ? readComp(N, i, 2) : 0.0f,
            T.base ? readComp(T, i, 0) : 0.0f, T.base ? readComp(T, i, 1) : 0.0f });
        glm::vec3 wp = glm::vec3(w * glm::vec4(x, y, z, 1.0f));
        M.bmin = glm::min(M.bmin, wp);
        M.bmax = glm::max(M.bmax, wp);
    }

    std::vector<unsigned> idx;
    if (p.indices >= 0) {
        const auto& a = g.accessors[p.indices];
        const auto& bv = g.bufferViews[a.bufferView];
        const unsigned char* d = g.buffers[bv.buffer].data.data() + bv.byteOffset + a.byteOffset;
        int sz = tinygltf::GetComponentSizeInBytes(a.componentType);
        idx.reserve(a.count);
        for (size_t k = 0; k < a.count; k++) {
            const unsigned char* e = d + k * sz;
            if (sz == 4) idx.push_back(*(const unsigned*)e);
            else if (sz == 2) idx.push_back(*(const unsigned short*)e);
            else idx.push_back(*e);
        }
    }

    MeshGL m;
    m.vao = makeVAO(v, idx);
    m.count = (int)(idx.empty() ? n : idx.size());
    m.indexed = !idx.empty();
    m.world = w;
    m.mat = materialInfo(g, p.material);
    M.meshes.push_back(m);
}

/* ===== RECORRER NODOS ===== */
void processNode(Model3D& M, int i, const glm::mat4& parent) {
    const auto& nd = M.gltf.nodes[i];
    glm::mat4 m(1.0f);
    if (nd.matrix.size() == 16) {
        for (int k = 0; k < 16; k++) glm::value_ptr(m)[k] = (float)nd.matrix[k];
    }
    else {
        if (nd.translation.size() == 3) m = glm::translate(m, glm::vec3(nd.translation[0], nd.translation[1], nd.translation[2]));
        if (nd.rotation.size() == 4) m *= glm::mat4_cast(glm::quat((float)nd.rotation[3], (float)nd.rotation[0], (float)nd.rotation[1], (float)nd.rotation[2]));
        if (nd.scale.size() == 3) m = glm::scale(m, glm::vec3(nd.scale[0], nd.scale[1], nd.scale[2]));
    }
    glm::mat4 world = parent * m;
    if (nd.mesh >= 0)
        for (const auto& p : M.gltf.meshes[nd.mesh].primitives) addPrimitive(M, p, world);
    for (int c : nd.children) processNode(M, c, world);
}

/* ===== IMAGENES: OMITIR MAPAS DE NORMALES ===== */
bool loadImageSkipNormals(tinygltf::Image* img, const int idx, std::string* err, std::string* warn,
    int reqW, int reqH, const unsigned char* bytes, int size, void* user) {
    if (img->uri.find("_normal") != std::string::npos) return true;
    return tinygltf::LoadImageData(img, idx, err, warn, reqW, reqH, bytes, size, user);
}

/* ===== CARGAR MODELO ===== */
bool loadModel(Model3D& M, const char* path, float absoluteScale) {
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(loadImageSkipNormals, nullptr);
    std::string err, warn;
    bool ok = loader.LoadASCIIFromFile(&M.gltf, &err, &warn, path);
    if (!warn.empty()) std::cout << path << " aviso: " << warn << std::endl;
    if (!ok) {
        std::cerr << "Fallo al cargar " << path << ": " << err << std::endl;
        return false;
    }
    int si = std::max(M.gltf.defaultScene, 0);
    if (si < (int)M.gltf.scenes.size())
        for (int n : M.gltf.scenes[si].nodes) processNode(M, n, glm::mat4(1.0f));
    if (M.meshes.empty()) return false;

    glm::vec3 size = M.bmax - M.bmin;
    float s = (absoluteScale > 0.0f) ? absoluteScale : 2.0f / std::max(size.x, std::max(size.y, size.z));
    M.fit = glm::scale(glm::mat4(1.0f), glm::vec3(s)) * glm::translate(glm::mat4(1.0f), -(M.bmin + M.bmax) * 0.5f);
    return true;
}

/* ===== SUBIR TEXTURAS ===== */
void uploadTextures(Model3D& M) {
    auto& g = M.gltf;
    M.textures.assign(g.textures.size(), 0);
    std::vector<bool> used(g.textures.size(), false);
    for (const auto& m : M.meshes)
        if (m.mat.tex >= 0 && m.mat.tex < (int)used.size()) used[m.mat.tex] = true;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int count = 0;
    for (size_t i = 0; i < g.textures.size(); i++) {
        if (!used[i]) continue;
        int src = g.textures[i].source;
        if (src < 0 || src >= (int)g.images.size()) continue;
        const auto& img = g.images[src];
        if (img.image.empty() || img.bits != 8) {
            std::cout << "Textura " << i << " (" << img.uri << ") no se pudo usar" << std::endl;
            continue;
        }
        GLenum f = img.component == 4 ? GL_RGBA : img.component == 3 ? GL_RGB : img.component == 2 ? GL_RG : GL_RED;
        glGenTextures(1, &M.textures[i]);
        glBindTexture(GL_TEXTURE_2D, M.textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, f, img.width, img.height, 0, f, GL_UNSIGNED_BYTE, img.image.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        count++;
    }
    for (auto& img : g.images) {
        img.image.clear();
        img.image.shrink_to_fit();
    }
    std::cout << "Texturas subidas: " << count << std::endl;
}

/* ===== COMPILAR SHADER ===== */
unsigned compile(GLenum type, const char* src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); std::cerr << log << std::endl; }
    return s;
}

/* ===== CUBO PARA EL MARCADOR DE LUZ ===== */
std::vector<float> makeCube() {
    const float nrm[6][3] = { { 0, 0, 1 },{ 0, 0, -1 },{ 1, 0, 0 },{ -1, 0, 0 },{ 0, 1, 0 },{ 0, -1, 0 } };
    const int ci[6][2] = { { -1, -1 },{ 1, -1 },{ 1, 1 },{ -1, -1 },{ 1, 1 },{ -1, 1 } };
    std::vector<float> v;
    for (int f = 0; f < 6; f++) {
        glm::vec3 N(nrm[f][0], nrm[f][1], nrm[f][2]);
        glm::vec3 ref = std::fabs(N.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 U = glm::normalize(glm::cross(ref, N)), V = glm::cross(N, U);
        for (int k = 0; k < 6; k++) {
            glm::vec3 p = N * 0.5f + U * (ci[k][0] * 0.5f) + V * (ci[k][1] * 0.5f);
            v.insert(v.end(), { p.x, p.y, p.z, N.x, N.y, N.z, 0.0f, 0.0f });
        }
    }
    return v;
}

/* ===== PROGRAMA PRINCIPAL ===== */
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1100, 700, "Visor 3D - Tiburon en el escenario submarino", NULL, NULL);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    glEnable(GL_DEPTH_TEST);

    /* ----- DEAR IMGUI ----- */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* ----- CARGA DE LOS MODELOS ----- */
    Model3D shark, env;
    if (!loadModel(shark, "scene.gltf", 0.0f)) return -1;
    uploadTextures(shark);
    bool envOk = loadModel(env, "escenario/scene.gltf", ENV_SCALE);
    if (envOk) uploadTextures(env);
    else std::cerr << "No se pudo cargar el escenario: revisa la carpeta 'escenario'" << std::endl;

    /* ----- LINEA DE LA CURVA Y MARCADOR DE LUZ ----- */
    std::vector<float> curve;
    for (int i = 0; i <= 400; i++) {
        glm::vec3 p, t;
        pathPoint(i / 400.0f, p, t);
        curve.insert(curve.end(), { p.x, p.y, p.z, 0, 0, 1, 0, 0 });
    }
    unsigned curveVAO = makeVAO(curve);
    unsigned cubeVAO = makeVAO(makeCube());

    /* ----- PROGRAMA DE SHADERS ----- */
    unsigned prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vsSource));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fsSource));
    glLinkProgram(prog);
    glUseProgram(prog);
    auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
    glUniform1i(U("tex"), 0);
    glUniform3f(U("sunDir"), -0.3f, -1.0f, -0.4f);

    /* ----- ESTADO DE LA CAMARA ----- */
    glm::vec3 cameraPos(0.0f, 2.0f, 17.0f), cameraFront(0.0f, 0.0f, -1.0f), cameraUp(0.0f, 1.0f, 0.0f);
    float yaw = -90.0f, pitch = -7.0f, lastX = 0.0f, lastY = 0.0f;
    bool looking = false, firstMouse = true;

    /* ----- ESTADO DE LA INTERFAZ ----- */
    bool paused = false, pingPong = false, spin = false;
    bool showCurve = true, showLight = true, lightFollows = true, showEnv = true, showBlend = true;
    float speed = 1.0f, spinSpeed = 90.0f, modelScale = 1.2f, modelYaw = 0.0f, camSpeed = 8.0f;
    float ambientK = 0.35f, diffuseK = 1.2f, specularK = 0.8f, shininess = 32.0f, sunK = 0.8f, fogDensity = 0.025f;
    float lightColor[3] = { 1.0f, 0.95f, 0.85f };
    float fogColor[3] = { 0.03f, 0.2f, 0.32f };
    float animT = 0.0f;
    const float PERIOD = 12.0f;

    /* ----- DIBUJO DE UN MODELO ----- */
    auto drawModel = [&](Model3D& M, const glm::mat4& base, int pass) {
        for (const auto& m : M.meshes) {
            bool blend = (m.mat.mode == 2);
            if ((pass == 1) != blend) continue;
            glm::mat4 mm = base * m.world;
            glUniformMatrix4fv(U("model"), 1, GL_FALSE, glm::value_ptr(mm));
            glUniform4fv(U("baseColor"), 1, m.mat.color);
            glUniform1i(U("alphaMode"), m.mat.mode);
            glUniform1f(U("alphaCutoff"), m.mat.cutoff);
            bool useTex = m.mat.tex >= 0 && m.mat.tex < (int)M.textures.size() && M.textures[m.mat.tex] != 0;
            if (useTex) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, M.textures[m.mat.tex]);
            }
            glUniform1i(U("useTexture"), useTex);
            glBindVertexArray(m.vao);
            if (m.indexed) glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, 0);
            else glDrawArrays(GL_TRIANGLES, 0, m.count);
        }
        };

    /* ----- BUCLE PRINCIPAL ----- */
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime;

        /* ----- ENTRADAS DE TECLADO (DEAR IMGUI) ----- */
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(window, true);
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) paused = !paused;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) animT = 0.0f;

        float step = camSpeed * dt;
        glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
        if (ImGui::IsKeyDown(ImGuiKey_W)) cameraPos += step * cameraFront;
        if (ImGui::IsKeyDown(ImGuiKey_S)) cameraPos -= step * cameraFront;
        if (ImGui::IsKeyDown(ImGuiKey_A)) cameraPos -= right * step;
        if (ImGui::IsKeyDown(ImGuiKey_D)) cameraPos += right * step;
        if (ImGui::IsKeyDown(ImGuiKey_E)) cameraPos += cameraUp * step;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) cameraPos -= cameraUp * step;

        /* ----- MIRAR CON CLIC DERECHO ----- */
        bool wantLook = io.MouseDown[1] && (looking || !io.WantCaptureMouse);
        if (wantLook && !looking) { glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); firstMouse = true; }
        if (!wantLook && looking) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        looking = wantLook;
        if (looking) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            if (firstMouse) { lastX = (float)mx; lastY = (float)my; firstMouse = false; }
            yaw += ((float)mx - lastX) * 0.1f;
            pitch += (lastY - (float)my) * 0.1f;
            lastX = (float)mx;
            lastY = (float)my;
            pitch = std::max(-89.0f, std::min(89.0f, pitch));
            glm::vec3 f;
            f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
            f.y = sin(glm::radians(pitch));
            f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
            cameraFront = glm::normalize(f);
        }

        /* ----- PANEL DE CONTROLES ----- */
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(340, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("Controles");
        ImGui::Text("W/A/S/D: mover camara");
        ImGui::Text("Q/E: bajar / subir camara");
        ImGui::Text("Clic derecho + mouse: mirar");
        ImGui::Text("Espacio: pausa   R: reiniciar   Esc: salir");
        ImGui::Separator();
        ImGui::Text("Animacion");
        ImGui::Checkbox("Pausa", &paused);
        ImGui::SliderFloat("Velocidad", &speed, 0.1f, 4.0f);
        ImGui::Checkbox("Ida y vuelta", &pingPong);
        ImGui::Checkbox("Girar 360 sobre su eje", &spin);
        ImGui::SliderFloat("Giro (grados/s)", &spinSpeed, 10.0f, 360.0f);
        ImGui::SliderFloat("Tamano del tiburon", &modelScale, 0.3f, 4.0f);
        ImGui::SliderFloat("Orientacion (grados)", &modelYaw, -180.0f, 180.0f);
        ImGui::Separator();
        ImGui::Text("Iluminacion Phong (luz del tiburon)");
        ImGui::SliderFloat("Ambiental", &ambientK, 0.0f, 1.0f);
        ImGui::SliderFloat("Difusa", &diffuseK, 0.0f, 3.0f);
        ImGui::SliderFloat("Especular", &specularK, 0.0f, 2.0f);
        ImGui::SliderFloat("Brillo", &shininess, 2.0f, 256.0f);
        ImGui::ColorEdit3("Color de la luz", lightColor);
        ImGui::Checkbox("La luz sigue al modelo", &lightFollows);
        ImGui::Separator();
        ImGui::Text("Escenario");
        ImGui::Checkbox("Mostrar escenario", &showEnv);
        ImGui::Checkbox("Mostrar transparencias", &showBlend);
        ImGui::SliderFloat("Luz del sol", &sunK, 0.0f, 2.0f);
        ImGui::SliderFloat("Niebla", &fogDensity, 0.0f, 0.15f);
        ImGui::ColorEdit3("Color del agua", fogColor);
        ImGui::Checkbox("Mostrar curva", &showCurve);
        ImGui::Checkbox("Mostrar luz", &showLight);
        ImGui::SliderFloat("Velocidad de camara", &camSpeed, 1.0f, 30.0f);
        ImGui::Separator();
        ImGui::Text("Camara: %.1f, %.1f, %.1f", cameraPos.x, cameraPos.y, cameraPos.z);
        ImGui::Text("%.0f FPS", io.Framerate);
        ImGui::End();

        /* ----- POSICION SOBRE LA CURVA ----- */
        if (!paused) animT += dt * speed;
        float cyc = animT / PERIOD, s, dirSign = 1.0f;
        if (pingPong) {
            float c = std::fmod(cyc, 2.0f);
            s = (c < 1.0f) ? c : 2.0f - c;
            dirSign = (c < 1.0f) ? 1.0f : -1.0f;
        }
        else {
            s = std::fmod(cyc, 1.0f);
        }
        glm::vec3 pos, tangent;
        pathPoint(s, pos, tangent);
        glm::vec3 dir = glm::normalize(tangent * dirSign);
        glm::vec3 lightPos = lightFollows ? pos + glm::vec3(0.0f, 1.5f, 2.0f) : glm::vec3(0.0f, 6.0f, 8.0f);
        glm::vec3 lightCol(lightColor[0], lightColor[1], lightColor[2]);

        /* ----- PREPARAR EL CUADRO ----- */
        int fw, fh;
        glfwGetFramebufferSize(window, &fw, &fh);
        if (fh == 0) fh = 1;
        glViewport(0, 0, fw, fh);
        glClearColor(fogColor[0], fogColor[1], fogColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)fw / fh, 0.1f, 300.0f);
        glUniformMatrix4fv(U("view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(U("projection"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform3fv(U("viewPos"), 1, glm::value_ptr(cameraPos));
        glUniform3fv(U("lightPos"), 1, glm::value_ptr(lightPos));
        glUniform3fv(U("lightColor"), 1, glm::value_ptr(lightCol));
        glUniform3fv(U("fogColor"), 1, fogColor);
        glUniform1f(U("ambientK"), ambientK);
        glUniform1f(U("diffuseK"), diffuseK);
        glUniform1f(U("specularK"), specularK);
        glUniform1f(U("shininess"), shininess);
        glUniform1f(U("sunK"), sunK);
        glUniform1f(U("fogDensity"), fogDensity);

        /* ----- DIBUJO DE LA LINEA ----- */
        if (showCurve) {
            glUniformMatrix4fv(U("model"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
            glUniform1i(U("useTexture"), 0);
            glUniform1i(U("unlit"), 1);
            glUniform1i(U("alphaMode"), 0);
            glUniform4f(U("baseColor"), 1.0f, 0.1f, 0.1f, 1.0f);
            glBindVertexArray(curveVAO);
            glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(curve.size() / 8));
        }

        /* ----- MATRIZ DEL TIBURON ----- */
        float heading = std::atan2(dir.x, dir.z) + glm::radians(modelYaw);
        float slope = std::atan2(dir.y, std::sqrt(dir.x * dir.x + dir.z * dir.z));
        float spinAngle = spin ? glm::radians(spinSpeed) * animT / std::max(speed, 0.01f) : 0.0f;
        glm::mat4 place = glm::translate(glm::mat4(1.0f), pos)
            * glm::rotate(glm::mat4(1.0f), heading, glm::vec3(0, 1, 0))
            * glm::rotate(glm::mat4(1.0f), -slope, glm::vec3(1, 0, 0))
            * glm::rotate(glm::mat4(1.0f), spinAngle, glm::vec3(0, 1, 0))
            * glm::scale(glm::mat4(1.0f), glm::vec3(modelScale))
            * shark.fit;

        /* ----- PASADA 1: OBJETOS OPACOS ----- */
        glUniform1i(U("unlit"), 0);
        if (envOk && showEnv) drawModel(env, env.fit, 0);
        drawModel(shark, place, 0);

        if (showLight) {
            glm::mat4 mm = glm::scale(glm::translate(glm::mat4(1.0f), lightPos), glm::vec3(0.25f));
            glUniformMatrix4fv(U("model"), 1, GL_FALSE, glm::value_ptr(mm));
            glUniform1i(U("useTexture"), 0);
            glUniform1i(U("unlit"), 1);
            glUniform1i(U("alphaMode"), 0);
            glUniform4f(U("baseColor"), lightCol.x, lightCol.y, lightCol.z, 1.0f);
            glBindVertexArray(cubeVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glUniform1i(U("unlit"), 0);
        }

        /* ----- PASADA 2: TRANSPARENCIAS ----- */
        if (showBlend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            if (envOk && showEnv) drawModel(env, env.fit, 1);
            drawModel(shark, place, 1);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        glBindVertexArray(0);

        /* ----- DIBUJO DE LA INTERFAZ ----- */
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    /* ----- LIMPIEZA ----- */
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}