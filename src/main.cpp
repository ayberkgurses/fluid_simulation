#include "FluidSim.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>

// ── Window / camera state ─────────────────────────────────────────────────────
static const int   WIN_W = 900, WIN_H = 900;
static float camYaw   = -90.0f, camPitch = -20.0f;
static float camFov   = 60.0f;
static glm::vec3 camPos(0.0f, 0.8f, 3.5f);
static bool  mouseLocked = false;
static bool  rightDragging = false;
static double lastX = WIN_W/2.0, lastY = WIN_H/2.0;
static bool  firstMouse = true;
static float moveSpeed = 2.0f;
static const float SENS = 0.10f;

// ── Grab state ────────────────────────────────────────────────────────────────
static bool              grabActive = false;
static std::vector<int>       grabIndices;
static std::vector<glm::vec3> grabOffsets;
static glm::vec3 grabCenter(0.0f);
static float     grabViewZ = -1.0f;

static glm::vec3 unprojectAtViewZ(double mx, double my, float viewZ,
                                  const glm::mat4& view, const glm::mat4& proj) {
    float ndcX =  (float)(mx / WIN_W) * 2.0f - 1.0f;
    float ndcY = -(float)(my / WIN_H) * 2.0f + 1.0f;
    float ex = ndcX * (-viewZ) / proj[0][0];
    float ey = ndcY * (-viewZ) / proj[1][1];
    return glm::vec3(glm::inverse(view) * glm::vec4(ex, ey, viewZ, 1.0f));
}

static glm::vec3 cameraFront() {
    float yR = glm::radians(camYaw), pR = glm::radians(camPitch);
    return glm::normalize(glm::vec3(
        std::cos(yR)*std::cos(pR),
        std::sin(pR),
        std::sin(yR)*std::cos(pR)));
}

static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

static void scroll_callback(GLFWwindow*, double, double yoff) {
    camFov = std::clamp(camFov - (float)yoff * 2.0f, 10.0f, 90.0f);
}

static void mousebutton_callback(GLFWwindow*, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rightDragging = (action == GLFW_PRESS);
        firstMouse = true;
    }
}

static void cursor_callback(GLFWwindow*, double xpos, double ypos) {
    if (!mouseLocked && !rightDragging) return;
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float dx = (float)(xpos - lastX), dy = (float)(lastY - ypos);
    lastX = xpos; lastY = ypos;
    camYaw   += dx * SENS;
    camPitch  = std::clamp(camPitch + dy * SENS, -89.0f, 89.0f);
}

static void key_callback(GLFWwindow* win, int key, int, int action, int) {
    if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        mouseLocked = !mouseLocked;
        firstMouse  = true;
        glfwSetInputMode(win, GLFW_CURSOR,
            mouseLocked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(win, true);
}

static FluidSim* g_sim = nullptr;
static void processInput(GLFWwindow* win, float dt) {
    glm::vec3 front = cameraFront();
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0,1,0)));
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) camPos += front * moveSpeed * dt;
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) camPos -= front * moveSpeed * dt;
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) camPos -= right * moveSpeed * dt;
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) camPos += right * moveSpeed * dt;
    if (glfwGetKey(win, GLFW_KEY_SPACE)      == GLFW_PRESS) camPos.y += moveSpeed * dt;
    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camPos.y -= moveSpeed * dt;
    static bool fWasUp = true;
    bool fDown = glfwGetKey(win, GLFW_KEY_F) == GLFW_PRESS;
    if (fDown && fWasUp && g_sim)
        g_sim->spawnBlock(0.0f, 0.5f, 0.0f, 8, 8, 8, 0.10f);
    fWasUp = !fDown;
}

// ── Shader helpers ────────────────────────────────────────────────────────────
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log);
        std::cerr << "Shader error: " << log << '\n';
    }
    return s;
}
static GLuint linkProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER,   vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ── Sprite shaders ────────────────────────────────────────────────────────────
static const char* SPR_VERT = R"glsl(
#version 330 core
layout(location=0) in vec4 aParticle; // xyz + density
uniform mat4 uVP;
uniform mat4 uProj;
uniform float uRadius;
out float vDensity;
void main(){
    vDensity = aParticle.w;
    vec4 viewPos = uVP * vec4(aParticle.xyz, 1.0);
    gl_Position = uProj * viewPos;
    float viewDepth = max(-viewPos.z, 0.001);
    gl_PointSize = uRadius * uProj[1][1] * 3200.0 / viewDepth;
}
)glsl";

static const char* SPR_FRAG = R"glsl(
#version 330 core
in float vDensity;
out vec4 FragColor;
uniform mat4 uProj;
void main(){
    vec2 pc = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(pc, pc);
    if (r2 > 1.0) discard;
    float z = sqrt(1.0 - r2);
    vec4 clipPos = uProj * vec4(0.0, 0.0, z - 1.0, 1.0);
    gl_FragDepth = (clipPos.z / clipPos.w) * 0.5 + 0.5;
    float t = clamp(z * 0.5 + 0.3 + vDensity * 0.15, 0.0, 1.0);
    vec3 highlight = vec3(0.20, 0.85, 0.80);
    vec3 shadow    = vec3(0.00, 0.45, 0.55);
    float alpha = clamp(z * z * z * 0.5, 0.0, 0.40);
    FragColor = vec4(mix(shadow, highlight, t), alpha);
}
)glsl";

// ── Box shaders (glass) ───────────────────────────────────────────────────────
static const char* BOX_VERT = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
void main(){
    vWorldPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal   = normalize(uNormalMat * aNormal);
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

static const char* BOX_FRAG = R"glsl(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;
uniform vec3 uCamPos;
uniform vec3 uLightPos;
void main(){
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vWorldPos);
    vec3 V = normalize(uCamPos   - vWorldPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0) * 0.6;
    float spec = pow(max(dot(N, H), 0.0), 64.0) * 0.8;
    vec3 col = vec3(0.90, 0.95, 1.00);
    FragColor = vec4(col * (0.10 + diff) + vec3(spec), 0.07);
}
)glsl";

// ── Box mesh ──────────────────────────────────────────────────────────────────
static void buildBox(std::vector<float>& verts, std::vector<unsigned>& inds) {
    // 6 faces, each 4 verts (pos xyz + normal xyz)
    float faces[6][6] = {
        { 0, 0,-1, 0, 0,-1}, // -Z
        { 0, 0, 1, 0, 0, 1}, // +Z
        {-1, 0, 0,-1, 0, 0}, // -X
        { 1, 0, 0, 1, 0, 0}, // +X
        { 0,-1, 0, 0,-1, 0}, // -Y
        { 0, 1, 0, 0, 1, 0}, // +Y
    };
    // Tangent pairs per face
    float tan1[6][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{1,0,0},{1,0,0}};
    float tan2[6][3] = {{0,1,0},{ 0,1,0},{0,1,0},{0, 1, 0},{0,0,1},{0,0,1}};

    for (int f = 0; f < 6; ++f) {
        float* n = faces[f]+3;
        float cx = faces[f][0], cy = faces[f][1], cz = faces[f][2];
        float* t1 = tan1[f]; float* t2 = tan2[f];
        unsigned base = (unsigned)(verts.size() / 6);
        for (int s = -1; s <= 1; s += 2)
        for (int t = -1; t <= 1; t += 2) {
            float px = cx + t1[0]*s + t2[0]*t;
            float py = cy + t1[1]*s + t2[1]*t;
            float pz = cz + t1[2]*s + t2[2]*t;
            verts.insert(verts.end(), {px,py,pz, n[0],n[1],n[2]});
        }
        // two triangles: 0,1,2 and 2,1,3
        inds.insert(inds.end(), {base,base+1,base+2, base+2,base+1,base+3});
    }
}

int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* win = glfwCreateWindow(WIN_W, WIN_H, "Fluid Simulation", nullptr, nullptr);
    if (!win) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win);
    glfwSetFramebufferSizeCallback(win, framebuffer_size_callback);
    glfwSetScrollCallback(win, scroll_callback);
    glfwSetCursorPosCallback(win, cursor_callback);
    glfwSetMouseButtonCallback(win, mousebutton_callback);
    glfwSetKeyCallback(win, key_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "GLAD init failed\n"; return -1;
    }

    // ── Simulation ────────────────────────────────────────────────────────────
    FluidSim sim;
    g_sim = &sim;
    sim.spawnBlock(0.0f, 0.35f, 0.0f, 12, 12, 12, 0.09f);

    // ── Sprite VBO ────────────────────────────────────────────────────────────
    GLuint sprVAO, sprVBO;
    glGenVertexArrays(1, &sprVAO);
    glGenBuffers(1, &sprVBO);
    glBindVertexArray(sprVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sprVBO);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    GLuint sprProg = linkProgram(SPR_VERT, SPR_FRAG);

    // ── Box mesh ──────────────────────────────────────────────────────────────
    std::vector<float>    boxVerts;
    std::vector<unsigned> boxInds;
    buildBox(boxVerts, boxInds);

    GLuint boxVAO, boxVBO, boxEBO;
    glGenVertexArrays(1, &boxVAO);
    glGenBuffers(1, &boxVBO);
    glGenBuffers(1, &boxEBO);
    glBindVertexArray(boxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, boxVBO);
    glBufferData(GL_ARRAY_BUFFER, boxVerts.size()*sizeof(float), boxVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, boxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, boxInds.size()*sizeof(unsigned), boxInds.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    GLuint boxProg = linkProgram(BOX_VERT, BOX_FRAG);

    // ── Floor quad ────────────────────────────────────────────────────────────
    float floorVerts[] = {
        -0.97f, -0.97f, -0.97f,   0.97f, -0.97f, -0.97f,
         0.97f, -0.97f,  0.97f,  -0.97f, -0.97f,  0.97f
    };
    unsigned floorInds[] = {0,1,2, 2,3,0};
    GLuint fVAO, fVBO, fEBO;
    glGenVertexArrays(1,&fVAO); glGenBuffers(1,&fVBO); glGenBuffers(1,&fEBO);
    glBindVertexArray(fVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(floorVerts), floorVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, fEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(floorInds), floorInds, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    static const char* FLOOR_VERT = R"glsl(
    #version 330 core
    layout(location=0) in vec3 aPos;
    uniform mat4 uVP;
    void main(){ gl_Position = uVP * vec4(aPos,1.0); }
    )glsl";
    static const char* FLOOR_FRAG = R"glsl(
    #version 330 core
    out vec4 FragColor;
    void main(){
        FragColor = vec4(0.08, 0.10, 0.14, 0.85);
    }
    )glsl";
    GLuint floorProg = linkProgram(FLOOR_VERT, FLOOR_FRAG);

    // ── Render state ──────────────────────────────────────────────────────────
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);

    double prevTime = glfwGetTime();

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float  dt  = (float)(now - prevTime);
        prevTime   = now;
        dt = std::min(dt, 1.0f/20.0f);
        

        glfwPollEvents();
        processInput(win, dt);

        // ── Matrices (needed for grab before sim step) ────────────────────────
        glm::vec3 front = cameraFront();
        glm::mat4 view  = glm::lookAt(camPos, camPos + front, glm::vec3(0,1,0));
        glm::mat4 proj  = glm::perspective(glm::radians(camFov),
                                            (float)WIN_W/WIN_H, 0.01f, 100.0f);

        // ── Left-click grab ───────────────────────────────────────────────────
        bool leftDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);

        if (leftDown && !grabActive) {
            // Build a ray from camera through mouse
            float ndcX =  (float)(mx / WIN_W) * 2.0f - 1.0f;
            float ndcY = -(float)(my / WIN_H) * 2.0f + 1.0f;
            glm::vec4 rayEye = glm::inverse(proj) * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
            glm::vec3 rayDir = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));

            // Find particle closest to the ray
            float bestDist = 1e9f, bestT = -1.0f;
            auto& parts = sim.getParticlesMut();
            for (auto& p : parts) {
                glm::vec3 pp(p.x, p.y, p.z);
                glm::vec3 toP = pp - camPos;
                float t = glm::dot(toP, rayDir);
                if (t < 0.0f) continue;
                float d = glm::length(camPos + rayDir * t - pp);
                if (d < bestDist) { bestDist = d; bestT = t; }
            }

            if (bestT > 0.0f && bestDist < 0.25f) {
                grabCenter = camPos + rayDir * bestT;
                glm::vec4 gv = view * glm::vec4(grabCenter, 1.0f);
                grabViewZ = gv.z;

                float sphereR = 0.30f;
                grabIndices.clear(); grabOffsets.clear();
                for (int i = 0; i < (int)parts.size(); ++i) {
                    glm::vec3 pp(parts[i].x, parts[i].y, parts[i].z);
                    if (glm::length(pp - grabCenter) < sphereR) {
                        grabIndices.push_back(i);
                        grabOffsets.push_back(pp - grabCenter);
                    }
                }
                grabActive = !grabIndices.empty();
            }
        }

        // Substeps
        float subDt = dt / 8.0f;
        for (int s = 0; s < 8; ++s) sim.step(subDt);

        // Apply grab after physics
        if (grabActive) {
            if (leftDown) {
                glm::vec3 newCenter = unprojectAtViewZ(mx, my, grabViewZ, view, proj);
                glm::vec3 delta = newCenter - grabCenter;
                auto& parts = sim.getParticlesMut();
                for (int i = 0; i < (int)grabIndices.size(); ++i) {
                    int idx = grabIndices[i];
                    glm::vec3 target = newCenter + grabOffsets[i];
                    parts[idx].x = target.x; parts[idx].y = target.y; parts[idx].z = target.z;
                    float invDt = (dt > 1e-6f) ? 1.0f / dt : 0.0f;
                    parts[idx].vx = delta.x * invDt;
                    parts[idx].vy = delta.y * invDt;
                    parts[idx].vz = delta.z * invDt;
                }
                grabCenter = newCenter;
            } else {
                grabActive = false;
            }
        }
        glm::mat4 vp      = proj * view;
        glm::mat3 normMat = glm::mat3(1.0f);

        // ── Clear ─────────────────────────────────────────────────────────────
        glClearColor(0.60f, 0.60f, 0.60f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ── Pass 1: floor ─────────────────────────────────────────────────────
        glUseProgram(floorProg);
        glUniformMatrix4fv(glGetUniformLocation(floorProg,"uVP"), 1, GL_FALSE, glm::value_ptr(vp));
        glBindVertexArray(fVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        // ── Pass 2: depth sprites ─────────────────────────────────────────────
        const auto& parts = sim.getParticles();
        std::vector<float> buf;
        buf.reserve(parts.size() * 4);
        for (auto& p : parts) {
            buf.push_back(p.x);
            buf.push_back(p.y);
            buf.push_back(p.z);
            buf.push_back(std::min(p.density / 1200.0f, 1.0f));
        }
        glBindBuffer(GL_ARRAY_BUFFER, sprVBO);
        glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_DYNAMIC_DRAW);

        glUseProgram(sprProg);
        glUniformMatrix4fv(glGetUniformLocation(sprProg,"uVP"),   1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(sprProg,"uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform1f(glGetUniformLocation(sprProg,"uRadius"), 0.055f);
        glBindVertexArray(sprVAO);
        glDrawArrays(GL_POINTS, 0, (GLsizei)parts.size());

        // ── Pass 3: glass box ─────────────────────────────────────────────────
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glUseProgram(boxProg);
        glUniformMatrix4fv(glGetUniformLocation(boxProg,"uMVP"),   1, GL_FALSE, glm::value_ptr(vp));
        glUniformMatrix4fv(glGetUniformLocation(boxProg,"uModel"), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
        glUniformMatrix3fv(glGetUniformLocation(boxProg,"uNormalMat"), 1, GL_FALSE, glm::value_ptr(normMat));
        glUniform3fv(glGetUniformLocation(boxProg,"uCamPos"),   1, glm::value_ptr(camPos));
        glm::vec3 light(2.0f, 4.0f, 3.0f);
        glUniform3fv(glGetUniformLocation(boxProg,"uLightPos"), 1, glm::value_ptr(light));
        glBindVertexArray(boxVAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)boxInds.size(), GL_UNSIGNED_INT, nullptr);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);

        glfwSwapBuffers(win);
    }

    // Cleanup
    glDeleteVertexArrays(1,&sprVAO); glDeleteBuffers(1,&sprVBO);
    glDeleteVertexArrays(1,&boxVAO); glDeleteBuffers(1,&boxVBO); glDeleteBuffers(1,&boxEBO);
    glDeleteVertexArrays(1,&fVAO);   glDeleteBuffers(1,&fVBO);   glDeleteBuffers(1,&fEBO);
    glDeleteProgram(sprProg); glDeleteProgram(boxProg); glDeleteProgram(floorProg);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
