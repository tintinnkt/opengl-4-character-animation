#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader_m.h>
#include <learnopengl/animation.h>
#include <learnopengl/model_animation.h>

#include "animator_blend.h"

#include <iostream>
#include <string>
#include <vector>
#include <cmath>

// ─── Window ───────────────────────────────────────────────────────────────────
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
void updateWindowTitle(GLFWwindow* window);

const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

// ─── Timing ───────────────────────────────────────────────────────────────────
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ─── Camera (third-person orbit) ─────────────────────────────────────────────
float camYaw      = 0.0f;
float camPitch    = 20.0f;
float camDistance = 4.5f;
float lastMouseX  = SCR_WIDTH  / 2.0f;
float lastMouseY  = SCR_HEIGHT / 2.0f;
bool  firstMouse  = true;

// ─── Character ────────────────────────────────────────────────────────────────
glm::vec3 characterPos   = glm::vec3(0.0f, 0.0f, 0.0f);
float     characterAngle = 0.0f;

const float WALK_SPEED = 2.5f;
const float RUN_SPEED  = 5.0f;       // slightly reduced to avoid overshooting
const float TURN_SPEED = 120.0f;

// ─── Animation state ──────────────────────────────────────────────────────────
enum class AnimState {
    IDLE,
    WALK, RUN,
    WALK_BACKWARD, RUN_BACKWARD,
    TURN_LEFT, TURN_RIGHT,
    JUMP, KICK, PUNCH
};
AnimState currentState = AnimState::IDLE;

bool isRunMode = true;

float     actionTimer    = 0.0f;
float     actionDuration = 0.0f;
AnimState returnState    = AnimState::IDLE;

const float JUMP_DURATION  = 1.1f;
const float KICK_DURATION  = 1.0f;
const float PUNCH_DURATION = 1.2f;

// ─── Animator + animation pointers ───────────────────────────────────────────
AnimatorBlend* animator          = nullptr;
Animation*     idleAnim          = nullptr;
Animation*     walkAnim          = nullptr;
Animation*     runAnim           = nullptr;
Animation*     walkBackwardAnim  = nullptr;
Animation*     runBackwardAnim   = nullptr;
Animation*     turnLeftAnim      = nullptr;
Animation*     turnRightAnim     = nullptr;
Animation*     jumpAnim          = nullptr;
Animation*     kickAnim          = nullptr;
Animation*     punchAnim         = nullptr;

// ─── Key debounce ─────────────────────────────────────────────────────────────
bool jumpPressed  = false;
bool kickPressed  = false;   // K
bool punchPressed = false;   // J

// ── Guard: only call PlayAnimation when the clip actually changes ─────────────
// This prevents the animator restarting the same clip every frame,
// which was causing the stutter/teleport in run state.
Animation* currentAnim = nullptr;
void switchAnimation(Animation* anim)
{
    if (anim == currentAnim) return;
    currentAnim = anim;
    animator->PlayAnimation(anim);
}

static const char* stateToString(AnimState s)
{
    switch (s) {
        case AnimState::IDLE:          return "Idle";
        case AnimState::WALK:          return "Walk";
        case AnimState::RUN:           return "Run";
        case AnimState::WALK_BACKWARD: return "Walk Backward";
        case AnimState::RUN_BACKWARD:  return "Run Backward";
        case AnimState::TURN_LEFT:     return "Turn Left";
        case AnimState::TURN_RIGHT:    return "Turn Right";
        case AnimState::JUMP:          return "Jump";
        case AnimState::KICK:          return "Kick";
        case AnimState::PUNCH:         return "Punch";
    }
    return "?";
}

// ─── Infinite ground grid (single full-screen quad, procedural fragment) ──────
//
//  Renders just 2 triangles (a quad) covering the whole screen.
//  The fragment shader reconstructs the world-space XZ position of each pixel
//  by unprojecting the clip-space coordinate, then uses fract() to draw
//  infinitely repeating grid lines with distance-based fade.
//  No geometry scales with view distance — always 4 vertices, 0 CPU updates.
//
unsigned int gridQuadVAO = 0, gridQuadVBO = 0;

void buildInfiniteGrid()
{
    float verts[] = { -1.0f,-1.0f,  1.0f,-1.0f,  -1.0f,1.0f,  1.0f,1.0f };
    glGenVertexArrays(1, &gridQuadVAO);
    glGenBuffers(1, &gridQuadVBO);
    glBindVertexArray(gridQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void drawInfiniteGrid(Shader& shader, const glm::mat4& proj, const glm::mat4& view)
{
    shader.use();
    shader.setMat4("projection",    proj);
    shader.setMat4("view",          view);
    shader.setMat4("invProjection", glm::inverse(proj));
    shader.setMat4("invView",       glm::inverse(view));
    glDepthMask(GL_FALSE);
    glBindVertexArray(gridQuadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

// ─── Dust ring particles ──────────────────────────────────────────────────────
struct DustRing { glm::vec3 pos; float scale; float alpha; bool alive; };

static const int   MAX_DUST        = 20;
static const float DUST_SPEED_WALK = 1.4f;
static const float DUST_SPEED_RUN  = 2.5f;
static const float DUST_FADE_WALK  = 1.8f;
static const float DUST_FADE_RUN   = 2.5f;
static const float DUST_SPAWN_WALK = 0.35f;
static const float DUST_SPAWN_RUN  = 0.18f;

std::vector<DustRing> dustRings(MAX_DUST);
float                 dustSpawnTimer = 0.0f;
unsigned int          dustVAO = 0, dustVBO = 0;
int                   dustVertCount = 0;

void buildDustMesh(int segments = 32)
{
    std::vector<float> verts;
    for (int i = 0; i < segments; ++i) {
        float a = (float)i / (float)segments * 2.0f * glm::pi<float>();
        verts.push_back(cosf(a)); verts.push_back(0.0f); verts.push_back(sinf(a));
    }
    dustVertCount = segments;
    glGenVertexArrays(1, &dustVAO);
    glGenBuffers(1, &dustVBO);
    glBindVertexArray(dustVAO);
    glBindBuffer(GL_ARRAY_BUFFER, dustVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float),
                 verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void spawnDust(const glm::vec3& pos)
{
    for (auto& r : dustRings) {
        if (!r.alive) {
            r.pos = glm::vec3(pos.x, 0.02f, pos.z);
            r.scale = 0.15f; r.alpha = 0.7f; r.alive = true;
            return;
        }
    }
}

void updateDust(float dt, bool running)
{
    float speed = running ? DUST_SPEED_RUN : DUST_SPEED_WALK;
    float fade  = running ? DUST_FADE_RUN  : DUST_FADE_WALK;
    for (auto& r : dustRings) {
        if (!r.alive) continue;
        r.scale += speed * dt;
        r.alpha -= fade  * dt;
        if (r.alpha <= 0.0f) r.alive = false;
    }
}

void drawDust(Shader& shader, const glm::mat4& proj, const glm::mat4& view)
{
    shader.use();
    shader.setMat4("projection", proj);
    shader.setMat4("view",       view);
    shader.setVec3("dustColor",  glm::vec3(0.75f, 0.72f, 0.65f));
    glBindVertexArray(dustVAO);
    for (auto& r : dustRings) {
        if (!r.alive) continue;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), r.pos);
        m = glm::scale(m, glm::vec3(r.scale, 1.0f, r.scale));
        shader.setMat4("model",  m);
        shader.setFloat("alpha", r.alpha);
        glDrawArrays(GL_LINE_LOOP, 0, dustVertCount);
    }
    glBindVertexArray(0);
}

// ─── Input ────────────────────────────────────────────────────────────────────
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Initial check for the start of the frame
    bool inAction = (currentState == AnimState::JUMP ||
                     currentState == AnimState::KICK ||
                     currentState == AnimState::PUNCH);

    // ── Run by default, Shift to Walk ────────────────────────────────────────
    bool shiftDown = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    isRunMode = !shiftDown;

    // ── One-shot actions ──────────────────────────────────────────────────────
    if (!inAction) {
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !jumpPressed) {
            jumpPressed  = true;
            returnState  = currentState;
            currentState = AnimState::JUMP;
            actionTimer  = 0.0f; actionDuration = JUMP_DURATION;
            currentAnim  = nullptr;        // force re-trigger even if same clip
            switchAnimation(jumpAnim);
        }
        // J = PUNCH
        if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS && !punchPressed) {
            punchPressed = true;
            returnState  = currentState;
            currentState = AnimState::PUNCH;
            actionTimer  = 0.0f; actionDuration = PUNCH_DURATION;
            currentAnim  = nullptr;
            switchAnimation(punchAnim);
        }
        // K = KICK
        if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS && !kickPressed) {
            kickPressed  = true;
            returnState  = currentState;
            currentState = AnimState::KICK;
            actionTimer  = 0.0f; actionDuration = KICK_DURATION;
            currentAnim  = nullptr;
            switchAnimation(kickAnim);
        }
    }

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) jumpPressed  = false;
    if (glfwGetKey(window, GLFW_KEY_J)     == GLFW_RELEASE) punchPressed = false;
    if (glfwGetKey(window, GLFW_KEY_K)     == GLFW_RELEASE) kickPressed  = false;

    // 🔥 THE FIX: Recalculate inAction so it catches newly started actions!
    inAction = (currentState == AnimState::JUMP ||
                currentState == AnimState::KICK ||
                currentState == AnimState::PUNCH);

    if (inAction) return;

    // ── Movement & turning ────────────────────────────────────────────────────
    bool wDown = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    bool sDown = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    bool aDown = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    bool dDown = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;

    if (wDown) {
        if (aDown) characterAngle += TURN_SPEED * deltaTime;
        if (dDown) characterAngle -= TURN_SPEED * deltaTime;

        float rad   = glm::radians(characterAngle);
        float speed = isRunMode ? RUN_SPEED : WALK_SPEED;
        characterPos += glm::vec3(sinf(rad), 0.0f, cosf(rad)) * speed * deltaTime;

        AnimState targetMove = isRunMode ? AnimState::RUN : AnimState::WALK;
        if (currentState != targetMove) {
            currentState = targetMove;
            switchAnimation(isRunMode ? runAnim : walkAnim);
        }
    }
    else if (sDown) {
        if (aDown) characterAngle += TURN_SPEED * deltaTime;
        if (dDown) characterAngle -= TURN_SPEED * deltaTime;

        float rad   = glm::radians(characterAngle);
        float speed = isRunMode ? RUN_SPEED : WALK_SPEED;
        characterPos -= glm::vec3(sinf(rad), 0.0f, cosf(rad)) * speed * deltaTime;

        AnimState targetBack = isRunMode ? AnimState::RUN_BACKWARD : AnimState::WALK_BACKWARD;
        if (currentState != targetBack) {
            currentState = targetBack;
            switchAnimation(isRunMode ? runBackwardAnim : walkBackwardAnim);
        }
    }
    else {
        if (aDown) {
            characterAngle += TURN_SPEED * deltaTime;
            if (currentState != AnimState::TURN_LEFT) {
                currentState = AnimState::TURN_LEFT;
                switchAnimation(turnLeftAnim);
            }
        }
        else if (dDown) {
            characterAngle -= TURN_SPEED * deltaTime;
            if (currentState != AnimState::TURN_RIGHT) {
                currentState = AnimState::TURN_RIGHT;
                switchAnimation(turnRightAnim);
            }
        }
        else {
            if (currentState != AnimState::IDLE) {
                currentState = AnimState::IDLE;
                switchAnimation(idleAnim);
            }
        }
    }
}
// ─── Callbacks ────────────────────────────────────────────────────────────────
void mouse_callback(GLFWwindow*, double xpos, double ypos)
{
    if (firstMouse) { lastMouseX = (float)xpos; lastMouseY = (float)ypos; firstMouse = false; }
    float dx = (float)xpos - lastMouseX;
    lastMouseX = (float)xpos;
    lastMouseY = (float)ypos;
    camYaw -= dx * 0.15f;
}

void scroll_callback(GLFWwindow*, double, double yoffset)
{
    camDistance = glm::clamp(camDistance - (float)yoffset * 0.3f, 1.5f, 12.0f);
}

void framebuffer_size_callback(GLFWwindow*, int w, int h) { glViewport(0, 0, w, h); }

void updateWindowTitle(GLFWwindow* window)
{
    std::string mode = isRunMode ? "[RUN]" : "[WALK]";
    std::string t = "Playable Character  |  " + mode
                  + "  State: " + std::string(stateToString(currentState))
                  + "  |  W:Fwd  S:Back  A/D:Turn  Shift:Walk  SPACE:Jump  J:Punch  K:Kick";
    glfwSetWindowTitle(window, t.c_str());
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT,
                                          "Playable Character", NULL, NULL);
    if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window,       mouse_callback);
    glfwSetScrollCallback(window,          scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR,  GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n"; return -1;
    }

    stbi_set_flip_vertically_on_load(true);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── Shaders ───────────────────────────────────────────────────────────────
    Shader charShader("anim_model.vs",     "anim_model.fs");
    Shader gridShader("infinite_grid.vs",  "infinite_grid.fs");   // replaces grid.vs/fs
    Shader dustShader("dust.vs",           "dust.fs");

    // ── Model ─────────────────────────────────────────────────────────────────
    Model ourModel(FileSystem::getPath(
        "resources/objects/mixamo/Ch17_nonPBR/Ch17_nonPBR.dae"));

    // ── Animations ────────────────────────────────────────────────────────────
    Animation _idle         (FileSystem::getPath("resources/objects/mixamo/animation/Idle.dae"),               &ourModel);
    Animation _walk         (FileSystem::getPath("resources/objects/mixamo/animation/Walking.dae"),             &ourModel);
    Animation _run          (FileSystem::getPath("resources/objects/mixamo/animation/Running.dae"),             &ourModel);
    Animation _walkBackward (FileSystem::getPath("resources/objects/mixamo/animation/Walking Backwards.dae"),   &ourModel);
    Animation _runBackward  (FileSystem::getPath("resources/objects/mixamo/animation/Run Backward.dae"),        &ourModel);
    Animation _turnLeft     (FileSystem::getPath("resources/objects/mixamo/animation/Left Turn 90.dae"),        &ourModel);
    Animation _turnRight    (FileSystem::getPath("resources/objects/mixamo/animation/Right Turn 90.dae"),       &ourModel);
    Animation _jump         (FileSystem::getPath("resources/objects/mixamo/animation/Standing Jump.dae"),       &ourModel);
    Animation _kick         (FileSystem::getPath("resources/objects/mixamo/animation/Roundhouse Kick.dae"),     &ourModel);
    Animation _punch        (FileSystem::getPath("resources/objects/mixamo/animation/Punching Bag.dae"),        &ourModel);

    idleAnim         = &_idle;
    walkAnim         = &_walk;
    runAnim          = &_run;
    walkBackwardAnim = &_walkBackward;
    runBackwardAnim  = &_runBackward;
    turnLeftAnim     = &_turnLeft;
    turnRightAnim    = &_turnRight;
    jumpAnim         = &_jump;
    kickAnim         = &_kick;
    punchAnim        = &_punch;

    AnimatorBlend _animator(idleAnim);
    animator    = &_animator;
    currentAnim = idleAnim;     // initialise guard to match starting anim

    // ── Ground & dust ─────────────────────────────────────────────────────────
    buildInfiniteGrid();
    buildDustMesh(32);

    // ─── Render loop ──────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        // One-shot action timer → return to previous state
        bool inAction = (currentState == AnimState::JUMP ||
                         currentState == AnimState::KICK ||
                         currentState == AnimState::PUNCH);
        if (inAction) {
            actionTimer += deltaTime;
            if (actionTimer >= actionDuration) {
                currentState = returnState;
                currentAnim  = nullptr;    // force re-trigger on return
                switch (returnState) {
                    case AnimState::WALK:          switchAnimation(walkAnim);         break;
                    case AnimState::RUN:           switchAnimation(runAnim);          break;
                    case AnimState::WALK_BACKWARD: switchAnimation(walkBackwardAnim); break;
                    case AnimState::RUN_BACKWARD:  switchAnimation(runBackwardAnim);  break;
                    default:                       switchAnimation(idleAnim);         break;
                }
            }
        }

        // Dust spawn
        bool isMoving = (currentState == AnimState::WALK          ||
                         currentState == AnimState::RUN           ||
                         currentState == AnimState::WALK_BACKWARD ||
                         currentState == AnimState::RUN_BACKWARD);
        if (isMoving) {
            bool running = (currentState == AnimState::RUN ||
                            currentState == AnimState::RUN_BACKWARD);
            float spawnInterval = running ? DUST_SPAWN_RUN : DUST_SPAWN_WALK;
            dustSpawnTimer += deltaTime;
            if (dustSpawnTimer >= spawnInterval) {
                dustSpawnTimer = 0.0f;
                spawnDust(characterPos);
            }
        } else {
            dustSpawnTimer = 0.0f;
        }
        updateDust(deltaTime, currentState == AnimState::RUN ||
                              currentState == AnimState::RUN_BACKWARD);

        animator->UpdateAnimation(deltaTime);
        updateWindowTitle(window);

        // ── Render ────────────────────────────────────────────────────────────
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Third-person camera
        float yawRad   = glm::radians(camYaw);
        float pitchRad = glm::radians(camPitch);
        glm::vec3 camOffset(
            camDistance * cosf(pitchRad) * sinf(yawRad),
            camDistance * sinf(pitchRad),
            camDistance * cosf(pitchRad) * cosf(yawRad));
        glm::vec3 camPos    = characterPos + camOffset + glm::vec3(0.0f, 0.8f, 0.0f);
        glm::vec3 camTarget = characterPos + glm::vec3(0.0f, 0.8f, 0.0f);

        glm::mat4 view = glm::lookAt(camPos, camTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                          (float)SCR_WIDTH / (float)SCR_HEIGHT,
                                          0.1f, 100.0f);

        drawInfiniteGrid(gridShader, proj, view);
        drawDust(dustShader, proj, view);

        // Character
        charShader.use();
        charShader.setMat4("projection", proj);
        charShader.setMat4("view",       view);

        const auto& transforms = animator->GetFinalBoneMatrices();
        for (int i = 0; i < (int)transforms.size(); ++i)
            charShader.setMat4("finalBonesMatrices[" + std::to_string(i) + "]",
                               transforms[i]);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, characterPos);
        model = glm::rotate(model, glm::radians(characterAngle), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, glm::vec3(1.0f));
        charShader.setMat4("model", model);
        ourModel.Draw(charShader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &gridQuadVAO); glDeleteBuffers(1, &gridQuadVBO);
    glDeleteVertexArrays(1, &dustVAO);     glDeleteBuffers(1, &dustVBO);
    glfwTerminate();
    return 0;
}
